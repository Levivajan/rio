// Copyright 2026 The Rio Authors. All rights reserved.

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_webui.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluSidebar* checks of docs/PLAN.md 12.3 that M2 to M8 left (docs/chromium/sidebar-pip.md): the hover peek
// against the real frame and overlay WebUIs, its mouse taps on the real widgets' input path, and the sidebar tween
// with reduced motion. Real pointer and key input is in clu_interactive_uitest.mm.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;
using Type = blink::WebInputEvent::Type;

constexpr int kPeekSlot = CluWindowSidebarPeek::kPeekSlot;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

class CluSidebarBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    ASSERT_TRUE(content::WaitForLoadStop(peek_contents()));
    // The tests simulate the pointer; the system pointer is wherever the host left it, so the peek's leave check
    // takes the simulated pointer's word. CluSidebarPeekRetractStartsWithNoLeaveEvent moves it off.
    peek().set_pointer_on_peek_for_testing(true);
    browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1100, 760));
    ASSERT_TRUE(base::test::RunUntil([&] {
      views::test::RunScheduledLayout(&browser_view());
      return !shell().layout().content_size.IsEmpty();
    }));
  }

  void TearDownOnMainThread() override {
    gfx::Animation::SetPrefersReducedMotionForTesting(false);
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluWindowSidebarPeek& peek() { return window().sidebar_peek(); }
  BrowserView& browser_view() {
    return *BrowserView::GetBrowserViewForBrowser(browser());
  }
  content::WebContents* peek_contents() {
    return shell().passive_contents(kPeekSlot);
  }

  void HideSidebar() {
    if (!shell().chrome_state().sidebar_hidden) {
      shell().ToggleSidebar();
    }
    ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
    ASSERT_TRUE(shell().layout().hot_zone);
  }

  // Opens the peek the way the frame's hot zone does, through the shell handler.
  void OpenPeek() {
    CluShellHandler handler(window().GetWeakPtr());
    CallFuture done;
    handler.SetSidebarPeek(mojom::ShellSetSidebarPeekInput::New(true),
                           done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
    ASSERT_TRUE(peek().is_open());
  }

  const base::DictValue* WindowState() {
    state_ = window().CurrentState();
    return state_.FindDict("windows")->FindDict(window().window_id());
  }

  // The window's surfaces of kind `surface`, as published.
  const base::DictValue* Surface(const std::string& surface) {
    const base::DictValue* w = WindowState();
    for (const base::Value& s : *w->FindListByDottedPath("overlay.surfaces")) {
      if (*s.GetDict().FindString("surface") == surface) {
        return &s.GetDict();
      }
    }
    return nullptr;
  }

  base::DictValue state_;
};

// 12.3 third box: opens from the left gutter after kPeekDwell, covers the sidebar width plus 16 px and never moves the
// page, closes on leave after the grace.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarHoverPeekOpensFromTheGutterAndClosesOnLeave) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  const gfx::Rect page = shell().layout().page_rect;
  ASSERT_EQ(page.x(), kCardInset);

  // On the page card's top margin, right of the 8 px gutter: no peek.
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(40, 3));
  // The reveal zone is x 0 to 5 (x 7 does not reveal).
  ASSERT_EQ(shell().layout().hot_zone->width(), kPeekZoneX + 1);
  // X 8 and x 7 start nothing.
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(8, 300));
  EXPECT_FALSE(peek().controller().dwell_running_for_testing());
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(7, 300));
  EXPECT_FALSE(peek().controller().dwell_running_for_testing());
  const base::TimeTicks start = base::TimeTicks::Now();
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(3, 300));
  // The mouse callback runs as the event is forwarded, so the x 7 check above saw its own event.
  EXPECT_TRUE(peek().controller().dwell_running_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  EXPECT_GE(base::TimeTicks::Now() - start, kPeekDwell);

  const ShellLayout& layout = shell().layout();
  const gfx::Rect expected(0, 0, layout.effective_width + kPeekBleed,
                           layout.content_size.height());
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot), expected);
  // The widget on screen: at the window's left edge, the sidebar width plus the bleed wide, never more.
  const gfx::Rect window_screen = frame->GetContainerBounds();
  const gfx::Rect peek_screen = peek_contents()->GetContainerBounds();
  EXPECT_EQ(peek_screen.x(), window_screen.x());
  EXPECT_EQ(peek_screen.width(), expected.width());
  EXPECT_LE(peek_screen.right(), window_screen.x() + expected.right());
  // The page never moves for a peek.
  EXPECT_EQ(shell().layout().page_rect, page);
  EXPECT_FALSE(shell().sidebar_anim());

  // State: shell.sidebar.peeking and the peek's surface in slot 3, which the overlay renders.
  EXPECT_EQ(WindowState()->FindBoolByDottedPath("shell.sidebar.peeking"), true);
  const base::DictValue* surface = Surface("sidebar-peek");
  ASSERT_TRUE(surface);
  EXPECT_EQ(*surface->FindString("layer"), "passive");
  EXPECT_EQ(surface->FindInt("slot"), kPeekSlot);
  EXPECT_EQ(surface->FindIntByDottedPath("rect.width"), expected.width());
  EXPECT_EQ(true, content::EvalJs(peek_contents(),
                                  content::JsReplace(
                                      kWaitForSelector,
                                      "[data-shell=\"sidebar-peek\"]")));
  // The frame hides its hot zone while the peek shows.
  EXPECT_EQ(true, content::EvalJs(frame, R"(
      new Promise(resolve => {
        const check = () => document.querySelector('[data-shell="hot-zone"]')
            ? setTimeout(check, 20) : resolve(true);
        check();
      }))"));

  // Over the peek's sidebar it stays; leaving starts the grace, and the slide, then it closes.
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(100, 300));
  EXPECT_TRUE(peek().controller().hovered());
  const base::TimeTicks left = base::TimeTicks::Now();
  content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave,
                              gfx::Point(400, 300));
  EXPECT_FALSE(peek().controller().hovered());
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_GE(base::TimeTicks::Now() - left, kPeekGrace);
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
  EXPECT_EQ(WindowState()->FindBoolByDottedPath("shell.sidebar.peeking"),
            false);
  EXPECT_FALSE(Surface("sidebar-peek"));
}

// The floating sidebar slides on frames the widget draws: a keyframe slide that starts as the peek mounts, while the
// passive widget is still hidden, runs out before the widget draws. Every frame the widget draws records
// the panel's right edge, from the first one: it starts off the left edge, passes through the spring with its
// overshoot, and on leaving slides back out over frames before the peek closes.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest, CluSidebarPeekSlidesInAndOut) {
  HideSidebar();
  // A hidden page runs no animation frames, so the samples begin with the first frame the shown widget draws.
  ASSERT_EQ(true, content::EvalJs(peek_contents(), R"(
      window.cluB64 = [];
      const tick = () => {
        const el = document.querySelector('[data-shell="sidebar-peek"]');
        if (el) {
          window.cluB64.push([el.getBoundingClientRect().right,
                              el.dataset.motion]);
        }
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
      true)"));
  OpenPeek();
  // At rest, its layout box: the transform aside, the right edge is its 5 inset plus the sidebar width.
  const content::EvalJsResult rest_result =
      content::EvalJs(peek_contents(), R"(
      new Promise(resolve => {
        const check = () => {
          const last = window.cluB64.at(-1);
          const el = document.querySelector('[data-shell="sidebar-peek"]');
          last && last[1] === 'shown' && window.cluB64.length > 30
              ? resolve(el.offsetLeft + el.offsetWidth) : setTimeout(check, 20);
        };
        check();
      }))");
  const double rest = rest_result.ExtractDouble();
  EXPECT_NEAR(rest, shell().layout().effective_width + 5, 1);
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(100, 300));
  content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave,
                              gfx::Point(400, 300));
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));

  const content::EvalJsResult result =
      content::EvalJs(peek_contents(), "window.cluB64");
  const base::ListValue& samples = result.ExtractList();
  ASSERT_GT(samples.size(), 30u);
  std::vector<double> in;
  std::vector<double> out;
  for (const base::Value& sample : samples) {
    const base::ListValue& pair = sample.GetList();
    (pair[1].is_string() && pair[1].GetString() == "leaving" ? out : in)
        .push_back(pair[0].GetDouble());
  }
  ASSERT_FALSE(in.empty());
  // First drawn frame: off the window's left edge.
  EXPECT_LT(in.front(), 0);
  int sliding_in = 0;
  double furthest = in.front();
  for (double right : in) {
    sliding_in += right > 0 && right < rest - 1;
    furthest = std::max(furthest, right);
  }
  EXPECT_GE(sliding_in, 3);
  // The spring's overshoot (3.5 at its peak), then it settles in place.
  EXPECT_GT(furthest, rest + 1);
  EXPECT_NEAR(in.back(), rest, 0.5);
  // The ease-in out: frames between rest and off the edge before the widget hides.
  int sliding_out = 0;
  for (double right : out) {
    sliding_out += right > 0 && right < rest - 1;
  }
  EXPECT_GE(sliding_out, 2);
}

// The retract runs on the browser's clock. 165 ms after the pointer leaves, the browser marks the peek leaving
// and the overlay slides it out at once, accelerating (ease-in); the widget stays until that slide has drawn its last
// frame off the edge, so it is never hidden mid-slide.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekRetractStartsOnTheGraceAndEndsOnScreen) {
  HideSidebar();
  ASSERT_EQ(true, content::EvalJs(peek_contents(), R"(
      window.cluB64 = [];
      const tick = () => {
        const el = document.querySelector('[data-shell="sidebar-peek"]');
        if (el) {
          window.cluB64.push([performance.now(), el.getBoundingClientRect().right,
                              el.dataset.motion]);
        }
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
      true)"));
  OpenPeek();
  ASSERT_EQ(true, content::EvalJs(peek_contents(), R"(
      new Promise(resolve => {
        const check = () => {
          const last = window.cluB64.at(-1);
          last && last[2] === 'shown' && window.cluB64.length > 30
              ? resolve(true) : setTimeout(check, 20);
        };
        check();
      }))"));
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(100, 300));
  const double left_at =
      content::EvalJs(peek_contents(), "performance.now()").ExtractDouble();
  const base::TimeTicks left = base::TimeTicks::Now();
  content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave,
                              gfx::Point(400, 300));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return peek().controller().leaving(); }));
  EXPECT_GE(base::TimeTicks::Now() - left, kPeekGrace);
  const base::DictValue* surface = Surface("sidebar-peek");
  ASSERT_TRUE(surface);
  EXPECT_EQ(surface->FindBoolByDottedPath("props.leaving"), true);
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_LE(base::TimeTicks::Now() - left, kPeekGrace + kPeekSlideOutHold +
                                               base::Milliseconds(100));

  const content::EvalJsResult result =
      content::EvalJs(peek_contents(), "window.cluB64");
  std::vector<std::pair<double, double>> out;
  for (const base::Value& sample : result.ExtractList()) {
    const base::ListValue& row = sample.GetList();
    if (row[2].is_string() && row[2].GetString() == "leaving") {
      out.emplace_back(row[0].GetDouble(), row[1].GetDouble());
    }
  }
  ASSERT_GE(out.size(), 4u);
  // The slide starts about 165 ms after the pointer left, never a frame or two later.
  EXPECT_GE(out.front().first - left_at, kPeekGrace.InMillisecondsF() - 1);
  EXPECT_LE(out.front().first - left_at, kPeekGrace.InMillisecondsF() + 50);
  // Ease-in: the last step is bigger than the first.
  const double first_step = out[0].second - out[1].second;
  const double last_step =
      out[out.size() - 2].second - out[out.size() - 1].second;
  EXPECT_GT(last_step, first_step);
  // Its last drawn frame is off the window's edge: the widget hid after the slide finished.
  EXPECT_LE(out.back().second, 0.5);
}

// The retract counts from the system pointer leaving the panel, not from the leave event AppKit hands the peek
// 100 ms and more late: with no leave event at all it starts about 165 ms after the pointer went.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekRetractStartsWithNoLeaveEvent) {
  HideSidebar();
  OpenPeek();
  peek().set_pointer_on_peek_for_testing(true);
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(100, 300));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().controller().hovered(); }));
  peek().set_pointer_on_peek_for_testing(false);
  const base::TimeTicks left = base::TimeTicks::Now();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return peek().controller().leaving(); }));
  EXPECT_GE(base::TimeTicks::Now() - left, kPeekGrace);
  EXPECT_LE(base::TimeTicks::Now() - left,
            kPeekGrace + kPeekLeaveCheck + base::Milliseconds(60));
  const base::DictValue* surface = Surface("sidebar-peek");
  ASSERT_TRUE(surface);
  EXPECT_EQ(surface->FindBoolByDottedPath("props.leaving"), true);
}

// A pointer flung left that crosses the zone between two samples reveals, through the frame's mouse tap; a held
// button at the edge (a tab, a text selection or a window resize dragged there) never does.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarRevealCatchesAFastPointerButNotAHeldButton) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  content::RenderWidgetHost* host =
      frame->GetPrimaryMainFrame()->GetRenderWidgetHost();
  const auto held_move = [&](int x) {
    blink::WebMouseEvent event(Type::kMouseMove,
                               blink::WebInputEvent::kLeftButtonDown,
                               base::TimeTicks::Now());
    event.button = blink::WebMouseEvent::Button::kLeft;
    event.SetPositionInWidget(x, 300);
    host->ForwardMouseEvent(event);
  };
  held_move(300);
  held_move(9);
  held_move(2);
  EXPECT_FALSE(peek().controller().dwell_running_for_testing());
  content::SimulateMouseEvent(frame, Type::kMouseDown,
                              blink::WebMouseEvent::Button::kLeft,
                              gfx::Point(2, 300));
  EXPECT_FALSE(peek().controller().dwell_running_for_testing());
  EXPECT_FALSE(peek().is_open());

  // Released and away, then flung from x 300 to x 9: x 9 is outside the zone, but the pointer is on its way into it.
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
  EXPECT_FALSE(peek().controller().dwell_running_for_testing());
  const base::TimeTicks start = base::TimeTicks::Now();
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(9, 300));
  EXPECT_TRUE(peek().controller().dwell_running_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  EXPECT_GE(base::TimeTicks::Now() - start, kPeekDwell);
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
}

// With no page open, hiding the sidebar snaps (no tween) and the sidebar floats at once, pinned: the overlay
// draws it with no slide, and leaving it, a close request or a window blur do not retract it.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarHiddenWithNoPageFloatsAndStays) {
  while (!window().ActiveRowId().empty()) {
    const std::string active = window().ActiveRowId();
    ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return window().ActiveRowId() != active; }));
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().page_shown(); }));
  ASSERT_FALSE(shell().chrome_state().sidebar_hidden);
  ASSERT_FALSE(peek().is_open());

  shell().ToggleSidebar();
  EXPECT_FALSE(shell().sidebar_anim());
  ASSERT_TRUE(shell().layout().hot_zone);
  ASSERT_TRUE(peek().is_open());
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  const base::DictValue* surface = Surface("sidebar-peek");
  ASSERT_TRUE(surface);
  EXPECT_EQ(surface->FindBoolByDottedPath("props.pinned"), true);
  EXPECT_EQ(true, content::EvalJs(peek_contents(),
                                  content::JsReplace(
                                      kWaitForSelector,
                                      "[data-shell=\"sidebar-peek\"][data-pinned=\"true\"]")));

  // The pointer moving on it starts no leave check: it never retracts, so nothing asks where the pointer is.
  {
    blink::WebMouseEvent move(blink::WebInputEvent::Type::kMouseMove,
                              blink::WebInputEvent::kNoModifiers,
                              base::TimeTicks::Now());
    move.SetPositionInWidget(40, 300);
    peek().OnPeekMouseEvent(move);
  }
  EXPECT_FALSE(peek().controller().leave_check_running_for_testing());

  // The pointer leaves, the overlay asks to close, the window blurs: it stays.
  peek().OnPeekMouseEvent(blink::WebMouseEvent(
      blink::WebInputEvent::Type::kMouseLeave,
      blink::WebInputEvent::kNoModifiers, base::TimeTicks::Now()));
  peek().SetOpen(false);
  peek().Close();
  EXPECT_TRUE(peek().is_open());
  EXPECT_FALSE(peek().controller().grace_running_for_testing());

  // Cmd+S again docks the sidebar and the floating one goes.
  shell().ToggleSidebar();
  EXPECT_FALSE(peek().is_open());
}

// The floating sidebar's own window buttons. Minimize and the green button's full screen act on the calling
// window (the native buttons sit under the peek's overlay while it floats).
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekWindowButtonsMinimizeAndGoFullScreen) {
  CluWindowHandler handler(window().GetWeakPtr());
  {
    ui_test_utils::FullscreenWaiter waiter(browser(),
                                           {.browser_fullscreen = true});
    CallFuture done;
    handler.ToggleFullscreen(done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
    waiter.Wait();
  }
  {
    ui_test_utils::FullscreenWaiter waiter(
        browser(), ui_test_utils::FullscreenWaiter::kNoFullscreen);
    CallFuture done;
    handler.ToggleFullscreen(done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
    waiter.Wait();
  }
  CallFuture done;
  handler.Minimize(done.GetCallback());
  ASSERT_TRUE(IsOk(done.Take()));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return browser()->GetWindow()->IsMinimized(); }));
}

// The bleed is shadow only: the pointer there has left the sidebar.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarHoverPeekTheBleedCountsAsLeaving) {
  HideSidebar();
  OpenPeek();
  const int width = shell().layout().effective_width;
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(width - 2, 200));
  EXPECT_TRUE(peek().controller().hovered());
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(width + 4, 200));
  EXPECT_FALSE(peek().controller().hovered());
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
}

// Cmd+S pins the sidebar, and any surface that opens after the peek (the command bar) closes it. A toast never does.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarHoverPeekClosesOnCmdSAndOtherSurfaces) {
  HideSidebar();
  OpenPeek();
  const int toggle = FindCommand("sidebar.toggle")->idc;
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toggle));
  EXPECT_FALSE(peek().is_open());
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);

  HideSidebar();
  OpenPeek();
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  ASSERT_TRUE(window().command_session());
  window().Flush();
  EXPECT_FALSE(peek().is_open());
  // Under the bar no peek opens.
  CluShellHandler handler(window().GetWeakPtr());
  CallFuture refused;
  handler.SetSidebarPeek(mojom::ShellSetSidebarPeekInput::New(true),
                         refused.GetCallback());
  ASSERT_TRUE(IsOk(refused.Take()));
  EXPECT_FALSE(peek().is_open());
  window().CloseCommandBar();

  OpenPeek();
  window().ShowToast("Copied", std::nullopt, base::Seconds(5));
  window().Flush();
  EXPECT_TRUE(peek().is_open());
  window().ShowToast("Copied again", std::nullopt, base::Seconds(5));
  window().Flush();
  EXPECT_TRUE(peek().is_open());

  // Focus mode moves the hot zone: the peek goes.
  shell().ToggleFocusMode();
  EXPECT_FALSE(peek().is_open());
}

// Its own hover card and row menu hold the peek open; it closes after the last one goes.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarHoverPeekStaysOpenForItsHoverCardAndMenu) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(AddTabAtIndex(0, embedded_test_server()->GetURL("/title1.html"),
                            ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !window().ActiveRowId().empty(); }));
  const std::string row = window().ActiveRowId();
  HideSidebar();
  OpenPeek();
  // The frame sends a zero-size anchor at the pointer.
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  ASSERT_TRUE(shell().IsPassiveVisible(2));
  const base::DictValue* card = Surface("tab-hover");
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindString("openedFrom"), peek().instance_id());
  // The card is the title pill hung 11 right of and 12 below the pointer (less its 8 pt shadow margin),
  // over the peek's sidebar.
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.x").value_or(0), 60 + 11 - 8);
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.y").value_or(0), 135 + 12 - 8);

  // The pointer leaves the peek for the card's side: the card holds it well past the grace.
  content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave,
                              gfx::Point(500, 130));
  const base::TimeTicks held = base::TimeTicks::Now();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return base::TimeTicks::Now() - held > kPeekGrace + kPeekSlideOut * 3;
  }));
  EXPECT_TRUE(peek().is_open());
  window().Hover(std::nullopt, std::nullopt);
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));

  // The native row menu: open while it runs, then counted as left (it took the pointer).
  OpenPeek();
  peek().MenuOpened();
  content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave,
                              gfx::Point(500, 130));
  const base::TimeTicks menu = base::TimeTicks::Now();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return base::TimeTicks::Now() - menu > kPeekGrace + kPeekSlideOut * 3;
  }));
  EXPECT_TRUE(peek().is_open());
  peek().MenuClosed();
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));

  // A peek that closes with its card up takes the card along.
  OpenPeek();
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  ASSERT_TRUE(shell().IsPassiveVisible(2));
  peek().Close();
  EXPECT_FALSE(shell().IsPassiveVisible(2));
}

// What renderers and the input path can send that makes no sense: nothing opens, nothing crashes.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarHoverPeekJunkInputIsHarmless) {
  CluShellHandler handler(window().GetWeakPtr());
  // The sidebar shows: nothing to peek.
  CallFuture shown;
  handler.SetSidebarPeek(mojom::ShellSetSidebarPeekInput::New(true),
                         shown.GetCallback());
  ASSERT_TRUE(IsOk(shown.Take()));
  EXPECT_FALSE(peek().is_open());
  // Alive and close with nothing open.
  CallFuture alive;
  handler.PointerAlive(alive.GetCallback());
  ASSERT_TRUE(IsOk(alive.Take()));
  CallFuture closed;
  handler.SetSidebarPeek(mojom::ShellSetSidebarPeekInput::New(false),
                         closed.GetCallback());
  ASSERT_TRUE(IsOk(closed.Take()));
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));

  // Pointer events on a hidden peek, and far outside any widget.
  HideSidebar();
  for (const gfx::Point& p : {gfx::Point(-5000, -5000), gfx::Point(2, 99999),
                              gfx::Point(99999, 10)}) {
    content::SimulateMouseEvent(peek_contents(), Type::kMouseMove, p);
    content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave, p);
    content::SimulateMouseEvent(shell().frame_contents(), Type::kMouseMove, p);
  }
  content::SimulateMouseEvent(shell().frame_contents(), Type::kMouseLeave,
                              gfx::Point(2, 300));
  EXPECT_FALSE(peek().controller().dwell_running_for_testing());
  EXPECT_FALSE(peek().is_open());

  // Repeated opens show one peek; a close of a closed peek is nothing.
  OpenPeek();
  const std::string id = peek().instance_id();
  OpenPeek();
  EXPECT_EQ(peek().instance_id(), id);
  peek().Close();
  peek().Close();
  EXPECT_FALSE(peek().is_open());

  // The passive pool has four slots: ?slot=3 is the peek's, anything past it or junk is no slot.
  EXPECT_EQ(OverlaySlotFromUrl(GURL("chrome://clu-overlay/?layer=passive&slot=3")),
            3);
  for (const char* url : {"chrome://clu-overlay/?layer=passive&slot=4",
                          "chrome://clu-overlay/?layer=passive&slot=-1",
                          "chrome://clu-overlay/?layer=passive&slot=3x"}) {
    EXPECT_FALSE(OverlaySlotFromUrl(GURL(url))) << url;
  }
}

// The page card's slide steps on the window compositor's frames, at the display's rate (120 Hz on
// ProMotion), never on a 16 ms timer; every step draws the card where (now - start) / ms puts it, and the slide lands
// exactly on the target.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarTweenStepsOnTheCompositorFrames) {
  const int toggle = FindCommand("sidebar.toggle")->idc;
  const gfx::Rect docked = shell().shown_page_rect();
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toggle));
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_TRUE(shell().tween_on_compositor_for_testing());
  std::vector<int> xs;
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (shell().sidebar_anim() &&
        (xs.empty() || xs.back() != shell().shown_page_rect().x())) {
      xs.push_back(shell().shown_page_rect().x());
    }
    return !shell().sidebar_anim();
  }));
  EXPECT_FALSE(shell().tween_on_compositor_for_testing());
  EXPECT_GE(shell().tween_steps_for_testing(), 2);
  // The card only ever moves toward the target, from the docked edge to kCardInset.
  for (size_t i = 0; i + 1 < xs.size(); ++i) {
    EXPECT_GE(xs[i], xs[i + 1]) << i;
  }
  ASSERT_FALSE(xs.empty());
  EXPECT_LE(xs.front(), docked.x());
  EXPECT_EQ(shell().shown_page_rect().x(), kCardInset);
  views::test::RunScheduledLayout(&browser_view());
  EXPECT_EQ(browser_view().multi_contents_view()->x(), kCardInset);
}

// 12.3 second box: Cmd+S hides the sidebar over kSidebarHideMs and docks it back over
// kSidebarShowMs; with reduced motion both snap (0 ms).
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarToggleTweensAndSnapsWithReducedMotion) {
  const int toggle = FindCommand("sidebar.toggle")->idc;
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toggle));
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_EQ(shell().sidebar_anim()->ms, kSidebarHideMs);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  EXPECT_EQ(shell().shown_page_rect().x(), kCardInset);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toggle));
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_EQ(shell().sidebar_anim()->ms, kSidebarShowMs);
  EXPECT_EQ(shell().sidebar_anim()->to, shell().layout().effective_width);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));

  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toggle));
  EXPECT_FALSE(shell().sidebar_anim());
  EXPECT_EQ(shell().shown_page_rect().x(), kCardInset);
  views::test::RunScheduledLayout(&browser_view());
  EXPECT_EQ(browser_view().multi_contents_view()->x(), kCardInset);
  const base::DictValue* w = WindowState();
  EXPECT_TRUE(w->FindByDottedPath("shell.sidebar.anim")->is_none());
  EXPECT_EQ(window().CurrentState().FindBoolByDottedPath(
                "app.appearance.reduceMotion"),
            true);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toggle));
  EXPECT_FALSE(shell().sidebar_anim());
  EXPECT_EQ(shell().shown_page_rect().x(), shell().layout().effective_width);
}


// The floating sidebar opens the Library as the docked one does. The open peek widens to it in place (the
// same surface), shows it, and stays held with the keyboard while it is open even with the pointer off it; released,
// it narrows back and retracts after the grace. A peek that goes while the sidebar stays hidden takes the Library along.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekOpensTheLibraryAndStaysHeld) {
  HideSidebar();
  OpenPeek();
  const std::string instance = peek().instance_id();
  const int narrow = shell().layout().sidebar_rect.width();
  EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
            narrow + kPeekBleed);

  CluArchiveHandler archive(window().GetWeakPtr());
  {
    CallFuture done;
    archive.Open(done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
  }
  ASSERT_TRUE(peek().is_open());
  EXPECT_EQ(peek().instance_id(), instance);
  const int wide = shell().layout().sidebar_rect.width();
  EXPECT_GE(wide, kLibraryWidth);
  EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
            wide + kPeekBleed);
  // The card keeps the whole window: the Library floats over it.
  EXPECT_EQ(shell().layout().page_rect.x(), kCardInset);
  EXPECT_EQ(true, content::EvalJs(
                      peek_contents(),
                      content::JsReplace(kWaitForSelector,
                                         "[data-shell=\"sidebar-peek\"] "
                                         ".clu-sidebar-library")));

  // The overlay holds it while the Library is open: the pointer leaving starts no grace, a blur does not close it.
  CluShellHandler shell_handler(window().GetWeakPtr());
  const auto hold = [&](bool held) {
    CallFuture done;
    shell_handler.HoldSidebarPeek(mojom::ShellHoldSidebarPeekInput::New(held),
                                  done.GetCallback());
    return IsOk(done.Take());
  };
  ASSERT_TRUE(hold(true));
  EXPECT_TRUE(peek().held());
  EXPECT_GE(peek().controller().children(), 1);
  {
    // Out past the Library's right edge: a leave at a point on the panel would be the widget losing the pointer.
    blink::WebMouseEvent leave(Type::kMouseLeave,
                               blink::WebInputEvent::kNoModifiers,
                               base::TimeTicks::Now());
    leave.SetPositionInWidget(shell().layout().sidebar_rect.width() + 200, 300);
    peek().OnPeekMouseEvent(leave);
  }
  EXPECT_FALSE(peek().controller().hovered());
  EXPECT_FALSE(peek().controller().grace_running_for_testing());
  EXPECT_TRUE(peek().is_open());

  // The Library closes with the pointer off it: the hold ends with it, the peek narrows back, then retracts. The
  // overlay's own release that follows changes nothing.
  peek().set_pointer_on_peek_for_testing(false);
  {
    CallFuture done;
    archive.Close(done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
  }
  ASSERT_TRUE(peek().is_open());
  EXPECT_FALSE(peek().held());
  EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
            narrow + kPeekBleed);
  EXPECT_TRUE(peek().controller().grace_running_for_testing());
  ASSERT_TRUE(hold(false));
  EXPECT_FALSE(peek().held());
  EXPECT_TRUE(peek().controller().grace_running_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));

  peek().set_pointer_on_peek_for_testing(true);
  // A hold with no peek open is ignored; a peek closed under the Library closes the Library with it.
  ASSERT_TRUE(hold(true));
  EXPECT_FALSE(peek().held());
  OpenPeek();
  {
    CallFuture done;
    archive.Open(done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
  }
  ASSERT_TRUE(hold(true));
  peek().Close();
  EXPECT_FALSE(peek().is_open());
  EXPECT_FALSE(peek().held());
  EXPECT_FALSE(window().archive_panel());
  EXPECT_EQ(shell().layout().sidebar_rect.width(), narrow);
}

// Back from the Library opened in the floating sidebar left an empty panel at the edge, and the edge
// no longer revealed. Back (archive.close from the peek) ends the hold before the panel narrows and hands the keyboard
// back; the pointer moving off it retracts it and its widget goes; a new edge hover reveals it again. Escape (the
// overlay closes the Library the same way) and a click on the page while the Library is open leave it just as clean,
// and a peek widget left on screen with no peek never keeps the edge from the frame.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekBackFromLibraryRetractsAndRevealsAgain) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  CluArchiveHandler archive(window().GetWeakPtr());
  CluShellHandler shell_handler(window().GetWeakPtr());
  const auto hold = [&](bool held) {
    CallFuture done;
    shell_handler.HoldSidebarPeek(mojom::ShellHoldSidebarPeekInput::New(held),
                                  done.GetCallback());
    return IsOk(done.Take());
  };
  const auto close_library = [&] {
    CallFuture done;
    archive.Close(done.GetCallback());
    return IsOk(done.Take());
  };
  // Revealed from the edge, then the Library opened from it (the pop-up's View Library), held by the overlay.
  const auto reveal_with_library = [&] {
    peek().set_pointer_on_peek_for_testing(true);
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
    ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
    CallFuture done;
    archive.Open(done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
    ASSERT_EQ(true, content::EvalJs(
                        peek_contents(),
                        content::JsReplace(kWaitForSelector,
                                           "[data-shell=\"sidebar-peek\"] "
                                           ".clu-sidebar-library")));
    ASSERT_TRUE(hold(true));
    ASSERT_TRUE(peek().held());
    ASSERT_GE(shell().layout().sidebar_rect.width(), kLibraryWidth);
    ASSERT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
              shell().layout().sidebar_rect.width() + kPeekBleed);
  };
  const auto gone_and_reveals_again = [&] {
    ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
    EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
    EXPECT_FALSE(window().archive_panel());
    EXPECT_FALSE(peek().held());
    EXPECT_FALSE(Surface("sidebar-peek"));
    peek().set_pointer_on_peek_for_testing(true);
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
    ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
    EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
    EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
              shell().layout().effective_width + kPeekBleed);
    EXPECT_EQ(true, content::EvalJs(
                        peek_contents(),
                        content::JsReplace(kWaitForSelector,
                                           "[data-shell=\"sidebar-peek\"]")));
    peek().Close();
    ASSERT_FALSE(peek().is_open());
  };

  // Back, pressed with the pointer on the panel: the hold ends at once, the panel narrows to the tabs and stays.
  reveal_with_library();
  ASSERT_TRUE(close_library());
  ASSERT_TRUE(peek().is_open());
  EXPECT_FALSE(peek().held());
  EXPECT_EQ(peek().controller().children(), 0);
  EXPECT_TRUE(peek().controller().hovered());
  EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
            shell().layout().effective_width + kPeekBleed);
  ASSERT_TRUE(hold(false));
  // The pointer moves off it with no event reaching the peek: the leave check sees it, the grace runs, it slides out.
  peek().set_pointer_on_peek_for_testing(false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return peek().controller().leaving() || !peek().is_open(); }));
  gone_and_reveals_again();

  // Escape: the overlay closes the Library (archive.close) with the pointer off the panel. It retracts after the grace.
  reveal_with_library();
  peek().set_pointer_on_peek_for_testing(false);
  ASSERT_TRUE(close_library());
  EXPECT_FALSE(peek().held());
  EXPECT_TRUE(peek().controller().grace_running_for_testing());
  gone_and_reveals_again();

  // A click on the page: the window takes activation back from the held peek with the pointer off it. The Library
  // closes as Back closes it and the peek retracts after the grace; with the pointer on the panel it stays.
  reveal_with_library();
  peek().WindowActivated();
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(window().archive_panel());
  peek().set_pointer_on_peek_for_testing(false);
  peek().WindowActivated();
  gone_and_reveals_again();

  // A peek widget left on screen with no peek (what 1159 showed): the pointer on it reaches the frame's reveal, and
  // the widget goes.
  ASSERT_FALSE(peek().is_open());
  shell().ShowPassive(kPeekSlot, gfx::Rect(0, 0, 230, 400));
  ASSERT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  {
    blink::WebMouseEvent move(Type::kMouseMove,
                              blink::WebInputEvent::kNoModifiers,
                              base::TimeTicks::Now());
    move.SetPositionInWidget(300, 300);
    peek().OnPeekMouseEvent(move);
    move.SetPositionInWidget(2, 300);
    peek().OnPeekMouseEvent(move);
  }
  EXPECT_TRUE(peek().controller().dwell_running_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  peek().Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().IsPassiveVisible(kPeekSlot); }));
}

// Back from the Library opened in the floating sidebar, pressed on the panel, took the whole floating
// sidebar away in the first frame. Back returns it to the tabs in place: it stays, narrowed and released, while the
// pointer is on it (the panel's own events say so, the Back press among them), retracts after the grace once the
// pointer leaves, and a new edge hover reveals it.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekBackFromLibraryStaysUnderThePointer) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  const auto reveal = [&] {
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
    ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  };
  const auto wait = [](base::TimeDelta delay) {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), delay);
    loop.Run();
  };
  const std::string tabs =
      "[data-shell=\"sidebar-peek\"] nav.clu-sidebar:not(.clu-sidebar-library)";
  const std::string back =
      "[data-shell=\"sidebar-peek\"] [aria-label=\"Back to tabs\"]";

  // Revealed from the edge, the pointer on the panel; its Library button opens the Library in it, held.
  reveal();
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(40, 300));
  const int narrow = shell().layout().effective_width;
  // From here only the panel's own events tell where the pointer is.
  peek().set_pointer_on_peek_for_testing(std::nullopt);
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().archive_panel(); }));
  ASSERT_EQ(true, content::EvalJs(peek_contents(),
                                  content::JsReplace(kWaitForSelector, back)));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  ASSERT_GE(shell().layout().sidebar_rect.width(), kLibraryWidth);

  // The pointer rests on the Library's Back arrow, which sits over the tabs' panel, and presses it.
  const auto center = [&](const char* axis) {
    return content::EvalJs(
               peek_contents(),
               content::JsReplace(
                   "(() => { const r = document.querySelector($1)"
                   ".getBoundingClientRect(); return Math.round($2 === 'x' ? "
                   "r.left + r.width / 2 : r.top + r.height / 2); })()",
                   back, axis))
        .ExtractInt();
  };
  const int back_x = center("x");
  const int back_y = center("y");
  ASSERT_LT(back_x, narrow);
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(back_x, back_y));
  ASSERT_EQ(true,
            content::EvalJs(peek_contents(),
                            content::JsReplace(
                                "document.querySelector($1).click(), true", back)));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));

  // Still shown in place, released, at the tabs' width, with its spaces, rows and bottom bar; no grace runs.
  EXPECT_TRUE(peek().is_open());
  EXPECT_FALSE(peek().held());
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  EXPECT_EQ(shell().layout().effective_width, narrow);
  EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
            narrow + kPeekBleed);
  EXPECT_TRUE(peek().controller().hovered());
  EXPECT_FALSE(peek().controller().grace_running_for_testing());
  EXPECT_EQ(true, content::EvalJs(peek_contents(),
                                  content::JsReplace(kWaitForSelector, tabs)));
  // Past the grace and the slide, with the pointer still on it, it is still there.
  wait(kPeekGrace + kPeekSlideOutHold + base::Milliseconds(200));
  EXPECT_TRUE(peek().is_open());
  EXPECT_FALSE(peek().controller().leaving());
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  EXPECT_EQ(false, content::EvalJs(peek_contents(), R"(
    document.querySelector('[data-shell="sidebar-peek"]').dataset.leaving === 'true')"));

  // The pointer leaves it for the page: after the grace it slides out and its widget goes.
  content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave,
                              gfx::Point(500, 300));
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(500, 300));
  EXPECT_TRUE(peek().controller().grace_running_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
  EXPECT_FALSE(Surface("sidebar-peek"));

  // A new edge hover reveals it again, showing the tabs. The system pointer is wherever the host left it, so the
  // simulated one speaks for it before the panel has an event of its own.
  peek().set_pointer_on_peek_for_testing(true);
  reveal();
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
            narrow + kPeekBleed);
  EXPECT_EQ(true, content::EvalJs(peek_contents(),
                                  content::JsReplace(kWaitForSelector, tabs)));
  peek().Close();
  ASSERT_FALSE(peek().is_open());
}

// Back or Escape in the floating sidebar's Library, with the pointer still on it, hid the whole
// floating sidebar at once (3 of 3). As the panel narrows from the Library's width to the tabs' and hands the
// keyboard back to the window, its widget can lose the pointer and send a leave at the point where the pointer still
// is, and the window passes through a blur. Neither takes it: it keeps the tabs in place while the pointer is on it,
// the overlay starts no grace of its own, and it retracts only once the system pointer is off the panel.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekBackAndEscapeKeepItUnderThePointer) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  const auto wait = [](base::TimeDelta delay) {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), delay);
    loop.Run();
  };
  const std::string tabs =
      "[data-shell=\"sidebar-peek\"] nav.clu-sidebar:not(.clu-sidebar-library)";
  const std::string back =
      "[data-shell=\"sidebar-peek\"] [aria-label=\"Back to tabs\"]";
  int narrow = 0;
  gfx::Point at;
  // Revealed from the edge with the pointer on it, the Library opened in it and held, the pointer on its Back arrow
  // (over the tabs' panel too).
  const auto reveal_with_library = [&] {
    peek().set_pointer_on_peek_for_testing(true);
    // The window takes activation back right after the hand-back.
    peek().set_window_active_for_testing(true);
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
    ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
    content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                                gfx::Point(40, 300));
    narrow = shell().layout().effective_width;
    ASSERT_TRUE(window().RunCommand("archive.open"));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().archive_panel(); }));
    ASSERT_EQ(true, content::EvalJs(peek_contents(),
                                    content::JsReplace(kWaitForSelector, back)));
    ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
    ASSERT_GE(shell().layout().sidebar_rect.width(), kLibraryWidth);
    const auto center = [&](const char* axis) {
      return content::EvalJs(
                 peek_contents(),
                 content::JsReplace(
                     "(() => { const r = document.querySelector($1)"
                     ".getBoundingClientRect(); return Math.round($2 === 'x' ? "
                     "r.left + r.width / 2 : r.top + r.height / 2); })()",
                     back, axis))
          .ExtractInt();
    };
    at = gfx::Point(center("x"), center("y"));
    ASSERT_LT(at.x(), narrow);
    content::SimulateMouseEvent(peek_contents(), Type::kMouseMove, at);
  };
  // The Library closed under the pointer: the widget's leave at the pointer's own point and a blur come after it.
  const auto stays_under_the_pointer = [&] {
    ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));
    ASSERT_TRUE(base::test::RunUntil([&] { return !peek().held(); }));
    content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave, at);
    EXPECT_TRUE(peek().is_open());
    EXPECT_TRUE(peek().controller().hovered());
    EXPECT_FALSE(peek().controller().grace_running_for_testing());
    EXPECT_TRUE(peek().controller().leave_check_running_for_testing());
    peek().Close();  // the window blur on the way back from the peek's widget
    EXPECT_TRUE(peek().is_open());
    // Past the grace and the slide, still there at the tabs' width, the tabs drawn, not sliding out.
    wait(kPeekGrace + kPeekSlideOutHold + base::Milliseconds(300));
    EXPECT_TRUE(peek().is_open());
    EXPECT_FALSE(peek().controller().leaving());
    EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
    EXPECT_EQ(shell().passive_rect_for_testing(kPeekSlot).width(),
              narrow + kPeekBleed);
    EXPECT_EQ(true, content::EvalJs(peek_contents(),
                                    content::JsReplace(kWaitForSelector, tabs)));
    EXPECT_EQ(false, content::EvalJs(peek_contents(), R"(
      document.querySelector('[data-shell="sidebar-peek"]').dataset.leaving === 'true')"));
    // The pointer goes with no further event to the widget: the leave check sees the system pointer off the panel,
    // and it slides out after the grace.
    peek().set_pointer_on_peek_for_testing(false);
    const base::TimeTicks off = base::TimeTicks::Now();
    ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
    EXPECT_GE(base::TimeTicks::Now() - off, kPeekGrace);
    EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
    EXPECT_FALSE(Surface("sidebar-peek"));
  };

  // Back, clicked with the pointer on it.
  reveal_with_library();
  ASSERT_EQ(true,
            content::EvalJs(peek_contents(),
                            content::JsReplace(
                                "document.querySelector($1).click(), true", back)));
  stays_under_the_pointer();

  // Escape in the Library's empty search field, the pointer resting on the panel.
  reveal_with_library();
  content::SimulateKeyPress(peek_contents(), ui::DomKey::ESCAPE,
                            ui::DomCode::ESCAPE, ui::VKEY_ESCAPE,
                            /*control=*/false, /*shift=*/false, /*alt=*/false,
                            /*command=*/false);
  stays_under_the_pointer();

  // A real leave, at the edge it crossed, still retracts after the grace.
  peek().set_pointer_on_peek_for_testing(true);
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(40, 300));
  content::SimulateMouseEvent(peek_contents(), Type::kMouseLeave,
                              gfx::Point(narrow + kPeekBleed + 1, 300));
  EXPECT_FALSE(peek().controller().hovered());
  EXPECT_TRUE(peek().controller().grace_running_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
}

// Back or Escape in the floating sidebar's Library blurs the window inside the keyboard's hand-back,
// before the release is recorded, which must not take the whole floating sidebar at once. The blur that comes
// with the hand-back now keeps it. A window that is still inactive once the release settles lost focus for real
// (Cmd+Tab right after Escape), and the floating sidebar goes then instead of staying over the inactive window.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekHandBackBlurKeepsItAndARealBlurTakesIt) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  const std::string back =
      "[data-shell=\"sidebar-peek\"] [aria-label=\"Back to tabs\"]";
  const auto reveal_with_library = [&] {
    peek().set_pointer_on_peek_for_testing(true);
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
    content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
    ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
    content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                                gfx::Point(40, 300));
    ASSERT_TRUE(window().RunCommand("archive.open"));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().archive_panel(); }));
    ASSERT_EQ(true, content::EvalJs(peek_contents(),
                                    content::JsReplace(kWaitForSelector, back)));
    ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  };
  // The window's blur arrives during the hand-back, as it does on macOS.
  bool blurred = false;
  peek().set_keyboard_handed_back_for_testing(base::BindLambdaForTesting([&] {
    blurred = true;
    peek().Close();
  }));

  // Back, the window active again a few milliseconds later: it stays past the settle.
  reveal_with_library();
  peek().set_window_active_for_testing(true);
  ASSERT_EQ(true,
            content::EvalJs(peek_contents(),
                            content::JsReplace(
                                "document.querySelector($1).click(), true", back)));
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().held(); }));
  EXPECT_TRUE(blurred);
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(peek().controller().blur_check_running_for_testing());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !peek().controller().blur_check_running_for_testing(); }));
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  peek().SetOpen(false);
  ASSERT_FALSE(peek().is_open());

  // Escape, then Cmd+Tab within the settle: the window stays inactive and the pointer the panel last saw stays on it.
  blurred = false;
  reveal_with_library();
  peek().set_window_active_for_testing(false);
  content::SimulateKeyPress(peek_contents(), ui::DomKey::ESCAPE,
                            ui::DomCode::ESCAPE, ui::VKEY_ESCAPE,
                            /*control=*/false, /*shift=*/false, /*alt=*/false,
                            /*command=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().held(); }));
  EXPECT_TRUE(blurred);
  EXPECT_TRUE(peek().is_open());
  const base::TimeTicks released = base::TimeTicks::Now();
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_LT(base::TimeTicks::Now() - released, kPeekGuard);
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
  peek().set_keyboard_handed_back_for_testing(base::RepeatingClosure());
  peek().set_window_active_for_testing(std::nullopt);
}

// Escape in the floating sidebar's Library with the pointer on the page: the release starts the grace, and the blur that
// comes with the keyboard's hand-back keeps the floating sidebar, so the grace slides it out instead of the blur taking
// it in one frame.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekEscapeWithThePointerOffSlidesOut) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  content::SimulateMouseEvent(peek_contents(), Type::kMouseMove,
                              gfx::Point(40, 300));
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_EQ(true, content::EvalJs(
                      peek_contents(),
                      content::JsReplace(
                          kWaitForSelector,
                          "[data-shell=\"sidebar-peek\"] [aria-label=\"Back to tabs\"]")));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  bool blurred = false;
  peek().set_keyboard_handed_back_for_testing(base::BindLambdaForTesting([&] {
    blurred = true;
    peek().Close();
  }));
  peek().set_window_active_for_testing(true);
  // The pointer went onto the page while the Library held the floating sidebar.
  peek().set_pointer_on_peek_for_testing(false);
  content::SimulateKeyPress(peek_contents(), ui::DomKey::ESCAPE,
                            ui::DomCode::ESCAPE, ui::VKEY_ESCAPE,
                            /*control=*/false, /*shift=*/false, /*alt=*/false,
                            /*command=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().held(); }));
  EXPECT_TRUE(blurred);
  EXPECT_TRUE(peek().is_open());
  EXPECT_FALSE(peek().controller().hovered());
  EXPECT_TRUE(peek().controller().grace_running_for_testing() ||
              peek().controller().leaving());
  ASSERT_TRUE(
      base::test::RunUntil([&] { return peek().controller().leaving(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
  peek().set_keyboard_handed_back_for_testing(base::RepeatingClosure());
  peek().set_window_active_for_testing(std::nullopt);
  peek().set_pointer_on_peek_for_testing(true);
}

// The floating sidebar holding its Library has the keyboard in its own widget. The window still publishes as focused,
// a toast (the Library's Copy reports by one) keeps it open, and the app going inactive while it holds ends the hold
// and takes it, as a window blur takes a floating sidebar nothing holds.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekHeldLibraryKeepsAToastAndGoesWithTheApp) {
  HideSidebar();
  OpenPeek();
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().archive_panel(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  window().Flush();
  EXPECT_EQ(WindowState()->FindBool("focused"), true);

  window().ShowToast("Copied File", std::nullopt, base::Seconds(5));
  window().Flush();
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(peek().held());
  EXPECT_TRUE(window().archive_panel());

  // Another app takes activation from the floating sidebar's widget: once that settles it goes, Library and all.
  peek().set_window_active_for_testing(false);
  views::Widget* widget = shell().passive_widget(kPeekSlot);
  ASSERT_TRUE(widget);
  peek().OnWidgetActivationChanged(widget, /*active=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_FALSE(peek().held());
  EXPECT_FALSE(window().archive_panel());
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));

  // The hand-back of a release activates the window: a held peek whose window is active again stays.
  peek().set_window_active_for_testing(true);
  OpenPeek();
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  peek().OnWidgetActivationChanged(widget, /*active=*/false);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(peek().held());
  peek().set_window_active_for_testing(std::nullopt);
}

// A toast is never a surface for the floating sidebar: one that showed while its Library held it stays after Back with
// the pointer on the panel, and the sidebar narrows to the tabs and stays. A toast opening after the release keeps it
// too.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekToastFromTheHoldKeepsItAfterBack) {
  HideSidebar();
  OpenPeek();
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  window().ShowToast("Copied File", std::nullopt, base::Seconds(5));
  window().Flush();
  ASSERT_TRUE(Surface("toast"));
  // The Library's Back with the pointer on the panel.
  peek().set_pointer_on_peek_for_testing(true);
  window().SetArchivePanel(false);
  window().Flush();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(peek().held());
  EXPECT_FALSE(window().archive_panel());
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(Surface("toast"));
  // A new toast while nothing holds it.
  window().ShowToast("Copied Link", std::nullopt, base::Seconds(5));
  window().Flush();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(peek().is_open());
}

// Switching spaces from the floating sidebar in focus mode keeps it open on the new space, with no space toast over the
// page, since the floating sidebar already shows the space.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekSpaceSwitchInFocusModeKeepsIt) {
  ASSERT_TRUE(space_actions::CreateSpace(&window(), std::string("Two"),
                                         std::nullopt, std::nullopt,
                                         std::nullopt)
                  .has_value());
  const std::vector<std::string> order =
      CluSpacesService::Get()->list().Order();
  ASSERT_EQ(order.size(), 2u);
  ASSERT_TRUE(window().ShowSpace(order[0]));
  if (shell().chrome_state().sidebar_hidden) {
    shell().ToggleSidebar();
  }
  shell().ToggleFocusMode();
  ASSERT_TRUE(shell().chrome_state().focus_mode);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  ASSERT_TRUE(shell().layout().hot_zone);
  window().Flush();
  OpenPeek();
  ASSERT_TRUE(window().ShowSpace(order[1]));
  window().Flush();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(window().space_id(), order[1]);
  EXPECT_TRUE(peek().is_open());
  EXPECT_FALSE(Surface("toast"));
  // With the floating sidebar closed, the switch still names the space by toast.
  peek().controller().Close();
  ASSERT_FALSE(peek().is_open());
  ASSERT_TRUE(window().ShowSpace(order[0]));
  window().Flush();
  EXPECT_TRUE(Surface("toast"));
  shell().ToggleFocusMode();
}

// Reveal Tab in Sidebar from the floating sidebar's Library with the pointer off it (on the menu bar): the Library
// closes and the floating sidebar stays through the reveal's settle, then the usual grace takes it. The docked sidebar
// stays hidden.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekRevealHoldsItUntilItSettles) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(AddTabAtIndex(0, embedded_test_server()->GetURL("/title1.html"),
                            ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !window().ActiveRowId().empty(); }));
  HideSidebar();
  OpenPeek();
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  peek().set_pointer_on_peek_for_testing(false);
  peek().controller().PeekPointerLeft();
  ASSERT_TRUE(window().RunCommand("tabs.reveal"));
  EXPECT_TRUE(window().sidebar_request_for_testing()->floating);
  EXPECT_FALSE(window().archive_panel());
  EXPECT_FALSE(peek().held());
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(peek().controller().action_running_for_testing());
  EXPECT_FALSE(peek().controller().grace_running_for_testing());
  EXPECT_FALSE(peek().controller().leaving());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !peek().controller().action_running_for_testing(); }));
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(peek().controller().grace_running_for_testing() ||
              peek().controller().leaving());
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  peek().set_pointer_on_peek_for_testing(true);
}

// Tabs ▸ New Folder… from the floating sidebar's Library with the pointer off it: the browser holds the floating
// sidebar for the rename from the ask on, so it never retracts before the list mounts and the rename starts. The hold
// ends when the rename is cancelled, and the grace takes it.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekNewFolderHoldsItForTheRename) {
  HideSidebar();
  OpenPeek();
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  peek().set_pointer_on_peek_for_testing(false);
  peek().controller().PeekPointerLeft();
  ASSERT_TRUE(window().RunCommand("tabs.newFolder"));
  EXPECT_TRUE(window().sidebar_request_for_testing()->floating);
  EXPECT_FALSE(window().archive_panel());
  // Held from the ask, before the overlay has answered.
  EXPECT_TRUE(peek().held());
  EXPECT_FALSE(peek().controller().grace_running_for_testing());
  const std::string folder = window().sidebar_request_for_testing()->node_id;
  const std::string renaming_row = content::JsReplace(
      "(() => { const row = [...document.querySelectorAll("
      "`[data-folder-id=\"${CSS.escape($1)}\"]`)]"
      ".find((el) => !el.closest('.clu-sidebar-warm'));"
      " return row?.dataset.renaming === 'true'; })()",
      folder);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(peek_contents(), renaming_row).ExtractBool();
  }));
  // Well past the grace and the slide-out it is still there, held.
  base::RunLoop loop;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), (kPeekGrace + kPeekSlideOutHold) * 3);
  loop.Run();
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(peek().held());
  // Escape in the rename field cancels it: the hold ends and the grace takes the floating sidebar.
  content::SimulateKeyPress(peek_contents(), ui::DomKey::ESCAPE,
                            ui::DomCode::ESCAPE, ui::VKEY_ESCAPE,
                            /*control=*/false, /*shift=*/false, /*alt=*/false,
                            /*command=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().held(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  peek().set_pointer_on_peek_for_testing(true);
}

// Tabs ▸ New Folder… and Reveal Tab in Sidebar with the floating sidebar open over a hidden sidebar: the floating
// sidebar answers, so the sidebar stays hidden, the new folder's row enters rename in the floating sidebar, which takes
// the keyboard for it, and the docked list never shows the rename field.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekAnswersTheTabsMenuWhileOpen) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(AddTabAtIndex(0, embedded_test_server()->GetURL("/title1.html"),
                            ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !window().ActiveRowId().empty(); }));
  HideSidebar();
  OpenPeek();
  ASSERT_TRUE(window().RunCommand("tabs.newFolder"));
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  EXPECT_TRUE(shell().layout().hot_zone);
  EXPECT_TRUE(peek().is_open());
  ASSERT_TRUE(window().sidebar_request_for_testing());
  EXPECT_EQ(window().sidebar_request_for_testing()->kind, "rename");
  EXPECT_TRUE(window().sidebar_request_for_testing()->floating);
  const std::string folder = window().sidebar_request_for_testing()->node_id;
  const std::string renaming_row = content::JsReplace(
      "(() => { const row = [...document.querySelectorAll("
      "`[data-folder-id=\"${CSS.escape($1)}\"]`)]"
      ".find((el) => !el.closest('.clu-sidebar-warm'));"
      " return row?.dataset.renaming === 'true'; })()",
      folder);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(peek_contents(), renaming_row).ExtractBool();
  }));
  EXPECT_TRUE(base::test::RunUntil([&] { return peek().held(); }));
  EXPECT_FALSE(
      content::EvalJs(shell().frame_contents(), renaming_row).ExtractBool());

  // Reveal Tab in Sidebar goes to the open floating sidebar too.
  ASSERT_TRUE(window().RunCommand("tabs.reveal"));
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  EXPECT_EQ(window().sidebar_request_for_testing()->kind, "reveal");
  EXPECT_TRUE(window().sidebar_request_for_testing()->floating);

  // With the floating sidebar closed the docked sidebar shows and answers, as before.
  peek().SetOpen(false);
  if (peek().is_open()) {
    peek().controller().Close();
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek().is_open(); }));
  ASSERT_TRUE(window().RunCommand("tabs.reveal"));
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  EXPECT_FALSE(window().sidebar_request_for_testing()->floating);
}

// Focus mode floats the sidebar too: the Library opens in the floating sidebar at its own width, not the docked
// sidebar's, and the page keeps the focus-mode gutter.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarFocusModeLibraryTakesItsWidth) {
  if (shell().chrome_state().sidebar_hidden) {
    shell().ToggleSidebar();
  }
  shell().ToggleFocusMode();
  ASSERT_TRUE(shell().chrome_state().focus_mode);
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  EXPECT_EQ(shell().layout().sidebar_rect.width(), kLibraryWidth);
  EXPECT_EQ(peek().controller().rect().width(), kLibraryWidth + kPeekBleed);
  EXPECT_EQ(shell().layout().page_rect.x(), kFocusGutter);
  shell().ToggleFocusMode();
}

// A file dragged out of the floating sidebar's Library, or a native row menu, takes the pointer
// with no leave event to the peek's widget. The last point the panel saw is forgotten then, so a click on the page,
// the Library's Escape and the leave check ask the system pointer instead of a stale point on the panel.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarPeekForgetsThePointerADragOrMenuTakes) {
  HideSidebar();
  content::WebContents* frame = shell().frame_contents();
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(300, 300));
  content::SimulateMouseEvent(frame, Type::kMouseMove, gfx::Point(2, 300));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().is_open(); }));
  peek().set_pointer_on_peek_for_testing(std::nullopt);
  const auto move_on_panel = [&] {
    blink::WebMouseEvent move(Type::kMouseMove,
                              blink::WebInputEvent::kNoModifiers,
                              base::TimeTicks::Now());
    move.SetPositionInWidget(40, 300);
    peek().OnPeekMouseEvent(move);
  };
  move_on_panel();
  ASSERT_TRUE(peek().has_peek_pointer_for_testing());
  ASSERT_TRUE(window().RunCommand("archive.open"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().archive_panel(); }));

  // A file tile's drag began (what the downloads handler calls once the drag session starts; the session itself
  // needs a real press).
  peek().PointerTaken();
  EXPECT_FALSE(peek().has_peek_pointer_for_testing());
  EXPECT_TRUE(peek().is_open());

  // A native row menu opens from the panel.
  move_on_panel();
  ASSERT_TRUE(peek().has_peek_pointer_for_testing());
  peek().MenuOpened();
  EXPECT_FALSE(peek().has_peek_pointer_for_testing());
  peek().MenuClosed();

  // The panel's next event speaks for the pointer again.
  move_on_panel();
  EXPECT_TRUE(peek().has_peek_pointer_for_testing());
  peek().Close();
  ASSERT_FALSE(peek().is_open());
}

// A Library left open when the sidebar hides belongs to no peek. It closes, so the next edge
// hover shows the tabs and is not held (and given the keyboard) from its first frame. archive.open with the sidebar
// already hidden (Cmd+Shift+L, the Window menu, the command bar) opens the floating sidebar at once to show the
// Library, never a Library with no surface; a Library opened inside a peek stays.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarHidingTheSidebarClosesItsLibrary) {
  CluArchiveHandler archive(window().GetWeakPtr());
  const auto open_library = [&] {
    CallFuture done;
    archive.Open(done.GetCallback());
    return IsOk(done.Take());
  };
  ASSERT_TRUE(open_library());
  ASSERT_TRUE(window().archive_panel());
  HideSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));
  OpenPeek();
  EXPECT_FALSE(peek().held());
  EXPECT_EQ(false, content::EvalJs(peek_contents(), R"(
    new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(
        () => resolve(!!document.querySelector(
            '[data-shell="sidebar-peek"] .clu-sidebar-library')))))
  )"));
  peek().Close();
  ASSERT_FALSE(peek().is_open());

  // Opened with the sidebar hidden by the command (Cmd+Shift+L), the floating sidebar opens at once showing it, and
  // its own relayout does not close it.
  ASSERT_TRUE(window().RunCommand("archive.open"));
  content::RunAllTasksUntilIdle();
  EXPECT_TRUE(window().archive_panel());
  ASSERT_TRUE(peek().is_open());
  EXPECT_EQ(true, content::EvalJs(peek_contents(), R"(
    new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(
        () => resolve(!!document.querySelector(
            '[data-shell="sidebar-peek"] .clu-sidebar-library')))))
  )"));
  peek().Close();
  ASSERT_FALSE(peek().is_open());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));
  // The same through tabs.archive's open call.
  ASSERT_TRUE(open_library());
  EXPECT_TRUE(window().archive_panel());
  EXPECT_TRUE(peek().is_open());
  peek().Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));

  // Opened inside the peek, it stays while the peek does.
  OpenPeek();
  ASSERT_TRUE(open_library());
  content::RunAllTasksUntilIdle();
  EXPECT_TRUE(peek().is_open());
  EXPECT_TRUE(window().archive_panel());
}

// A frame tooltip at the page card's edge draws in the hover card's passive overlay slot, above the card,
// moved back inside the window when it would pass its edge, and goes away when the frame hides it.
IN_PROC_BROWSER_TEST_F(CluSidebarBrowserTest,
                       CluSidebarTooltipDrawsAboveThePageCard) {
  constexpr int kHoverSlot = 2;
  const gfx::Rect page = shell().layout().page_rect;
  CluShellHandler handler(window().GetWeakPtr());
  auto tip = [&](const std::string& label, const gfx::Rect& r) {
    auto input = mojom::ShellTooltipInput::New();
    if (!label.empty()) {
      input->label = label;
      input->keys = std::vector<std::string>{"\u2318", "["};
      input->rect = mojom::Rect::New(r.x(), r.y(), r.width(), r.height());
    }
    CallFuture done;
    handler.Tooltip(std::move(input), done.GetCallback());
    return IsOk(done.Take());
  };

  // The back button's pill straddles the card's left edge.
  const gfx::Rect over(page.x() - 60, page.y() + 8, 140, 52);
  ASSERT_TRUE(tip("Go back", over));
  ASSERT_TRUE(shell().IsPassiveVisible(kHoverSlot));
  EXPECT_EQ(shell().passive_rect_for_testing(kHoverSlot), over);
  EXPECT_TRUE(shell().passive_rect_for_testing(kHoverSlot).Intersects(page));
  const base::DictValue* surface = Surface("tooltip");
  ASSERT_TRUE(surface);
  EXPECT_EQ(*surface->FindString("layer"), "passive");
  EXPECT_EQ(surface->FindInt("slot"), kHoverSlot);
  EXPECT_EQ(*surface->FindStringByDottedPath("props.label"), "Go back");
  content::WebContents* overlay = shell().passive_contents(kHoverSlot);
  ASSERT_TRUE(overlay);
  EXPECT_EQ(true, content::EvalJs(overlay, content::JsReplace(
                                               kWaitForSelector,
                                               "[role=tooltip].clu-tooltip")));
  EXPECT_EQ("Go back\u2318[",
            content::EvalJs(overlay,
                            "document.querySelector('.clu-tooltip').textContent"));

  // Past the window's right edge it moves back inside.
  const gfx::Size size = shell().layout().content_size;
  ASSERT_TRUE(tip("Reload", gfx::Rect(size.width() - 40, 100, 140, 52)));
  EXPECT_EQ(shell().passive_rect_for_testing(kHoverSlot).right(), size.width());
  EXPECT_EQ(*Surface("tooltip")->FindStringByDottedPath("props.label"),
            "Reload");

  // No label hides it.
  ASSERT_TRUE(tip("", gfx::Rect()));
  EXPECT_FALSE(shell().IsPassiveVisible(kHoverSlot));
  EXPECT_FALSE(Surface("tooltip"));
}

}  // namespace
}  // namespace clu
