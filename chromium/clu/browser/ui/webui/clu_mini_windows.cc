// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_mini_windows.h"

#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "clu/browser/multitask/clu_mini_rules.h"
#include "clu/browser/multitask/clu_multitask_hooks.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace clu::mini_windows {

namespace {

constexpr base::TimeDelta kCapToast = base::Seconds(3);

bool g_show_inactive_for_testing = false;

bool IsMainWindow(CluWindowState* window) {
  return window && !window->shell().is_mini() && !window->is_private() &&
         !window->hidden_by_switch() && !window->browser()->IsDeleteScheduled();
}

// A main window of `profile`, preferring one that shows `space_id`.
CluWindowState* MainWindowOf(Profile* profile, const std::string& space_id) {
  CluWindowState* any = nullptr;
  for (CluWindowState* window : space_actions::AllWindows()) {
    if (!IsMainWindow(window) || window->profile() != profile) {
      continue;
    }
    if (window->space_id() == space_id) {
      return window;
    }
    if (!any) {
      any = window;
    }
  }
  CluWindowState* last = LastMainWindow();
  return last && last->profile() == profile ? last : any;
}

// A new main window of `profile`, opening on `space_id`; `ready` runs once it
// shows the space.
void NewMainWindow(Profile* profile,
                   const std::string& space_id,
                   base::OnceCallback<void(CluWindowState*)> ready) {
  BrowserWindowCreateParams params(*profile, /*from_user_gesture=*/true);
  params.should_trigger_session_restore = false;
  BrowserWindowInterface* browser = CreateBrowserWindow(std::move(params));
  CluWindowState* window =
      browser ? CluWindowState::GetOrCreate(browser) : nullptr;
  if (!window) {
    return;
  }
  window->SetInitialSpace(space_id);
  window->PrepareEmpty();
  browser->GetWindow()->Show();
  window->WhenFrameReady(base::BindOnce(
      [](base::WeakPtr<CluWindowState> window,
         base::OnceCallback<void(CluWindowState*)> ready) {
        if (window) {
          std::move(ready).Run(window.get());
        }
      },
      window->GetWeakPtr(), std::move(ready)));
}

// The confirmed cross-profile move lands here, in a window of the target
// profile.
void OpenInWindow(std::string space_id, GURL url, CluWindowState* window) {
  if (window->space_id() != space_id) {
    window->ShowSpace(space_id);
  }
  std::ignore = window->CreateRow(url, space_id, /*activate=*/true);
  window->browser()->GetWindow()->Activate();
}

// The space picker of one Mini window (mini.pickSpace).
class SpacePicker : public ui::SimpleMenuModel::Delegate {
 public:
  explicit SpacePicker(CluWindowState& mini)
      : mini_(mini.GetWeakPtr()), model_(this) {
    if (CluSpacesService* spaces = CluSpacesService::Get()) {
      for (const SpaceEntry& space : spaces->list().spaces()) {
        model_.AddItem(static_cast<int>(space_ids_.size()),
                       base::UTF8ToUTF16(space.name));
        space_ids_.push_back(space.id);
      }
    }
  }

  void Run(views::Widget* widget) {
    const gfx::Point at = display::Screen::Get()->GetCursorScreenPoint();
    runner_ = std::make_unique<views::MenuRunner>(
        &model_, views::MenuRunner::CONTEXT_MENU,
        base::BindRepeating(&SpacePicker::OnClosed, base::Unretained(this)));
    runner_->RunMenuAt(
        widget, /*button_controller=*/nullptr, gfx::Rect(at, gfx::Size()),
        views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id < 0 ||
        static_cast<size_t>(command_id) >= space_ids_.size()) {
      return;
    }
    // The pick runs after the menu's own loop returns.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<CluWindowState> mini, std::string space_id) {
              if (mini) {
                std::ignore = mini->multitask().MoveToSpace(space_id, false);
              }
            },
            mini_, space_ids_[command_id]));
  }

  bool running() const { return runner_ && runner_->IsRunning(); }

 private:
  void OnClosed() {}

  base::WeakPtr<CluWindowState> mini_;
  ui::SimpleMenuModel model_;
  std::vector<std::string> space_ids_;
  std::unique_ptr<views::MenuRunner> runner_;
};

std::unique_ptr<SpacePicker>& CurrentPicker() {
  static base::NoDestructor<std::unique_ptr<SpacePicker>> picker;
  return *picker;
}

}  // namespace

content::WebContents* Open(Profile* profile,
                           const GURL& url,
                           bool command_bar) {
  if (!profile || profile->IsOffTheRecord() || !AcceptMiniUrl(url)) {
    return nullptr;
  }
  const std::vector<CluWindowState*> open = All();
  if (RouteMiniLink(open.size()) == MiniRoute::kTodayTab) {
    // A 7th link opens as a Today tab at the top of the last focused main
    // window's space (decision 27), in its own profile: never with another
    // profile's cookies. With no main window of the profile, the caller
    // leaves the link to Chromium, which opens one.
    CluWindowState* main = MainWindowOf(profile, std::string());
    if (!main) {
      return nullptr;
    }
    base::expected<std::string, std::string> row =
        main->CreateRow(url, std::nullopt, /*activate=*/true);
    tabs::TabInterface* tab =
        row.has_value() ? main->FindTab(row.value()) : nullptr;
    main->ShowToast(MiniCapToast(open.size()), std::nullopt, kCapToast);
    main->browser()->GetWindow()->Activate();
    return tab ? tab->GetContents() : nullptr;
  }

  display::Screen* screen = display::Screen::Get();
  const gfx::Rect work_area =
      screen->GetDisplayNearestPoint(screen->GetCursorScreenPoint())
          .work_area();
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP, *profile,
                                   /*from_user_gesture=*/true);
  // The empty prompt is a small window at the screen's top-right; a link
  // opens at the page size.
  const bool prompt = command_bar && url.IsAboutBlank();
  const gfx::Rect bounds = prompt ? MiniPromptBounds(work_area, open.size())
                                  : MiniWindowBounds(work_area, open.size());
  params.initial_bounds = bounds;
  // Mini windows are not restored after a restart (05 "Limits and lifecycle").
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  BrowserWindowInterface* browser = nullptr;
  {
    CluShell::ScopedCreatingMiniWindow creating;
    browser = CreateBrowserWindow(std::move(params));
  }
  CluWindowState* window =
      browser ? CluWindowState::GetOrCreate(browser) : nullptr;
  if (!window || !window->shell().is_mini()) {
    // Never left behind empty and hidden.
    if (browser) {
      browser->GetWindow()->Close();
    }
    return nullptr;
  }
  // Chromium reads a popup's bounds as its content size and adds the title bar
  // (BrowserView::GetSavedWindowPlacement); the Mini window is 880 by 620
  // outside. The origin stays where Chromium put it, except for the prompt,
  // which keeps its top-right place.
  gfx::Rect placed = browser->GetWindow()->GetBounds();
  placed.set_size(bounds.size());
  if (prompt) {
    placed = bounds;
    window->multitask().SetMiniPrompt();
  }
  browser->GetWindow()->SetBounds(placed);

  // The one page goes straight into the popup's strip: Chromium's navigator
  // would send a new tab of a popup to a tabbed window.
  std::unique_ptr<content::WebContents> page =
      content::WebContents::Create(content::WebContents::CreateParams(profile));
  if (!url.IsAboutBlank()) {
    content::NavigationController::LoadURLParams load(url);
    load.transition_type = ui::PAGE_TRANSITION_LINK;
    page->GetController().LoadURLWithParams(load);
  }
  content::WebContents* shown = page.get();
  browser->GetTabStripModel()->AppendWebContents(std::move(page),
                                                 /*foreground=*/true);
  // The Mini window grows from 0.98 while it fades in (95 ms) and fades out
  // on close (150 ms), AppKit's document-window animation. Chromium turns it
  // off for every browser window, so the Mini window turns it back on before
  // it shows.
  if (views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
          browser->GetWindow()->GetNativeWindow())) {
    widget->SetVisibilityChangedAnimationsEnabled(true);
  }
  if (g_show_inactive_for_testing) {
    browser->GetWindow()->ShowInactive();
  } else {
    browser->GetWindow()->Show();
  }
  if (command_bar) {
    window->OpenCommandBar(CommandBarMode::kMini);
  }
  return shown;
}

std::vector<CluWindowState*> All() {
  std::vector<CluWindowState*> minis;
  for (CluWindowState* window : space_actions::AllWindows()) {
    if (window->shell().is_mini() && !window->browser()->IsDeleteScheduled()) {
      minis.push_back(window);
    }
  }
  return minis;
}

CluWindowState* LastMainWindow() {
  CluWindowState* found = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&found](BrowserWindowInterface* browser) {
        CluWindowState* window =
            CluWindowState::Get(browser->GetUnownedUserDataHost());
        if (IsMainWindow(window)) {
          found = window;
          return false;
        }
        return true;
      });
  return found;
}

void AdoptIntoSpace(Profile* profile,
                    const std::string& space_id,
                    std::unique_ptr<content::WebContents> contents) {
  auto adopt = [](std::string space_id,
                  std::unique_ptr<content::WebContents> contents,
                  CluWindowState* window) {
    if (window->space_id() != space_id) {
      window->ShowSpace(space_id);
    }
    window->multitask().AdoptContentsAsRow(std::move(contents), std::nullopt);
    window->browser()->GetWindow()->Activate();
  };
  if (CluWindowState* window = MainWindowOf(profile, space_id)) {
    adopt(space_id, std::move(contents), window);
    return;
  }
  // No main window: one opens on the space first (05 "If no main window
  // exists").
  NewMainWindow(profile, space_id,
                base::BindOnce(adopt, space_id, std::move(contents)));
}

void OpenUrlInSpace(const std::string& space_id, const GURL& url) {
  CluSpacesService* spaces = CluSpacesService::Get();
  const SpaceEntry* space =
      spaces ? spaces->list().FindSpace(space_id) : nullptr;
  if (!space) {
    return;
  }
  // The target profile loads first; its window shows the space, then the page
  // opens there fresh.
  spaces->LoadProfile(
      space->profile_id,
      base::BindOnce(
          [](std::string space_id, GURL url, Profile* profile) {
            if (!profile) {
              return;
            }
            CluWindowState* window = MainWindowOf(profile, space_id);
            if (!window) {
              NewMainWindow(profile, space_id,
                            base::BindOnce(&OpenInWindow, space_id, url));
              return;
            }
            window->WhenFrameReady(base::BindOnce(
                [](base::WeakPtr<CluWindowState> window, std::string space_id,
                   GURL url) {
                  if (window) {
                    OpenInWindow(space_id, url, window.get());
                  }
                },
                window->GetWeakPtr(), space_id, url));
          },
          space_id, url));
}

void ShowSpacePicker(CluWindowState& mini) {
  views::Widget* widget = mini.shell().frame_view()
                              ? mini.shell().frame_view()->GetWidget()
                              : nullptr;
  if (!widget) {
    return;
  }
  // A second pick while the menu runs its nested loop waits for it.
  if (CurrentPicker() && CurrentPicker()->running()) {
    return;
  }
  CurrentPicker() = std::make_unique<SpacePicker>(mini);
  CurrentPicker()->Run(widget);
}

void SetShowInactiveForTesting(bool inactive) {
  g_show_inactive_for_testing = inactive;
}

}  // namespace clu::mini_windows

namespace clu {

bool OpenUrlsFromOtherApp(const std::vector<GURL>& urls) {
  if (urls.empty()) {
    return false;
  }
  // Settings, Links: off, links from other apps open as Chromium opens them (docs/chromium/parity-settings.md).
  if (CluAppSettings* settings = CluAppSettings::Get();
      settings && !settings->GetBool(kLinksMiniFromOtherAppsKey)) {
    return false;
  }
  // Every URL must be one a Mini window opens; anything else (a .webloc file,
  // another scheme) stays with Chromium.
  for (const GURL& url : urls) {
    if (!AcceptMiniUrl(url)) {
      return false;
    }
  }
  CluWindowState* main = mini_windows::LastMainWindow();
  Profile* profile =
      main ? main->profile() : ProfileManager::GetLastUsedProfileIfLoaded();
  if (!profile || profile->IsOffTheRecord()) {
    return false;
  }
  // Forced incognito (policy) and a locked profile stay Chromium's: it opens the
  // link in a private window, or shows the profile picker.
  if (ProfileManager::MaybeForceOffTheRecordMode(profile) != profile ||
      profiles::IsProfileLocked(profile->GetPath())) {
    return false;
  }
  for (const GURL& url : urls) {
    mini_windows::Open(profile, url, /*command_bar=*/false);
  }
  return true;
}

}  // namespace clu
