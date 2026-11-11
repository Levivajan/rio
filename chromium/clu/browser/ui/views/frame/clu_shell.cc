// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_shell.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/clu_shortcuts.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/multitask/clu_mini_rules.h"
#include "clu/browser/ui/views/frame/clu_anchor_view.h"
#include "clu/browser/ui/views/frame/clu_look.h"
#include "clu/browser/ui/views/frame/clu_peek_view.h"
#include "clu/browser/ui/views/frame/clu_key_equivalent.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "clu/browser/ui/views/frame/clu_ui_zoom.h"
#include "clu/common/clu_urls.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/mojom/page/draggable_region.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/base_window.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/events/blink/web_input_event.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/draggable_region.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace clu {

namespace {

CluShell::ClientFactory g_client_factory = nullptr;

// ScopedCreatingMiniWindow nesting depth.
int g_creating_mini = 0;

int64_t NowMs() {
  return base::Time::Now().InMillisecondsSinceUnixEpoch();
}

double NowMsF() {
  return base::Time::Now().InMillisecondsFSinceUnixEpochIgnoringNull();
}

// A tween whose frames never come lands this long after its end.
constexpr base::TimeDelta kTweenLandSlack = base::Milliseconds(100);

// Commands a held key repeats: the ones that step through something.
bool IsRepeatableCommand(std::string_view id) {
  static constexpr std::string_view kRepeatable[] = {
      "tabs.next",   "tabs.prev",   "archive.mruNext", "archive.mruPrev",
      "spaces.next", "spaces.prev", "nav.back",        "nav.forward",
      "zoom.in",     "zoom.out",    "find.next",       "find.prev",
  };
  return std::ranges::find(kRepeatable, id) != std::end(kRepeatable);
}

// Clu's own WebContents load only their own WebUI; the shell routes a fresh document to the bridge through the
// embedding context, which is how the WebUI controllers find this window. Rio's UI never zooms (CluUiZoomLock).
void LoadCluWebUi(views::WebView* view,
                  BrowserWindowInterface* browser,
                  content::WebContentsDelegate* delegate,
                  const char* url) {
  content::WebContents* contents = view->GetWebContents();
  contents->SetDelegate(delegate);
  webui::SetBrowserWindowInterface(contents, browser);
  CluUiZoomLock::Lock(contents);
  view->LoadInitialURL(GURL(url));
}

// The frame WebUI's view. When the focused view stops taking focus (the page's renderer crashed, so Chromium shows its
// crash card and the page view refuses focus), views moves focus on as if Tab were pressed, and the frame's first
// button would take it with a keyboard focus ring and its tooltip. The frame takes that focus as a document
// with nothing chosen, as a click on its background would; a real Tab still walks its buttons.
// Two-finger scrolls over the frame's app-region: drag areas (the sidebar's empty space and the bottom bar's gaps)
// never reach the frame's renderer: BridgedContentView hands them to the root view as ui::ScrollEvents, so a swipe
// there switched no space and slid no Library in. This hands them on to the frame document as wheels, the
// way its own view sends the rest. Each goes without a phase: Mac scroll events carry only part of theirs, and the
// swipe reducer reads momentum from the delta curve (swipeReducer.ts).
class CluFrameWheelForwarder : public ui::EventHandler {
 public:
  explicit CluFrameWheelForwarder(views::WebView* view) : view_(view) {}

  void OnScrollEvent(ui::ScrollEvent* event) override {
    if (event->type() != ui::EventType::kScroll ||
        (event->x_offset() == 0 && event->y_offset() == 0)) {
      return;
    }
    content::WebContents* contents = view_->GetWebContents();
    content::RenderWidgetHostView* host_view =
        contents ? contents->GetRenderWidgetHostView() : nullptr;
    if (!host_view || !host_view->GetRenderWidgetHost()) {
      return;
    }
    gfx::Point at = gfx::ToFlooredPoint(event->root_location_f());
    views::View::ConvertPointFromWidget(view_, &at);
    blink::WebMouseWheelEvent wheel = ui::MakeWebMouseWheelEvent(*event);
    wheel.phase = blink::WebMouseWheelEvent::kPhaseNone;
    wheel.momentum_phase = blink::WebMouseWheelEvent::kPhaseNone;
    wheel.SetPositionInWidget(at.x(), at.y());
    host_view->GetRenderWidgetHost()->ForwardWheelEvent(wheel);
    event->SetHandled();
  }

 private:
  const raw_ptr<views::WebView> view_;
};

class CluFrameWebView : public views::WebView {
  METADATA_HEADER(CluFrameWebView, views::WebView)

 public:
  explicit CluFrameWebView(content::BrowserContext* browser_context)
      : views::WebView(browser_context), wheels_(this) {
    AddPreTargetHandler(&wheels_);
  }
  ~CluFrameWebView() override { RemovePreTargetHandler(&wheels_); }

 protected:
  void AboutToRequestFocusFromTabTraversal(bool reverse) override {
    views::FocusManager* focus = GetFocusManager();
    views::View* from = focus ? focus->GetFocusedView() : nullptr;
    if (from && from != this && !from->IsFocusable()) {
      return;
    }
    views::WebView::AboutToRequestFocusFromTabTraversal(reverse);
  }

 private:
  CluFrameWheelForwarder wheels_;
};

BEGIN_METADATA(CluFrameWebView)
END_METADATA

}  // namespace

DEFINE_ELEMENT_IDENTIFIER_VALUE(kCluFrameWebViewElementId);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kCluOverlayWebViewElementId);
DEFINE_USER_DATA(CluShell);

// Steps the page card's tween on every frame the window's compositor begins. viz begins them from the display link
// of the screen the window is on (ui/display/mac DisplayLinkMac: 120 Hz on ProMotion, 60 on most externals), so the
// card and the frame WebUI's sidebar, which draws on requestAnimationFrame, move on the same display frames.
// It never steps on a timer of its own.
class CluShell::TweenDriver : public ui::CompositorAnimationObserver {
 public:
  TweenDriver(CluShell* shell, ui::Compositor* compositor) : shell_(shell) {
    observation_.Observe(compositor);
  }

  // ui::CompositorAnimationObserver: `timestamp` is the frame's begin time, mapped onto the wall clock the browser
  // and the frame WebUI share for SidebarAnim::start_at_ms.
  void OnAnimationStep(base::TimeTicks timestamp) override {
    // May destroy this (the tween ends): nothing after it.
    shell_->StepTween(NowMsF() -
                      (base::TimeTicks::Now() - timestamp).InMillisecondsF());
  }
  void OnCompositingShuttingDown(ui::Compositor* compositor) override {
    observation_.Reset();
    shell_->StopTween();
  }

 private:
  const raw_ptr<CluShell> shell_;
  base::ScopedObservation<ui::Compositor, ui::CompositorAnimationObserver>
      observation_{this};
};

CluShell::CluShell(BrowserWindowInterface* browser, views::View* browser_view)
    : browser_(browser),
      browser_view_(browser_view),
      mini_(g_creating_mini > 0 &&
            browser->GetType() == BrowserWindowInterface::TYPE_POPUP),
      scoped_unowned_user_data_(browser->GetUnownedUserDataHost(), *this) {
  CHECK(browser_view_);
  layout_ = LayoutFor(gfx::Size());
  shown_page_rect_ = layout_.page_rect;

  // The frame WebUI fills the window under everything else, so it is the browser view's first child.
  auto frame = std::make_unique<CluFrameWebView>(browser_->GetProfile());
  frame->SetProperty(views::kElementIdentifierKey, kCluFrameWebViewElementId);
  frame_view_ = browser_view_->AddChildViewAt(std::move(frame), 0);
  // The frame's app-region: drag areas move the window (DraggableRegionsChanged()); Blink sends them only when asked.
  frame_view_->GetWebContents()->SetSupportsDraggableRegions(true);
  LoadCluWebUi(frame_view_, browser_, this,
               mini_ ? kCluMiniFrameUrl : kCluFrameUrl);

  anchor_view_ = browser_view_->AddChildView(std::make_unique<CluAnchorView>());
  extension_anchor_view_ =
      browser_view_->AddChildView(std::make_unique<CluAnchorView>());
  extension_anchor_view_->SetProperty(views::kElementIdentifierKey,
                                      kCluExtensionAnchorViewElementId);
  downloads_anchor_view_ =
      browser_view_->AddChildView(std::make_unique<CluAnchorView>());
  downloads_anchor_view_->SetProperty(views::kElementIdentifierKey,
                                      kCluDownloadsAnchorViewElementId);

  if (views::Widget* widget = browser_view_->GetWidget()) {
    browser_widget_observation_.Observe(widget);
    CreateOverlay();
    CreatePassiveOverlays();
    RegisterKeyTable(widget);
    // A change in Settings, Shortcuts registers the window's keys again.
    shortcuts_subscription_ = AddShortcutsChangedCallback(base::BindRepeating(
        &CluShell::RebindKeyTable, base::Unretained(this)));
  }

  if (g_client_factory) {
    g_client_factory(browser_);
  }
}

CluShell::Passive::Passive() = default;
CluShell::Passive::Passive(Passive&&) = default;
CluShell::Passive& CluShell::Passive::operator=(Passive&&) = default;
CluShell::Passive::~Passive() = default;

CluShell::~CluShell() {
  tween_timer_.Stop();
  tween_driver_.reset();
  UnregisterAccelerators();
  // The WebUI documents call into the client, so they go first: the overlay widgets, then the frame view.
  for (Passive& passive : passive_) {
    passive.view = nullptr;
    passive.widget.reset();
  }
  HideLivePreview();
  overlay_view_ = nullptr;
  overlay_widget_.reset();
  DropExtraViews();
  pane_observations_.RemoveAllObservations();
  if (peek_view_) {
    CluPeekView* peek = peek_view_;
    peek_view_ = nullptr;
    browser_view_->RemoveChildViewT(peek);
  }
  if (frame_view_) {
    views::WebView* frame = frame_view_;
    frame_view_ = nullptr;
    browser_view_->RemoveChildViewT(frame);
  }
  if (anchor_view_) {
    CluAnchorView* anchor = anchor_view_;
    anchor_view_ = nullptr;
    browser_view_->RemoveChildViewT(anchor);
  }
  if (extension_anchor_view_) {
    CluAnchorView* anchor = extension_anchor_view_;
    extension_anchor_view_ = nullptr;
    browser_view_->RemoveChildViewT(anchor);
  }
  if (downloads_anchor_view_) {
    CluAnchorView* anchor = downloads_anchor_view_;
    downloads_anchor_view_ = nullptr;
    browser_view_->RemoveChildViewT(anchor);
  }
  client_.reset();
}

void CluShell::SetExtensionAnchorRect(const std::string& id,
                                      const gfx::Rect& rect) {
  extension_anchor_id_ = id;
  extension_anchor_rect_ = rect;
  // The popup reads its anchor's bounds as it opens, before the next layout pass.
  if (extension_anchor_view_) {
    extension_anchor_view_->SetBoundsRect(rect);
  }
  browser_view_->InvalidateLayout();
}

views::View* CluShell::ExtensionPopupAnchorFor(const std::string& id) {
  return extension_anchor_rect_ && !id.empty() && id == extension_anchor_id_
             ? static_cast<views::View*>(extension_anchor_view_)
             : static_cast<views::View*>(anchor_view_);
}

void CluShell::SetDownloadsButtonRect(const std::optional<gfx::Rect>& rect) {
  if (rect == downloads_button_rect_) {
    return;
  }
  downloads_button_rect_ = rect;
  browser_view_->InvalidateLayout();
}

std::optional<gfx::Rect> CluShell::DownloadsAnchorRect() const {
  return DownloadsButtonAnchor(layout_, downloads_button_rect_,
                               anim_.has_value());
}

views::View* CluShell::DownloadBubbleAnchor() {
  const std::optional<gfx::Rect> button = DownloadsAnchorRect();
  if (!button || !downloads_anchor_view_) {
    return anchor_view_;
  }
  // The bubble reads its anchor's bounds as it opens, before the next layout pass.
  downloads_anchor_view_->SetBoundsRect(*button);
  return downloads_anchor_view_;
}

// static
std::vector<CluShell::KeyBinding> CluShell::KeyBindings(bool mini) {
  std::vector<KeyBinding> keys;
  for (const CommandSpec& spec : CommandTable()) {
    // Each window kind has its own variant; kBoth entries work in either
    // (docs/PLAN.md 8 "Mini variant").
    if (spec.variant ==
        (mini ? CommandVariant::kMain : CommandVariant::kMini)) {
      continue;
    }
    // The keys the user gave it in Settings, Shortcuts, else the table's (clu_shortcuts.h).
    const CommandKeys own = KeysFor(spec);
    if (own.primary) {
      keys.push_back(KeyBinding{
          .accelerator =
              ui::Accelerator(own.primary->key, own.primary->modifiers),
          .id = spec.id,
          .reserved = IsReservedEntry(spec),
          .chromium_command = MenuTagFor(spec) != spec.idc});
    }
    for (const CommandAlt& alt : own.alts) {
      if (IsPageOnlyAlt(spec, alt)) {
        continue;
      }
      keys.push_back(KeyBinding{
          .accelerator = ui::Accelerator(alt.accelerator.key,
                                         alt.accelerator.modifiers),
          .id = spec.id,
          .reserved =
              alt.route == CommandRoute::kRouter || IsReservedEntry(spec),
          .chromium_command = MenuTagFor(spec) != spec.idc});
    }
  }
  return keys;
}

void CluShell::RegisterKeyTable(views::Widget* widget) {
  views::FocusManager* focus_manager = widget->GetFocusManager();
  if (!focus_manager) {
    return;
  }
  focus_manager_ = focus_manager;
  for (KeyBinding& binding : KeyBindings(mini_)) {
    // An extension shortcut that already took the chord keeps it.
    if (focus_manager->HasPriorityHandler(binding.accelerator)) {
      LOG(WARNING) << "clu: a key of the key table is taken by another "
                      "shortcut: "
                   << binding.id;
      continue;
    }
    focus_manager->RegisterAccelerator(
        binding.accelerator,
        binding.reserved ? ui::AcceleratorManager::kHighPriority
                         : ui::AcceleratorManager::kNormalPriority,
        this);
    bindings_.push_back(std::move(binding));
  }
}

void CluShell::RebindKeyTable() {
  views::Widget* widget = browser_view_->GetWidget();
  if (!widget || !focus_manager_) {
    return;
  }
  UnregisterAccelerators();
  bindings_.clear();
  RegisterKeyTable(widget);
}

void CluShell::UnregisterAccelerators() {
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerators(this);
    focus_manager_ = nullptr;
  }
}

bool CluShell::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const auto it = std::ranges::find(bindings_, accelerator,
                                    &KeyBinding::accelerator);
  if (it == bindings_.end() || !client_) {
    return false;
  }
  // Holding a key repeats only the commands that step (rows, spaces, history, zoom, find); a held Cmd+T must not
  // open and close the bar over and over. The repeat is consumed whatever the route: a menu chord let go here would
  // reach the main menu item with the same chord, which runs the command anyway.
  if (accelerator.IsRepeat() && !IsRepeatableCommand(it->id)) {
    return true;
  }
  const std::string id = it->id;
  // A declined chord of a Chromium command goes on to Chromium (a private window's Cmd+Shift+T restores through
  // TabRestoreService); a declined Clu chord that is reserved is consumed, so the page never sees it.
  const bool consume_declined = it->reserved && !it->chromium_command;
  if (record_keys_for_testing_) {
    key_commands_for_testing_.push_back(id);
  }
  if (record_only_for_testing_) {
    return consume_declined || client_->CanRunCommand(id);
  }
  // Running the command may close the window and destroy this shell, so nothing here is used after it.
  const bool ran = client_->RunKeyCommand(id);
  return ran || consume_declined;
}

bool CluShell::CanHandleAccelerators() const {
  return client_ != nullptr;
}

// static
CluShell* CluShell::From(BrowserWindowInterface* browser) {
  return browser ? Get(browser->GetUnownedUserDataHost()) : nullptr;
}

void CluShell::ToggleSidebar() {
  chrome_.sidebar_hidden = !chrome_.sidebar_hidden;
  Relayout(/*animate=*/true);
}

void CluShell::ToggleFocusMode() {
  chrome_.focus_mode = !chrome_.focus_mode;
  Relayout(/*animate=*/true);
}

void CluShell::ToggleToolbar() {
  chrome_.toolbar = !chrome_.toolbar;
  Relayout(/*animate=*/false);
}

void CluShell::SetToolbar(bool shown) {
  if (chrome_.toolbar == shown) {
    return;
  }
  chrome_.toolbar = shown;
  Relayout(/*animate=*/false);
}

void CluShell::SetDevStrip(bool shown) {
  if (chrome_.dev_strip == shown) {
    return;
  }
  chrome_.dev_strip = shown;
  Relayout(/*animate=*/false);
}

void CluShell::SetSidebarWidth(double width) {
  const int stored = StoredSidebarWidth(width);
  if (stored == chrome_.sidebar_width) {
    return;
  }
  chrome_.sidebar_width = stored;
  Relayout(/*animate=*/false);
}

void CluShell::SetLibraryOpen(bool open) {
  if (chrome_.library_open == open) {
    return;
  }
  chrome_.library_open = open;
  Relayout(/*animate=*/true, /*library=*/true);
}

void CluShell::SetLibraryWidth(int width) {
  width = std::max(0, width);
  if (chrome_.library_width == width) {
    return;
  }
  chrome_.library_width = width;
  if (chrome_.library_open) {
    Relayout(/*animate=*/true, /*library=*/true);
  }
}

void CluShell::ResetSidebarWidth() {
  SetSidebarWidth(kSidebarWidth);
}

void CluShell::RestoreSidebar(double width, bool hidden) {
  const int stored = StoredSidebarWidth(width);
  if (stored == chrome_.sidebar_width && hidden == chrome_.sidebar_hidden) {
    return;
  }
  chrome_.sidebar_width = stored;
  chrome_.sidebar_hidden = hidden;
  Relayout(/*animate=*/false);
}

void CluShell::UpdateForLayout(const gfx::Size& content_size,
                               bool fullscreen) {
  const gfx::Size size(std::max(0, content_size.width()),
                       std::max(0, content_size.height()));
  // A page or an extension that asked for full screen; the window's own ⌃⌘F full screen keeps the shell.
  ExclusiveAccessManager* access = ExclusiveAccessManager::From(browser_.get());
  const FullscreenController* controller =
      access ? access->fullscreen_controller() : nullptr;
  const bool content_fullscreen =
      controller && (controller->IsWindowFullscreenForTabOrPending() ||
                     controller->IsExtensionFullscreenOrPending());
  if (size == layout_.content_size && fullscreen == chrome_.fullscreen &&
      content_fullscreen == chrome_.content_fullscreen) {
    return;
  }
  chrome_.fullscreen = fullscreen;
  chrome_.content_fullscreen = content_fullscreen;
  // In the window's own full screen the top row follows the bar macOS reveals at the screen's top.
  if (fullscreen && !content_fullscreen) {
    if (!titlebar_watch_) {
      titlebar_watch_ = WatchTitlebarReveal(
          browser_view_->GetWidget(),
          base::BindRepeating(&CluShell::SetTitlebarReveal,
                              base::Unretained(this)));
    }
  } else {
    titlebar_watch_.reset();
    titlebar_reveal_ = 0;
  }
  StopTween();
  layout_ = LayoutFor(size);
  shown_page_rect_ = layout_.page_rect;
  extension_anchor_id_.clear();
  UpdateOverlayBounds();
  NotifyChanged(/*urgent=*/false);
}

void CluShell::SetTitlebarReveal(int covered) {
  covered = std::clamp(covered, 0, 200);
  if (!chrome_.fullscreen || chrome_.content_fullscreen) {
    covered = 0;
  }
  if (covered == titlebar_reveal_) {
    return;
  }
  titlebar_reveal_ = covered;
  // The row moves with each step of the bar's slide.
  NotifyChanged(/*urgent=*/true);
}

content::WebContents* CluShell::frame_contents() {
  return frame_view_ ? frame_view_->GetWebContents() : nullptr;
}

content::WebContents* CluShell::overlay_contents() {
  return overlay_view_ ? overlay_view_->GetWebContents() : nullptr;
}

void CluShell::ShowOverlay(bool focus, const std::optional<gfx::Rect>& rect) {
  if (!overlay_widget_) {
    return;
  }
  top_overlay_rect_ = rect;
  UpdateOverlayBounds();
  if (!focus) {
    overlay_widget_->ShowInactive();
    return;
  }
  overlay_widget_->Show();
  overlay_view_->RequestFocus();
  if (content::WebContents* contents = overlay_contents()) {
    contents->Focus();
  }
}

void CluShell::HideOverlay() {
  top_overlay_rect_.reset();
  if (!overlay_widget_ || !overlay_widget_->IsVisible()) {
    return;
  }
  overlay_widget_->Hide();
  if (views::Widget* widget = browser_view_->GetWidget()) {
    widget->Activate();
  }
}

bool CluShell::IsOverlayVisible() const {
  return overlay_widget_ && overlay_widget_->IsVisible();
}

void CluShell::ShowPassive(int slot,
                           const gfx::Rect& rect,
                           bool click_through,
                           bool focus) {
  if (slot < 0 || slot >= kPassiveSlots || !passive_[slot].widget ||
      rect.IsEmpty()) {
    return;
  }
  Passive& passive = passive_[slot];
  passive.rect = rect;
  const gfx::Rect window = browser_view_->GetBoundsInScreen();
  if (!window.IsEmpty()) {
    passive.widget->SetBounds(
        gfx::Rect(window.origin() + rect.OffsetFromOrigin(), rect.size()));
  }
  SetWidgetIgnoresMouse(passive.widget.get(), click_through);
  if (focus) {
    // The folder popover takes typing at once: the slot may activate while it shows.
    passive.widget->widget_delegate()->SetCanActivate(true);
    passive.view->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    passive.widget->Show();
    passive.view->RequestFocus();
    if (content::WebContents* contents = passive.view->GetWebContents()) {
      contents->Focus();
    }
  } else {
    if (passive.widget->IsActive()) {
      if (views::Widget* widget = browser_view_->GetWidget()) {
        widget->Activate();
      }
    }
    passive.widget->widget_delegate()->SetCanActivate(false);
    passive.view->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    // Shown without activation: the page or the frame keeps focus (02-tabs F13 and F15).
    passive.widget->ShowInactive();
  }
  // The top overlay stays above every passive one: a toast or the switcher that shows while the command bar is open
  // sits under its scrim. Mac orders a child window it shows above its siblings.
  if (IsOverlayVisible()) {
    overlay_widget_->StackAboveWidget(passive.widget.get());
  }
}

void CluShell::HidePassive(int slot) {
  if (slot < 0 || slot >= kPassiveSlots || !passive_[slot].widget) {
    return;
  }
  passive_[slot].rect = gfx::Rect();
  const bool active = passive_[slot].widget->IsActive();
  passive_[slot].widget->Hide();
  passive_[slot].widget->widget_delegate()->SetCanActivate(false);
  passive_[slot].view->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  // A focused folder popover hands the keyboard back to the window, as the top overlay does.
  if (active) {
    if (views::Widget* widget = browser_view_->GetWidget()) {
      widget->Activate();
    }
  }
}

bool CluShell::IsPassiveVisible(int slot) const {
  return slot >= 0 && slot < kPassiveSlots && passive_[slot].widget &&
         passive_[slot].widget->IsVisible();
}

void CluShell::ShowLivePreview(content::BrowserContext* context,
                               const GURL& url,
                               const gfx::Rect& rect,
                               const std::string& user_agent) {
  if (!context || !url.is_valid() || !url.SchemeIs(url::kHttpsScheme) ||
      rect.IsEmpty()) {
    HideLivePreview();
    return;
  }
  content::WebContents* contents = live_preview_contents_for_testing();
  // Another site or profile gets a fresh page; the same one only moves (a title change redraws the card).
  if (!contents || contents->GetBrowserContext() != context ||
      live_preview_url_ != url) {
    HideLivePreview();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    params.parent = browser_view_->GetWidget()->GetNativeView();
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
    params.bounds = gfx::Rect();
    params.name = "CluLivePreview";
    live_preview_widget_ = std::make_unique<views::Widget>();
    live_preview_widget_->Init(std::move(params));
    auto view = std::make_unique<views::WebView>(context);
    view->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    live_preview_view_ = live_preview_widget_->SetContentsView(std::move(view));
    live_preview_url_ = url;
    contents = live_preview_view_->GetWebContents();
    // The site's phone view (Gmail's inbox list, Calendar's day) fits the card; its desktop page would be cropped.
    content::NavigationController::LoadURLParams load(url);
    if (!user_agent.empty()) {
      contents->SetUserAgentOverride(
          blink::UserAgentOverride::UserAgentOnly(user_agent),
          /*override_in_new_tabs=*/false);
      load.override_user_agent =
          content::NavigationController::UA_OVERRIDE_TRUE;
    }
    contents->GetController().LoadURLWithParams(load);
    // A glance, never a sound: Meet's or Calendar's page stays muted.
    contents->SetAudioMuted(true);
    // The card's 15 pt corners (tabs.css .clu-live-card); the host exists once the page is in the widget.
    if (live_preview_view_->holder()->native_view()) {
      live_preview_view_->holder()->SetNativeViewCornerRadii(
          gfx::RoundedCornersF(15));
    }
    // A glance too: the pointer stays with the row under it, as with the card (click_through).
    SetWidgetIgnoresMouse(live_preview_widget_.get(), true);
  }
  live_preview_rect_ = rect;
  const gfx::Rect window = browser_view_->GetBoundsInScreen();
  if (!window.IsEmpty()) {
    live_preview_widget_->SetBounds(
        gfx::Rect(window.origin() + rect.OffsetFromOrigin(), rect.size()));
  }
  live_preview_widget_->ShowInactive();
  // Above the card it fills (a redraw reshows the card over an open page), under the top overlay (the command bar).
  for (Passive& passive : passive_) {
    if (passive.widget && passive.widget->IsVisible()) {
      live_preview_widget_->StackAboveWidget(passive.widget.get());
    }
  }
  if (IsOverlayVisible()) {
    overlay_widget_->StackAboveWidget(live_preview_widget_.get());
  }
}

void CluShell::HideLivePreview() {
  live_preview_rect_ = gfx::Rect();
  live_preview_url_ = GURL();
  live_preview_view_ = nullptr;
  live_preview_widget_.reset();
}

content::WebContents* CluShell::live_preview_contents_for_testing() {
  return live_preview_view_ ? live_preview_view_->GetWebContents() : nullptr;
}

content::WebContents* CluShell::passive_contents(int slot) {
  if (slot < 0 || slot >= kPassiveSlots || !passive_[slot].view) {
    return nullptr;
  }
  return passive_[slot].view->GetWebContents();
}

std::optional<int> CluShell::OverlaySlotOf(content::WebContents* contents) {
  if (!contents) {
    return std::nullopt;
  }
  if (contents == overlay_contents()) {
    return -1;
  }
  if (peek_view_ && contents == peek_view_->under_contents()) {
    return kUnderPeekSlot;
  }
  for (int slot = 0; slot < kPassiveSlots; ++slot) {
    if (contents == passive_contents(slot)) {
      return slot;
    }
  }
  return std::nullopt;
}

CluShell::ScopedCreatingMiniWindow::ScopedCreatingMiniWindow() {
  ++g_creating_mini;
}

CluShell::ScopedCreatingMiniWindow::~ScopedCreatingMiniWindow() {
  --g_creating_mini;
}

void CluShell::SetMiniConfirming(bool confirming) {
  if (!mini_ || mini_confirming_ == confirming) {
    return;
  }
  mini_confirming_ = confirming;
  layout_.page_rect = MiniPageRect(layout_.content_size, mini_confirming_);
  shown_page_rect_ = layout_.page_rect;
  browser_view_->InvalidateLayout();
  NotifyChanged(/*urgent=*/false);
}

CluPeekView* CluShell::EnsurePeekView() {
  if (!peek_view_) {
    // Added last, so it sits over the page contents; the overlay widgets stay
    // above it.
    peek_view_ = browser_view_->AddChildView(
        std::make_unique<CluPeekView>(browser_, this));
    peek_view_->SetVisible(false);
  }
  return peek_view_;
}

void CluShell::SetPeekShown(bool shown) {
  if (shown == peek_shown_ || (shown && !peek_view_)) {
    return;
  }
  peek_shown_ = shown;
  browser_view_->InvalidateLayout();
}

void CluShell::SetMultiContentsView(MultiContentsView* view) {
  pane_observations_.RemoveAllObservations();
  multi_contents_view_ = view;
  if (!view) {
    return;
  }
  // The panes sit flush with the page card the frame draws; the 10 DIP resize
  // area between them is the gap.
  view->SetSplitViewInsets(gfx::Insets());
  for (ContentsContainerView* container : view->contents_container_views()) {
    if (container) {
      pane_observations_.AddObservation(container);
    }
  }
}

void CluShell::SetExtraPanes(std::vector<content::WebContents*> panes,
                             std::vector<double> shares,
                             size_t active,
                             bool stacked) {
  ClearExtraPanes();
  if (panes.size() < 3 || shares.size() != panes.size() ||
      active >= panes.size() || !multi_contents_view_) {
    return;
  }
  // The hosted views sit right above the page so the Peek and the anchors stay
  // over them.
  const std::optional<size_t> page_index =
      browser_view_->GetIndexOf(multi_contents_view_.get());
  size_t insert_at =
      page_index ? *page_index + 1 : browser_view_->children().size();
  for (size_t i = 0; i < panes.size(); ++i) {
    extra_panes_.push_back(panes[i]);
    if (i == active || !panes[i]) {
      extra_views_.push_back(nullptr);
      continue;
    }
    auto view = std::make_unique<views::WebView>(panes[i]->GetBrowserContext());
    view->SetWebContents(panes[i]);
    views::WebView* added = browser_view_->AddChildViewAt(std::move(view),
                                                          insert_at++);
    pane_observations_.AddObservation(added);
    extra_views_.push_back(added);
  }
  extra_shares_ = std::move(shares);
  extra_active_ = active;
  extra_stacked_ = stacked;
  browser_view_->InvalidateLayout();
  NotifyChanged(/*urgent=*/false);
}

void CluShell::ClearExtraPanes() {
  if (extra_panes_.empty()) {
    return;
  }
  DropExtraViews();
  browser_view_->InvalidateLayout();
  NotifyChanged(/*urgent=*/false);
}

void CluShell::DropExtraViews() {
  std::vector<raw_ptr<views::WebView>> hosted = std::move(extra_views_);
  extra_views_.clear();
  extra_panes_.clear();
  extra_shares_.clear();
  extra_active_ = 0;
  for (raw_ptr<views::WebView>& view : hosted) {
    if (!view) {
      continue;
    }
    views::WebView* webview = view.get();
    view = nullptr;
    // The page leaves this view before Chromium shows it anywhere else.
    webview->SetWebContents(nullptr);
    if (pane_observations_.IsObservingSource(webview)) {
      pane_observations_.RemoveObservation(webview);
    }
    browser_view_->RemoveChildViewT(webview);
  }
}

void CluShell::SetExtraShares(std::vector<double> shares) {
  if (shares.size() != extra_panes_.size() || shares == extra_shares_) {
    return;
  }
  extra_shares_ = std::move(shares);
  browser_view_->InvalidateLayout();
  NotifyChanged(/*urgent=*/false);
}

int CluShell::ExtraPaneRoom(const gfx::Rect& card) const {
  const int count = static_cast<int>(extra_panes_.size());
  const int length = extra_stacked_ ? card.height() : card.width();
  return std::max(0, length - kExtraPaneGap * std::max(0, count - 1));
}

std::vector<gfx::Rect> CluShell::ExtraPaneRects(const gfx::Rect& card) const {
  std::vector<gfx::Rect> rects;
  const int count = static_cast<int>(extra_panes_.size());
  if (count < 3) {
    return rects;
  }
  const int room = ExtraPaneRoom(card);
  int start = 0;
  int used = 0;
  for (int i = 0; i < count; ++i) {
    // Each pane its share; the last pane takes the rounding.
    const int size =
        i == count - 1
            ? std::max(0, room - used)
            : std::clamp(static_cast<int>(room * extra_shares_[i] + 0.5), 0,
                         std::max(0, room - used));
    used += size;
    gfx::Rect pane = extra_stacked_
                         ? gfx::Rect(card.x(), card.y() + start, card.width(),
                                     size)
                         : gfx::Rect(card.x() + start, card.y(), size,
                                     card.height());
    pane.Inset(gfx::Insets::TLBR(std::min(kPaneBarHeight, pane.height()), 0,
                                 0, 0));
    pane.set_width(std::max(pane.width(), 1));
    pane.set_height(std::max(pane.height(), 1));
    rects.push_back(pane);
    start += size + kExtraPaneGap;
  }
  return rects;
}

views::WebView* CluShell::ExtraPaneView(size_t index) const {
  return index < extra_views_.size() ? extra_views_[index].get() : nullptr;
}

std::vector<CluShell::PaneRect> CluShell::SplitPaneRects() {
  std::vector<PaneRect> panes;
  if (!multi_contents_view_ || !frame_view_) {
    return panes;
  }
  if (!extra_panes_.empty()) {
    // Three or four panes: the active page in MultiContentsView, the rest in
    // the hosted views.
    for (size_t i = 0; i < extra_panes_.size(); ++i) {
      views::View* view = extra_views_[i].get();
      if (i == extra_active_) {
        view = multi_contents_view_->GetActiveContentsView();
      }
      if (!view || !extra_panes_[i]) {
        continue;
      }
      panes.push_back(PaneRect{.contents = extra_panes_[i].get(),
                               .rect = views::View::ConvertRectToTarget(
                                   view, frame_view_, view->GetLocalBounds())});
    }
    return panes;
  }
  if (!multi_contents_view_->IsInSplitView()) {
    return panes;
  }
  for (ContentsContainerView* container :
       multi_contents_view_->contents_container_views()) {
    ContentsWebView* view = container ? container->contents_view() : nullptr;
    content::WebContents* contents = view ? view->GetWebContents() : nullptr;
    if (!contents || !container->GetVisible()) {
      continue;
    }
    panes.push_back(PaneRect{.contents = contents,
                             .rect = views::View::ConvertRectToTarget(
                                 view, frame_view_, view->GetLocalBounds())});
  }
  return panes;
}

void CluShell::OnViewBoundsChanged(views::View* observed_view) {
  NotifyChanged(/*urgent=*/false);
}

void CluShell::OnViewIsDeleting(views::View* observed_view) {
  pane_observations_.RemoveObservation(observed_view);
}

void CluShell::SetPageShown(bool shown) {
  if (page_shown_ == shown) {
    return;
  }
  page_shown_ = shown;
  browser_view_->InvalidateLayout();
  // The hover peek pins open while a hidden sidebar has no page beside it.
  NotifyChanged(/*urgent=*/false);
}

void CluShell::SetClient(std::unique_ptr<Client> client) {
  CHECK(!client_);
  client_ = std::move(client);
}

// static
void CluShell::SetClientFactory(ClientFactory factory) {
  g_client_factory = factory;
}

void CluShell::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CluShell::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

content::KeyboardEventProcessingResult CluShell::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown ||
      !focus_manager_) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  const ui::Accelerator accelerator =
      ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
  if (focus_manager_->HasPriorityHandler(accelerator) &&
      focus_manager_->ProcessAccelerator(accelerator)) {
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

bool CluShell::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  // Keys the WebUI did not handle go to the browser window's accelerators, as they do from a tab.
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, browser_view_->GetFocusManager());
}

void CluShell::DraggableRegionsChanged(
    const std::vector<blink::mojom::DraggableRegionPtr>& regions,
    content::WebContents* contents) {
  if (contents == frame_contents()) {
    drag_region_ = gfx::DraggableRegionsToSkRegion(regions);
  }
}

bool CluShell::IsWindowDragArea(const gfx::Point& point) {
  views::Widget* widget = browser_view_->GetWidget();
  if (!frame_view_ || !frame_view_->GetVisible() || drag_region_.isEmpty() ||
      !widget || widget->IsFullscreen()) {
    return false;
  }
  const gfx::Point in_frame =
      views::View::ConvertPointToTarget(browser_view_, frame_view_, point);
  if (!frame_view_->GetLocalBounds().Contains(in_frame) ||
      !drag_region_.contains(in_frame.x(), in_frame.y())) {
    return false;
  }
  // Only where the frame WebUI itself takes the click: the page view, a Peek or a pane bar over it keep theirs.
  views::View* root = widget->GetRootView();
  views::View* target = root->GetEventHandlerForPoint(
      views::View::ConvertPointToTarget(browser_view_, root, point));
  return target && frame_view_->Contains(target);
}

bool CluShell::HandleContextMenu(content::RenderFrameHost& render_frame_host,
                                 const content::ContextMenuParams& params) {
  // Clu's WebUIs draw their own menus; the page context menu never opens over the sidebar or an overlay. A text field
  // (a rename field, the command bar input) keeps the standard Cut, Copy and Paste menu, as a native field does.
  return !params.is_editable;
}

void CluShell::OnWidgetBoundsChanged(views::Widget* widget,
                                     const gfx::Rect& new_bounds) {
  UpdateOverlayBounds();
}

void CluShell::OnWidgetDestroying(views::Widget* widget) {
  shortcuts_subscription_ = {};
  UnregisterAccelerators();
  browser_widget_observation_.Reset();
}

ShellLayout CluShell::LayoutFor(const gfx::Size& size) const {
  ShellLayout layout = ComputeShellLayout(size, chrome_);
  if (mini_) {
    layout.page_rect = MiniPageRect(size, mini_confirming_);
    layout.traffic_lights = true;
    layout.hot_zone = std::nullopt;
  }
  return layout;
}

void CluShell::Relayout(bool animate, bool library) {
  const gfx::Rect before = shown_page_rect_;
  StopTween();
  layout_ = LayoutFor(layout_.content_size);
  extension_anchor_id_.clear();
  // The sidebar docks in kSidebarShowMs and leaves in kSidebarHideMs with a page loaded; the Library widens and
  // narrows it over kLibraryMs. With no page open the hide snaps: the sidebar floats in place at once.
  const int ms = library                ? kLibraryMs
                 : layout_.traffic_lights ? kSidebarShowMs
                 : page_shown_          ? kSidebarHideMs
                                        : 0;
  if (animate && ms > 0 && !gfx::Animation::PrefersReducedMotion() &&
      !before.IsEmpty() && before != layout_.page_rect) {
    StartTween(before, layout_.page_rect, ms, /*exponential=*/library);
  } else {
    shown_page_rect_ = layout_.page_rect;
    NotifyChanged(/*urgent=*/false);
  }
  browser_view_->InvalidateLayout();
}

void CluShell::StartTween(const gfx::Rect& from,
                          const gfx::Rect& to,
                          int ms,
                          bool exponential) {
  anim_ = SidebarAnim{.from = from.x(),
                      .to = to.x(),
                      .start_at_ms = NowMs(),
                      .ms = ms,
                      .from_rect = from,
                      .to_rect = to,
                      .exponential = exponential};
  tween_steps_ = 0;
  shown_page_rect_ = from;
  NotifyChanged(/*urgent=*/true);
  views::Widget* widget = browser_view_->GetWidget();
  if (ui::Compositor* compositor = widget ? widget->GetCompositor() : nullptr) {
    tween_driver_ = std::make_unique<TweenDriver>(this, compositor);
  }
  tween_timer_.Start(
      FROM_HERE, base::Milliseconds(ms) + kTweenLandSlack,
      base::BindOnce([](CluShell* shell) { shell->StepTween(NowMsF()); },
                     base::Unretained(this)));
}

void CluShell::StepTween(double now_ms) {
  if (!anim_) {
    return;
  }
  ++tween_steps_;
  const double progress = TweenProgress(*anim_, now_ms);
  shown_page_rect_ = LerpRect(anim_->from_rect, anim_->to_rect, progress);
  browser_view_->InvalidateLayout();
  if (progress >= 1) {
    StopTween();
    NotifyChanged(/*urgent=*/false);
  }
}

void CluShell::StopTween() {
  tween_timer_.Stop();
  tween_driver_.reset();
  if (anim_) {
    anim_.reset();
    shown_page_rect_ = layout_.page_rect;
  }
}

void CluShell::NotifyChanged(bool urgent) {
  // The window buttons follow the sidebar (M11): hidden with it and in focus mode.
  SetWindowButtonsVisible(browser_view_->GetWidget(), layout_.traffic_lights);
  for (Observer& observer : observers_) {
    observer.OnShellChanged(urgent);
  }
}

void CluShell::CreateOverlay() {
  views::Widget* parent = browser_view_->GetWidget();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.parent = parent->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
  // The browser view has no size before its first layout, and Mac refuses an empty rect away from the origin. All
  // zeros is allowed; the first layout sets the real bounds (UpdateOverlayBounds).
  const gfx::Rect bounds = browser_view_->GetBoundsInScreen();
  params.bounds = bounds.IsEmpty() ? gfx::Rect() : bounds;
  params.name = "CluTopOverlay";
  overlay_widget_ = std::make_unique<views::Widget>();
  overlay_widget_->Init(std::move(params));

  auto view = std::make_unique<views::WebView>(browser_->GetProfile());
  view->SetProperty(views::kElementIdentifierKey, kCluOverlayWebViewElementId);
  overlay_view_ = overlay_widget_->SetContentsView(std::move(view));
  overlay_view_->GetWebContents()->SetPageBaseBackgroundColor(
      SK_ColorTRANSPARENT);
  LoadCluWebUi(overlay_view_, browser_, this, kCluTopOverlayUrl);
}

void CluShell::CreatePassiveOverlays() {
  views::Widget* parent = browser_view_->GetWidget();
  for (int slot = 0; slot < kPassiveSlots; ++slot) {
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    params.parent = parent->GetNativeView();
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
    // All zeros until the first surface opens: Mac refuses an empty rect away from the origin.
    params.bounds = gfx::Rect();
    params.name = "CluPassiveOverlay";
    Passive& passive = passive_[slot];
    passive.widget = std::make_unique<views::Widget>();
    passive.widget->Init(std::move(params));
    auto view = std::make_unique<views::WebView>(browser_->GetProfile());
    view->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    passive.view = passive.widget->SetContentsView(std::move(view));
    passive.view->GetWebContents()->SetPageBaseBackgroundColor(
        SK_ColorTRANSPARENT);
    const std::string url = base::StrCat(
        {kCluPassiveOverlayUrlPrefix, base::NumberToString(slot)});
    LoadCluWebUi(passive.view, browser_, this, url.c_str());
  }
}

void CluShell::UpdateOverlayBounds() {
  const gfx::Rect bounds = browser_view_->GetBoundsInScreen();
  // Mac widgets cannot be zero-sized; a minimized or not yet laid out window keeps the last bounds.
  if (bounds.IsEmpty()) {
    return;
  }
  if (overlay_widget_) {
    // A rect-sized top overlay keeps its place in the window; the overlay WebUI draws its surfaces from that origin
    // (windows[id].overlay.topRect).
    overlay_widget_->SetBounds(
        top_overlay_rect_
            ? gfx::Rect(bounds.origin() + top_overlay_rect_->OffsetFromOrigin(),
                        top_overlay_rect_->size())
            : bounds);
  }
  for (Passive& passive : passive_) {
    if (passive.widget && !passive.rect.IsEmpty()) {
      passive.widget->SetBounds(gfx::Rect(
          bounds.origin() + passive.rect.OffsetFromOrigin(), passive.rect.size()));
    }
  }
  if (live_preview_widget_ && !live_preview_rect_.IsEmpty()) {
    live_preview_widget_->SetBounds(
        gfx::Rect(bounds.origin() + live_preview_rect_.OffsetFromOrigin(),
                  live_preview_rect_.size()));
  }
}

bool HandleBrowserCommand(BrowserWindowInterface* browser, int command_id) {
  CluShell* shell = CluShell::From(browser);
  if (!shell) {
    // Cmd+T in a window without Clu's layout (an undocked DevTools window, a popup): the bar opens in the profile's
    // last active Clu window, where Chromium's own New Tab would add a blank tab.
    if (!browser || command_id != IDC_NEW_TAB) {
      return false;
    }
    // The most recently active one is the point here, as for Chromium's own New Tab.
    BrowserWindowInterface* target = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* other) {
          CluShell* other_shell = CluShell::From(other);
          if (other_shell && other_shell->client() &&
              other->GetProfile() == browser->GetProfile() &&
              other->GetWindow()->IsVisible()) {
            target = other;
          }
          return !target;
        });
    if (!target) {
      return false;
    }
    target->GetWindow()->Activate();
    return CluShell::From(target)->client()->RunCommand("commandbar.newTab");
  }
  // Back, Forward, Reload, Stop, Find and Zoom from Chromium's menu items (picked with the mouse, or their key
  // equivalents) act on a Peek that has focus, not on the tab under it (docs/chromium/m6.md).
  if (const std::string_view page = PageCommandForChromeCommand(command_id);
      !page.empty() && shell->client()) {
    if (std::optional<bool> ran =
            shell->client()->PeekPageCommand(page, /*run=*/true)) {
      return *ran;
    }
  }
  // A Chromium command Clu replaces (Cmd+T from the Dock menu or AppleScript, Chromium's own Ctrl+Tab binding), or
  // an IDC_CLU_* id from Clu's main menu.
  std::string_view id = CluCommandForChromeCommand(command_id);
  // Chromium's Ctrl+Tab and Ctrl+Shift+Tab come from the held chord; Clu's menu items carry IDC_CLU_* ids, and one
  // fired by its key equivalent (the overlay window is key while the command bar is open) is the chord too.
  const bool from_key = command_id == IDC_CYCLE_TO_NEXT_TAB ||
                        command_id == IDC_CYCLE_TO_PREV_TAB ||
                        IsKeyEquivalentInFlight();
  if (id.empty()) {
    if (const CommandSpec* spec = FindCommandByIdc(command_id)) {
      id = spec->id;
    }
  }
  if (id.empty()) {
    return false;
  }
  // The shell's own commands work before the frame WebUI has connected. A Mini window has no sidebar: its client
  // sends them to the main window.
  if (id == "sidebar.toggle" && !shell->is_mini()) {
    shell->ToggleSidebar();
    return true;
  }
  if (id == "shell.focusMode" && !shell->is_mini()) {
    shell->ToggleFocusMode();
    return true;
  }
  if (id == "shell.toolbar" && !shell->is_mini()) {
    shell->ToggleToolbar();
    return true;
  }
  if (!shell->client()) {
    return false;
  }
  return from_key ? shell->client()->RunKeyCommand(id)
                  : shell->client()->RunCommand(id);
}

bool IsReservedCommand(BrowserWindowInterface* browser, int command_id) {
  // Router keys Chromium leaves to the page (Cmd+1..9, Cmd+L and Clu's own ids, docs/PLAN.md 8).
  return CluShell::From(browser) && IsReservedChromeCommand(command_id);
}

bool SupportsCluCommand(BrowserWindowInterface* browser, int command_id) {
  // Asked while the Browser is built too, before its window (and shell) exist:
  // every normal window gets the Clu layout, and so does the popup Clu makes as
  // a Mini window.
  if (!browser || !FindCommandByIdc(command_id)) {
    return false;
  }
  if (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
    return true;
  }
  const CluShell* shell = CluShell::From(browser);
  return browser->GetType() == BrowserWindowInterface::TYPE_POPUP &&
         ((shell && shell->is_mini()) || IsCreatingMiniWindow());
}

bool IsCluCommandEnabled(BrowserWindowInterface* browser, int command_id) {
  CluShell* shell = CluShell::From(browser);
  const CommandSpec* spec = FindCommandByIdc(command_id);
  return shell && spec && shell->client() &&
         shell->client()->CanRunCommand(spec->id);
}

bool OverrideCommandEnabled(BrowserWindowInterface* browser,
                            int command_id,
                            bool* enabled) {
  const std::string_view page = PageCommandForChromeCommand(command_id);
  CluShell* shell = page.empty() ? nullptr : CluShell::From(browser);
  if (!shell || !shell->client()) {
    return false;
  }
  const std::optional<bool> can =
      shell->client()->PeekPageCommand(page, /*run=*/false);
  if (!can) {
    return false;
  }
  *enabled = *can;
  return true;
}

bool KeepsCommandEnabled(BrowserWindowInterface* browser, int command_id) {
  // Asked while the Browser is built too, before its window (and shell) exist: every normal window gets the Clu
  // layout.
  return browser && command_id == IDC_RESTORE_TAB &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

views::View* GetBubbleAnchorView(BrowserWindowInterface* browser) {
  CluShell* shell = CluShell::From(browser);
  return shell ? shell->anchor_view() : nullptr;
}

views::View* GetDownloadBubbleAnchorView(BrowserWindowInterface* browser) {
  CluShell* shell = CluShell::From(browser);
  return shell ? shell->DownloadBubbleAnchor() : nullptr;
}

views::View* GetExtensionPopupAnchorView(BrowserWindowInterface* browser,
                                         const std::string& action_id) {
  CluShell* shell = CluShell::From(browser);
  return shell ? shell->ExtensionPopupAnchorFor(action_id) : nullptr;
}

bool IsCreatingMiniWindow() {
  return g_creating_mini > 0;
}

}  // namespace clu
