// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_sidebar_peek.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "ui/gfx/geometry/rect_f.h"

namespace clu {

gfx::Rect SidebarPeekRect(const ShellLayout& layout) {
  const int width = std::max(0, layout.sidebar_rect.width());
  const int height = std::max(0, layout.sidebar_rect.height());
  if (!layout.hot_zone || width == 0 || height == 0) {
    return gfx::Rect();
  }
  return gfx::Rect(0, 0, width + kPeekBleed, height);
}

bool ReachesPeekZone(const gfx::Rect& zone,
                     const gfx::PointF& point,
                     const std::optional<gfx::PointF>& previous,
                     base::TimeDelta gap,
                     bool buttons_down) {
  if (buttons_down || zone.IsEmpty() || !std::isfinite(point.x()) ||
      !std::isfinite(point.y())) {
    return false;
  }
  if (point.y() < zone.y() || point.y() >= zone.bottom()) {
    return false;
  }
  if (point.x() < zone.right()) {
    return true;
  }
  if (!previous || !std::isfinite(previous->x()) || !gap.is_positive() ||
      gap > kPeekEdgeSampleGap) {
    return false;
  }
  const float dx = point.x() - previous->x();
  if (dx >= 0) {
    return false;
  }
  const double frames = kPeekEdgeLookahead / std::max(gap, kPeekEdgeMinGap);
  return point.x() + dx * frames < zone.right();
}

bool SurfaceClosesSidebarPeek(std::string_view surface,
                              bool from_peek,
                              bool open_before) {
  return surface != "toast" && !from_peek && !open_before;
}

CluSidebarPeek::CluSidebarPeek(Delegate* delegate,
                               const base::TickClock* clock)
    : delegate_(delegate),
      clock_(clock ? clock : base::DefaultTickClock::GetInstance()),
      dwell_(clock),
      grace_(clock),
      guard_(clock),
      leave_check_(clock),
      blur_check_(clock),
      action_(clock) {}

CluSidebarPeek::~CluSidebarPeek() = default;

void CluSidebarPeek::FramePointerAt(const gfx::PointF& point,
                                    bool buttons_down) {
  const base::TimeTicks now = clock_->NowTicks();
  // The same position again at the same moment (an enter, then its move) is one sample.
  if (!buttons_down && last_point_ && *last_point_ == point &&
      now == last_time_) {
    return;
  }
  const ShellLayout& layout = delegate_->PeekLayout();
  const bool reached =
      layout.hot_zone && ReachesPeekZone(*layout.hot_zone, point, last_point_,
                                         now - last_time_, buttons_down);
  if (buttons_down) {
    last_point_.reset();
  } else {
    last_point_ = point;
    last_time_ = now;
  }
  if (!reached) {
    dwell_.Stop();
    dwell_fired_ = false;
    return;
  }
  StartDwell(/*crossed_out=*/false);
}

void CluSidebarPeek::FramePointerLeft(std::optional<gfx::PointF> point) {
  const ShellLayout& layout = delegate_->PeekLayout();
  const base::TimeTicks now = clock_->NowTicks();
  // Past the window's left edge, moving left from a recent sample with no button held: it went for the edge.
  const bool crossed_out =
      point && last_point_ && layout.hot_zone && point->x() < 0 &&
      point->x() < last_point_->x() && now - last_time_ <= kPeekEdgeSampleGap &&
      ReachesPeekZone(*layout.hot_zone, *point, last_point_, now - last_time_,
                      /*buttons_down=*/false);
  last_point_.reset();
  if (crossed_out) {
    StartDwell(/*crossed_out=*/true);
    return;
  }
  dwell_.Stop();
  dwell_fired_ = false;
}

void CluSidebarPeek::StartDwell(bool crossed_out) {
  if (open_ || dwell_fired_ || dwell_.IsRunning()) {
    return;
  }
  dwell_crossed_out_ = crossed_out;
  dwell_.Start(FROM_HERE, kPeekDwell,
               base::BindOnce(&CluSidebarPeek::OnDwell, base::Unretained(this)));
}

void CluSidebarPeek::OnDwell() {
  dwell_fired_ = true;
  Open();
  // The pointer left the window: unless it is on the peek by now, the grace takes the peek away again.
  if (open_ && dwell_crossed_out_ &&
      !delegate_->PointerInside(PanelRect())) {
    PointerOutsidePeek();
  }
  dwell_crossed_out_ = false;
  // The peek opened under the resting pointer, which sends the peek nothing until it moves, so the first reveal after
  // Cmd+S would retract only once the late leave came.
  if (open_ && hovered_) {
    StartLeaveCheck();
  }
}

void CluSidebarPeek::Open() {
  dwell_.Stop();
  if (open_) {
    ArmGuard();
    return;
  }
  const ShellLayout& layout = delegate_->PeekLayout();
  const gfx::Rect rect = SidebarPeekRect(layout);
  if (rect.IsEmpty() || !delegate_->CanOpen()) {
    return;
  }
  VLOG(1) << "clu-peek open rect=" << rect.ToString();
  open_ = true;
  rect_ = rect;
  hot_zone_ = layout.hot_zone;
  dwell_fired_ = true;
  hovered_ = true;
  children_ = 0;
  window_children_ = 0;
  guard_paused_ = false;
  released_at_ = base::TimeTicks();
  delegate_->ShowPeek(rect_);
  ArmGuard();
}

void CluSidebarPeek::Close() {
  if (open_ && delegate_->PinsPeek() && delegate_->CanOpen() &&
      SidebarPeekRect(delegate_->PeekLayout()) == rect_) {
    VLOG(1) << "clu-peek close: pinned, it stays";
    dwell_.Stop();
    return;
  }
  Hide();
}

void CluSidebarPeek::CloseOnBlur() {
  // The Library's Back or Escape hands the keyboard from the peek's own widget back to the window,
  // and activation can pass through "inactive" on the way. Right after a release that is no blur: the peek stays, and
  // the pointer decides as the release left it. On the panel it retracts once the pointer leaves; off it the grace the
  // release started slides it out.
  if (open_ && !released_at_.is_null() &&
      clock_->NowTicks() - released_at_ < kPeekReleaseSettle) {
    const bool on = delegate_->PointerOnPeek(PanelRect());
    VLOG(1) << "clu-peek blur right after the hold ended, pointer on the "
               "panel="
            << on << ": stays";
    if (on) {
      PointerInsidePeek();
    }
    // The hand-back activates the window again within milliseconds. The widget's last point cannot tell it from
    // Cmd+Tab: an inactive app's widgets get no mouse events, so that point never moves off the panel.
    blur_check_.Start(FROM_HERE, kPeekReleaseSettle,
                      base::BindOnce(&CluSidebarPeek::OnBlurCheck,
                                     base::Unretained(this)));
    return;
  }
  VLOG(1) << "clu-peek close: window blur, open=" << open_
          << " hovered=" << hovered_;
  Close();
}

void CluSidebarPeek::OnBlurCheck() {
  if (!open_ || delegate_->WindowActive()) {
    return;
  }
  VLOG(1) << "clu-peek close: the window is still inactive after the blur";
  Close();
}

void CluSidebarPeek::Hide() {
  dwell_.Stop();
  if (!open_) {
    return;
  }
  VLOG(1) << "clu-peek hide hovered=" << hovered_ << " children=" << children_
          << " leaving=" << leaving_;
  open_ = false;
  rect_ = gfx::Rect();
  hot_zone_.reset();
  hovered_ = false;
  children_ = 0;
  window_children_ = 0;
  guard_paused_ = false;
  leaving_ = false;
  grace_.Stop();
  slide_.Stop();
  guard_.Stop();
  leave_check_.Stop();
  blur_check_.Stop();
  action_.Stop();
  delegate_->HidePeek();
}

void CluSidebarPeek::PeekPointerAt(float x) {
  if (!open_) {
    return;
  }
  // The bleed carries only the shadow: a pointer there is over the page as far as the user can tell.
  if (x >= 0 && x < rect_.width() - kPeekBleed) {
    PointerInsidePeek();
  } else {
    PointerOutsidePeek();
  }
}

void CluSidebarPeek::PeekPointerLeft() {
  if (open_) {
    VLOG(1) << "clu-peek leave event from the peek widget";
    PointerOutsidePeek();
  }
}

void CluSidebarPeek::PeekLeaveWithPointerOn() {
  if (!open_) {
    return;
  }
  // The widget said the pointer left while the system pointer is still on the panel (the panel
  // narrowing or handing the keyboard back under it). The widget may get no more events for this pointer, so the leave
  // check watches the system pointer until it goes, for as long as the guard would.
  VLOG(1) << "clu-peek leave event with the system pointer still on the "
             "panel: not a leave";
  PointerInsidePeek();
  leave_check_until_ = clock_->NowTicks() + kPeekGuard;
}

gfx::Rect CluSidebarPeek::PanelRect() const {
  return gfx::Rect(rect_.x(), rect_.y(),
                   std::max(0, rect_.width() - kPeekBleed), rect_.height());
}

void CluSidebarPeek::PointerInsidePeek() {
  hovered_ = true;
  grace_.Stop();
  StopLeaving();
  ArmGuard();
  StartLeaveCheck();
}

void CluSidebarPeek::StartLeaveCheck() {
  if (delegate_->PinsPeek()) {
    leave_check_.Stop();
    return;
  }
  // A longer watch already running (PeekLeaveWithPointerOn) keeps its end.
  leave_check_until_ = std::max(leave_check_until_,
                                clock_->NowTicks() + kPeekLeaveCheckWindow);
  if (!leave_check_.IsRunning()) {
    leave_check_.Start(FROM_HERE, kPeekLeaveCheck,
                       base::BindRepeating(&CluSidebarPeek::OnLeaveCheck,
                                           base::Unretained(this)));
  }
}

void CluSidebarPeek::OnLeaveCheck() {
  if (!open_ || !hovered_ || delegate_->PinsPeek()) {
    leave_check_.Stop();
    return;
  }
  // A hover card or row menu opened from the peek holds it; the pointer on one of them is not a leave.
  if (children_ == 0 && !delegate_->PointerOnPeek(PanelRect())) {
    VLOG(1) << "clu-peek leave check: pointer off the panel";
    PointerOutsidePeek();
    return;
  }
  if (clock_->NowTicks() >= leave_check_until_) {
    leave_check_.Stop();
  }
}

void CluSidebarPeek::StopLeaving() {
  if (!leaving_) {
    return;
  }
  VLOG(1) << "clu-peek slide back in";
  leaving_ = false;
  slide_.Stop();
  delegate_->SlidePeekOut(false);
}

void CluSidebarPeek::PointerOutsidePeek() {
  VLOG(1) << "clu-peek pointer outside the panel";
  hovered_ = false;
  leave_check_.Stop();
  StartGrace();
}

void CluSidebarPeek::Alive() {
  if (open_) {
    ArmGuard();
  }
}

void CluSidebarPeek::SetChildren(int count, bool pointer_taken) {
  const int before = children_;
  window_children_ = std::max(0, count);
  children_ = window_children_ + (action_.IsRunning() ? 1 : 0);
  if (!open_) {
    return;
  }
  if (children_ > 0) {
    grace_.Stop();
    StopLeaving();
    return;
  }
  if (before > 0 && pointer_taken) {
    hovered_ = false;
  }
  VLOG(1) << "clu-peek children 0 (were " << before
          << ") pointer_taken=" << pointer_taken << " hovered=" << hovered_;
  if (guard_paused_) {
    ArmGuard();
  }
  StartGrace();
}

void CluSidebarPeek::HoldForAction(base::TimeDelta settle) {
  if (!open_) {
    return;
  }
  VLOG(1) << "clu-peek a menu action holds it for " << settle;
  action_.Start(FROM_HERE, settle,
                base::BindOnce(&CluSidebarPeek::OnActionSettled,
                               base::Unretained(this)));
  SetChildren(window_children_, /*pointer_taken=*/false);
}

void CluSidebarPeek::OnActionSettled() {
  VLOG(1) << "clu-peek the menu action settled";
  // Ended as a child does: the grace runs unless the pointer is on the panel.
  SetChildren(window_children_, /*pointer_taken=*/false);
}

void CluSidebarPeek::HoldReleased() {
  released_at_ = clock_->NowTicks();
  CheckPointer();
}

void CluSidebarPeek::CheckPointer() {
  if (!open_ || children_ > 0 || delegate_->PinsPeek()) {
    return;
  }
  const bool on = delegate_->PointerOnPeek(PanelRect());
  VLOG(1) << "clu-peek check pointer on=" << on
          << " panel=" << PanelRect().ToString();
  if (on) {
    PointerInsidePeek();
  } else {
    PointerOutsidePeek();
  }
}

void CluSidebarPeek::LayoutChanged() {
  const ShellLayout& layout = delegate_->PeekLayout();
  const bool pinned = delegate_->PinsPeek();
  const gfx::Rect rect = SidebarPeekRect(layout);
  if (open_ && layout.hot_zone == hot_zone_ && !rect.IsEmpty() &&
      rect != rect_) {
    // The same edge with a wider or narrower sidebar (the Library opening or closing in it): it stays and takes
    // the new width.
    VLOG(1) << "clu-peek layout: same edge, new width " << rect_.width()
            << " -> " << rect.width();
    rect_ = rect;
    delegate_->ShowPeek(rect_);
  } else if (open_ && (layout.hot_zone != hot_zone_ || rect != rect_)) {
    VLOG(1) << "clu-peek layout: hot zone moved or nothing to peek, hides";
    Hide();
  }
  if (!open_) {
    if (!layout.hot_zone) {
      FramePointerLeft();
    }
    // Hiding the sidebar with no page open floats it, and it stays.
    if (pinned) {
      Open();
    }
  } else if (pinned_ && !pinned) {
    // A page came: the floating sidebar now retracts once the pointer is off it.
    hovered_ = delegate_->PointerInside(PanelRect());
    VLOG(1) << "clu-peek layout: a page came, unpinned, hovered=" << hovered_;
    StartGrace();
  }
  pinned_ = pinned;
}

void CluSidebarPeek::StartGrace() {
  if (!open_ || hovered_ || children_ > 0 || grace_.IsRunning() || leaving_ ||
      delegate_->PinsPeek()) {
    return;
  }
  VLOG(1) << "clu-peek grace starts";
  grace_.Start(FROM_HERE, kPeekGrace,
               base::BindOnce(&CluSidebarPeek::OnGrace, base::Unretained(this)));
}

void CluSidebarPeek::OnGrace() {
  if (!open_ || hovered_ || children_ > 0) {
    return;
  }
  VLOG(1) << "clu-peek grace ran out: slides out";
  if (delegate_->PrefersReducedMotion()) {
    Close();
    return;
  }
  // The overlay slides out from this moment; the widget goes when the overlay says the slide is done, at the
  // latest after the hold.
  leaving_ = true;
  delegate_->SlidePeekOut(true);
  slide_.Start(FROM_HERE, kPeekSlideOutHold,
               base::BindOnce(&CluSidebarPeek::OnSlideOut,
                              base::Unretained(this)));
}

void CluSidebarPeek::OnSlideOut() {
  if (!open_ || !leaving_) {
    return;
  }
  VLOG(1) << "clu-peek slide-out hold ran out: closes";
  Close();
  // A peek that became pinned meanwhile stays: it shows again in place.
  if (open_) {
    StopLeaving();
  }
}

void CluSidebarPeek::ArmGuard() {
  guard_paused_ = false;
  guard_.Start(FROM_HERE, kPeekGuard,
               base::BindOnce(&CluSidebarPeek::OnGuard, base::Unretained(this)));
}

void CluSidebarPeek::OnGuard() {
  if (!open_) {
    return;
  }
  if (children_ > 0) {
    guard_paused_ = true;
    return;
  }
  if (delegate_->PinsPeek() || delegate_->PointerInside(rect_)) {
    ArmGuard();
    return;
  }
  VLOG(1) << "clu-peek guard ran out with the pointer off: closes";
  Close();
}

}  // namespace clu
