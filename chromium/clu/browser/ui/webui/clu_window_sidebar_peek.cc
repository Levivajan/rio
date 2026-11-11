// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace clu {

// Hands every mouse event Chromium routes to a WebUI's main frame widget to `callback`, before the renderer gets it,
// and follows the main frame when its host changes (the first WebUI commit swaps it). The main frame's widget host
// outlives a renderer crash and goes only after RenderFrameHostChanged() or WebContentsDestroyed().
class CluWindowSidebarPeek::MouseTap : public content::WebContentsObserver {
 public:
  using Callback = content::RenderWidgetHost::MouseEventCallback;

  MouseTap(content::WebContents* contents, Callback callback)
      : content::WebContentsObserver(contents),
        callback_(std::move(callback)) {
    if (contents) {
      Attach(contents->GetPrimaryMainFrame()->GetRenderWidgetHost());
    }
  }
  MouseTap(const MouseTap&) = delete;
  MouseTap& operator=(const MouseTap&) = delete;
  ~MouseTap() override { Detach(); }

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override {
    if (!new_host || !new_host->IsInPrimaryMainFrame()) {
      return;
    }
    Detach();
    Attach(new_host->GetRenderWidgetHost());
  }
  void WebContentsDestroyed() override { Detach(); }

 private:
  void Attach(content::RenderWidgetHost* host) {
    if (!host || host == host_) {
      return;
    }
    host_ = host;
    host_->AddMouseEventCallback(callback_);
  }
  void Detach() {
    if (host_) {
      host_->RemoveMouseEventCallback(callback_);
      host_ = nullptr;
    }
  }

  const Callback callback_;
  raw_ptr<content::RenderWidgetHost> host_ = nullptr;
};

namespace {

constexpr int kButtonsDown = blink::WebInputEvent::kLeftButtonDown |
                             blink::WebInputEvent::kMiddleButtonDown |
                             blink::WebInputEvent::kRightButtonDown |
                             blink::WebInputEvent::kBackButtonDown |
                             blink::WebInputEvent::kForwardButtonDown;

bool IsPointerEvent(blink::WebInputEvent::Type type) {
  switch (type) {
    case blink::WebInputEvent::Type::kMouseDown:
    case blink::WebInputEvent::Type::kMouseUp:
    case blink::WebInputEvent::Type::kMouseMove:
    case blink::WebInputEvent::Type::kMouseEnter:
    case blink::WebInputEvent::Type::kContextMenu:
      return true;
    default:
      return false;
  }
}

}  // namespace

CluWindowSidebarPeek::CluWindowSidebarPeek(CluWindowState& window,
                                           const base::TickClock* clock)
    : window_(window), peek_(this, clock) {
  static_assert(kPeekSlot < CluShell::kPassiveSlots);
  CluShell& shell = window_->shell();
  if (views::Widget* widget = shell.passive_widget(kPeekSlot)) {
    widget_observation_.Observe(widget);
  }
  frame_tap_ = std::make_unique<MouseTap>(
      shell.frame_contents(),
      base::BindRepeating(&CluWindowSidebarPeek::OnFrameMouseEvent,
                          base::Unretained(this)));
  peek_tap_ = std::make_unique<MouseTap>(
      shell.passive_contents(kPeekSlot),
      base::BindRepeating(&CluWindowSidebarPeek::OnPeekMouseEvent,
                          base::Unretained(this)));
}

CluWindowSidebarPeek::~CluWindowSidebarPeek() {
  // The taps go first: nothing reaches the controller while it is torn down.
  frame_tap_.reset();
  peek_tap_.reset();
}

void CluWindowSidebarPeek::SetOpen(bool open) {
  // shell.setSidebarPeek: open from the frame's edge dwell, closed by the overlay once its slide-out is done or by
  // Escape on a peek nothing holds.
  VLOG(1) << "clu-peek shell.setSidebarPeek open=" << open
          << " is_open=" << peek_.is_open() << " held=" << held_
          << " hovered=" << peek_.hovered();
  if (open) {
    peek_.Open();
  } else {
    peek_.Close();
  }
}

void CluWindowSidebarPeek::Alive() {
  peek_.Alive();
}

void CluWindowSidebarPeek::Close() {
  peek_.CloseOnBlur();
}

void CluWindowSidebarPeek::AddSurface(
    std::vector<OverlaySurfaceSnapshot>& surfaces) {
  if (!peek_.is_open()) {
    return;
  }
  OverlaySurfaceSnapshot surface;
  surface.instance_id = instance_id_;
  surface.surface = "sidebar-peek";
  surface.layer = "passive";
  surface.modality = "passive";
  surface.focus = false;
  surface.slot = kPeekSlot;
  surface.rect = peek_.rect();
  // Pinned: the overlay draws it in place, with no slide, and never slides it out.
  surface.props.Set("pinned", PinsPeek());
  // The grace ran out and the overlay slides the peek out, on the browser's clock.
  surface.props.Set("leaving", peek_.leaving());
  surfaces.push_back(std::move(surface));
}

// Two rules keep the floating sidebar's holds simple:
// - A toast is never a surface here. Its opening, showing or closing never closes, holds or releases the peek, held or
//   not (SurfaceClosesSidebarPeek).
// - A menu action that acts inside the open peek holds it as the Library does, and the hold ends when the action does:
//   New Folder's rename holds it until the rename is committed or cancelled (the overlay's release), Reveal Tab's
//   scroll for kPeekRevealSettle. The usual 165 ms grace applies after (HoldForAction).
void CluWindowSidebarPeek::Reconcile(
    const std::vector<OverlaySurfaceSnapshot>& surfaces) {
  if (!peek_.is_open()) {
    return;
  }
  for (const OverlaySurfaceSnapshot& surface : surfaces) {
    if (!SurfaceClosesSidebarPeek(
            surface.surface,
            surface.instance_id == instance_id_ ||
                surface.opened_from == instance_id_,
            before_.contains(surface.instance_id))) {
      continue;
    }
    VLOG(1) << "clu-peek close: surface " << surface.surface << " "
            << surface.instance_id << " opened over it";
    peek_.Close();
    return;
  }
}

void CluWindowSidebarPeek::OnShellChanged() {
  // A Library left open when the sidebar hides (Cmd+S, focus mode) with no peek closes, so the next peek shows the tabs
  // and is not held from its first frame. Only the hiding does it: the Library opening while the sidebar is already
  // hidden relays the shell out too, and opens a peek to show it (SetArchivePanel). Docking keeps it. Closing relays
  // the shell out, so it runs in the next task, not inside this layout pass.
  const bool hidden = window_->shell().layout().hot_zone.has_value();
  const bool hid = !sidebar_hidden_ && hidden;
  sidebar_hidden_ = hidden;
  if (hid && !peek_.is_open() && window_->archive_panel()) {
    VLOG(1) << "clu-peek the sidebar hid with the Library open and no peek: "
               "the Library closes";
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<CluWindowState> window) {
                         if (window && !window->sidebar_peek().is_open() &&
                             window->archive_panel() &&
                             window->shell().layout().hot_zone) {
                           window->SetArchivePanel(false);
                         }
                       },
                       window_->GetWeakPtr()));
  }
  peek_.LayoutChanged();
}

void CluWindowSidebarPeek::SetHoverCard(bool shown) {
  if (hover_card_ == shown) {
    return;
  }
  hover_card_ = shown;
  UpdateChildren(/*pointer_taken=*/false);
}

void CluWindowSidebarPeek::MenuOpened() {
  PointerTaken();
  ++menus_;
  UpdateChildren(/*pointer_taken=*/false);
}

void CluWindowSidebarPeek::MenuClosed() {
  menus_ = std::max(0, menus_ - 1);
  // The native menu took the pointer while it ran.
  UpdateChildren(/*pointer_taken=*/true);
}

void CluWindowSidebarPeek::SetHeld(bool held) {
  if (!peek_.is_open()) {
    held_ = false;
    return;
  }
  if (held_ == held) {
    return;
  }
  VLOG(1) << "clu-peek hold " << held_ << " -> " << held
          << " hovered=" << peek_.hovered()
          << " peek_pointer=" << peek_pointer_.has_value()
          << " panel=" << peek_.rect().ToString();
  held_ = held;
  UpdateChildren(/*pointer_taken=*/false);
  // The pointer may have left while it was held with no event since: released, it retracts once it is off. The
  // release is recorded before the keyboard goes back: handing it back can blur the window inside ShowPassive, and
  // that blur must find the release to keep the peek.
  if (!held_) {
    peek_.HoldReleased();
  }
  // Typing goes to the peek while it is held (the Library's filter, a pop-up row's Rename) and back to the window
  // after.
  window_->shell().ShowPassive(kPeekSlot, peek_.rect(), /*click_through=*/false,
                               /*focus=*/held_);
  if (!held_ && keyboard_handed_back_for_testing_) {
    keyboard_handed_back_for_testing_.Run();
  }
}

void CluWindowSidebarPeek::HoldForAction(std::string_view kind) {
  if (!peek_.is_open()) {
    return;
  }
  VLOG(1) << "clu-peek menu action " << kind << " holds it";
  if (kind == "rename") {
    // The rename field takes the keyboard; the overlay's release ends the hold once the rename is committed or
    // cancelled.
    SetHeld(true);
  } else {
    peek_.HoldForAction(kPeekRevealSettle);
  }
}

void CluWindowSidebarPeek::LibraryClosing() {
  VLOG(1) << "clu-peek Library closing, is_open=" << peek_.is_open()
          << " held=" << held_;
  if (peek_.is_open()) {
    SetHeld(false);
  }
}

void CluWindowSidebarPeek::LibraryClosed() {
  VLOG(1) << "clu-peek Library closed, panel=" << peek_.rect().ToString();
  peek_.CheckPointer();
}

void CluWindowSidebarPeek::WindowActivated() {
  // Back, Escape and the Library's button end the hold before they hand the window the keyboard, so they never get
  // here; a pointer still on the panel is the peek's own business.
  VLOG(1) << "clu-peek window activated, is_open=" << peek_.is_open()
          << " held=" << held_;
  if (!peek_.is_open() || !held_) {
    return;
  }
  const gfx::Rect& rect = peek_.rect();
  if (PointerOnPeek(gfx::Rect(rect.x(), rect.y(), rect.width() - kPeekBleed,
                              rect.height()))) {
    return;
  }
  VLOG(1) << "clu-peek click on the page while held: the Library closes";
  // The Library closes the way Back closes it: the hold ends, the panel narrows to the tabs, and the pointer, off it,
  // starts the usual grace.
  if (window_->archive_panel()) {
    window_->SetArchivePanel(false);
    return;
  }
  peek_.Close();
}

void CluWindowSidebarPeek::OnWidgetActivationChanged(views::Widget* widget,
                                                     bool active) {
  // A held peek's widget has the keyboard, so the window is already inactive and sees no blur when the app goes. The
  // release hands activation straight to the window; another app taking it shows once the activation settles.
  if (active || !held_) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<CluWindowState> window) {
                       if (window) {
                         window->sidebar_peek().ReleaseIfInactive();
                       }
                     },
                     window_->GetWeakPtr()));
}

void CluWindowSidebarPeek::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
}

void CluWindowSidebarPeek::ReleaseIfInactive() {
  if (!held_ || !peek_.is_open()) {
    return;
  }
  views::Widget* widget = window_->shell().passive_widget(kPeekSlot);
  const bool active =
      window_active_for_testing_
          ? *window_active_for_testing_
          : (widget && widget->IsActive()) || window_->browser()->IsActive();
  if (active) {
    return;
  }
  VLOG(1) << "clu-peek the app went inactive while held: the hold ends and the "
             "peek goes";
  SetHeld(false);
  peek_.Close();
}

void CluWindowSidebarPeek::PointerTaken() {
  peek_pointer_.reset();
}

void CluWindowSidebarPeek::UpdateChildren(bool pointer_taken) {
  peek_.SetChildren((hover_card_ ? 1 : 0) + menus_ + (held_ ? 1 : 0),
                    pointer_taken);
}

bool CluWindowSidebarPeek::OnFrameMouseEvent(
    const blink::WebMouseEvent& event) {
  // The frame WebUI fills the window's client area, so its widget coordinates are window coordinates.
  if (event.GetType() == blink::WebInputEvent::Type::kMouseLeave) {
    peek_.FramePointerLeft(event.PositionInWidget());
  } else if (IsPointerEvent(event.GetType())) {
    // A tab, a text selection or a window resize dragged to the edge never reveals.
    peek_.FramePointerAt(
        event.PositionInWidget(),
        event.GetType() == blink::WebInputEvent::Type::kMouseDown ||
            (event.GetModifiers() & kButtonsDown) != 0);
  }
  return false;
}

bool CluWindowSidebarPeek::OnPeekMouseEvent(const blink::WebMouseEvent& event) {
  if (!peek_.is_open()) {
    VLOG(1) << "clu-peek event on a peek widget with no peek: it hides";
    // The peek's widget outlived its peek (an empty panel left at the edge after the Library's Back): it goes,
    // and the pointer over it counts as over the frame so the edge still reveals. The widget sits at the window's
    // origin, so its coordinates are the frame's. It hides in the next task, not inside its own input callback.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<CluWindowState> window) {
                         if (window && !window->sidebar_peek().is_open()) {
                           window->shell().HidePassive(kPeekSlot);
                         }
                       },
                       window_->GetWeakPtr()));
    return OnFrameMouseEvent(event);
  }
  if (event.GetType() == blink::WebInputEvent::Type::kMouseLeave) {
    peek_pointer_.reset();
    // A leave sent at a point on the panel is AppKit's word that the widget stopped tracking the
    // pointer (it narrowed under it, or handed the keyboard back to the window), not that the pointer went. The system
    // pointer decides then; a real leave is sent at the edge it crossed.
    const gfx::Rect& rect = peek_.rect();
    const gfx::Rect panel(rect.x(), rect.y(),
                          std::max(0, rect.width() - kPeekBleed),
                          rect.height());
    const bool at_panel = gfx::RectF(panel).Contains(event.PositionInWidget());
    VLOG(1) << "clu-peek leave event at "
            << event.PositionInWidget().ToString() << " at_panel=" << at_panel
            << " panel=" << panel.ToString();
    if (at_panel && PointerOnPeek(panel)) {
      peek_.PeekLeaveWithPointerOn();
    } else {
      peek_.PeekPointerLeft();
    }
  } else if (IsPointerEvent(event.GetType())) {
    peek_pointer_ = event.PositionInWidget();
    peek_.PeekPointerAt(event.PositionInWidget().x());
  }
  return false;
}

const ShellLayout& CluWindowSidebarPeek::PeekLayout() {
  return window_->shell().layout();
}

bool CluWindowSidebarPeek::CanOpen() {
  // Under the command bar or a dialog the hot zone is covered; a request from a renderer then gets nothing either.
  // A Mini window has no sidebar to peek.
  return !window_->shell().is_mini() && !window_->shell().IsOverlayVisible();
}

void CluWindowSidebarPeek::ShowPeek(const gfx::Rect& rect) {
  VLOG(1) << "clu-peek show " << rect.ToString()
          << (instance_id_.empty() ? " (new peek)" : " (same peek, resized)")
          << " held=" << held_;
  if (!instance_id_.empty()) {
    // The open peek took a new width (the Library opening or closing in it): the same surface, resized.
    window_->MarkDirty();
    window_->shell().ShowPassive(kPeekSlot, peek_.rect(), /*click_through=*/false,
                                 /*focus=*/held_);
    return;
  }
  held_ = false;
  peek_pointer_.reset();
  instance_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  before_.clear();
  for (const OverlaySurfaceSnapshot& surface : window_->Surfaces()) {
    if (surface.instance_id != instance_id_) {
      before_.insert(surface.instance_id);
    }
  }
  // The overlay renders the peek from this state, so it goes out before the widget shows.
  window_->MarkDirty();
  window_->Flush();
  window_->shell().ShowPassive(kPeekSlot, rect);
}

void CluWindowSidebarPeek::HidePeek() {
  VLOG(1) << "clu-peek widget hides, held=" << held_
          << " library=" << window_->archive_panel();
  instance_id_.clear();
  before_.clear();
  held_ = false;
  peek_pointer_.reset();
  window_->shell().HidePassive(kPeekSlot);
  // A Library opened in the floating sidebar goes with it while the sidebar stays hidden, so the next peek shows the
  // tabs. Cmd+S docking the sidebar keeps it open there.
  if (window_->archive_panel() && window_->shell().layout().hot_zone) {
    window_->SetArchivePanel(false);
  }
  // Its hover card goes with it (a native row menu is modal and ends on its own).
  if (hover_card_) {
    window_->Hover(std::nullopt, std::nullopt);
  }
  window_->MarkDirty();
}

void CluWindowSidebarPeek::SlidePeekOut(bool leaving) {
  // The overlay starts the slide from this state, so it goes out now rather than with the next coalesced patch.
  window_->MarkDirty();
  window_->Flush();
}

bool CluWindowSidebarPeek::PointerInside(const gfx::Rect& rect) {
  views::WebView* frame = window_->shell().frame_view();
  display::Screen* screen = display::Screen::Get();
  if (!frame || !screen) {
    return false;
  }
  const gfx::Rect window = frame->GetBoundsInScreen();
  const gfx::Point cursor = screen->GetCursorScreenPoint();
  return rect.Contains(cursor - window.OffsetFromOrigin());
}

bool CluWindowSidebarPeek::PointerOnPeek(const gfx::Rect& rect) {
  if (pointer_on_peek_for_testing_) {
    return *pointer_on_peek_for_testing_;
  }
  // Back from the Library, pressed on the panel, retracted it at once. The panel's own events say
  // where the pointer is on it (the Back press itself among them); a pointer that left it sent the widget a leave. The
  // widget sits at the window's origin, so its coordinates are the window's.
  if (peek_pointer_) {
    return gfx::RectF(rect).Contains(*peek_pointer_);
  }
  return PointerInside(rect);
}

bool CluWindowSidebarPeek::PrefersReducedMotion() {
  return gfx::Animation::PrefersReducedMotion();
}

bool CluWindowSidebarPeek::WindowActive() {
  if (window_active_for_testing_) {
    return *window_active_for_testing_;
  }
  // A held peek's widget has the keyboard and the window counts as inactive meanwhile.
  return held_ || window_->browser()->IsActive();
}

bool CluWindowSidebarPeek::PinsPeek() {
  return !window_->shell().page_shown();
}

}  // namespace clu
