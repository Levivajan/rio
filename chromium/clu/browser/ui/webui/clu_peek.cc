// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_peek.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/content_settings/page_specific_content_settings_delegate.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/find_bar/find_bar_state.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "clu/browser/ai/clu_ai_client.h"
#include "clu/browser/command/clu_copy_url.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/clu_shortcuts.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/multitask/clu_peek_rules.h"
#include "clu/browser/ui/views/frame/clu_peek_view.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/content_settings/browser/page_specific_content_settings.h"
#include "components/find_in_page/find_types.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/permissions/permission_recovery_success_rate_tracker.h"
#include "components/permissions/permission_request_manager.h"
#include "components/zoom/page_zoom.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_zoom.h"
#include "content/public/common/referrer.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/page_transition_types.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"

namespace clu {

namespace {

constexpr base::TimeDelta kCopyToast = base::Milliseconds(1500);

// Table commands that act on the focused page (PLAN 3.8) and what each does in
// a Peek. Only the main chord: the alternates stay with the page so its text
// fields keep Cmd+Left and Cmd+Right.
struct PageCommand {
  const char* id;
  std::optional<CluPeek::NavAction> nav;
  bool copy = false;
  bool markdown = false;
};
constexpr PageCommand kPageCommands[] = {
    {"nav.back", CluPeek::NavAction::kBack},
    {"nav.forward", CluPeek::NavAction::kForward},
    {"nav.reload", CluPeek::NavAction::kReload},
    {"nav.hardReload", CluPeek::NavAction::kHardReload},
    {"nav.stop", CluPeek::NavAction::kStop},
    {"edit.copyUrl", std::nullopt, true, false},
    {"edit.copyUrlMarkdown", std::nullopt, true, true},
};

const PageCommand* PageCommandFor(const ui::Accelerator& accelerator) {
  for (const PageCommand& command : kPageCommands) {
    const CommandSpec* spec = FindCommand(command.id);
    // The page command's key as the user set it in Settings, Shortcuts.
    const std::optional<CommandAccelerator> key =
        spec ? KeysFor(*spec).primary : std::nullopt;
    if (key && ui::Accelerator(key->key, key->modifiers) == accelerator) {
      return &command;
    }
  }
  return nullptr;
}

std::string NewInstanceId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

gfx::Rect CenterOf(const gfx::Rect& rect) {
  return gfx::Rect(rect.CenterPoint(), gfx::Size(1, 1));
}

}  // namespace

CluPeek::CluPeek(CluWindowState& window) : window_(window) {
  // BrowserWidget tears down the window's features, the find bar among them,
  // before the BrowserView and this Peek go (browser_widget.cc).
  if (BrowserWindowInterface* browser = window_->browser()) {
    browser_did_close_subscription_ = browser->RegisterBrowserDidClose(
        base::BindRepeating(&CluPeek::OnBrowserDidClose,
                            base::Unretained(this)));
  }
}

void CluPeek::OnBrowserDidClose(BrowserWindowInterface* browser) {
  ReleasePage(/*window_closing=*/true);
}

CluPeek::~CluPeek() {
  attach_timer_.Stop();
  close_timer_.Stop();
  if (page_) {
    ReleasePage(/*window_closing=*/true);
    Observe(nullptr);
    page_->SetDelegate(nullptr);
    if (CluPeekView* view = window_->shell().peek_view()) {
      view->SetPage(nullptr);
    }
  }
}

void CluPeek::Open(const std::string& source_row_id,
                   const GURL& url,
                   const content::Referrer& referrer) {
  if (!IsPeekableUrl(url)) {
    return;
  }
  std::unique_ptr<content::WebContents> page;
  if (!page_) {
    page = content::WebContents::Create(
        content::WebContents::CreateParams(window_->profile()));
  }
  content::WebContents* target = page ? page.get() : page_.get();
  // The open Peek's page takes a new first navigation: one that turns into a
  // download closes the Peek, as for a new page.
  committed_ = false;
  want_summary_ = false;
  from_hover_ = false;
  summary_.reset();
  content::NavigationController::LoadURLParams params(url);
  params.referrer = referrer;
  params.transition_type = ui::PAGE_TRANSITION_LINK;
  target->GetController().LoadURLWithParams(params);
  if (page) {
    Show(source_row_id, std::move(page));
  } else {
    // A new Peek while one is open: the same page navigates and the card
    // replays from its origin.
    Show(source_row_id, nullptr);
  }
}

content::WebContents* CluPeek::Adopt(
    const std::string& source_row_id,
    std::unique_ptr<content::WebContents> page) {
  if (!page) {
    return nullptr;
  }
  content::WebContents* raw = page.get();
  if (page_) {
    // One Peek per window: the new page replaces the open one.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                               TakePage());
  }
  Show(source_row_id, std::move(page));
  return raw;
}

void CluPeek::Show(const std::string& source_row_id,
                   std::unique_ptr<content::WebContents> page) {
  close_timer_.Stop();
  closing_.reset();
  if (page) {
    page_ = std::move(page);
    page_->SetDelegate(this);
    Observe(page_.get());
    ConfigurePage(page_.get());
    committed_ = false;
  }
  source_row_id_ = source_row_id;
  want_summary_ = false;
  from_hover_ = false;
  summary_.reset();
  ++seq_;
  instance_id_ = NewInstanceId();
  attached_ = false;
  card_rect_ = PeekCardRect(window_->shell().layout().page_rect);
  origin_rect_ = CenterOf(card_rect_);

  CluShell& shell = window_->shell();
  CluPeekView* view = shell.EnsurePeekView();
  view->SetPage(page_.get());
  view->SetCard(PageBounds());
  view->SetPageVisible(false);
  shell.SetPeekShown(true);
  // The overlay plays the open animation from this state, then reports it done;
  // 250 ms at the latest.
  window_->MarkDirty();
  window_->Flush();
  attach_timer_.Start(
      FROM_HERE, base::Milliseconds(kPeekAttachFallbackMs),
      base::BindOnce(&CluPeek::Attach, weak_ptr_factory_.GetWeakPtr()));
}

void CluPeek::Attach() {
  attach_timer_.Stop();
  if (!page_ || attached_) {
    return;
  }
  attached_ = true;
  if (CluPeekView* view = window_->shell().peek_view()) {
    view->SetPageVisible(true);
  }
  page_->Focus();
  window_->MarkDirty();
}

void CluPeek::AnimationDone(int seq) {
  if (closing_ && closing_->seq == seq) {
    FinishClose();
    return;
  }
  if (page_ && seq == seq_) {
    Attach();
  }
}

void CluPeek::Close() {
  if (!page_) {
    return;
  }
  // The overlay shrinks the card toward the origin, then reports it done; 250
  // ms at the latest (05 "Closing").
  closing_ = Closing{
      .seq = ++seq_, .origin_rect = origin_rect_, .card_rect = card_rect_};
  instance_id_ = NewInstanceId();
  std::unique_ptr<content::WebContents> page = TakePage();
  window_->MarkDirty();
  window_->Flush();
  close_timer_.Start(
      FROM_HERE, base::Milliseconds(kPeekCloseFallbackMs),
      base::BindOnce(&CluPeek::FinishClose, weak_ptr_factory_.GetWeakPtr()));
  window_->FocusActivePage();
  // Destroyed after the view let go of it, and later: Close() runs from the
  // page's own delegate calls (a new tab from the Peek, Cmd+W through its
  // PreHandleKeyboardEvent), so the page must outlive them.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(page));
}

void CluPeek::FinishClose() {
  close_timer_.Stop();
  closing_.reset();
  if (!page_) {
    window_->shell().SetPeekShown(false);
  }
  window_->MarkDirty();
}

void CluPeek::ConfigurePage(content::WebContents* page) {
  FindBarState::ConfigureWebContents(page);
  zoom::ZoomController::CreateForWebContents(page);
  if (!content_settings::PageSpecificContentSettings::GetDelegateForWebContents(
          page)) {
    content_settings::PageSpecificContentSettings::CreateForWebContents(
        page, std::make_unique<PageSpecificContentSettingsDelegate>(page));
  }
  permissions::PermissionRequestManager::CreateForWebContents(page);
  permissions::PermissionRecoverySuccessRateTracker::CreateForWebContents(page);
  // The prompt factory and the bubbles find the window of a page with no tab
  // here (permission_prompt_factory.cc GetBrowser()).
  webui::SetBrowserWindowInterface(page, window_->browser());
}

void CluPeek::ReleasePage(bool window_closing) {
  if (!page_) {
    return;
  }
  if (FindBarTracksPage()) {
    // The find bar observes the page's FindTabHelper: it must let go before the
    // page goes.
    FindBarController* find = FindBarController::From(window_->browser());
    if (window_closing) {
      find->ChangeWebContents(nullptr);
    } else {
      find->EndFindSession(find_in_page::SelectionAction::kKeep,
                           find_in_page::ResultAction::kKeep);
      find->ChangeWebContents(
          window_->browser()->GetTabStripModel()->GetActiveWebContents());
    }
  }
  // A tab sets its own context, and the two may not both be set
  // (webui_embedding_context.cc).
  webui::SetBrowserWindowInterface(page_.get(), nullptr);
}

bool CluPeek::FindBarTracksPage() {
  FindBarController* find =
      page_ ? FindBarController::From(window_->browser()) : nullptr;
  return find && find->HasFindBar() && find->web_contents() == page_.get();
}

bool CluPeek::FindBarOnPage() {
  // Esc hides the bar without detaching it; a hidden bar takes no commands.
  return FindBarTracksPage() &&
         FindBarController::From(window_->browser())
             ->find_bar()
             ->IsFindBarVisible();
}

void CluPeek::Find(bool find_next, bool forward) {
  FindBarController* find = FindBarController::From(window_->browser());
  if (!find || !page_) {
    return;
  }
  if (find->web_contents() != page_.get()) {
    find->ChangeWebContents(page_.get());
  }
  find->Show(find_next, forward);
}

std::unique_ptr<content::WebContents> CluPeek::TakePage() {
  attach_timer_.Stop();
  ReleasePage(/*window_closing=*/false);
  attached_ = false;
  if (CluPeekView* view = window_->shell().peek_view()) {
    view->SetPage(nullptr);
  }
  Observe(nullptr);
  if (page_) {
    page_->SetDelegate(nullptr);
  }
  return std::move(page_);
}

void CluPeek::PostClose() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&CluPeek::Close, weak_ptr_factory_.GetWeakPtr()));
}

std::optional<std::string> CluPeek::Expand() {
  if (!page_) {
    return std::nullopt;
  }
  // Peeking from a Today row puts the new tab right after it; from a pinned row
  // or a favorite, at the top of Today.
  std::optional<std::string> after;
  if (CluModelService* model = window_->model()) {
    const TabNode* source = model->tree().Get(source_row_id_);
    if (source && source->section == Section::kToday) {
      after = source_row_id_;
    }
  }
  std::unique_ptr<content::WebContents> page = TakePage();
  std::optional<std::string> row =
      window_->multitask().AdoptContentsAsRow(std::move(page), after);
  closing_.reset();
  window_->shell().SetPeekShown(false);
  window_->MarkDirty();
  return row;
}

bool CluPeek::ToSplit() {
  if (!page_) {
    return false;
  }
  const std::string source = source_row_id_;
  CluModelService* model = window_->model();
  const TabNode* node = model ? model->tree().Get(source) : nullptr;
  // A favorite has no space to split in, and a full split takes no pane: the
  // Peek stays (05 "Open in split").
  if (!node || node->section == Section::kFavorite || model->SplitOf(source)) {
    window_->ShowToast(kSplitFullToast, std::nullopt, base::Seconds(3));
    return false;
  }
  const std::optional<std::string> row = Expand();
  if (!row) {
    return false;
  }
  return window_->multitask()
      .CreateSplit({source, *row}, SplitDir::kRow, *row)
      .has_value();
}

bool CluPeek::Nav(NavAction action) {
  if (!page_) {
    return false;
  }
  content::NavigationController& controller = page_->GetController();
  switch (action) {
    case NavAction::kBack:
      if (controller.CanGoBack()) {
        controller.GoBack();
      }
      break;
    case NavAction::kForward:
      if (controller.CanGoForward()) {
        controller.GoForward();
      }
      break;
    case NavAction::kReload:
      controller.Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
      break;
    case NavAction::kHardReload:
      controller.Reload(content::ReloadType::BYPASSING_CACHE,
                        /*check_for_repost=*/true);
      break;
    case NavAction::kStop:
      page_->Stop();
      break;
  }
  return true;
}

void CluPeek::CopyUrl(bool markdown) {
  const std::optional<std::string> url =
      page_ ? ShareableUrl(page_->GetLastCommittedURL()) : std::nullopt;
  if (!url) {
    window_->ShowToast("No URL to copy", std::nullopt, kCopyToast);
    return;
  }
  const std::string text =
      markdown ? MarkdownLink(page_->GetTitle(), *url) : *url;
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(base::UTF8ToUTF16(text));
  }
  window_->ShowToast(markdown ? "Copied link as Markdown" : "Copied Current URL",
                     std::nullopt, kCopyToast);
}

bool CluPeek::HasFocus() {
  if (!page_ || !attached_) {
    return false;
  }
  // The views focus follows the page's (WebView::OnWebContentsFocused), and
  // stays while the menu bar is open.
  CluPeekView* peek_view = window_->shell().peek_view();
  if (peek_view && peek_view->page_view()->HasFocus()) {
    return true;
  }
  content::RenderWidgetHostView* view = page_->GetRenderWidgetHostView();
  return view && view->HasFocus();
}

bool CluPeek::TakesPageCommands() {
  return page_ && attached_ && (HasFocus() || FindBarOnPage());
}

std::optional<bool> CluPeek::RunPageCommand(std::string_view id, bool run) {
  if (!TakesPageCommands()) {
    return std::nullopt;
  }
  static constexpr std::pair<std::string_view, NavAction> kNav[] = {
      {"nav.back", NavAction::kBack},
      {"nav.forward", NavAction::kForward},
      {"nav.reload", NavAction::kReload},
      {"nav.hardReload", NavAction::kHardReload},
      {"nav.stop", NavAction::kStop}};
  for (const auto& [name, action] : kNav) {
    if (id != name) {
      continue;
    }
    if (run) {
      return Nav(action);
    }
    content::NavigationController& controller = page_->GetController();
    switch (action) {
      case NavAction::kBack:
        return controller.CanGoBack();
      case NavAction::kForward:
        return controller.CanGoForward();
      case NavAction::kStop:
        return page_->IsLoading();
      case NavAction::kReload:
      case NavAction::kHardReload:
        return true;
    }
  }
  if (id == "edit.copyUrl" || id == "edit.copyUrlMarkdown") {
    if (run) {
      CopyUrl(id == "edit.copyUrlMarkdown");
    }
    return true;
  }
  if (id == "find.open" || id == "find.next" || id == "find.prev") {
    if (run) {
      Find(/*find_next=*/id != "find.open", /*forward=*/id != "find.prev");
    }
    return true;
  }
  static constexpr std::pair<std::string_view, content::PageZoom> kZoom[] = {
      {"zoom.in", content::PAGE_ZOOM_IN},
      {"zoom.out", content::PAGE_ZOOM_OUT},
      {"zoom.reset", content::PAGE_ZOOM_RESET}};
  for (const auto& [name, zoom] : kZoom) {
    if (id != name) {
      continue;
    }
    if (run) {
      zoom::PageZoom::Zoom(page_.get(), zoom);
      return true;
    }
    switch (zoom) {
      case content::PAGE_ZOOM_IN:
        return chrome::CanZoomIn(page_.get());
      case content::PAGE_ZOOM_OUT:
        return chrome::CanZoomOut(page_.get());
      case content::PAGE_ZOOM_RESET:
        return chrome::CanResetZoom(page_.get());
    }
  }
  return std::nullopt;
}

void CluPeek::Relayout() {
  if (!page_) {
    return;
  }
  const gfx::Rect card = PeekCardRect(window_->shell().layout().page_rect);
  if (card == card_rect_) {
    return;
  }
  card_rect_ = card;
  if (CluPeekView* view = window_->shell().peek_view()) {
    view->SetCard(PageBounds());
  }
  window_->MarkDirty();
}

std::optional<PeekSnapshot> CluPeek::Snapshot() {
  if (!page_) {
    return std::nullopt;
  }
  PeekSnapshot peek;
  peek.seq = seq_;
  const GURL& url = page_->GetVisibleURL();
  peek.url = StateUrl(url);
  peek.host = PeekDisplayHost(url);
  peek.title = page_->GetTitle();
  // As a live row's icon: the URL carries a hash of the icon Chromium shows, so the frame asks again once it arrives.
  content::NavigationEntry* entry = page_->GetController().GetLastCommittedEntry();
  peek.favicon = entry && entry->GetFavicon().valid
                     ? FaviconUrlFor(url, base::NumberToString(base::PersistentHash(
                                              entry->GetFavicon().url.spec())))
                     : FaviconUrlFor(url);
  peek.can_go_back = page_->GetController().CanGoBack();
  peek.origin_rect = origin_rect_;
  peek.card_rect = card_rect_;
  peek.summary = summary_;
  return peek;
}

std::optional<OverlaySurfaceSnapshot> CluPeek::Surface() {
  if (!page_ && !closing_) {
    return std::nullopt;
  }
  OverlaySurfaceSnapshot surface;
  surface.instance_id = instance_id_;
  surface.surface = "peek";
  surface.layer = "under-peek";
  surface.modality = "passive";
  surface.focus = false;
  if (closing_) {
    // PeekClosingFrame in src/shared/multitask.ts; Chromium hands over no
    // capture, so the frame shrinks empty.
    surface.props.Set(
        "closing",
        base::DictValue()
            .Set("seq", closing_->seq)
            .Set("originRect",
                 base::DictValue()
                     .Set("x", closing_->origin_rect.x())
                     .Set("y", closing_->origin_rect.y())
                     .Set("width", closing_->origin_rect.width())
                     .Set("height", closing_->origin_rect.height()))
            .Set("cardRect", base::DictValue()
                                 .Set("x", closing_->card_rect.x())
                                 .Set("y", closing_->card_rect.y())
                                 .Set("width", closing_->card_rect.width())
                                 .Set("height", closing_->card_rect.height()))
            .Set("image", base::Value())
            .Set("shrinking", true));
  }
  return surface;
}

content::WebContents* CluPeek::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  if (params.disposition == WindowOpenDisposition::kCurrentTab) {
    // Navigating inside a Peek stays inside it (05 "Peek rules").
    base::WeakPtr<content::NavigationHandle> handle =
        source->GetController().LoadURLWithParams(
            content::NavigationController::LoadURLParams(params));
    if (handle && navigation_handle_callback) {
      std::move(navigation_handle_callback).Run(*handle);
    }
    return source;
  }
  // Any other disposition opens a normal Today tab and closes the Peek.
  if (IsWebUrl(params.url)) {
    std::ignore = window_->CreateRow(params.url, std::nullopt,
                                     /*activate=*/true);
  }
  PostClose();
  return nullptr;
}

content::WebContents* CluPeek::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  // A window.open from inside the Peek opens a normal Today tab and closes the
  // Peek (05 "Peek rules").
  // When no row takes it, AdoptContentsAsRow has already freed the contents,
  // so Chromium must not get the pointer back.
  content::WebContents* raw = new_contents.get();
  const bool adopted = window_->multitask()
                           .AdoptContentsAsRow(std::move(new_contents),
                                               std::nullopt)
                           .has_value();
  PostClose();
  return adopted ? raw : nullptr;
}

void CluPeek::CloseContents(content::WebContents* source) {
  // window.close() in the Peek page.
  PostClose();
}

content::KeyboardEventProcessingResult CluPeek::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown) {
    // Esc always closes the Peek; a Peek page has
    // no HTML full screen to leave first.
    if (event.windows_key_code == ui::VKEY_ESCAPE &&
        !(event.GetModifiers() & blink::WebInputEvent::kKeyModifiers)) {
      PostClose();
      return content::KeyboardEventProcessingResult::HANDLED;
    }
    // Page commands act on the Peek page, not on the tab under it (PLAN 3.8):
    // the main menu would resolve them to the active tab.
    if (const PageCommand* command = PageCommandFor(
            ui::GetAcceleratorFromNativeWebKeyboardEvent(event))) {
      if (command->nav) {
        Nav(*command->nav);
      } else {
        CopyUrl(command->markdown);
      }
      return content::KeyboardEventProcessingResult::HANDLED;
    }
  }
  return window_->shell().PreHandleKeyboardEvent(source, event);
}

bool CluPeek::HandleKeyboardEvent(content::WebContents* source,
                                  const input::NativeWebKeyboardEvent& event) {
  return window_->shell().HandleKeyboardEvent(source, event);
}

void CluPeek::DidFinishNavigation(content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame()) {
    return;
  }
  // A first navigation that turns into a download closes the Peek; the download
  // bubble handles the file.
  if (handle->IsDownload() && !committed_) {
    PostClose();
    return;
  }
  if (handle->HasCommitted()) {
    committed_ = true;
  }
  window_->MarkDirty();
}

void CluPeek::TitleWasSet(content::NavigationEntry* entry) {
  window_->MarkDirty();
}

gfx::Rect CluPeek::PageBounds() const {
  gfx::Rect page = card_rect_;
  if (summary_) {
    page.Inset(gfx::Insets::TLBR(std::min(kSummaryStrip, page.height() / 2), 0,
                                 0, 0));
  }
  return page;
}

void CluPeek::Summarize() {
  want_summary_ = page_ != nullptr;
  from_hover_ = want_summary_;
}

void CluPeek::DocumentOnLoadCompletedInPrimaryMainFrame() {
  if (!want_summary_ || !page_) {
    return;
  }
  want_summary_ = false;
  // The page's own text, read in an isolated world so the page's scripts cannot see or change the call.
  page_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"(document.body ? document.body.innerText : '').slice(0, 6000)",
      base::BindOnce(&CluPeek::OnPageText, weak_ptr_factory_.GetWeakPtr(),
                     seq_),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void CluPeek::OnPageText(int seq, base::Value text) {
  if (seq != seq_ || !page_ || !text.is_string() ||
      base::TrimWhitespaceASCII(text.GetString(), base::TRIM_ALL).empty()) {
    return;
  }
  AskOnDeviceModel({"summary", base::UTF16ToUTF8(page_->GetTitle()),
                    page_->GetVisibleURL().spec(), text.GetString()},
                   base::BindOnce(&CluPeek::OnSummary,
                                  weak_ptr_factory_.GetWeakPtr(), seq));
}

void CluPeek::OnSummary(int seq, std::optional<std::string> answer) {
  if (seq != seq_ || !page_ || !answer) {
    return;
  }
  std::string summary = std::move(*answer);
  base::TruncateUTF8ToByteSize(summary, 600, &summary);
  summary_ = std::move(summary);
  // The page moves down under the summary strip the overlay draws at the card's top.
  if (CluPeekView* view = window_->shell().peek_view()) {
    view->SetCard(PageBounds());
  }
  window_->MarkDirty();
}

void CluPeek::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  PostClose();
}

}  // namespace clu
