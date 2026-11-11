// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_settings_window.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "clu/browser/settings/clu_settings_hooks.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_ui_zoom.h"
#include "clu/common/clu_urls.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace clu {

namespace {

CluSettingsWindow*& Instance() {
  static CluSettingsWindow* instance = nullptr;
  return instance;
}

// The window's tabs (src/shared/ipc/data.ts SETTINGS_TABS), import-arc, General with its import card open
// (IMPORT_ARC_HASH in GeneralPane.tsx), and import-chrome, General with the Chrome import first (IMPORT_CHROME_HASH);
// anything else shows the page's own choice.
bool IsTab(std::string_view tab) {
  for (std::string_view known : {"general", "profiles", "ai", "links",
                                 "shortcuts", "icon", "advanced",
                                 "import-arc", "import-chrome"}) {
    if (tab == known) {
      return true;
    }
  }
  return false;
}

// The last active Rio window other than `except`, or null.
BrowserWindowInterface* OtherRioWindow(BrowserWindowInterface* except) {
  BrowserWindowInterface* found = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&found, except](BrowserWindowInterface* browser) {
        if (browser != except && !browser->IsDeleteScheduled() &&
            CluShell::From(browser) && !CluShell::From(browser)->is_mini()) {
          found = browser;
          return false;
        }
        return true;
      });
  return found;
}

}  // namespace

// static
bool CluSettingsWindow::Show(BrowserWindowInterface* browser,
                             std::string_view tab) {
  if (!browser || !CluShell::From(browser)) {
    return false;
  }
  CluSettingsWindow*& instance = Instance();
  if (!instance) {
    instance = new CluSettingsWindow(browser, tab);
  } else if (!instance->widget_ || instance->widget_->IsClosed()) {
    return false;
  } else {
    instance->Navigate(tab);
  }
  instance->widget_->Show();
  instance->widget_->Activate();
  return true;
}

// static
CluSettingsWindow* CluSettingsWindow::Get() {
  return Instance();
}

// static
BrowserWindowInterface* CluSettingsWindow::BrowserFor(
    content::WebContents* contents) {
  CluSettingsWindow* instance = Instance();
  return instance && contents && instance->contents() == contents
             ? instance->browser_.get()
             : nullptr;
}

CluSettingsWindow::CluSettingsWindow(BrowserWindowInterface* browser,
                                     std::string_view tab) {
  SetCanResize(false);
  SetCanMaximize(false);
  SetCanMinimize(false);
  SetShowCloseButton(true);
  Bind(browser);

  // A window's contents view comes from its delegate before Init: a widget with a non-client view may not have it
  // set afterwards.
  Profile* profile = browser->GetProfile()->GetOriginalProfile();
  web_view_ = SetContentsView(std::make_unique<views::WebView>(profile));

  // Centered on the window that asked, as a macOS settings window opens, unless it was moved before: then Init puts it
  // where it last stood (GetSavedWindowPlacement).
  const gfx::Rect over = browser->GetWindow()->GetBounds();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = this;
  params.name = "CluSettingsWindow";
#if BUILDFLAG(IS_MAC)
  // macOS's document-window show and hide: a short scale and fade.
  params.animation_enabled = true;
#endif
  params.bounds = gfx::Rect(over.CenterPoint().x() - kWidth / 2,
                            over.CenterPoint().y() - kHeight / 2, kWidth,
                            kHeight);
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  widget_observation_.Observe(widget_.get());
  // The close button and Close() end here, so the window has one way to go.
  widget_->MakeCloseSynchronous(base::BindOnce(
      &CluSettingsWindow::OnCloseSynchronous, weak_ptr_factory_.GetWeakPtr()));

  OpenPage(tab);

  if (views::FocusManager* focus = widget_->GetFocusManager()) {
    focus->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_W, ui::EF_COMMAND_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
  }
  if (CluAppSettings* settings = CluAppSettings::Get()) {
    settings_observation_.Observe(settings);
  }
  Style();
}

CluSettingsWindow::~CluSettingsWindow() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  deleting_ = true;
  if (Instance() == this) {
    Instance() = nullptr;
  }
  if (web_view_) {
    if (content::WebContents* page = web_view_->web_contents()) {
      page->SetDelegate(nullptr);
    }
  }
  Observe(nullptr);
  widget_observation_.Reset();
  web_view_ = nullptr;
  widget_.reset();
}

bool CluSettingsWindow::ShouldSaveWindowPlacement() const {
  return true;
}

void CluSettingsWindow::SaveWindowPlacement(
    const gfx::Rect& bounds,
    ui::mojom::WindowShowState show_state) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state || bounds.IsEmpty()) {
    return;
  }
  base::DictValue origin;
  origin.Set("x", bounds.x());
  origin.Set("y", bounds.y());
  local_state->SetDict(kCluSettingsWindowOriginPref, std::move(origin));
}

bool CluSettingsWindow::GetSavedWindowPlacement(
    const views::Widget* widget,
    gfx::Rect* bounds,
    ui::mojom::WindowShowState* show_state) const {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  display::Screen* screen = display::Screen::Get();
  if (!local_state || !screen) {
    return false;
  }
  // Junk (a string, a missing coordinate) reads as nothing saved.
  const base::DictValue& origin =
      local_state->GetDict(kCluSettingsWindowOriginPref);
  const std::optional<int> x = origin.FindInt("x");
  const std::optional<int> y = origin.FindInt("y");
  if (!x || !y) {
    return false;
  }
  const gfx::Rect saved(*x, *y, kWidth, kHeight);
  // On no display now (a screen was unplugged): centered on the asking window instead.
  if (!screen->GetDisplayMatching(saved).bounds().Intersects(saved)) {
    return false;
  }
  *bounds = saved;
  *show_state = ui::mojom::WindowShowState::kNormal;
  return true;
}

content::WebContents* CluSettingsWindow::contents() {
  return web_view_ ? web_view_->web_contents() : nullptr;
}

void CluSettingsWindow::Bind(BrowserWindowInterface* browser) {
  browser_ = browser;
  browser_close_subscription_ =
      browser->RegisterBrowserDidClose(base::BindRepeating(
          &CluSettingsWindow::OnBrowserClosed, base::Unretained(this)));
}

void CluSettingsWindow::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (browser != browser_) {
    return;
  }
  browser_close_subscription_ = {};
  browser_ = nullptr;
  BrowserWindowInterface* next = OtherRioWindow(browser);
  if (!next) {
    Close();
    return;
  }
  // The page's controller bound to the closed window; a reload binds a new one to the next window.
  Bind(next);
  content::WebContents* page = contents();
  if (!page) {
    return;
  }
  // A page outlives its profile only as a crash (~WebContentsOfBrowserContext CHECK): the closed window may
  // have been its profile's last (a Replace removing an earlier import, a space deleted), so on another profile the
  // page opens anew there, on the tab it showed.
  Profile* profile = next->GetProfile()->GetOriginalProfile();
  if (page->GetBrowserContext() != profile) {
    const std::string tab(page->GetLastCommittedURL().ref());
    page->SetDelegate(nullptr);
    Observe(nullptr);
    web_view_->SetWebContents(nullptr);
    web_view_->SetBrowserContext(profile);
    OpenPage(tab);
    return;
  }
  page->GetController().Reload(content::ReloadType::NORMAL,
                               /*check_for_repost=*/false);
}

void CluSettingsWindow::OpenPage(std::string_view tab) {
  content::WebContents* page = web_view_->GetWebContents();
  page->SetDelegate(this);
  Observe(page);
  // Rio's own UI never zooms (docs/chromium/fixes.md).
  CluUiZoomLock::Lock(page);
  web_view_->LoadInitialURL(
      GURL(IsTab(tab) ? base::StrCat({kCluSettingsUrl, "#", tab})
                      : std::string(kCluSettingsUrl)));
}

void CluSettingsWindow::Navigate(std::string_view tab) {
  content::WebContents* page = contents();
  if (!page || !IsTab(tab)) {
    return;
  }
  // A fragment change only: the page follows hashchange, keeping its state.
  web_view_->LoadInitialURL(GURL(base::StrCat({kCluSettingsUrl, "#", tab})));
}

void CluSettingsWindow::Close() {
  if (widget_ && !widget_->IsClosed()) {
    widget_->Close();
  } else {
    DeleteSoon();
  }
}

void CluSettingsWindow::Style() {
  CluAppSettings* settings = CluAppSettings::Get();
  StyleSettingsWindow(widget_.get(),
                      settings ? settings->AppearanceMode() : "auto");
}

std::u16string CluSettingsWindow::GetWindowTitle() const {
  content::WebContents* page =
      web_view_ ? web_view_->web_contents() : nullptr;
  return page ? page->GetTitle() : std::u16string();
}

void CluSettingsWindow::OnCloseSynchronous(views::Widget::ClosedReason reason) {
  // A synchronous close skips Widget's own save, so the window keeps its spot here.
  if (widget_) {
    SaveWindowPlacement(widget_->GetWindowBoundsInScreen(),
                        ui::mojom::WindowShowState::kNormal);
  }
  DeleteSoon();
}

void CluSettingsWindow::OnWidgetDestroying(views::Widget* widget) {
  // The native window went first (the app quitting); it still has its bounds here.
  SaveWindowPlacement(widget->GetWindowBoundsInScreen(),
                      ui::mojom::WindowShowState::kNormal);
  widget_observation_.Reset();
  DeleteSoon();
}

void CluSettingsWindow::DeleteSoon() {
  if (Instance() == this) {
    Instance() = nullptr;
  }
  if (deleting_) {
    return;
  }
  deleting_ = true;
  // Posted: the widget is still on the stack. The destructor resets it, which closes the native window.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
}

bool CluSettingsWindow::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  views::FocusManager* focus = widget_ ? widget_->GetFocusManager() : nullptr;
  return focus &&
         unhandled_keyboard_event_handler_.HandleKeyboardEvent(event, focus);
}

void CluSettingsWindow::TitleWasSet(content::NavigationEntry* entry) {
  if (widget_) {
    widget_->UpdateWindowTitle();
    SetSettingsWindowTitle(widget_.get());
  }
}

bool CluSettingsWindow::AcceleratorPressed(const ui::Accelerator& accelerator) {
  Close();
  return true;
}

bool CluSettingsWindow::CanHandleAccelerators() const {
  return widget_ && widget_->IsActive();
}

void CluSettingsWindow::OnAppSettingsChanged() {
  Style();
}

bool ShowAppSettingsForProfile(Profile* profile) {
  if (!profile) {
    return false;
  }
  Profile* original = profile->GetOriginalProfile();
  BrowserWindowInterface* browser = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&browser, original](BrowserWindowInterface* candidate) {
        CluShell* shell = CluShell::From(candidate);
        if (!candidate->IsDeleteScheduled() && shell && !shell->is_mini() &&
            candidate->GetProfile()->GetOriginalProfile() == original) {
          browser = candidate;
          return false;
        }
        return true;
      });
  if (!browser) {
    browser = OtherRioWindow(nullptr);
  }
  if (!browser) {
    // No Rio window at all: one opens, as Chromium's Settings opens one, and the settings window comes up over it.
    browser = chrome::OpenEmptyWindow(original,
                                      /*should_trigger_session_restore=*/false);
  }
  return browser && CluSettingsWindow::Show(browser, std::string_view());
}

#if !BUILDFLAG(IS_MAC)
void StyleSettingsWindow(views::Widget* widget, std::string_view mode) {}
void SetSettingsWindowTitle(views::Widget* widget) {}
#endif

}  // namespace clu
