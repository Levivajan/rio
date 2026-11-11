// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_SIDEBAR_PEEK_H_
#define CLU_BROWSER_UI_WEBUI_CLU_SIDEBAR_PEEK_H_

#include <optional>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"

namespace base {
class TickClock;
}  // namespace base

// Hover peek (docs/plan/01-shell.md F4, docs/PLAN.md 12.3 third box), with the renderer's twin in
// src/shared/shell/peek.ts. While the sidebar is hidden (or in focus mode) the layout
// leaves a hot zone at the window's left edge (x 0 to 5, kPeekZoneX). The pointer reaching it, or moving left fast
// enough to cross it between two samples, opens after kPeekDwell, with no button held. It opens the
// sidebar as a passive overlay at the sidebar rect plus a 16 px shadow bleed, over the page, which never moves. Leaving the peek starts a
// 165 ms grace (then the 90 ms slide-out, none with reduced motion) and closes it unless the pointer comes back. A
// child surface opened from the peek (its hover card, its row menu) holds it open. A 5 s guard, re-armed by every
// sign of the pointer on the peek, closes a peek nothing reports on, unless the pointer still rests on it.
//
// The browser process runs the dwell, the grace and the guard on one clock, from the mouse events Chromium routes
// to the frame and peek WebUIs, so a renderer cannot keep the peek over the page or open it on its own. The
// renderers keep their own copies of the same timings for the slide animation.
namespace clu {

// The reveal starts 40 ms after the pointer reaches the edge: a pointer sweeping past the edge does not reveal,
// and the slide starts within about 60 ms. PEEK_DWELL_MS in src/shared/shell/peek.ts.
inline constexpr base::TimeDelta kPeekDwell = base::Milliseconds(40);
// A pointer moving left reaches the edge when it would be in the zone one 60 Hz frame after its last sample. Samples
// further apart than kPeekEdgeSampleGap are a new approach; the speed is taken over at least kPeekEdgeMinGap so two
// samples a millisecond apart cannot fling it. PEEK_EDGE_* in src/shared/shell/peek.ts.
inline constexpr base::TimeDelta kPeekEdgeLookahead = base::Microseconds(16667);
inline constexpr base::TimeDelta kPeekEdgeSampleGap = base::Milliseconds(50);
inline constexpr base::TimeDelta kPeekEdgeMinGap = base::Milliseconds(8);
// The retract starts 165 ms after the pointer leaves and is gone
// 90 ms later; src/shared/shell/peek.ts slides it out on the same numbers, and the widget goes once the slide is done.
inline constexpr base::TimeDelta kPeekGrace = base::Milliseconds(165);
inline constexpr base::TimeDelta kPeekSlideOut = base::Milliseconds(90);
// The browser's grace starts the slide-out (Delegate::SlidePeekOut), so it starts 165 ms after the pointer left
// whatever the overlay's main thread is doing. The overlay closes the peek once its slide is done; the widget stays
// at most this long after the slide starts, so a slide that started a state patch late still ends on screen.
inline constexpr base::TimeDelta kPeekSlideOutHold =
    kPeekSlideOut + base::Milliseconds(60);
inline constexpr base::TimeDelta kPeekGuard = base::Seconds(5);
// While the pointer is over the open peek the browser looks where the system pointer is this often, so the grace
// starts within a frame of the pointer leaving the panel. AppKit can hand the peek's widget its leave (or the move past
// the panel) 100 ms and more after the pointer went, and the grace waited for that event.
inline constexpr base::TimeDelta kPeekLeaveCheck = base::Milliseconds(16);
// The late leave follows a move, so the check runs only this long after the peek's last pointer event (or its opening
// under the pointer); a resting pointer wakes nothing, and its next move starts the check again. A pinned peek never
// retracts and runs none.
inline constexpr base::TimeDelta kPeekLeaveCheckWindow = base::Milliseconds(300);
// A window blur this soon after a hold ended (the Library's Back or Escape handing the keyboard from
// the peek's widget back to the window) does not take a peek the pointer is on.
inline constexpr base::TimeDelta kPeekReleaseSettle = base::Milliseconds(500);
// The transparent strip right of the peek's sidebar that carries its shadow.
inline constexpr int kPeekBleed = 16;
// A menu action's scroll in the open peek (Reveal Tab in Sidebar) holds it this long, then the grace applies.
inline constexpr base::TimeDelta kPeekRevealSettle = base::Milliseconds(800);

// Where the peek goes for `layout`, in window coordinates: the sidebar rect plus the bleed, from the window's left
// edge. Empty while the layout has no hot zone (the sidebar shows) or no sidebar width: nothing to peek.
gfx::Rect SidebarPeekRect(const ShellLayout& layout);

// Whether the pointer at `point` reaches the reveal `zone` (window coordinates): inside it (left of the window counts),
// or moving left from `previous`, taken `gap` earlier, fast enough to be in it within kPeekEdgeLookahead, at the
// zone's height. A held button (a tab, a text selection or a window resize dragged to the edge) never reaches it.
// reachesPeekZone in src/shared/shell/peek.ts is the twin.
bool ReachesPeekZone(const gfx::Rect& zone,
                     const gfx::PointF& point,
                     const std::optional<gfx::PointF>& previous,
                     base::TimeDelta gap,
                     bool buttons_down);

// Whether a window surface closes the open peek when it shows (CluWindowSidebarPeek::Reconcile): a toast never does,
// nor the peek itself, a surface opened from it, or one already open when it opened.
bool SurfaceClosesSidebarPeek(std::string_view surface,
                              bool from_peek,
                              bool open_before);

class CluSidebarPeek {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // The window's current shell layout.
    virtual const ShellLayout& PeekLayout() = 0;
    // Whether the window can take a peek now: no top-layer surface (the command bar, a dialog) covers it.
    virtual bool CanOpen() = 0;
    // Shows the peek's passive overlay at `rect`, or hides it.
    virtual void ShowPeek(const gfx::Rect& rect) = 0;
    virtual void HidePeek() = 0;
    // The grace ran out: the overlay slides the open peek out (`leaving`), or the pointer came back onto it during
    // the slide and it slides back in.
    virtual void SlidePeekOut(bool leaving) = 0;
    // Whether the system pointer is inside `rect` (window coordinates) right now. A pointer that rests on the peek
    // without moving sends nothing, so the guard asks before it closes.
    virtual bool PointerInside(const gfx::Rect& rect) = 0;
    // Whether the system pointer is over the open peek's panel `rect` (window coordinates, the bleed left out), asked
    // every kPeekLeaveCheck while the pointer is on it.
    virtual bool PointerOnPeek(const gfx::Rect& rect) = 0;
    virtual bool PrefersReducedMotion() = 0;
    // The window shows no page: the hidden sidebar then floats and never retracts.
    virtual bool PinsPeek() = 0;
    // Whether the window, or the peek's own widget while it holds the keyboard, is active.
    virtual bool WindowActive() = 0;
  };

  // `clock` drives the dwell, grace and guard timers; null is the default tick clock.
  explicit CluSidebarPeek(Delegate* delegate,
                          const base::TickClock* clock = nullptr);
  CluSidebarPeek(const CluSidebarPeek&) = delete;
  CluSidebarPeek& operator=(const CluSidebarPeek&) = delete;
  ~CluSidebarPeek();

  bool is_open() const { return open_; }
  // The open peek's rect, empty while closed.
  const gfx::Rect& rect() const { return rect_; }

  // The pointer in the frame WebUI at `point` (window coordinates), `buttons_down` when a mouse button is held: a
  // sample that reaches the hot zone (ReachesPeekZone) starts the dwell, unless one runs or already fired since the
  // pointer came in; any other cancels the dwell.
  void FramePointerAt(const gfx::PointF& point, bool buttons_down);
  // The pointer left the frame WebUI. Left at `point` past the window's left edge on its way there (a fast pointer
  // on a window that does not touch the screen's edge), it reached the zone: the dwell runs and the peek opens, then
  // retracts after the grace unless the pointer comes onto it.
  void FramePointerLeft(std::optional<gfx::PointF> point = std::nullopt);

  // Opens the peek: the dwell firing, or shell.setSidebarPeek({open: true}). Only while the layout has a hot zone and
  // the window can take it; an open peek only re-arms its guard.
  void Open();
  // Closes the peek: the grace, shell.setSidebarPeek({open: false}), window blur, another surface opening. A pinned
  // peek (PinsPeek) stays while the window can take it: only a surface that covers it closes it.
  void Close();
  // The window lost focus: closes, unless a hold ended within kPeekReleaseSettle (the keyboard passing from the peek's
  // widget back to the window). That blur keeps the grace the release started for a pointer off the panel, and
  // is looked at again kPeekReleaseSettle later: a window still inactive then lost focus for real (Cmd+Tab right after
  // Escape), and the peek closes.
  void CloseOnBlur();

  // The pointer in the peek's WebUI at `x` (peek coordinates): over its sidebar it is inside, over the bleed it has
  // left.
  void PeekPointerAt(float x);
  // The pointer left the peek's WebUI.
  void PeekPointerLeft();
  // The peek's WebUI said the pointer left, but the system pointer is still on the panel: it stays hovered and the
  // leave check watches the system pointer.
  void PeekLeaveWithPointerOn();
  // shell.pointerAlive: the overlay says the pointer is still over the peek.
  void Alive();
  // Surfaces opened from the peek: its hover card and its row menu. While any is open the grace cannot close the peek
  // and the guard waits. When the last one closes, `pointer_taken` says it took the pointer (the native menu): the
  // peek then counts as left until the pointer moves over it again.
  void SetChildren(int count, bool pointer_taken);
  // A menu action acting inside the open peek (Reveal Tab's scroll) holds it as a child for `settle`; then the grace
  // applies as when a child closes.
  void HoldForAction(base::TimeDelta settle);
  // A hold just ended under a narrower panel (the Library's Back, Escape or its button): asks where the pointer
  // is, since the panel may have narrowed out from under it with no event. Off the panel the grace runs; on it the
  // leave check watches for it going.
  void CheckPointer();
  // A hold just ended (the Library's Back, Escape or its button, a pop-up row's Rename): CheckPointer(), and a blur
  // within kPeekReleaseSettle does not take the peek at once (CloseOnBlur). Called before the keyboard goes
  // back to the window, since that hand-back can blur the window on the spot.
  void HoldReleased();
  // The layout changed: a peek whose hot zone moved (a resize, focus mode) or that has nothing to peek any more (Cmd+S
  // showed the sidebar) closes; one whose width alone changed (the Library opening in it) shows again at it. With no page shown a hidden sidebar opens pinned; once a page shows it goes when the
  // pointer is off it.
  void LayoutChanged();

  bool hovered() const { return hovered_; }
  // The grace ran out and the peek is sliding out.
  bool leaving() const { return leaving_; }
  int children() const { return children_; }
  bool dwell_running_for_testing() const { return dwell_.IsRunning(); }
  bool grace_running_for_testing() const { return grace_.IsRunning(); }
  bool guard_running_for_testing() const { return guard_.IsRunning(); }
  bool slide_running_for_testing() const { return slide_.IsRunning(); }
  bool leave_check_running_for_testing() const {
    return leave_check_.IsRunning();
  }
  bool blur_check_running_for_testing() const {
    return blur_check_.IsRunning();
  }
  bool action_running_for_testing() const { return action_.IsRunning(); }

 private:
  // rect_ without the bleed: the panel the user sees.
  gfx::Rect PanelRect() const;
  void PointerInsidePeek();
  void PointerOutsidePeek();
  void StartGrace();
  void OnGrace();
  // The slide-out's hold ran out: the peek goes.
  void OnSlideOut();
  // The pointer came back, or a child opened, during the slide-out: the peek slides back in.
  void StopLeaving();
  void ArmGuard();
  void OnGuard();
  void StartLeaveCheck();
  // kPeekLeaveCheck: the system pointer is off the panel, so the pointer has left whether or not its event came.
  void OnLeaveCheck();
  // kPeekReleaseSettle after a blur CloseOnBlur let pass: closes if the window is still inactive.
  void OnBlurCheck();
  // The action hold's settle ran out.
  void OnActionSettled();
  // Closes whether pinned or not.
  void Hide();

  // `crossed_out`: the pointer left the window on its way to the edge, so it may not be on the peek once it opens.
  void StartDwell(bool crossed_out);
  void OnDwell();

  const raw_ptr<Delegate> delegate_;
  const raw_ptr<const base::TickClock> clock_;
  // The frame's last pointer sample and when it came, for the speed toward the edge.
  std::optional<gfx::PointF> last_point_;
  base::TimeTicks last_time_;
  // The running dwell came from a pointer that left past the window's left edge.
  bool dwell_crossed_out_ = false;
  bool open_ = false;
  gfx::Rect rect_;
  // The hot zone the peek opened from: focus mode starting or ending moves it, which closes the peek.
  std::optional<gfx::Rect> hot_zone_;
  // The dwell fired (or the peek opened) since the pointer came into the hot zone: no second dwell until it leaves.
  bool dwell_fired_ = false;
  // The peek opens under the pointer, at the window's edge inside the sidebar rect.
  bool hovered_ = false;
  int children_ = 0;
  // The count SetChildren() last set: children_ adds one while an action holds the peek.
  int window_children_ = 0;
  // The guard expired while a child was open; it runs again once the last child closes.
  bool guard_paused_ = false;
  // PinsPeek() at the last layout change.
  bool pinned_ = false;
  bool leaving_ = false;
  base::OneShotTimer dwell_;
  base::OneShotTimer grace_;
  base::OneShotTimer slide_;
  base::OneShotTimer guard_;
  base::RepeatingTimer leave_check_;
  base::OneShotTimer blur_check_;
  base::OneShotTimer action_;
  // The leave check stops at this time unless a pointer event moves it on.
  base::TimeTicks leave_check_until_;
  // When the last hold ended (HoldReleased()).
  base::TimeTicks released_at_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_SIDEBAR_PEEK_H_
