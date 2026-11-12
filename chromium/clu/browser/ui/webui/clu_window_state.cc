// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_window_state.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "content/public/browser/render_widget_host_view.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "clu/browser/ai/clu_link_hover.h"
#include "clu/browser/capabilities/clu_keep_live.h"
#include "clu/browser/model/clu_nav_state.h"
#include "clu/browser/model/clu_page_state.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/ui/webui/clu_command_bar.h"
#include "clu/browser/ui/webui/clu_default_browser.h"
#include "clu/browser/ui/webui/clu_icon_backfill.h"
#include "clu/browser/ui/webui/clu_overlay_rects.h"
#include "clu/browser/ui/webui/clu_platform_handler.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_space_menu.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_tab_menu.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/updater/clu_update_service.h"
#include "clu/browser/ui/webui/clu_window_app_state.h"
#include "clu/browser/ui/webui/clu_window_site.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "components/tabs/public/tab_interface.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/color_analysis.h"
#include "ui/gfx/image/image.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/event_monitor.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace clu {

namespace {

// The 16 ms batch of the state contract (src/shared/state.ts).
constexpr base::TimeDelta kPublishDelay = base::Milliseconds(16);

// A live page's icon. favicon2 answers a page URL from Chromium's favicon database, and the WebUI caches each answer
// by URL, so an icon asked for before Chromium stored it would stay the default globe. The URL carries a hash of the
// icon Chromium shows now: once the icon arrives (or changes), the URL changes and the frame asks again.
std::optional<std::string> PageFaviconUrl(content::WebContents* contents) {
  const GURL& url = contents->GetVisibleURL();
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  if (!entry || !entry->GetFavicon().valid) {
    return FaviconUrlFor(url);
  }
  return FaviconUrlFor(url, base::NumberToString(base::PersistentHash(
                                entry->GetFavicon().url.spec())));
}

// The dominant color of the icon Chromium shows for the page, as #rrggbb, or nullopt without one: the selected
// favorite's ring takes it. The k-means pass runs once per icon URL, not on every publish.
std::optional<std::string> PageIconColor(content::WebContents* contents) {
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  if (!entry || !entry->GetFavicon().valid ||
      entry->GetFavicon().image.IsEmpty()) {
    return std::nullopt;
  }
  static base::NoDestructor<base::flat_map<std::string, std::string>> colors;
  const std::string& icon = entry->GetFavicon().url.spec();
  if (auto it = colors->find(icon); it != colors->end()) {
    return it->second;
  }
  const SkBitmap bitmap = entry->GetFavicon().image.AsBitmap();
  if (bitmap.drawsNothing()) {
    return std::nullopt;
  }
  const SkColor color = color_utils::CalculateKMeanColorOfBitmap(bitmap);
  // A small cache: a long session starts over rather than growing without bound.
  if (colors->size() >= 256) {
    colors->clear();
  }
  std::string hex = base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                                       SkColorGetG(color), SkColorGetB(color));
  colors->insert_or_assign(icon, hex);
  return hex;
}
constexpr int kToastSlot = 0;
constexpr int kSwitcherSlot = 1;
constexpr int kHoverSlot = 2;

// `text` cut at `max` bytes on a UTF-8 boundary (a base::Value string must be valid UTF-8).
std::string CappedText(const std::string& text, size_t max) {
  std::string capped;
  base::TruncateUTF8ToByteSize(text, max, &capped);
  return capped;
}

std::string NewId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

double NowMs() {
  return static_cast<double>(base::Time::Now().InMillisecondsSinceUnixEpoch());
}

const char* ModeName(CommandBarMode mode) {
  switch (mode) {
    case CommandBarMode::kEditUrl:
      return "edit-url";
    case CommandBarMode::kSplitAdd:
      return "split-add";
    case CommandBarMode::kMini:
      return "mini";
    case CommandBarMode::kNewTab:
      return "new-tab";
  }
  return "new-tab";
}

}  // namespace

CluWindowState::ClosedPage::ClosedPage() = default;
CluWindowState::ClosedPage::ClosedPage(const ClosedPage&) = default;
CluWindowState::ClosedPage& CluWindowState::ClosedPage::operator=(
    const ClosedPage&) = default;
CluWindowState::ClosedPage::~ClosedPage() = default;

DEFINE_USER_DATA(CluWindowState);

// static
CluWindowState* CluWindowState::GetOrCreate(BrowserWindowInterface* browser) {
  CluShell* shell = CluShell::From(browser);
  if (!shell) {
    return nullptr;
  }
  if (CluWindowState* state = Get(browser->GetUnownedUserDataHost())) {
    return state;
  }
  // The shell owns the state as its client (see the class comment).
  auto state = base::WrapUnique(new CluWindowState(browser, *shell));
  CluWindowState* raw = state.get();
  shell->SetClient(std::move(state));
  return raw;
}

CluWindowState::CluWindowState(BrowserWindowInterface* browser,
                               CluShell& shell)
    : browser_(browser),
      shell_(&shell),
      window_id_(NewId()),
      profile_id_(NewId()),
      space_id_(NewId()),
      group_id_(window_id_),
      scoped_unowned_user_data_(browser->GetUnownedUserDataHost(), *this) {
  SetLinkHoverHandler(&CluWindowState::OnLinkHover);
  switcher_ = std::make_unique<CluSwitcher>(*this, nullptr);
  tab_menu_ = std::make_unique<CluTabMenu>(*this);
  space_menu_ = std::make_unique<CluSpaceMenu>(*this);
  // M9: the sidebar's downloads button reads the profile's downloads from state; Memory Saver learns Clu's
  // exemptions once the first window exists.
  downloads_ = std::make_unique<CluDownloads>(
      profile(), base::BindRepeating(&CluWindowState::MarkDirty,
                                     base::Unretained(this)));
  // A screenshot landing in the screenshot folder shows as a card above the sidebar's bottom bar.
  screenshots_ = std::make_unique<CluScreenshotWatcher>(base::BindRepeating(
      &CluWindowState::MarkDirty, base::Unretained(this)));
  CluKeepLive::Start();
  app_state_ = std::make_unique<CluWindowAppState>(*this, profile());
  // A new window starts with the sidebar the last window left (shell.lastSidebarWidth, F13); a private window with
  // the defaults, and a Mini window none.
  if (CluAppSettings* settings = CluAppSettings::Get();
      settings && !profile()->IsOffTheRecord() && !shell.is_mini()) {
    shell.RestoreSidebar(settings->last_sidebar_width(),
                         settings->last_sidebar_hidden());
    shell.SetToolbar(settings->last_toolbar());
  }
  site_ = std::make_unique<CluWindowSite>(*this);
  if (CluSpacesService* spaces = CluSpacesService::Get()) {
    spaces_observation_.Observe(spaces);
  }
  shell_observation_.Observe(&shell);
  if (TemplateURLService* engines =
          TemplateURLServiceFactory::GetForProfile(profile())) {
    engines_observation_.Observe(engines);
    // Nothing else loads the engines of a profile first opened by a space switch; the bar's engine rows and the
    // session's engine name need them. OnTemplateURLServiceChanged() republishes once they are in.
    engines->Load();
  }
  browser_->GetTabStripModel()->AddObserver(this);
  multitask_ = std::make_unique<CluWindowMultitask>(*this);
  sidebar_peek_ = std::make_unique<CluWindowSidebarPeek>(*this);
  subscriptions_.push_back(browser_->RegisterDidBecomeActive(
      base::BindRepeating(&CluWindowState::OnActiveChanged,
                          base::Unretained(this))));
  subscriptions_.push_back(browser_->RegisterDidBecomeInactive(
      base::BindRepeating(&CluWindowState::OnActiveChanged,
                          base::Unretained(this))));
  // Updates are the app's, not the window's: every window shows app.update (docs/chromium/m12.md). Started after the
  // window is built, so Sparkle's first check never runs inside it.
  subscriptions_.push_back(CluUpdateService::Get().AddObserver(
      base::BindRepeating(&CluWindowState::MarkDirty, base::Unretained(this))));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&EnsureUpdatesStarted));
  // So is app.defaultBrowser; each window checks it once when it opens, and again whenever it becomes active.
  subscriptions_.push_back(CluDefaultBrowser::Get().AddObserver(
      base::BindRepeating(&CluWindowState::MarkDirty, base::Unretained(this))));
  CluDefaultBrowser::Get().Refresh();
  if (tabs::TabInterface* tab = browser_->GetActiveTabInterface()) {
    last_active_at_.insert_or_assign(CluTabId::Get(tab->GetContents()),
                                     NowMs());
  }
  // Private and guest windows have no model: their tabs never reach tab_nodes.
  // A Mini window's one page is no row either (docs/chromium/m6.md).
  CluModelService* model =
      shell.is_mini() ? nullptr : CluModelService::Get(profile());
  if (model) {
    model_ = model;
    model_observation_.Observe(model);
    model->AddWindow(this);
    // Posted: the shell (and this state) are made while the window is built, and the strip must not change then.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<CluWindowState> self) {
              if (self && self->model_) {
                self->model_->WhenLoaded(base::BindOnce(
                    &CluWindowState::OnModelLoaded, self->GetWeakPtr()));
              }
            },
            weak_ptr_factory_.GetWeakPtr()));
  } else if (profile()->IsOffTheRecord()) {
    // A private window keeps no rows: its strip is ready once the window is built.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CluWindowState::OnPrivateWindowReady,
                                  weak_ptr_factory_.GetWeakPtr()));
  }
}

CluWindowState::~CluWindowState() {
  // Prompts answer nothing now: their documents are already gone with the shell's views. A theme picker still open
  // ends its preview, which other windows on that space would keep showing.
  if (CluAppSettings* settings = CluAppSettings::Get()) {
    for (const OverlaySurfaceSnapshot& prompt : prompts_) {
      if (prompt.surface == "theme-picker") {
        if (const std::string* space = prompt.props.FindString("spaceId")) {
          settings->RevertPreview(*space);
        }
      }
    }
  }
  prompt_callbacks_.clear();
  // A fade must not reach into this window while it is torn down.
  weak_ptr_factory_.InvalidateWeakPtrs();
  fade_.reset();
  sidebar_peek_.reset();
  // The Peek's page and the media observers go while the strip is still there.
  multitask_.reset();
  if (model_) {
    model_->RemoveWindow(this);
  }
  browser_->GetTabStripModel()->RemoveObserver(this);
  // A Clu window on screen that closes takes the siblings a cross-profile switch hid along (docs/chromium/m4.md).
  if (!hidden_by_switch_ && !profile()->IsOffTheRecord()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](std::string group_id) {
                         // Another window of the group still on screen (a fade that closed its source) keeps them.
                         for (CluWindowState* window :
                              space_actions::AllWindows()) {
                           if (window->group_id() == group_id &&
                               !window->hidden_by_switch() &&
                               !window->browser()->IsDeleteScheduled()) {
                             return;
                           }
                         }
                         for (CluWindowState* window :
                              space_actions::AllWindows()) {
                           if (window->group_id() == group_id &&
                               window->hidden_by_switch()) {
                             window->browser()->GetWindow()->Close();
                           }
                         }
                       },
                       group_id_));
  }
}

Profile* CluWindowState::profile() {
  return browser_->GetProfile();
}

base::WeakPtr<CluWindowState> CluWindowState::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

CluModelService* CluWindowState::model() {
  return model_ready_ ? model_.get() : nullptr;
}

void CluWindowState::Subscribe(
    mojo::PendingRemote<mojom::StateObserver> observer,
    bool frame) {
  Flush();
  const mojo::RemoteSetElementId id = observers_.Add(std::move(observer));
  observers_.Get(id)->OnState(SnapshotMessage(published_, rev_));
  if (frame) {
    frame_subscribed_ = true;
    // The frame document (re)subscribed: watch its widget's trackpad phases.
    content::WebContents* contents = shell_->frame_contents();
    wheel_watcher_ = std::make_unique<CluWheelLiftWatcher>(
        contents ? contents->GetPrimaryMainFrame()->GetRenderWidgetHost()
                 : nullptr,
        base::BindRepeating(&CluWindowState::OnWheelPhase,
                            base::Unretained(this)));
    NotifyFrameReady();
  }
}

base::DictValue CluWindowState::CurrentState() {
  Flush();
  base::DictValue state = published_.Clone();
  state.Set("rev", rev_);
  return state;
}

void CluWindowState::MarkDirty() {
  dirty_ = true;
  if (!publish_timer_.IsRunning()) {
    publish_timer_.Start(FROM_HERE, kPublishDelay,
                         base::BindOnce(&CluWindowState::Publish,
                                        base::Unretained(this)));
  }
}

void CluWindowState::Flush() {
  if (rev_ == 0 || dirty_) {
    Publish();
  }
}

tabs::TabInterface* CluWindowState::FindTab(std::string_view tab_id) {
  if (tab_index_) {
    const auto it = tab_index_->find(tab_id);
    return it == tab_index_->end() ? nullptr : it->second.get();
  }
  TabStripModel* strip = browser_->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!CluPlaceholder::Is(contents) && CluTabId::Has(contents) &&
        CluTabId::Get(contents) == tab_id) {
      return strip->GetTabAtIndex(i);
    }
  }
  return nullptr;
}

tabs::TabInterface* CluWindowState::ActiveTab() {
  tabs::TabInterface* tab = browser_->GetActiveTabInterface();
  return tab && !CluPlaceholder::Is(tab->GetContents()) ? tab : nullptr;
}

std::string CluWindowState::ActiveRowId() {
  tabs::TabInterface* tab = ActiveTab();
  return tab ? CluTabId::Get(tab->GetContents()) : std::string();
}

std::optional<int> CluWindowState::IndexOf(tabs::TabInterface* tab) {
  if (!tab) {
    return std::nullopt;
  }
  const int index = browser_->GetTabStripModel()->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return std::nullopt;
  }
  return index;
}

void CluWindowState::FocusActivePage() {
  if (tabs::TabInterface* tab = ActiveTab()) {
    tab->GetContents()->Focus();
  }
}

void CluWindowState::ActivateTab(tabs::TabInterface* tab) {
  if (const std::optional<int> index = IndexOf(tab)) {
    browser_->GetTabStripModel()->ActivateTabAt(
        *index, TabStripUserGestureDetails(
                    TabStripUserGestureDetails::GestureType::kOther));
  }
}

void CluWindowState::GoBack(tabs::TabInterface* tab) {
  content::NavigationController& controller =
      tab->GetContents()->GetController();
  if (controller.CanGoBack()) {
    controller.GoBack();
  }
}

void CluWindowState::GoForward(tabs::TabInterface* tab) {
  content::NavigationController& controller =
      tab->GetContents()->GetController();
  if (controller.CanGoForward()) {
    controller.GoForward();
  }
}

void CluWindowState::Reload(tabs::TabInterface* tab, bool bypass_cache) {
  tab->GetContents()->GetController().Reload(
      bypass_cache ? content::ReloadType::BYPASSING_CACHE
                   : content::ReloadType::NORMAL,
      /*check_for_repost=*/true);
}

void CluWindowState::StopLoading(tabs::TabInterface* tab) {
  tab->GetContents()->Stop();
}

// Command bar and overlay surfaces ---------------------------------------------------------------------------------

void CluWindowState::OpenCommandBar(CommandBarMode requested) {
  // edit-url without a page to edit falls back to new-tab (04-command "Modes").
  tabs::TabInterface* target =
      requested == CommandBarMode::kEditUrl ? ActiveTab() : nullptr;
  const CommandBarMode mode =
      requested == CommandBarMode::kEditUrl
          ? (target ? CommandBarMode::kEditUrl : CommandBarMode::kNewTab)
          : requested;
  std::optional<std::string> target_tab_id;
  std::string initial_text;
  if (target) {
    target_tab_id = CluTabId::Get(target->GetContents());
    const GURL& url = target->GetContents()->GetVisibleURL();
    initial_text = url.IsAboutBlank() ? std::string() : StateUrl(url);
  }

  if (command_) {
    // The same mode again closes the bar; the other mode switches the open session in place (F1, F3).
    if (command_->mode == ModeName(mode)) {
      CloseCommandBar();
      return;
    }
    command_->mode = ModeName(mode);
    command_->target_tab_id = std::move(target_tab_id);
    command_->initial_text = std::move(initial_text);
    MarkDirty();
    Flush();
    return;
  }

  CommandSessionSnapshot session;
  session.session_id = NewId();
  session.mode = ModeName(mode);
  session.target_tab_id = std::move(target_tab_id);
  session.initial_text = std::move(initial_text);
  session.engine_name = DefaultSearchEngine(profile()).name;
  command_ = std::move(session);
  command_instance_id_ = NewId();
  Hover(std::nullopt, std::nullopt);
  // The command bar closes any open Peek, a clicked one as well as a link's hover preview.
  if (multitask_ && multitask_->has_peek()) {
    multitask_->peek().Close();
  }
  // The overlay renders the bar from this state, so it goes out before the overlay shows.
  MarkDirty();
  Flush();
  UpdateTopOverlay();
}

void CluWindowState::CloseCommandBar() {
  if (!command_) {
    return;
  }
  command_.reset();
  command_instance_id_.clear();
  MarkDirty();
  Flush();
  UpdateTopOverlay();
  FocusActivePage();
}

void CluWindowState::OpenPrompt(const std::string& surface,
                                base::DictValue props,
                                std::string modality) {
  OverlaySurfaceSnapshot prompt;
  prompt.instance_id = NewId();
  prompt.surface = surface;
  prompt.modality = std::move(modality);
  prompt.props = std::move(props);
  // One prompt at a time: a new one replaces the last, whose caller hears "cancelled".
  FailPrompts();
  prompts_.clear();
  prompts_.push_back(std::move(prompt));
  Hover(std::nullopt, std::nullopt);
  MarkDirty();
  Flush();
  UpdateTopOverlay();
}

void CluWindowState::OpenAnchoredPrompt(
    const std::string& surface,
    const gfx::Rect& rect,
    base::DictValue props,
    base::OnceCallback<void(base::Value)> done) {
  OverlaySurfaceSnapshot prompt;
  prompt.instance_id = NewId();
  prompt.surface = surface;
  prompt.modality = "light-dismiss";
  prompt.rect = rect;
  prompt.props = std::move(props);
  const std::string instance_id = prompt.instance_id;
  FailPrompts();
  prompts_.clear();
  prompts_.push_back(std::move(prompt));
  prompt_callbacks_[instance_id] = std::move(done);
  Hover(std::nullopt, std::nullopt);
  MarkDirty();
  Flush();
  UpdateTopOverlay();
}

bool CluWindowState::CloseOverlayInstance(std::string_view instance_id) {
  if (command_ && instance_id == command_instance_id_) {
    CloseCommandBar();
    return true;
  }
  // Closing a prompt (Esc, Cancel) answers a confirm with false.
  if (RespondOverlay(instance_id, base::Value(false))) {
    return true;
  }
  if (switcher_surface_ && instance_id == switcher_surface_->instance_id) {
    switcher_->Cancel();
    return true;
  }
  if (hover_surface_ && instance_id == hover_surface_->instance_id) {
    Hover(std::nullopt, std::nullopt);
    return true;
  }
  return false;
}

std::optional<gfx::Rect> CluWindowState::TopOverlayRect() {
  if (command_ || prompts_.empty()) {
    return std::nullopt;
  }
  return PassiveTopOverlayRect(shell_->layout().content_size, prompts_);
}

void CluWindowState::UpdateTopOverlay() {
  const std::optional<gfx::Rect> top_rect = TopOverlayRect();
  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
      browser_->GetWindow()->GetNativeWindow());
  if (!top_rect || !widget) {
    page_click_monitor_.reset();
  } else if (!page_click_monitor_) {
    page_click_monitor_ = views::EventMonitor::CreateWindowMonitor(
        this, widget->GetNativeWindow(), {ui::EventType::kMousePressed});
  }
  if (command_ || !prompts_.empty()) {
    // Another overlay opening (the command bar, a menu, the theme editor) closes the tab hover card and a link's hover
    // preview.
    if (hover_surface_ && hover_surface_->surface == "tab-hover") {
      Hover(std::nullopt, std::nullopt);
    }
    if (multitask_ && multitask_->has_peek() && multitask_->peek().from_hover()) {
      multitask_->peek().Close();
    }
    shell_->ShowOverlay(/*focus=*/true, top_rect);
  } else if (multitask_ && multitask_->WantsTopOverlay()) {
    // The split drop preview: shown over the page without taking focus from the
    // sidebar's drag.
    shell_->ShowOverlay(/*focus=*/false);
  } else {
    shell_->HideOverlay();
  }
}

void CluWindowState::ShowToast(const std::string& text,
                               const std::optional<std::string>& action_command,
                               base::TimeDelta duration) {
  ToastSnapshot toast;
  toast.id = NewId();
  base::TruncateUTF8ToByteSize(text, 200, &toast.text);
  if (action_command) {
    toast.action_label =
        *action_command == kShareToastAction ? "Share" : "Undo";
    toast.action_command = *action_command;
  }
  toast.until = NowMs() + duration.InMillisecondsF();
  // A new toast replaces the one showing.
  toasts_.push_back(std::move(toast));
  while (toasts_.size() > kMaxToasts) {
    toasts_.erase(toasts_.begin());
  }
  UpdateToastSurface();
}

void CluWindowState::ExpireToasts() {
  const double now = NowMs();
  std::erase_if(toasts_,
                [&](const ToastSnapshot& toast) { return toast.until <= now; });
  UpdateToastSurface();
}

void CluWindowState::UpdateToastSurface() {
  toast_timer_.Stop();
  if (toasts_.empty()) {
    toast_instance_id_.clear();
    shell_->HidePassive(kToastSlot);
    MarkDirty();
    return;
  }
  if (toast_instance_id_.empty()) {
    toast_instance_id_ = NewId();
  }
  double next = toasts_.front().until;
  for (const ToastSnapshot& toast : toasts_) {
    next = std::min(next, toast.until);
  }
  toast_timer_.Start(
      FROM_HERE, base::Milliseconds(std::max(1.0, next - NowMs())),
      base::BindOnce(&CluWindowState::ExpireToasts, base::Unretained(this)));
  MarkDirty();
  Flush();
  shell_->ShowPassive(kToastSlot,
                      ToastRect(shell_->layout().page_rect, toasts_.size()));
}

void CluWindowState::HideHoverNow() {
  hover_close_timer_.Stop();
  shell_->HideLivePreview();
  if (hover_surface_) {
    hover_surface_.reset();
    hover_row_id_.clear();
    folder_peek_held_ = false;
    mail_card_held_ = false;
    shell_->HidePassive(kHoverSlot);
    MarkDirty();
    if (sidebar_peek_) {
      sidebar_peek_->SetHoverCard(false);
    }
  }
}

void CluWindowState::Hover(const std::optional<std::string>& row_id,
                           const std::optional<gfx::Rect>& anchor,
                           bool fade) {
  // The pointer left the tile for the Gmail card it opened, which holds it open (HoldHover).
  if (!row_id && fade && mail_card_held_ && hover_surface_ &&
      hover_surface_->surface == "tab-hover" && !command_ &&
      prompts_.empty()) {
    return;
  }
  // A newer hover or the pointer leaving drops the card a Gmail row was waiting to open.
  gmail_wait_row_id_.clear();
  if (!row_id || !anchor || command_ || !prompts_.empty()) {
    // The pointer leaving the row fades the title pill out over kHoverCardOut: it stays marked closing for the
    // overlay's fade, then goes. A preview or mail card, a closing one, and a command bar or prompt opening hide it
    // at once.
    const std::string* kind =
        hover_surface_ ? hover_surface_->props.FindString("kind") : nullptr;
    if (fade && hover_surface_ && hover_surface_->surface == "tab-hover" &&
        !hover_close_timer_.IsRunning() && !command_ && prompts_.empty() &&
        !(kind && *kind == "preview")) {
      hover_surface_->props.Set("closing", true);
      hover_row_id_.clear();
      MarkDirty();
      hover_close_timer_.Start(FROM_HERE, kHoverCardOut,
                               base::BindOnce(&CluWindowState::HideHoverNow,
                                              base::Unretained(this)));
      return;
    }
    HideHoverNow();
    return;
  }
  hover_close_timer_.Stop();
  HoverCardInput input;
  content::WebContents* contents = nullptr;
  if (CluModelService* m = model()) {
    const TabNode* node = m->tree().Get(*row_id);
    if (!node || !node->is_tab()) {
      Hover(std::nullopt, std::nullopt);
      return;
    }
    contents = m->FindContents(*row_id);
    input.title = node->title;
    input.title_override = node->title_override.value_or(std::string());
    input.url = node->url;
    input.pinned_url = DisplayUrl(node->pinned_url.value_or(std::string()));
    input.off_pinned = IsOffPinned(
        contents ? PageUrl(contents) : node->url, node->pinned_url);
    input.muted = node->muted;
    input.lifecycle = contents ? "live" : "unloaded";
  } else if (tabs::TabInterface* tab = FindTab(*row_id)) {
    contents = tab->GetContents();
    input.title = base::UTF16ToUTF8(contents->GetTitle());
    input.url = contents->GetVisibleURL().spec();
    input.muted = contents->IsAudioMuted();
  } else {
    Hover(std::nullopt, std::nullopt);
    return;
  }
  if (contents) {
    input.url = PageUrl(contents);
    input.title = base::UTF16ToUTF8(contents->GetTitle());
    input.audible = contents->IsCurrentlyAudible();
    if (contents->WasDiscarded()) {
      input.lifecycle = "unloaded";
    }
  }
  // A Gmail row's card is drawn from its account's unread mail, read from the account's feed with the row's
  // own profile and cookies (the Gmail page is never loaded). Until the first answer nothing shows, so the card never
  // shows a guess and then changes kind; an answer already read shows at once and is reread once it is stale.
  if (const std::optional<GURL> feed = GmailFeedUrl(input.url)) {
    content::BrowserContext* context =
        contents ? contents->GetBrowserContext() : profile();
    if (gmail_feed_.Stale(context, *feed)) {
      gmail_wait_row_id_ = *row_id;
      gmail_wait_anchor_ = *anchor;
      gmail_feed_.Fetch(context, *feed,
                        base::BindOnce(&CluWindowState::OnGmailFeed,
                                       weak_ptr_factory_.GetWeakPtr(),
                                       *row_id));
    }
    const CluGmailFeed::Result* result = gmail_feed_.Find(context, *feed);
    if (!result) {
      // Another row's card closes now; this one opens when the feed answers.
      if (hover_surface_ && hover_row_id_ != *row_id) {
        HideHoverNow();
      }
      return;
    }
    if (result->ok) {
      input.mail = result->unread;
    }
  }
  // The card shows Rio's own pages as rio:// URLs, as the URL pill does.
  input.url = DisplayUrl(input.url);
  const HoverCardModel card = BuildHoverCard(input);
  const gfx::Rect rect =
      HoverCardRect(shell_->layout().content_size, *anchor, card);
  base::ListValue chips;
  for (const std::string& chip : card.chips) {
    chips.Append(chip);
  }
  base::ListValue mail_items;
  for (const GmailUnreadItem& item : card.mail_items) {
    mail_items.Append(base::DictValue()
                          .Set("author", item.author)
                          .Set("title", item.title)
                          .Set("issued", item.issued)
                          .Set("url", item.url));
  }
  OverlaySurfaceSnapshot surface;
  surface.instance_id =
      hover_surface_ ? hover_surface_->instance_id : NewId();
  surface.surface = "tab-hover";
  surface.layer = "passive";
  surface.modality = "passive";
  surface.focus = false;
  surface.slot = kHoverSlot;
  surface.rect = rect;
  // With the sidebar hidden the rows are the hover peek's: the card is its child and holds it open (F4).
  if (sidebar_peek_ && sidebar_peek_->is_open()) {
    surface.opened_from = sidebar_peek_->instance_id();
  }
  surface.props = base::DictValue()
                      .Set("tabId", *row_id)
                      .Set("title", card.title)
                      .Set("titleLines", card.title_lines)
                      .Set("location", card.location)
                      .Set("pinned", card.pinned.empty()
                                         ? base::Value()
                                         : base::Value(card.pinned))
                      .Set("chips", std::move(chips))
                      .Set("width", card.width)
                      .Set("height", card.height)
                      .Set("kind", card.kind)
                      .Set("unread", card.unread)
                      .Set("site", card.site.empty() ? base::Value()
                                                     : base::Value(card.site));
  if (card.kind == "mail") {
    surface.props.Set("mailSource", card.mail_source);
    surface.props.Set("mailItems", std::move(mail_items));
  }
  const bool from_peek = surface.opened_from.has_value();
  if (hover_row_id_ != *row_id) {
    mail_card_held_ = false;
  }
  hover_surface_ = std::move(surface);
  hover_row_id_ = *row_id;
  hover_anchor_ = *anchor;
  MarkDirty();
  if (from_peek) {
    sidebar_peek_->SetHoverCard(true);
  }
  Flush();
  // The publish may have closed the peek (a newer surface was waiting), and the peek's card with it.
  if (!hover_surface_) {
    shell_->HideLivePreview();
    return;
  }
  // The card only shows a title: a click where it is, or where it just was, reaches the row under it. Gmail's message
  // list takes the pointer inside its own rect: its rows highlight and open their message.
  const bool mail_list = card.kind == "mail" && !card.mail_items.empty();
  shell_->ShowPassive(kHoverSlot, rect, /*click_through=*/!mail_list);
  // A preview site's card holds a live view of the site's compact page, its own page in the tab's profile
  // (so the tab's sign-in applies); the tab itself is never navigated, reloaded or woken. Others show no page.
  const std::string live_url =
      card.kind == "preview" ? LivePreviewUrl(input.url) : std::string();
  // An asleep row has no page; its rows are this window's profile's (model()), so the card's page loads there.
  if (!live_url.empty()) {
    content::BrowserContext* context =
        contents ? contents->GetBrowserContext() : profile();
    shell_->ShowLivePreview(context, GURL(live_url), LivePreviewPageRect(rect),
                            LivePreviewUserAgent());
  } else {
    shell_->HideLivePreview();
  }
}

void CluWindowState::HoldHover(const std::string& row_id, bool held) {
  if (!hover_surface_ || hover_surface_->surface != "tab-hover" ||
      hover_row_id_.empty() || hover_row_id_ != row_id) {
    return;
  }
  mail_card_held_ = held;
  if (!held) {
    Hover(std::nullopt, std::nullopt, /*fade=*/true);
  }
}

bool CluWindowState::OpenMail(const std::string& row_id,
                              const std::string& url) {
  // Only a Gmail page, and only into a Gmail row: the card's links come from the feed, never from the page.
  const GURL target(url);
  if (!target.is_valid() || !target.SchemeIs(url::kHttpsScheme) ||
      target.host() != "mail.google.com") {
    return false;
  }
  CluModelService* m = model();
  const TabNode* node = m ? m->tree().Get(row_id) : nullptr;
  if (!node || !node->is_tab()) {
    return false;
  }
  content::WebContents* contents = m->FindContents(row_id);
  if (!GmailFeedUrl(contents ? PageUrl(contents) : node->url)) {
    return false;
  }
  HideHoverNow();
  if (contents) {
    // A live or discarded page loads the message in place; a discarded one wakes with it.
    content::NavigationController::LoadURLParams params(target);
    params.transition_type = ui::PAGE_TRANSITION_LINK;
    contents->GetController().LoadURLWithParams(params);
  } else {
    // An asleep row has no page: it wakes on the message when it activates (LoadRowPage), with the message as a new
    // entry on top of its saved history, so Back returns to where the row was. A pending reopen would wake it on its
    // old page instead, so it goes, and its history is the one the message lands on.
    TabNode* asleep = m->mutable_tree().Mutable(row_id);
    std::optional<std::string> history = asleep->nav_state;
    if (auto reopen = m->TakeReopenOverride(row_id)) {
      history = std::move(reopen->second);
    }
    asleep->url = target.spec();
    asleep->nav_state =
        history ? PushNavEntry(*history, target.spec()) : std::nullopt;
    m->Commit();
  }
  return ActivateRow(row_id);
}

void CluWindowState::OnGmailFeed(const std::string& row_id) {
  if (row_id.empty() || row_id != gmail_wait_row_id_) {
    return;
  }
  Hover(row_id, gmail_wait_anchor_);
}

void CluWindowState::Tooltip(const std::string& label,
                             std::vector<std::string> keys,
                             const std::optional<gfx::Rect>& rect,
                             bool above,
                             bool closing) {
  constexpr char kTooltipSurface[] = "tooltip";
  // A hover card fading out gives the slot up at once, so the pill neither waits for it nor goes with it.
  if (hover_close_timer_.IsRunning()) {
    HideHoverNow();
  }
  const bool open =
      hover_surface_ && hover_surface_->surface == kTooltipSurface;
  // A tab's hover card or a folder popover keeps the slot: the pointer is on a row, not on a button.
  if (hover_surface_ && !open) {
    return;
  }
  // The pill stays inside the window, as the frame placed it.
  gfx::Rect at = rect.value_or(gfx::Rect());
  at.AdjustToFit(gfx::Rect(shell_->layout().content_size));
  if (label.empty() || at.IsEmpty() || command_ || !prompts_.empty()) {
    if (open) {
      Hover(std::nullopt, std::nullopt);
    }
    return;
  }
  base::ListValue caps;
  for (std::string& key : keys) {
    caps.Append(std::move(key));
  }
  OverlaySurfaceSnapshot surface;
  surface.instance_id = open ? hover_surface_->instance_id : NewId();
  surface.surface = kTooltipSurface;
  surface.layer = "passive";
  surface.modality = "passive";
  surface.focus = false;
  surface.slot = kHoverSlot;
  surface.rect = at;
  surface.props = base::DictValue()
                      .Set("label", label)
                      .Set("keys", std::move(caps))
                      .Set("above", above)
                      .Set("closing", closing);
  hover_surface_ = std::move(surface);
  hover_row_id_.clear();
  MarkDirty();
  Flush();
  if (!hover_surface_) {
    return;
  }
  // A tooltip takes no clicks: the button under it gets them.
  shell_->ShowPassive(kHoverSlot, at, /*click_through=*/true);
}

bool CluWindowState::FolderPeek(const std::optional<std::string>& folder_id,
                                const std::optional<gfx::Rect>& rect,
                                std::optional<bool> held) {
  constexpr char kFolderPeekSurface[] = "folder-peek";
  // A hover card fading out gives the slot up at once, so its timer never hides the popover.
  if (hover_close_timer_.IsRunning()) {
    HideHoverNow();
  }
  const bool open = hover_surface_ && hover_surface_->surface == kFolderPeekSurface;
  if (!folder_id) {
    if (open && !folder_peek_held_) {
      Hover(std::nullopt, std::nullopt);
    }
    return true;
  }
  CluModelService* m = model();
  const TabNode* node = m ? m->tree().Get(*folder_id) : nullptr;
  if (!node || !node->is_folder()) {
    return false;
  }
  if (held) {
    if (open && hover_row_id_ == *folder_id) {
      folder_peek_held_ = *held;
      if (!*held) {
        Hover(std::nullopt, std::nullopt);
      }
    }
    return true;
  }
  if (!rect || command_ || !prompts_.empty()) {
    return true;
  }
  // The frame computed the popover beside its sidebar; keep it inside the window.
  gfx::Rect bounds = *rect;
  bounds.Intersect(gfx::Rect(shell_->layout().content_size));
  if (bounds.IsEmpty()) {
    return true;
  }
  // Another folder's popover is a new instance, so its search and highlight start fresh.
  OverlaySurfaceSnapshot surface;
  surface.instance_id = open && hover_row_id_ == *folder_id
                            ? hover_surface_->instance_id
                            : NewId();
  surface.surface = kFolderPeekSurface;
  surface.layer = "passive";
  surface.modality = "passive";
  // Its search field takes typing at once.
  surface.focus = true;
  surface.slot = kHoverSlot;
  surface.rect = bounds;
  surface.props = base::DictValue().Set("folderId", *folder_id);
  hover_surface_ = std::move(surface);
  hover_row_id_ = *folder_id;
  folder_peek_held_ = false;
  MarkDirty();
  Flush();
  if (!hover_surface_) {
    return true;
  }
  shell_->ShowPassive(kHoverSlot, bounds, /*click_through=*/false,
                      /*focus=*/true);
  return true;
}

std::vector<OverlaySurfaceSnapshot> CluWindowState::Surfaces() {
  std::vector<OverlaySurfaceSnapshot> surfaces;
  if (command_) {
    OverlaySurfaceSnapshot surface;
    surface.instance_id = command_instance_id_;
    surface.surface = "command";
    surface.props.Set("sessionId", command_->session_id);
    surfaces.push_back(std::move(surface));
  }
  for (const OverlaySurfaceSnapshot& prompt : prompts_) {
    surfaces.push_back(prompt);
  }
  if (!toasts_.empty()) {
    OverlaySurfaceSnapshot surface;
    surface.instance_id = toast_instance_id_;
    surface.surface = "toast";
    surface.layer = "passive";
    surface.modality = "passive";
    surface.focus = false;
    surface.slot = kToastSlot;
    surface.rect = ToastRect(shell_->layout().page_rect, toasts_.size());
    surfaces.push_back(std::move(surface));
  }
  if (switcher_surface_) {
    surfaces.push_back(*switcher_surface_);
  }
  if (hover_surface_) {
    surfaces.push_back(*hover_surface_);
  }
  if (multitask_) {
    multitask_->AddSurfaces(surfaces);
  }
  if (sidebar_peek_) {
    sidebar_peek_->AddSurface(surfaces);
  }
  return surfaces;
}

// Switcher (02-tabs F13) -------------------------------------------------------------------------------------------

std::vector<std::string> CluWindowState::RecentTabs() {
  std::vector<std::string> recent;
  for (const std::string& id : mru_.list()) {
    // A recent tab that left the tree (or, without a model, the strip) is not offered.
    if (CluModelService* m = model()) {
      const TabNode* node = m->tree().Get(id);
      if (node && node->is_tab() && !m->pending_clear().contains(id)) {
        recent.push_back(id);
      }
    } else if (FindTab(id)) {
      recent.push_back(id);
    }
  }
  return recent;
}

std::string CluWindowState::CurrentTabId() {
  return ActiveRowId();
}

void CluWindowState::ShowSwitcher(const SwitcherView& view) {
  base::ListValue cards;
  for (const std::string& id : view.tab_ids) {
    std::string title;
    std::string url;
    std::optional<std::string> favicon;
    if (CluModelService* m = model()) {
      if (const TabNode* node = m->tree().Get(id)) {
        title = node->title_override.value_or(node->title);
        url = node->url;
      }
      if (content::WebContents* contents = m->FindContents(id)) {
        title = base::UTF16ToUTF8(contents->GetTitle());
        url = PageUrl(contents);
        favicon = PageFaviconUrl(contents);
      }
    } else if (tabs::TabInterface* tab = FindTab(id)) {
      title = base::UTF16ToUTF8(tab->GetContents()->GetTitle());
      url = tab->GetContents()->GetVisibleURL().spec();
      favicon = PageFaviconUrl(tab->GetContents());
    }
    if (!favicon) {
      favicon = FaviconUrlFor(GURL(url));
    }
    cards.Append(base::DictValue()
                     .Set("tabId", id)
                     .Set("title", CappedText(title, 300))
                     .Set("host", DisplayHost(url))
                     .Set("favicon", favicon ? base::Value(*favicon)
                                             : base::Value())
                     .Set("thumb", base::Value()));
  }
  const gfx::Rect rect =
      SwitcherRect(shell_->layout().content_size, view.tab_ids.size());
  OverlaySurfaceSnapshot surface;
  surface.instance_id =
      switcher_surface_ ? switcher_surface_->instance_id : NewId();
  surface.surface = "tab-switcher";
  surface.layer = "passive";
  surface.modality = "passive";
  surface.focus = false;
  surface.slot = kSwitcherSlot;
  surface.rect = rect;
  surface.props = base::DictValue()
                      .Set("cards", std::move(cards))
                      .Set("selected", view.selected);
  switcher_surface_ = std::move(surface);
  MarkDirty();
  Flush();
  shell_->ShowPassive(kSwitcherSlot, rect);
}

void CluWindowState::HideSwitcher() {
  switcher_surface_.reset();
  shell_->HidePassive(kSwitcherSlot);
  MarkDirty();
  UpdateEventMonitor();
}

void CluWindowState::CommitSwitch(const std::string& tab_id) {
  if (model()) {
    ActivateRow(tab_id);
  } else if (tabs::TabInterface* tab = FindTab(tab_id)) {
    ActivateTab(tab);
  }
  UpdateEventMonitor();
}

void CluWindowState::OnEvent(const ui::Event& event) {
  if (event.type() == ui::EventType::kMousePressed) {
    // A press on the page beside the tweak editor. The editor's window is key, and AppKit would spend a press on
    // another window making that window key; the browser window becomes key first, so the press reaches the page.
    // The editor stays open.
    views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
        browser_->GetWindow()->GetNativeWindow());
    if (widget && !widget->IsActive() && TopOverlayRect()) {
      widget->Activate();
    }
    return;
  }
  if (!event.IsKeyEvent() || !switcher_->active()) {
    return;
  }
  const ui::KeyEvent* key = event.AsKeyEvent();
  // The Control key going up commits; so does any key without Control held, which covers a lost key-up. A Control
  // key-down is no release: Mac redispatches the flagsChanged of a press the page left alone, so it can arrive after
  // the Tab that started the session.
  if ((key->type() == ui::EventType::kKeyReleased &&
       key->key_code() == ui::VKEY_CONTROL) ||
      !key->IsControlDown()) {
    switcher_->ControlReleased();
  } else if (key->type() == ui::EventType::kKeyPressed &&
             key->key_code() == ui::VKEY_ESCAPE) {
    switcher_->Cancel();
  }
  UpdateEventMonitor();
}

void CluWindowState::UpdateEventMonitor() {
  if (!switcher_->active()) {
    event_monitor_.reset();
    return;
  }
  if (event_monitor_) {
    return;
  }
  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
      browser_->GetWindow()->GetNativeWindow());
  if (!widget) {
    return;
  }
  // The whole app's keys, not only the browser window's: with the command bar open the key window is the top
  // overlay's, and the Control release must still commit. Another window becoming key ends the session (Blur).
  event_monitor_ = views::EventMonitor::CreateApplicationMonitor(
      this, widget->GetNativeWindow(),
      {ui::EventType::kKeyPressed, ui::EventType::kKeyReleased});
}

void CluWindowState::OnShellChanged(bool urgent) {
  // The toast sits in the page card's corner and the switcher at a rect computed from the content size: after a
  // resize, full screen or a moved card they move with it, as the published state does. The hover card's anchor row
  // may have moved, so it goes.
  const bool card_moved = shell_->layout().page_rect != passive_page_rect_;
  passive_page_rect_ = shell_->layout().page_rect;
  if (card_moved && !toasts_.empty()) {
    UpdateToastSurface();
  }
  if (shell_->layout().content_size != passive_content_size_) {
    passive_content_size_ = shell_->layout().content_size;
    if (const std::optional<SwitcherView> view = switcher_->view()) {
      ShowSwitcher(*view);
    }
    Hover(std::nullopt, std::nullopt);
  }
  if (multitask_) {
    multitask_->OnShellChanged();
  }
  if (sidebar_peek_) {
    sidebar_peek_->OnShellChanged();
  }
  MarkDirty();
  // The frame WebUI starts its slide from the patch that carries the tween.
  if (urgent) {
    Flush();
  }
}

void CluWindowState::OnModelChanged() {
  MarkDirty();
}

void CluWindowState::OnTemplateURLServiceChanged() {
  MarkDirty();
}

void CluWindowState::OnTemplateURLServiceShuttingDown() {
  engines_observation_.Reset();
}

void CluWindowState::OnActiveChanged(BrowserWindowInterface* browser) {
  if (!browser_->IsActive()) {
    // 02-tabs F13: window blur cancels the switcher; hover cards go with it, and so does the hover peek (F4).
    switcher_->Blur();
    Hover(std::nullopt, std::nullopt);
    // A held peek took the keyboard into its own widget, which takes activation from the window: it stays.
    if (sidebar_peek_ && !sidebar_peek_->held()) {
      VLOG(1) << "clu-peek window blur";
      sidebar_peek_->Close();
    }
  } else {
    // A click on the page while the floating sidebar holds its Library.
    if (sidebar_peek_) {
      sidebar_peek_->WindowActivated();
    }
    // The user may have answered macOS's default browser prompt, or changed it in System Settings, while away.
    CluDefaultBrowser::Get().Refresh();
  }
  MarkDirty();
}

// State --------------------------------------------------------------------------------------------------------------

namespace {

// How often a Calendar row's page is read again while state changes.
constexpr base::TimeDelta kCalendarReadInterval = base::Seconds(30);
constexpr size_t kMaxCalendarSubtitleLength = 32;

// Runs in an isolated world on a calendar.google.com page, so the page's scripts never see it. Each event chip
// ([data-eventchip]) starts with its time ("4:30pm", "4:30 – 5pm", "16:30"); the earliest one at or after now is
// the next event. Returns "Event at 4:30 PM", or "" when the page shows none. Caps the chips and the text it reads.
constexpr char16_t kCalendarNextEventScript[] = uR"JS((() => {
  const now = new Date();
  const nowMin = now.getHours() * 60 + now.getMinutes();
  let best = -1;
  for (const chip of Array.from(document.querySelectorAll('[data-eventchip]')).slice(0, 300)) {
    const text = String(chip.textContent || '').slice(0, 300);
    const m = /(?<!\d)(\d{1,2})(?::([0-5]\d))?\s*([ap]m)\b|(?<!\d)(\d{1,2}):([0-5]\d)(?!\d)/i.exec(text);
    if (!m) continue;
    let h = Number(m[1] ?? m[4]);
    const min = Number(m[2] ?? m[5] ?? 0);
    const later = /\d\s*([ap]m)\b/i.exec(text.slice(m.index));
    const half = String(m[3] ?? (later ? later[1] : '')).toLowerCase();
    if (half) {
      if (h < 1 || h > 12) continue;
      h = (h % 12) + (half === 'pm' ? 12 : 0);
    }
    if (h > 23) continue;
    const at = h * 60 + min;
    if (at >= nowMin && (best < 0 || at < best)) best = at;
  }
  if (best < 0) return '';
  const h = Math.floor(best / 60);
  return 'Event at ' + (((h + 11) % 12) + 1) + ':' + String(best % 60).padStart(2, '0') + (h < 12 ? ' AM' : ' PM');
})())JS";

}  // namespace

void CluWindowState::ReadCalendarSubtitle(const std::string& row_id,
                                          content::WebContents* contents) {
  // A new page (another navigation) is read at once; the same page again after kCalendarReadInterval.
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  const int page = entry ? entry->GetUniqueID() : 0;
  const base::TimeTicks now = base::TimeTicks::Now();
  const auto [it, first] =
      calendar_read_at_.try_emplace(row_id, std::make_pair(page, now));
  if (!first && it->second.first == page &&
      now - it->second.second < kCalendarReadInterval) {
    return;
  }
  it->second = std::make_pair(page, now);
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      kCalendarNextEventScript,
      base::BindOnce(&CluWindowState::OnCalendarSubtitle,
                     weak_ptr_factory_.GetWeakPtr(), row_id),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void CluWindowState::OnCalendarSubtitle(const std::string& row_id,
                                        base::Value value) {
  // Only the one shape the script makes; anything else (a page that broke the script, junk) clears it.
  std::string subtitle;
  if (value.is_string() &&
      value.GetString().size() <= kMaxCalendarSubtitleLength &&
      value.GetString().starts_with("Event at ")) {
    subtitle = value.GetString();
  }
  const auto it = calendar_subtitles_.find(row_id);
  if (subtitle.empty()) {
    if (it == calendar_subtitles_.end()) {
      return;
    }
    calendar_subtitles_.erase(it);
  } else {
    if (it != calendar_subtitles_.end() && it->second == subtitle) {
      return;
    }
    calendar_subtitles_[row_id] = std::move(subtitle);
  }
  MarkDirty();
}

TabSnapshot CluWindowState::PageSnapshot(content::WebContents* contents) {
  content::NavigationController& controller = contents->GetController();
  const GURL& url = contents->GetVisibleURL();
  TabSnapshot tab;
  tab.id = CluTabId::Get(contents);
  tab.url = StateUrl(url);
  if (content::NavigationEntry* pending = controller.GetPendingEntry()) {
    tab.pending_url = StateUrl(pending->GetURL());
  }
  tab.title = contents->GetTitle();
  tab.favicon = PageFaviconUrl(contents);
  tab.icon_color = PageIconColor(contents);
  tab.loading = contents->IsLoading();
  tab.progress = tab.loading ? contents->GetLoadProgress() : 0;
  tab.can_go_back = controller.CanGoBack();
  tab.can_go_forward = controller.CanGoForward();
  tab.audible = contents->IsCurrentlyAudible();
  tab.muted = contents->IsAudioMuted();
  tab.discarded = contents->WasDiscarded();
  tab.secure = url.SchemeIsCryptographic();
  if (auto* zoom = zoom::ZoomController::FromWebContents(contents)) {
    tab.zoom_factor = zoom->GetZoomPercent() / 100.0;
  }
  return tab;
}

WindowSnapshot CluWindowState::TakeSnapshot() {
  // A snapshot looks up the page of every row of the profile (FindTab, FindContents), each a search of the tab
  // strips: answer them from one pass instead. Nothing here changes a tab strip.
  std::map<std::string, raw_ptr<tabs::TabInterface>, std::less<>> tab_index;
  TabStripModel* index_strip = browser_->GetTabStripModel();
  for (int i = 0; i < index_strip->count(); ++i) {
    content::WebContents* contents = index_strip->GetWebContentsAt(i);
    if (!CluPlaceholder::Is(contents) && CluTabId::Has(contents)) {
      tab_index.try_emplace(CluTabId::Get(contents), index_strip->GetTabAtIndex(i));
    }
  }
  const base::AutoReset<raw_ptr<const std::map<
      std::string, raw_ptr<tabs::TabInterface>, std::less<>>>>
      tab_index_scope(&tab_index_, &tab_index);
  std::optional<CluModelService::ContentsIndexScope> contents_index;
  if (CluModelService* m = model()) {
    contents_index.emplace(*m);
  }

  WindowSnapshot s;
  s.version = std::string(version_info::GetVersionNumber());
  s.window_id = window_id_;
  s.profile_id = profile_id_;
  s.space_id = space_id_;
  s.off_the_record = profile()->IsOffTheRecord();
  s.profile_name = s.off_the_record ? kPrivateSpaceName : kDefaultProfileName;
  if (swipe_) {
    s.swipe = SwipeSnapshot{.fraction = swipe_->fraction,
                            .target_space_id = swipe_->target_space_id};
  }
  s.wheel = WheelSnapshot{.phased = wheel_phased_, .lifts = wheel_lifts_};
  // A held floating sidebar has the keyboard in its own widget: the window still counts as focused.
  s.focused = browser_->IsActive() || (sidebar_peek_ && sidebar_peek_->held());
  s.fullscreen = shell_->chrome_state().fullscreen;
  s.dark = ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
           ui::NativeTheme::PreferredColorScheme::kDark;
  s.reduce_motion = gfx::Animation::PrefersReducedMotion();

  if (model()) {
    FillModelSnapshot(s);
  } else {
    std::map<std::string, double, std::less<>> last_active_at;
    TabStripModel* strip = browser_->GetTabStripModel();
    for (int i = 0; i < strip->count(); ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      if (CluPlaceholder::Is(contents)) {
        continue;
      }
      TabSnapshot tab = PageSnapshot(contents);
      tab.title_override = CluPageName::Get(contents);
      if (const auto it = last_active_at_.find(tab.id);
          it != last_active_at_.end()) {
        tab.last_active_at = it->second;
        last_active_at.insert(*it);
      }
      s.tabs.push_back(std::move(tab));
    }
    // Closed tabs drop out of the activity map.
    last_active_at_ = std::move(last_active_at);
  }

  const std::string active = ActiveRowId();
  if (!active.empty()) {
    s.active_tab_id = active;
  }
  s.sidebar_request = sidebar_request_;
  s.chrome = shell_->chrome_state();
  s.layout = shell_->layout();
  s.anim = shell_->sidebar_anim();
  s.peeking = sidebar_peek_ && sidebar_peek_->is_open();
  s.titlebar_reveal = shell_->titlebar_reveal();
  s.command = command_;
  s.surfaces = Surfaces();
  s.top_overlay_rect = TopOverlayRect();
  s.toasts = toasts_;
  s.archive_panel = archive_panel_;
  s.search_engine_id = DefaultSearchEngine(profile()).id;
  s.update = CluUpdateService::Get().Snapshot();
  s.default_browser = std::string(CluDefaultBrowser::Get().state_name());
  s.downloads = downloads_->Facts();
  s.screenshot_arrivals = screenshots_->arrivals();
  app_state_->Fill(s);
  site_->Fill(s);
  if (multitask_) {
    multitask_->FillSnapshot(s);
  }
  return s;
}

void CluWindowState::FillModelSnapshot(WindowSnapshot& s) {
  CluModelService& m = *model_;
  const CluTabTree& tree = m.tree();
  s.has_model = true;
  s.persistent = m.persistent();
  s.recovered = m.recovered();
  s.archive_revision = m.archive_revision();
  s.archive_after_hours = m.archive_after_hours();
  const std::set<std::string>& pending = m.pending_clear();

  auto refs = [&](const std::vector<std::string>& ids) {
    std::vector<NodeRefSnapshot> out;
    for (const std::string& id : ids) {
      if (const TabNode* node = tree.Get(id)) {
        out.push_back({.folder = node->is_folder(), .id = id});
      }
    }
    return out;
  };

  auto own_lists = [&](SpaceSnapshot& space) {
    space.pinned = refs(tree.Pinned(space.id));
    for (const std::string& id : tree.Today(space.id)) {
      // A pending Clear's rows are off the list until it commits or is undone.
      if (!pending.contains(id)) {
        space.today.push_back(id);
      }
    }
    space.pinned_collapsed = tree.SpaceState(space.id).pinned_collapsed;
  };
  if (CluSpacesService* service = CluSpacesService::Get()) {
    // Every space across profiles, in switcher order (M4); only this profile's spaces carry their lists.
    const CluSpaceList& list = service->list();
    for (const ProfileEntry& profile : list.profiles()) {
      s.profiles.push_back(ProfileSnapshot());
      s.profiles.back().id = profile.id;
      s.profiles.back().name = profile.name;
      s.profiles.back().is_default = profile.is_default;
      if (profile.id == m.profile_id()) {
        s.profile_name = profile.name;
      }
    }
    for (const SpaceEntry& entry : list.spaces()) {
      SpaceSnapshot space;
      space.id = entry.id;
      space.name = entry.name;
      space.icon_kind = entry.icon_kind;
      space.icon_value = entry.icon_value;
      space.color = entry.color;
      space.profile_id = entry.profile_id;
      if (entry.profile_id == m.profile_id() && m.FindSpace(entry.id)) {
        own_lists(space);
      }
      s.spaces.push_back(std::move(space));
    }
  } else {
    for (const SpaceRow& row : m.spaces()) {
      SpaceSnapshot space;
      space.id = row.id;
      space.name = row.name;
      space.icon_kind = row.icon_kind;
      space.icon_value = row.icon_value;
      space.color = row.color;
      own_lists(space);
      s.spaces.push_back(std::move(space));
    }
  }
  s.favorites = tree.Favorites();

  for (const TabNode* node : tree.All()) {
    if (node->is_folder()) {
      FolderSnapshot folder;
      folder.id = node->id;
      folder.space_id = node->space_id;
      folder.name = node->title_override.value_or(kDefaultFolderName);
      folder.icon = node->custom_icon;
      folder.collapsed = node->collapsed;
      folder.children = refs(tree.Children(node->id));
      s.folders.push_back(std::move(folder));
      continue;
    }
    content::WebContents* contents = m.FindContents(node->id);
    TabSnapshot tab;
    if (contents) {
      tab = PageSnapshot(contents);
    } else {
      tab.id = node->id;
      tab.url = node->url;
      tab.title = base::UTF8ToUTF16(node->title);
      // A favorite, pinned or Today row never opened here gets its site's icon fetched once.
      const GURL page_url(node->url);
      if (!icon_backfill_) {
        icon_backfill_ = std::make_unique<CluIconBackfill>(
            profile(), base::BindRepeating(&CluWindowState::MarkDirty,
                                           weak_ptr_factory_.GetWeakPtr()));
      }
      tab.favicon = FaviconUrlFor(page_url, icon_backfill_->Version(page_url));
      tab.muted = node->muted;
      tab.live = false;
    }
    tab.id = node->id;
    tab.section = std::string(SectionName(node->section));
    tab.unread_badge = node->section != Section::kFavorite ||
                       m.UnreadBadgeShown(node->id);
    tab.space_id = node->space_id;
    if (!node->parent_id.empty()) {
      tab.parent_id = node->parent_id;
    }
    tab.pinned_url = node->pinned_url;
    // tab.url is the display form (rio://settings/); the pinned URL is the real one (chrome://settings/).
    tab.off_pinned =
        IsOffPinned(contents ? PageUrl(contents) : node->url, node->pinned_url);
    // A pinned row keeps its title while it is away from its pinned URL: the
    // row shows the title it had there, which the model stops updating meanwhile.
    if (tab.off_pinned && node->anchored() && !node->title.empty()) {
      tab.title = base::UTF8ToUTF16(node->title);
    }
    tab.title_override = node->title_override;
    // A loaded Google Calendar pinned row shows its next event under the title; a row with no page shows
    // nothing.
    if (contents && node->section == Section::kPinned &&
        !contents->IsLoading() && !contents->WasDiscarded() &&
        contents->GetLastCommittedURL().SchemeIs(url::kHttpsScheme) &&
        contents->GetLastCommittedURL().host() == "calendar.google.com") {
      ReadCalendarSubtitle(node->id, contents);
      if (const auto it = calendar_subtitles_.find(node->id);
          it != calendar_subtitles_.end()) {
        tab.subtitle = it->second;
      }
    }
    tab.custom_icon = node->custom_icon;
    tab.split_id = node->split_id;
    tab.muted = node->muted;
    tab.last_active_at = static_cast<double>(node->last_active_at);
    s.tabs.push_back(std::move(tab));
  }
  s.profile_id = m.profile_id();
}

void CluWindowState::Publish() {
  // The peek reconciles on every state batch: a surface that opened since the peek did closes it.
  if (sidebar_peek_ && sidebar_peek_->is_open()) {
    sidebar_peek_->Reconcile(Surfaces());
  }
  publish_timer_.Stop();
  dirty_ = false;
  const WindowSnapshot snapshot = TakeSnapshot();
  // Rebuilds and compares only the tabs whose snapshot changed; the rest of state is small and built whole.
  base::ListValue ops =
      UpdateCluState(published_, published_tabs_, snapshot);
  // The incremental update lands on the state a full build gives (dev builds run with DCHECKs on).
  DCHECK(published_ == BuildCluState(snapshot));
  app_state_->Apply(snapshot);
  if (rev_ == 0) {
    // Nothing went out yet: every subscriber starts from the snapshot.
    rev_ = 1;
    return;
  }
  if (ops.empty()) {
    return;
  }
  ++rev_;
  if (observers_.empty()) {
    return;
  }
  const std::string message = PatchMessage(std::move(ops), rev_);
  for (auto& observer : observers_) {
    observer->OnState(message);
  }
}

void CluWindowState::WatchScreenshotsForTesting(const base::FilePath& folder) {
  screenshots_ = std::make_unique<CluScreenshotWatcher>(
      folder, base::BindRepeating(&CluWindowState::MarkDirty,
                                  base::Unretained(this)));
}

bool CluWindowState::HasPlaceholderForTesting() {
  TabStripModel* strip = browser_->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    if (CluPlaceholder::Is(strip->GetWebContentsAt(i))) {
      return true;
    }
  }
  return false;
}

}  // namespace clu
