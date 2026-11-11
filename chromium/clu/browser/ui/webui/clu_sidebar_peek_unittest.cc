// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_sidebar_peek.h"

#include <limits>
#include <vector>

#include "base/test/task_environment.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

// The timing rules of the hover peek on a mock clock: the cases of src/shared/shell/peek.test.ts, plus the junk
// the mouse taps and Mojo can hand it.
namespace clu {
namespace {

constexpr gfx::Size kWindow(1280, 800);

class FakeWindow : public CluSidebarPeek::Delegate {
 public:
  FakeWindow() { SetChrome(/*hidden=*/true, /*focus=*/false); }

  void SetChrome(bool hidden,
                 bool focus,
                 gfx::Size size = kWindow,
                 bool library = false) {
    ChromeState chrome;
    chrome.sidebar_hidden = hidden;
    chrome.focus_mode = focus;
    chrome.library_open = library;
    layout_ = ComputeShellLayout(size, chrome);
  }

  const ShellLayout& PeekLayout() override { return layout_; }
  bool CanOpen() override { return can_open; }
  void ShowPeek(const gfx::Rect& rect) override {
    shown = rect;
    ++shows;
  }
  void HidePeek() override {
    shown.reset();
    ++hides;
  }
  void SlidePeekOut(bool leaving) override { slides.push_back(leaving); }
  bool PointerInside(const gfx::Rect& rect) override {
    ++pointer_asks;
    return pointer_inside;
  }
  bool PointerOnPeek(const gfx::Rect& rect) override {
    on_peek_rects.push_back(rect);
    return pointer_on_peek;
  }
  bool PrefersReducedMotion() override { return reduced_motion; }
  bool PinsPeek() override { return pinned; }
  bool WindowActive() override { return window_active; }

  std::optional<gfx::Rect> shown;
  int shows = 0;
  int hides = 0;
  int pointer_asks = 0;
  std::vector<bool> slides;
  bool can_open = true;
  bool pointer_inside = false;
  bool pointer_on_peek = true;
  std::vector<gfx::Rect> on_peek_rects;
  bool reduced_motion = false;
  bool pinned = false;
  bool window_active = true;

 private:
  ShellLayout layout_;
};

class CluSidebarPeekTest : public testing::Test {
 protected:
  void FastForward(base::TimeDelta delta) {
    task_environment_.FastForwardBy(delta);
  }
  // The pointer resting in the hot zone at the window's left edge.
  void IntoHotZone() { peek_.FramePointerAt(gfx::PointF(2, 300), false); }
  // Opens the peek through the dwell.
  void OpenByDwell() {
    IntoHotZone();
    FastForward(kPeekDwell);
    ASSERT_TRUE(peek_.is_open());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakeWindow window_;
  CluSidebarPeek peek_{&window_, task_environment_.GetMockTickClock()};
};

TEST_F(CluSidebarPeekTest, ThePeekRectIsTheSidebarPlusTheBleedOnlyWhileHidden) {
  ChromeState chrome;
  EXPECT_TRUE(SidebarPeekRect(ComputeShellLayout(kWindow, chrome)).IsEmpty());
  chrome.sidebar_hidden = true;
  EXPECT_EQ(SidebarPeekRect(ComputeShellLayout(kWindow, chrome)),
            gfx::Rect(0, 0, kSidebarWidth + kPeekBleed, 800));
  chrome.sidebar_width = 333;
  EXPECT_EQ(SidebarPeekRect(ComputeShellLayout(kWindow, chrome)),
            gfx::Rect(0, 0, 333 + kPeekBleed, 800));
  // Focus mode hides the sidebar too, with the 4 px gutter as the hot zone.
  chrome = ChromeState();
  chrome.focus_mode = true;
  EXPECT_EQ(SidebarPeekRect(ComputeShellLayout(kWindow, chrome)),
            gfx::Rect(0, 0, kSidebarWidth + kPeekBleed, 800));
  // No room, no peek.
  EXPECT_TRUE(
      SidebarPeekRect(ComputeShellLayout(gfx::Size(0, 0), chrome)).IsEmpty());
}

TEST_F(CluSidebarPeekTest, OpensAfterTheDwellInTheHotZone) {
  IntoHotZone();
  FastForward(kPeekDwell - base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());
  // Moving inside the zone does not restart the dwell.
  peek_.FramePointerAt(gfx::PointF(5, 310), false);
  FastForward(base::Milliseconds(1));
  ASSERT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.shown, gfx::Rect(0, 0, kSidebarWidth + kPeekBleed, 800));
  EXPECT_EQ(window_.shows, 1);
  EXPECT_TRUE(peek_.hovered());
}

// The zone reveals from x 0 to 5; x 7 (and 6) do not. The slide starts within about 60 ms.
TEST_F(CluSidebarPeekTest, OnlyTheFirstFivePointsReveal) {
  EXPECT_EQ(kPeekDwell, base::Milliseconds(40));
  EXPECT_LE(kPeekDwell + kPeekEdgeLookahead, base::Milliseconds(60));
  EXPECT_EQ(kPeekGrace, base::Milliseconds(165));
  EXPECT_EQ(kPeekSlideOut, base::Milliseconds(90));
  peek_.FramePointerAt(gfx::PointF(7, 300), false);
  EXPECT_FALSE(peek_.dwell_running_for_testing());
  peek_.FramePointerAt(gfx::PointF(6, 300), false);
  EXPECT_FALSE(peek_.dwell_running_for_testing());
  peek_.FramePointerAt(gfx::PointF(5, 300), false);
  EXPECT_TRUE(peek_.dwell_running_for_testing());
}

// Hiding the sidebar with no page open floats it at once, and it stays: no grace, no guard, no close request,
// no blur. Only a surface that covers it closes it. Once a page shows it retracts when the pointer is off it.
TEST_F(CluSidebarPeekTest, WithNoPageTheHiddenSidebarFloatsAndStays) {
  window_.pinned = true;
  peek_.LayoutChanged();
  ASSERT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.shown, gfx::Rect(0, 0, kSidebarWidth + kPeekBleed, 800));

  peek_.PeekPointerLeft();
  FastForward(kPeekGrace + kPeekSlideOutHold + kPeekGuard * 3);
  EXPECT_TRUE(peek_.is_open());
  peek_.Close();
  EXPECT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.hides, 0);

  // The command bar covers it.
  window_.can_open = false;
  peek_.Close();
  EXPECT_FALSE(peek_.is_open());
  window_.can_open = true;
  peek_.LayoutChanged();
  ASSERT_TRUE(peek_.is_open());

  // A page opens with the pointer away from the panel: the grace runs and the panel goes.
  window_.pinned = false;
  window_.pointer_inside = false;
  peek_.LayoutChanged();
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
  // And nothing reopens it.
  peek_.LayoutChanged();
  EXPECT_FALSE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, LeavingTheHotZoneBeforeTheDwellCancelsIt) {
  IntoHotZone();
  FastForward(kPeekDwell / 2);
  // Out of the 8 px zone, onto the page card's margin.
  peek_.FramePointerAt(gfx::PointF(20, 300), false);
  FastForward(kPeekDwell);
  EXPECT_FALSE(peek_.is_open());
  IntoHotZone();
  FastForward(kPeekDwell / 2);
  peek_.FramePointerLeft();
  FastForward(kPeekDwell);
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.shows, 0);
}

// The rule both the frame and the browser run (reachesPeekZone in src/shared/shell/peek.ts).
TEST_F(CluSidebarPeekTest, ReachesPeekZoneCountsTheZoneAndAFastPointerMovingLeft) {
  const gfx::Rect zone(0, 0, kPeekZoneX + 1, 800);
  const base::TimeDelta frame = base::Milliseconds(16);
  EXPECT_TRUE(ReachesPeekZone(zone, gfx::PointF(5, 400), std::nullopt, frame,
                              false));
  EXPECT_TRUE(ReachesPeekZone(zone, gfx::PointF(-3, 400), std::nullopt, frame,
                              false));
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(6, 400), std::nullopt, frame,
                               false));
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(2, 800), std::nullopt, frame,
                               false));
  // 60 px in one frame: at x 9 now, in the zone before the next sample.
  EXPECT_TRUE(ReachesPeekZone(zone, gfx::PointF(9, 400), gfx::PointF(69, 400),
                              frame, false));
  // A slow drift, a move to the right, a stale sample, two samples 1 ms apart.
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(8, 400), gfx::PointF(9, 400),
                               frame, false));
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(9, 400), gfx::PointF(3, 400),
                               frame, false));
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(9, 400),
                               gfx::PointF(200, 400), base::Milliseconds(200),
                               false));
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(9, 400), gfx::PointF(10, 400),
                               base::Milliseconds(1), false));
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(9, 400), gfx::PointF(69, 400),
                               base::TimeDelta(), false));
  // A held button never reaches it.
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(2, 400), std::nullopt, frame,
                               true));
  EXPECT_FALSE(ReachesPeekZone(zone, gfx::PointF(9, 400), gfx::PointF(69, 400),
                               frame, true));
  EXPECT_FALSE(ReachesPeekZone(gfx::Rect(), gfx::PointF(0, 0), std::nullopt,
                               frame, false));
}

TEST_F(CluSidebarPeekTest, AFastPointerThatCrossesTheZoneBetweenSamplesReveals) {
  peek_.FramePointerAt(gfx::PointF(69, 300), false);
  FastForward(base::Milliseconds(16));
  peek_.FramePointerAt(gfx::PointF(9, 300), false);
  EXPECT_TRUE(peek_.dwell_running_for_testing());
  FastForward(kPeekDwell);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_TRUE(peek_.hovered());
}

TEST_F(CluSidebarPeekTest, AFlingThatStopsShortCancelsAtItsNextSample) {
  peek_.FramePointerAt(gfx::PointF(69, 300), false);
  FastForward(base::Milliseconds(16));
  peek_.FramePointerAt(gfx::PointF(9, 300), false);
  FastForward(base::Milliseconds(16));
  peek_.FramePointerAt(gfx::PointF(8, 300), false);
  FastForward(kPeekDwell * 3);
  EXPECT_FALSE(peek_.is_open());
}

// Dragging a tab, a text selection or a window resize to the edge never reveals; released there, it does.
TEST_F(CluSidebarPeekTest, AHeldButtonNeverReveals) {
  peek_.FramePointerAt(gfx::PointF(2, 300), true);
  peek_.FramePointerAt(gfx::PointF(69, 300), true);
  FastForward(base::Milliseconds(16));
  peek_.FramePointerAt(gfx::PointF(9, 300), true);
  peek_.FramePointerLeft(gfx::PointF(-4, 300));
  FastForward(kPeekDwell * 3);
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.shows, 0);
  peek_.FramePointerAt(gfx::PointF(2, 300), false);
  FastForward(kPeekDwell);
  EXPECT_TRUE(peek_.is_open());
}

// A window clear of the screen's edge: the pointer flung out past its left edge reaches the zone. The peek opens, and
// as the pointer is not on it the grace takes it away again.
TEST_F(CluSidebarPeekTest, APointerFlungOutPastTheLeftEdgeRevealsThenRetracts) {
  peek_.FramePointerAt(gfx::PointF(9, 300), false);
  FastForward(base::Milliseconds(16));
  peek_.FramePointerLeft(gfx::PointF(-30, 300));
  ASSERT_TRUE(peek_.dwell_running_for_testing());
  FastForward(kPeekDwell);
  ASSERT_TRUE(peek_.is_open());
  EXPECT_FALSE(peek_.hovered());
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
  // Leaving up or right, or long after the last sample, reveals nothing.
  peek_.FramePointerAt(gfx::PointF(9, 300), false);
  FastForward(base::Milliseconds(16));
  peek_.FramePointerLeft(gfx::PointF(9, -2));
  peek_.FramePointerAt(gfx::PointF(9, 300), false);
  FastForward(base::Milliseconds(200));
  peek_.FramePointerLeft(gfx::PointF(-30, 300));
  FastForward(kPeekDwell * 3);
  EXPECT_EQ(window_.shows, 1);
}

TEST_F(CluSidebarPeekTest, NothingPeeksWhileTheSidebarShows) {
  window_.SetChrome(/*hidden=*/false, /*focus=*/false);
  IntoHotZone();
  FastForward(kPeekDwell * 2);
  EXPECT_FALSE(peek_.is_open());
  peek_.Open();
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.shows, 0);
}

// A window under the command bar or a dialog takes no peek; the next rest after that does.
TEST_F(CluSidebarPeekTest, AWindowThatCannotTakeAPeekGetsNone) {
  window_.can_open = false;
  IntoHotZone();
  FastForward(kPeekDwell * 2);
  peek_.Open();
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.shows, 0);
  window_.can_open = true;
  peek_.FramePointerLeft();
  IntoHotZone();
  FastForward(kPeekDwell);
  EXPECT_TRUE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, FocusModePeeksFromItsGutter) {
  window_.SetChrome(/*hidden=*/false, /*focus=*/true);
  peek_.FramePointerAt(gfx::PointF(6, 300), false);
  FastForward(kPeekDwell);
  EXPECT_FALSE(peek_.is_open());
  peek_.FramePointerAt(gfx::PointF(2, 300), false);
  FastForward(kPeekDwell);
  EXPECT_TRUE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, TheDwellFiresOnceUntilThePointerLeavesTheZone) {
  OpenByDwell();
  peek_.Close();
  // Still in the zone: no second peek from the same rest.
  IntoHotZone();
  FastForward(kPeekDwell * 2);
  EXPECT_FALSE(peek_.is_open());
  peek_.FramePointerLeft();
  IntoHotZone();
  FastForward(kPeekDwell);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.shows, 2);
}

TEST_F(CluSidebarPeekTest, LeavingStartsTheGraceAndTheSlideOut) {
  OpenByDwell();
  peek_.PeekPointerAt(100);
  peek_.PeekPointerLeft();
  // The slide-out starts on the browser's clock 165 ms after the pointer left, and the widget stays past the
  // 90 ms slide so the overlay's slide always ends on screen.
  FastForward(kPeekGrace - base::Milliseconds(1));
  EXPECT_FALSE(peek_.leaving());
  EXPECT_TRUE(window_.slides.empty());
  FastForward(base::Milliseconds(1));
  EXPECT_TRUE(peek_.leaving());
  EXPECT_EQ(window_.slides, std::vector<bool>({true}));
  FastForward(kPeekSlideOut);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_GT(kPeekSlideOutHold, kPeekSlideOut);
  FastForward(kPeekSlideOutHold - kPeekSlideOut - base::Milliseconds(1));
  EXPECT_TRUE(peek_.is_open());
  FastForward(base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());
  EXPECT_FALSE(window_.shown);
  EXPECT_EQ(window_.hides, 1);
}

// AppKit can hand the peek its leave 100 ms and more late; the browser looks at the system pointer every frame
// while it is on the panel, so the retract starts about 165 ms after the pointer actually left.
TEST_F(CluSidebarPeekTest, CluSidebarPeekRetractCountsFromThePointerLeavingNotItsLateEvent) {
  OpenByDwell();
  peek_.PeekPointerAt(100);
  EXPECT_TRUE(peek_.leave_check_running_for_testing());
  FastForward(kPeekLeaveCheck * 10);
  EXPECT_TRUE(peek_.hovered());
  EXPECT_FALSE(peek_.grace_running_for_testing());
  ASSERT_FALSE(window_.on_peek_rects.empty());
  // The panel, the shadow's bleed left out.
  EXPECT_EQ(window_.on_peek_rects.back().width(),
            peek_.rect().width() - kPeekBleed);
  // The pointer goes off the panel and no event comes.
  window_.pointer_on_peek = false;
  FastForward(kPeekLeaveCheck);
  EXPECT_FALSE(peek_.hovered());
  EXPECT_TRUE(peek_.grace_running_for_testing());
  EXPECT_FALSE(peek_.leave_check_running_for_testing());
  FastForward(kPeekGrace);
  EXPECT_TRUE(peek_.leaving());
  // The late leave changes nothing.
  peek_.PeekPointerLeft();
  EXPECT_EQ(window_.slides, std::vector<bool>({true}));
}

// The first reveal after Cmd+S: the peek opens under the resting pointer, which sends it nothing until it moves away.
TEST_F(CluSidebarPeekTest, APeekOpenedUnderTheRestingPointerSeesItLeave) {
  OpenByDwell();
  EXPECT_TRUE(peek_.leave_check_running_for_testing());
  window_.pointer_on_peek = false;
  FastForward(kPeekLeaveCheck);
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace);
  EXPECT_TRUE(peek_.leaving());
}

// The check wakes the UI thread every frame, so it runs only right after the pointer moved: a resting pointer stops
// it, and the next move on the peek starts it again.
TEST_F(CluSidebarPeekTest, TheLeaveCheckStopsWhileThePointerRests) {
  OpenByDwell();
  // The resting pointer is inside the peek, so the guard keeps it open.
  window_.pointer_inside = true;
  peek_.PeekPointerAt(100);
  FastForward(kPeekLeaveCheckWindow + kPeekLeaveCheck);
  EXPECT_FALSE(peek_.leave_check_running_for_testing());
  EXPECT_TRUE(peek_.hovered());
  EXPECT_TRUE(peek_.is_open());
  const size_t asks = window_.on_peek_rects.size();
  FastForward(base::Minutes(1));
  EXPECT_EQ(window_.on_peek_rects.size(), asks);
  // The next move starts it again, and the leave after it is seen within a frame.
  peek_.PeekPointerAt(120);
  EXPECT_TRUE(peek_.leave_check_running_for_testing());
  window_.pointer_inside = false;
  window_.pointer_on_peek = false;
  FastForward(kPeekLeaveCheck);
  EXPECT_TRUE(peek_.grace_running_for_testing());
}

// A pinned floating sidebar (no page) never retracts, so nothing asks where the pointer is while it rests there.
TEST_F(CluSidebarPeekTest, APinnedPeekRunsNoLeaveCheck) {
  window_.pinned = true;
  peek_.LayoutChanged();
  ASSERT_TRUE(peek_.is_open());
  peek_.PeekPointerAt(100);
  EXPECT_FALSE(peek_.leave_check_running_for_testing());
  FastForward(base::Seconds(10));
  EXPECT_TRUE(window_.on_peek_rects.empty());
  EXPECT_TRUE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, ThePointerOnAChildOfThePeekIsNoLeave) {
  OpenByDwell();
  peek_.PeekPointerAt(100);
  peek_.SetChildren(1, false);
  window_.pointer_on_peek = false;
  FastForward(kPeekLeaveCheck * 4);
  EXPECT_TRUE(peek_.hovered());
  EXPECT_FALSE(peek_.grace_running_for_testing());
}

// The overlay's slide ends and it closes the peek before the hold; a pointer back on the peek during the slide brings
// it back in and the grace starts over when it leaves again.
TEST_F(CluSidebarPeekTest, CluSidebarPeekSlideOutEndsOnTheOverlaysCloseOrComesBack) {
  OpenByDwell();
  peek_.PeekPointerLeft();
  FastForward(kPeekGrace + base::Milliseconds(30));
  ASSERT_TRUE(peek_.leaving());
  peek_.PeekPointerAt(50);
  EXPECT_FALSE(peek_.leaving());
  EXPECT_FALSE(peek_.slide_running_for_testing());
  EXPECT_EQ(window_.slides, std::vector<bool>({true, false}));
  FastForward(kPeekSlideOutHold * 3);
  EXPECT_TRUE(peek_.is_open());
  peek_.PeekPointerLeft();
  FastForward(kPeekGrace + kPeekSlideOut);
  ASSERT_TRUE(peek_.leaving());
  // shell.setSidebarPeek({open: false}) from the overlay once its slide is done.
  peek_.Close();
  EXPECT_FALSE(peek_.is_open());
  EXPECT_FALSE(peek_.leaving());
  EXPECT_FALSE(peek_.slide_running_for_testing());
  EXPECT_EQ(window_.hides, 1);
}

TEST_F(CluSidebarPeekTest, ReducedMotionClosesRightAfterTheGrace) {
  window_.reduced_motion = true;
  OpenByDwell();
  peek_.PeekPointerLeft();
  FastForward(kPeekGrace - base::Milliseconds(1));
  EXPECT_TRUE(peek_.is_open());
  FastForward(base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, ComingBackDuringTheGraceKeepsThePeek) {
  OpenByDwell();
  peek_.PeekPointerLeft();
  FastForward(kPeekGrace - base::Milliseconds(10));
  peek_.PeekPointerAt(50);
  EXPECT_FALSE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace * 3);
  EXPECT_TRUE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, TheBleedCountsAsOutside) {
  OpenByDwell();
  // The sidebar ends at 214; 214..229 is the shadow.
  peek_.PeekPointerAt(kSidebarWidth - 1);
  EXPECT_TRUE(peek_.hovered());
  peek_.PeekPointerAt(kSidebarWidth);
  EXPECT_FALSE(peek_.hovered());
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, ChildrenHoldThePeekAndTheGraceRunsAfterTheLast) {
  OpenByDwell();
  // The hover card shows from a row, then the pointer leaves for the page.
  peek_.SetChildren(1, false);
  peek_.PeekPointerLeft();
  FastForward(kPeekGrace * 4);
  EXPECT_TRUE(peek_.is_open());
  // The card hides: the pointer is out, so the grace runs.
  peek_.SetChildren(0, false);
  FastForward(kPeekGrace + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
}

// A toast is never a surface for the peek: opening while the peek is open, held or not, it never closes it. Other new
// surfaces do, and the peek's own children and surfaces open before it never do.
TEST(CluSidebarPeekSurfaceTest, AToastNeverClosesThePeek) {
  EXPECT_FALSE(SurfaceClosesSidebarPeek("toast", /*from_peek=*/false,
                                        /*open_before=*/false));
  EXPECT_FALSE(SurfaceClosesSidebarPeek("toast", false, true));
  EXPECT_TRUE(SurfaceClosesSidebarPeek("command-bar", false, false));
  EXPECT_TRUE(SurfaceClosesSidebarPeek("link-peek", false, false));
  EXPECT_FALSE(SurfaceClosesSidebarPeek("command-bar", false, true));
  EXPECT_FALSE(SurfaceClosesSidebarPeek("tab-hover", true, false));
  EXPECT_TRUE(SurfaceClosesSidebarPeek("", false, false));
}

// A menu action's scroll inside the open peek (Reveal Tab in Sidebar, the pointer off on the menu bar) holds it through
// the settle; then the usual 165 ms grace and slide-out apply.
TEST_F(CluSidebarPeekTest, AMenuActionHoldsThePeekUntilItSettles) {
  OpenByDwell();
  peek_.PeekPointerLeft();
  ASSERT_TRUE(peek_.grace_running_for_testing());
  peek_.HoldForAction(kPeekRevealSettle);
  EXPECT_FALSE(peek_.grace_running_for_testing());
  EXPECT_EQ(peek_.children(), 1);
  // The window's own children change meanwhile (the Library's hold ending): the action still holds it.
  peek_.SetChildren(0, false);
  EXPECT_EQ(peek_.children(), 1);
  FastForward(kPeekRevealSettle - base::Milliseconds(1));
  EXPECT_TRUE(peek_.is_open());
  EXPECT_FALSE(peek_.leaving());
  FastForward(base::Milliseconds(1));
  EXPECT_EQ(peek_.children(), 0);
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace - base::Milliseconds(1));
  EXPECT_FALSE(peek_.leaving());
  FastForward(base::Milliseconds(1) + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
}

// The pointer on the panel when the action settles keeps the peek, and a closed peek takes no action hold.
TEST_F(CluSidebarPeekTest, AMenuActionEndsAsAChildDoes) {
  peek_.HoldForAction(kPeekRevealSettle);
  EXPECT_FALSE(peek_.action_running_for_testing());
  OpenByDwell();
  peek_.PeekPointerAt(100);
  peek_.HoldForAction(kPeekRevealSettle);
  FastForward(kPeekRevealSettle + kPeekGrace * 3);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_EQ(peek_.children(), 0);
  // Closing ends the hold with the peek.
  peek_.HoldForAction(kPeekRevealSettle);
  peek_.Close();
  EXPECT_FALSE(peek_.action_running_for_testing());
}

TEST_F(CluSidebarPeekTest, ANativeMenuTakesThePointerUntilItMovesAgain) {
  OpenByDwell();
  peek_.PeekPointerAt(100);
  peek_.SetChildren(1, false);
  FastForward(kPeekGuard * 2);
  EXPECT_TRUE(peek_.is_open());
  // The row menu closes: the peek counts as left even though no leave came.
  peek_.SetChildren(0, true);
  EXPECT_FALSE(peek_.hovered());
  EXPECT_TRUE(peek_.grace_running_for_testing());
  // A move over the peek before the grace ends keeps it.
  peek_.PeekPointerAt(100);
  FastForward(kPeekGrace * 3);
  EXPECT_TRUE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, TheGuardClosesAPeekNothingReportsOn) {
  OpenByDwell();
  // The pointer is gone and no leave came (the overlay never saw it).
  FastForward(kPeekGuard - base::Milliseconds(1));
  EXPECT_TRUE(peek_.is_open());
  FastForward(base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.pointer_asks, 1);
}

TEST_F(CluSidebarPeekTest, TheGuardKeepsAPeekThePointerRestsOn) {
  OpenByDwell();
  window_.pointer_inside = true;
  FastForward(kPeekGuard * 3);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.pointer_asks, 3);
  window_.pointer_inside = false;
  FastForward(kPeekGuard);
  EXPECT_FALSE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, AliveAndMovesReArmTheGuard) {
  OpenByDwell();
  for (int i = 0; i < 6; ++i) {
    FastForward(kPeekGuard - base::Milliseconds(500));
    if (i % 2) {
      peek_.Alive();
    } else {
      peek_.PeekPointerAt(80);
    }
  }
  EXPECT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.pointer_asks, 0);
  FastForward(kPeekGuard);
  EXPECT_FALSE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, TheGuardWaitsForChildrenThenRunsAgain) {
  OpenByDwell();
  peek_.SetChildren(2, false);
  FastForward(kPeekGuard * 3);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.pointer_asks, 0);
  peek_.SetChildren(1, false);
  FastForward(kPeekGuard);
  EXPECT_TRUE(peek_.is_open());
  // The last child closes with the pointer still over the peek: the guard runs again from here.
  peek_.SetChildren(0, false);
  FastForward(kPeekGuard - base::Milliseconds(1));
  EXPECT_TRUE(peek_.is_open());
  FastForward(base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, ASecondOpenReArmsTheGuardAndShowsOnce) {
  OpenByDwell();
  FastForward(kPeekGuard - base::Milliseconds(100));
  peek_.Open();
  FastForward(kPeekGuard - base::Milliseconds(100));
  EXPECT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.shows, 1);
}

TEST_F(CluSidebarPeekTest, CmdSResizeAndFocusModeClose) {
  // Cmd+S shows the sidebar: nothing to peek.
  OpenByDwell();
  window_.SetChrome(/*hidden=*/false, /*focus=*/false);
  peek_.LayoutChanged();
  EXPECT_FALSE(peek_.is_open());

  // A resize moves the peek rect.
  window_.SetChrome(/*hidden=*/true, /*focus=*/false);
  peek_.FramePointerLeft();
  OpenByDwell();
  peek_.LayoutChanged();
  EXPECT_TRUE(peek_.is_open());
  window_.SetChrome(/*hidden=*/true, /*focus=*/false, gfx::Size(1280, 600));
  peek_.LayoutChanged();
  EXPECT_FALSE(peek_.is_open());

  // Focus mode starting moves the hot zone to its 4 px gutter.
  window_.SetChrome(/*hidden=*/true, /*focus=*/false);
  peek_.FramePointerLeft();
  OpenByDwell();
  window_.SetChrome(/*hidden=*/true, /*focus=*/true);
  peek_.LayoutChanged();
  EXPECT_FALSE(peek_.is_open());
}

// The Library opening or closing in the floating sidebar changes its width alone: it shows again at the new
// width and keeps its hover state, children and timers.
TEST_F(CluSidebarPeekTest, TheLibraryWidensTheOpenPeekInPlace) {
  OpenByDwell();
  const gfx::Rect narrow = *window_.shown;
  peek_.SetChildren(1, false);
  window_.SetChrome(/*hidden=*/true, /*focus=*/false, kWindow,
                    /*library=*/true);
  peek_.LayoutChanged();
  ASSERT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.hides, 0);
  EXPECT_EQ(window_.shows, 2);
  EXPECT_EQ(window_.shown->width(), kLibraryWidth + kPeekBleed);
  EXPECT_EQ(peek_.rect(), *window_.shown);
  EXPECT_EQ(peek_.children(), 1);

  window_.SetChrome(/*hidden=*/true, /*focus=*/false);
  peek_.LayoutChanged();
  ASSERT_TRUE(peek_.is_open());
  EXPECT_EQ(*window_.shown, narrow);
  // Released with the pointer off it, it retracts after the grace as before.
  peek_.PeekPointerLeft();
  peek_.SetChildren(0, false);
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
}

// The hold ends as the Library closes and the panel narrows with no pointer event. Where the pointer is decides:
// off the narrow panel the grace runs and it retracts; on it the leave check watches, and it retracts once the
// pointer goes. Held, closed or pinned, the check does nothing.
TEST_F(CluSidebarPeekTest,
       CheckPointerAfterTheLibraryClosesRetractsOffThePanel) {
  OpenByDwell();
  peek_.SetChildren(1, false);
  window_.SetChrome(/*hidden=*/true, /*focus=*/false, kWindow,
                    /*library=*/true);
  peek_.LayoutChanged();
  window_.pointer_on_peek = false;
  peek_.CheckPointer();
  EXPECT_FALSE(peek_.grace_running_for_testing());

  window_.SetChrome(/*hidden=*/true, /*focus=*/false);
  peek_.LayoutChanged();
  peek_.SetChildren(0, false);
  window_.on_peek_rects.clear();
  peek_.CheckPointer();
  ASSERT_EQ(window_.on_peek_rects.size(), 1u);
  EXPECT_EQ(window_.on_peek_rects[0].width(), kSidebarWidth);
  EXPECT_FALSE(peek_.hovered());
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
  peek_.CheckPointer();
  EXPECT_EQ(window_.on_peek_rects.size(), 1u);

  // Back pressed with the pointer on the panel: it stays until the pointer goes.
  window_.pointer_on_peek = true;
  window_.SetChrome(/*hidden=*/true, /*focus=*/false);
  peek_.FramePointerAt(gfx::PointF(300, 300), false);
  OpenByDwell();
  peek_.CheckPointer();
  EXPECT_TRUE(peek_.hovered());
  EXPECT_FALSE(peek_.grace_running_for_testing());
  EXPECT_TRUE(peek_.leave_check_running_for_testing());
  window_.pointer_on_peek = false;
  FastForward(kPeekLeaveCheck + kPeekGrace + kPeekSlideOutHold +
              base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());

  // Pinned (no page open), it never retracts.
  window_.pinned = true;
  peek_.Open();
  ASSERT_TRUE(peek_.is_open());
  peek_.CheckPointer();
  EXPECT_FALSE(peek_.grace_running_for_testing());
  EXPECT_TRUE(peek_.is_open());
}

// Back or Escape in the Library, with the pointer on the panel. The released, narrowed panel keeps
// the peek through a leave from its widget while the system pointer is still on it, and through a blur right after
// the release; the leave check then watches the system pointer past its usual window, and the peek retracts once the
// pointer goes. A blur later than kPeekReleaseSettle still closes it.
TEST_F(CluSidebarPeekTest, BackWithThePointerOnThePanelKeepsItUntilItLeaves) {
  OpenByDwell();
  peek_.SetChildren(1, false);  // held by the Library
  window_.SetChrome(/*hidden=*/true, /*focus=*/false, kWindow,
                    /*library=*/true);
  peek_.LayoutChanged();
  // Back: the hold ends, the panel narrows.
  peek_.SetChildren(0, false);
  peek_.HoldReleased();
  window_.SetChrome(/*hidden=*/true, /*focus=*/false);
  peek_.LayoutChanged();
  peek_.CheckPointer();
  ASSERT_TRUE(peek_.is_open());
  EXPECT_EQ(window_.shown->width(), kSidebarWidth + kPeekBleed);
  EXPECT_TRUE(peek_.hovered());

  peek_.PeekLeaveWithPointerOn();
  peek_.CloseOnBlur();
  EXPECT_TRUE(peek_.is_open());
  EXPECT_TRUE(peek_.hovered());
  EXPECT_FALSE(peek_.grace_running_for_testing());
  // Well past the leave check's usual window it still watches, and the peek stays.
  FastForward(kPeekLeaveCheckWindow * 4);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_TRUE(peek_.leave_check_running_for_testing());
  EXPECT_TRUE(window_.slides.empty());
  // The pointer goes: within a check it counts as left, then the grace and the slide.
  window_.pointer_on_peek = false;
  FastForward(kPeekLeaveCheck);
  EXPECT_FALSE(peek_.hovered());
  EXPECT_TRUE(peek_.grace_running_for_testing());
  FastForward(kPeekGrace + kPeekSlideOutHold + base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());

  // A blur long after the release closes it, pointer or not.
  window_.pointer_on_peek = true;
  peek_.FramePointerAt(gfx::PointF(300, 300), false);
  OpenByDwell();
  peek_.SetChildren(1, false);
  peek_.SetChildren(0, false);
  peek_.HoldReleased();
  FastForward(kPeekReleaseSettle);
  peek_.CloseOnBlur();
  EXPECT_FALSE(peek_.is_open());

  // A new peek starts with no release: a blur closes it at once.
  peek_.FramePointerAt(gfx::PointF(300, 300), false);
  OpenByDwell();
  peek_.CloseOnBlur();
  EXPECT_FALSE(peek_.is_open());
  // Closed, neither does anything.
  peek_.PeekLeaveWithPointerOn();
  peek_.HoldReleased();
  EXPECT_FALSE(peek_.is_open());
}

// The blur right after a release is let pass, then looked at again once the release settles. The hand-back
// has made the window active by then and the peek stays; a window still inactive (Cmd+Tab right after Escape, the
// widget's last point still on the panel) lost focus for real, and the peek closes then rather than when the pointer
// leaves that screen area.
TEST_F(CluSidebarPeekTest, ABlurLetPassIsCheckedAgainOnceTheReleaseSettles) {
  OpenByDwell();
  peek_.SetChildren(1, false);
  peek_.SetChildren(0, false);
  peek_.HoldReleased();
  window_.window_active = false;
  peek_.CloseOnBlur();
  ASSERT_TRUE(peek_.is_open());
  EXPECT_TRUE(peek_.blur_check_running_for_testing());
  window_.window_active = true;  // the hand-back activates the window again
  FastForward(kPeekReleaseSettle);
  EXPECT_TRUE(peek_.is_open());
  EXPECT_FALSE(peek_.blur_check_running_for_testing());
  EXPECT_EQ(window_.hides, 0);

  // Escape, then Cmd+Tab: the window stays inactive with the pointer on the panel.
  peek_.SetChildren(1, false);
  peek_.SetChildren(0, false);
  peek_.HoldReleased();
  window_.window_active = false;
  peek_.CloseOnBlur();
  ASSERT_TRUE(peek_.is_open());
  FastForward(kPeekReleaseSettle - base::Milliseconds(1));
  EXPECT_TRUE(peek_.is_open());
  FastForward(base::Milliseconds(1));
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.hides, 1);

  // A peek that closed before the check runs is left alone, and a new one starts with no check.
  window_.window_active = true;
  peek_.FramePointerAt(gfx::PointF(300, 300), false);
  OpenByDwell();
  peek_.SetChildren(1, false);
  peek_.SetChildren(0, false);
  peek_.HoldReleased();
  window_.window_active = false;
  peek_.CloseOnBlur();
  peek_.Close();
  EXPECT_FALSE(peek_.blur_check_running_for_testing());
  window_.window_active = true;
  peek_.FramePointerAt(gfx::PointF(300, 300), false);
  OpenByDwell();
  EXPECT_FALSE(peek_.blur_check_running_for_testing());
  FastForward(kPeekReleaseSettle);
  EXPECT_TRUE(peek_.is_open());
}

// Escape in the Library with the pointer on the page: the release starts the grace, and the blur of the keyboard's
// hand-back right after it keeps the peek, so the grace slides it out rather than the blur taking it in one frame.
TEST_F(CluSidebarPeekTest, TheHandBackBlurWithThePointerOffKeepsTheSlideOut) {
  OpenByDwell();
  peek_.SetChildren(1, false);  // held by the Library
  window_.pointer_on_peek = false;
  peek_.SetChildren(0, false);
  peek_.HoldReleased();
  ASSERT_TRUE(peek_.grace_running_for_testing());
  window_.window_active = false;
  peek_.CloseOnBlur();
  EXPECT_TRUE(peek_.is_open());
  EXPECT_FALSE(peek_.hovered());
  EXPECT_TRUE(peek_.grace_running_for_testing());
  EXPECT_TRUE(peek_.blur_check_running_for_testing());
  EXPECT_EQ(window_.hides, 0);
  window_.window_active = true;  // the hand-back activates the window again
  FastForward(kPeekGrace);
  EXPECT_TRUE(peek_.leaving());
  EXPECT_FALSE(window_.slides.empty());
  EXPECT_TRUE(peek_.is_open());
  FastForward(kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.hides, 1);
}

TEST_F(CluSidebarPeekTest, ALayoutChangeWithoutAZoneCancelsTheDwell) {
  IntoHotZone();
  window_.SetChrome(/*hidden=*/false, /*focus=*/false);
  peek_.LayoutChanged();
  FastForward(kPeekDwell * 2);
  EXPECT_FALSE(peek_.is_open());
}

TEST_F(CluSidebarPeekTest, CloseIsIdempotentAndStopsEveryTimer) {
  peek_.Close();
  EXPECT_EQ(window_.hides, 0);
  OpenByDwell();
  peek_.PeekPointerLeft();
  peek_.Close();
  peek_.Close();
  EXPECT_EQ(window_.hides, 1);
  EXPECT_FALSE(peek_.grace_running_for_testing());
  EXPECT_FALSE(peek_.guard_running_for_testing());
  FastForward(kPeekGuard * 2);
  EXPECT_EQ(window_.hides, 1);
  EXPECT_EQ(window_.pointer_asks, 0);
}

// What the mouse taps and the bridge can hand it while nothing is open, or out of range.
TEST_F(CluSidebarPeekTest, JunkInputDoesNothingHarmful) {
  peek_.Alive();
  peek_.PeekPointerAt(50);
  peek_.PeekPointerLeft();
  peek_.SetChildren(-3, true);
  peek_.SetChildren(std::numeric_limits<int>::max(), false);
  peek_.SetChildren(0, true);
  EXPECT_FALSE(peek_.is_open());
  EXPECT_EQ(window_.shows, 0);

  const float junk[] = {std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(), -1e9f, 1e9f};
  for (float v : junk) {
    peek_.FramePointerAt(gfx::PointF(v, 10), false);
    peek_.FramePointerAt(gfx::PointF(2, v), false);
  }
  FastForward(kPeekDwell * 2);
  EXPECT_FALSE(peek_.is_open());

  OpenByDwell();
  peek_.SetChildren(-1, false);
  EXPECT_EQ(peek_.children(), 0);
  for (float v : junk) {
    peek_.PeekPointerAt(v);
  }
  // NaN and the infinities count as outside, so the grace closes it.
  FastForward(kPeekGrace + kPeekSlideOutHold);
  EXPECT_FALSE(peek_.is_open());
}

}  // namespace
}  // namespace clu
