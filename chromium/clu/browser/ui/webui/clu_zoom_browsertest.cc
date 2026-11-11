// Copyright 2026 The Rio Authors. All rights reserved.

#include <string>

#include "base/command_line.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/common/clu_urls.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/input/web_gesture_device.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "ui/display/display_switches.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/webview/webview.h"

// Rio's own UI never zooms (docs/chromium/ui-polish.md): CluUiZoomLock keeps
// the frame, overlay and passive WebUIs at 100% while Cmd+plus, Cmd+minus, Cmd+0 and pinch act on the page.
namespace clu {
namespace {

// The WebUI's CSS width against its view's DIP width: 1.0 unless the document is zoomed.
constexpr char kCssWidth[] = "window.innerWidth";
constexpr char kPinchScale[] = "window.visualViewport.scale";

class CluZoomBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().overlay_contents()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().passive_contents(0)));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/title1.html")));
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  content::WebContents* page() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }
  content::HostZoomMap* zoom_map() {
    return content::HostZoomMap::GetDefaultForBrowserContext(
        browser()->GetProfile());
  }

  // Every Rio WebUI of the window is at zoom level 0 and draws one CSS pixel per DIP.
  void ExpectUiUnzoomed() {
    for (content::WebContents* ui :
         {shell().frame_contents(), shell().overlay_contents(),
          shell().passive_contents(0)}) {
      EXPECT_TRUE(
          blink::ZoomValuesEqual(content::HostZoomMap::GetZoomLevel(ui), 0.0))
          << ui->GetLastCommittedURL();
    }
    EXPECT_EQ(shell().frame_view()->width(),
              content::EvalJs(shell().frame_contents(), kCssWidth));
    EXPECT_DOUBLE_EQ(
        1.0,
        content::EvalJs(shell().frame_contents(), kPinchScale).ExtractDouble());
  }
};

// Cmd+plus, Cmd+minus and Cmd+0 (Chromium's zoom commands, which the key table and the app menu run) zoom the page,
// even while the sidebar has focus, and never the frame.
IN_PROC_BROWSER_TEST_F(CluZoomBrowserTest, CluPageZoomLeavesTheFrameAt100) {
  shell().frame_contents()->Focus();
  chrome::ExecuteCommand(browser(), IDC_ZOOM_PLUS);
  chrome::ExecuteCommand(browser(), IDC_ZOOM_PLUS);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::HostZoomMap::GetZoomLevel(page()) > 0.0;
  }));
  ExpectUiUnzoomed();

  chrome::ExecuteCommand(browser(), IDC_ZOOM_MINUS);
  chrome::ExecuteCommand(browser(), IDC_ZOOM_MINUS);
  chrome::ExecuteCommand(browser(), IDC_ZOOM_MINUS);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::HostZoomMap::GetZoomLevel(page()) < 0.0;
  }));
  ExpectUiUnzoomed();

  chrome::ExecuteCommand(browser(), IDC_ZOOM_NORMAL);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return blink::ZoomValuesEqual(content::HostZoomMap::GetZoomLevel(page()),
                                  0.0);
  }));
  ExpectUiUnzoomed();
}

// A zoom level saved for the WebUIs' hosts (an old build, a synced or imported profile) and the profile's default
// zoom both stay off Rio's UI; the page still takes the default.
IN_PROC_BROWSER_TEST_F(CluZoomBrowserTest, CluSavedZoomForTheFrameHostIsIgnored) {
  const double doubled = blink::ZoomFactorToZoomLevel(2.0);
  zoom_map()->SetZoomLevelForHost(kCluFrameHost, doubled);
  zoom_map()->SetZoomLevelForHost(kCluOverlayHost, doubled);
  ExpectUiUnzoomed();

  const double larger = blink::ZoomFactorToZoomLevel(1.5);
  zoom_map()->SetDefaultZoomLevel(larger);
  ExpectUiUnzoomed();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  EXPECT_TRUE(blink::ZoomValuesEqual(
      content::HostZoomMap::GetZoomLevel(page()), larger));

  // A level set on the frame itself is put back at once.
  content::HostZoomMap::SetZoomLevel(shell().frame_contents(), doubled);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return blink::ZoomValuesEqual(
        content::HostZoomMap::GetZoomLevel(shell().frame_contents()), 0.0);
  }));
  ExpectUiUnzoomed();

  // A reloaded frame (a new document, possibly a new RenderFrameHost) is locked again.
  shell().frame_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                                    false);
  ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
  ExpectUiUnzoomed();
}

// A pinch over the sidebar never scales it; a pinch over the page still zooms the page.
IN_PROC_BROWSER_TEST_F(CluZoomBrowserTest, CluPinchZoomsThePageNotTheSidebar) {
  content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(shell().frame_contents());
  content::SimulateGesturePinchSequence(shell().frame_contents(),
                                        gfx::Point(100, 200), 2.0f,
                                        blink::WebGestureDevice::kTouchscreen);
  content::SimulateGesturePinchSequence(shell().frame_contents(),
                                        gfx::Point(100, 200), 2.0f,
                                        blink::WebGestureDevice::kTouchpad);

  // A synthetic pinch that reaches the page before its compositor is ready is dropped, so it is sent again until the
  // page has zoomed.
  content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(page());
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (content::EvalJs(page(), kPinchScale).ExtractDouble() > 1.0) {
      return true;
    }
    content::SimulateGesturePinchSequence(page(), gfx::Point(200, 200), 2.0f,
                                          blink::WebGestureDevice::kTouchscreen);
    return false;
  }));
  // The page's pinch has gone through the renderer, so the sidebar's (sent first) would have too.
  ExpectUiUnzoomed();
}

// On a 2x display the frame draws at the display's scale and still lays out one CSS pixel per DIP: a window moved to
// another display redraws the sidebar at that display's scale, never at a zoom.
class CluZoomHiDpiBrowserTest : public CluZoomBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    CluZoomBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kForceDeviceScaleFactor, "2");
  }
};

IN_PROC_BROWSER_TEST_F(CluZoomHiDpiBrowserTest, CluFrameDrawsAtTheDisplayScale) {
  const double frame_scale =
      content::EvalJs(shell().frame_contents(), "window.devicePixelRatio")
          .ExtractDouble();
  EXPECT_DOUBLE_EQ(2.0, frame_scale);
  EXPECT_DOUBLE_EQ(
      content::EvalJs(page(), "window.devicePixelRatio").ExtractDouble(),
      frame_scale);
  ExpectUiUnzoomed();
}

}  // namespace
}  // namespace clu
