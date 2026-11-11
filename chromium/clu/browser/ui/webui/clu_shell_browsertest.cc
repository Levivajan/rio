// Copyright 2026 The Rio Authors. All rights reserved.

#include <cstdlib>
#include <memory>
#include <string>

#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/infobars/simple_alert_infobar_creator.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble_anchor_util_views.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "clu/browser/ui/webui/clu_overlay_rects.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/common/clu_urls.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/infobar_delegate.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/base_window.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluShell* checks of the M2 "Done when" row (docs/chromium/PLAN.md section 6), against the real frame and overlay
// WebUIs over the real bridge.
namespace clu {
namespace {

// Resolves once `selector` matches in the document. Clu's WebUIs render after their bridge connects.
constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

// Types into the command bar the way React sees typing: through the native value setter, then an input event.
constexpr char kTypeIntoCommandBar[] = R"(
  (() => {
    const input = document.querySelector('[data-surface="command"] input');
    const setter = Object.getOwnPropertyDescriptor(
        HTMLInputElement.prototype, 'value').set;
    setter.call(input, $1);
    input.dispatchEvent(new InputEvent('input', {bubbles: true}));
    return document.activeElement === input;
  })())";

constexpr char kRowCenter[] = R"(
  (() => {
    const r = document.querySelector($1).getBoundingClientRect();
    return [Math.round(r.x + r.width / 2), Math.round(r.y + r.height / 2)];
  })())";

class CluShellBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().overlay_contents()));
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window_state() {
    return *CluWindowState::GetOrCreate(browser());
  }
  BrowserView& browser_view() {
    return *BrowserView::GetBrowserViewForBrowser(browser());
  }
  TabStripModel& tab_strip() { return *browser()->GetTabStripModel(); }

  // The strip's pages without the window's background placeholder (CluWindowState::EnsurePlaceholder()).
  int Pages() {
    int count = 0;
    for (int i = 0; i < tab_strip().count(); ++i) {
      if (!CluPlaceholder::Is(tab_strip().GetWebContentsAt(i))) {
        ++count;
      }
    }
    return count;
  }

  std::string RowSelector(int index) {
    return base::StrCat(
        {"nav.clu-sidebar [data-tab-id=\"",
         CluTabId::Get(tab_strip().GetWebContentsAt(index)), "\"]"});
  }

  void WaitForSelector(content::WebContents* contents,
                       const std::string& selector) {
    EXPECT_EQ(true, content::EvalJs(
                        contents, content::JsReplace(kWaitForSelector, selector)));
  }

  // The page contents sit at the rect the frame WebUI draws the card at, within 1 px. An infobar may push the top
  // down; the other edges are exact.
  void ExpectContentsAtPageRect() {
    views::test::RunScheduledLayout(&browser_view());
    const gfx::Rect card = shell().shown_page_rect();
    const gfx::Rect contents = browser_view().multi_contents_view()->bounds();
    EXPECT_LE(std::abs(contents.x() - card.x()), 1) << contents.ToString();
    EXPECT_LE(std::abs(contents.right() - card.right()), 1)
        << contents.ToString();
    EXPECT_LE(std::abs(contents.bottom() - card.bottom()), 1)
        << contents.ToString();
    EXPECT_GE(contents.y(), card.y() - 1) << contents.ToString();
  }
};

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellFrameRendersTheSidebarFromTheTabStrip) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title2.html"),
                            ui::PAGE_TRANSITION_TYPED));
  ASSERT_EQ(Pages(), 2);

  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, RowSelector(0));
  WaitForSelector(frame, RowSelector(1));
  EXPECT_EQ("true",
            content::EvalJs(frame, content::JsReplace(
                                       "document.querySelector($1).dataset.active",
                                       RowSelector(1))));

  // Closing a tab in Chromium removes its row through a state patch.
  const std::string closed = RowSelector(1);
  tab_strip().CloseWebContentsAt(1, TabCloseTypes::CLOSE_USER_GESTURE);
  EXPECT_EQ(true, content::EvalJs(frame, content::JsReplace(R"(
      new Promise(resolve => {
        const check = () => document.querySelector($1) ? setTimeout(check, 20)
                                                         : resolve(true);
        check();
      }))",
                                                           closed)));
}

// A two-finger scroll over the sidebar's empty space, a window drag area, reaches the frame document as
// a wheel, as one over a row does, so a swipe there switches spaces and slides the Library in.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest, CluShellAScrollOverADragAreaReachesTheFrame) {
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, "nav.clu-sidebar .clu-tab-list");
  ASSERT_TRUE(content::ExecJs(frame, R"(
      window.cluWheels = [];
      document.addEventListener('wheel', e => window.cluWheels.push(e.deltaX),
                                {capture: true});)"));
  const base::ListValue bottom =
      content::EvalJs(frame, R"(
      (() => {
        const r = document.querySelector('nav.clu-sidebar .clu-tab-list')
                      .getBoundingClientRect();
        return [Math.round(r.x + r.width / 2), Math.round(r.bottom - 4)];
      })())")
          .ExtractList()
          .Clone();
  ASSERT_EQ(bottom.size(), 2u);
  const gfx::Point in_frame(bottom[0].GetInt(), bottom[1].GetInt());
  views::WebView* frame_view = shell().frame_view();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell().IsWindowDragArea(
        views::View::ConvertPointToTarget(frame_view, &browser_view(), in_frame));
  }));

  views::Widget* widget = browser_view().GetWidget();
  const gfx::PointF in_root(views::View::ConvertPointToTarget(
      frame_view, widget->GetRootView(), in_frame));
  // Fingers moving left: the frame sees deltaX > 0, toward the next space.
  ui::ScrollEvent scroll(ui::EventType::kScroll, in_root, in_root,
                         base::TimeTicks::Now(), 0, -40, 0, -40, 0, 2);
  widget->OnScrollEvent(&scroll);
  EXPECT_EQ(true, content::EvalJs(frame, R"(
      new Promise(resolve => {
        const check = () => window.cluWheels.length
            ? resolve(window.cluWheels[0] > 0) : setTimeout(check, 20);
        check();
      }))"));
}

// View > Show Toolbar (Cmd+Shift+D) moves the page card under the toolbar's band, the frame draws the URL pill
// there, and the pill's right-click menu turns the mode back off. Duplicate Tab gave the key up.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest, CluShellToolbarModeMovesThePageUnderTheBand) {
  const CommandSpec* toolbar = FindCommand("shell.toolbar");
  ASSERT_TRUE(toolbar);
  EXPECT_EQ(toolbar->accelerator.key, ui::VKEY_D);
  EXPECT_EQ(toolbar->accelerator.modifiers,
            ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN);
  EXPECT_FALSE(FindCommand("tabs.duplicate")->has_accelerator);

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toolbar->idc));
  EXPECT_TRUE(shell().chrome_state().toolbar);
  EXPECT_EQ(shell().layout().page_rect.y(), kCardInset + kToolbarHeight);
  ExpectContentsAtPageRect();
  const base::DictValue state = window_state().CurrentState();
  const base::DictValue* window =
      state.FindDict("windows")->FindDict(window_state().window_id());
  ASSERT_TRUE(window);
  EXPECT_EQ(window->FindBoolByDottedPath("shell.toolbar"), true);
  WaitForSelector(shell().frame_contents(),
                  "[data-shell=\"toolbar\"] [data-shell=\"url-pill\"]");

  std::unique_ptr<ui::MenuModel> menu = window_state().ToolbarMenuModel();
  ASSERT_EQ(menu->GetItemCount(), 1u);
  EXPECT_EQ(menu->GetLabelAt(0), u"Hide Toolbar");
  menu->ActivatedAt(0);
  EXPECT_FALSE(shell().chrome_state().toolbar);
  EXPECT_EQ(shell().layout().page_rect.y(), kCardInset);
  ExpectContentsAtPageRect();
  EXPECT_EQ(window_state().ToolbarMenuModel()->GetLabelAt(0),
            u"Show as Toolbar");
}

// A text field in Clu's WebUIs (a rename field, the command bar input) gets the standard Cut, Copy and Paste
// menu; anywhere else the WebUI draws its own menu and the page menu never opens.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellTextFieldsKeepTheTextContextMenu) {
  content::WebContents* frame = shell().frame_contents();
  ASSERT_TRUE(frame);
  content::ContextMenuParams params;
  params.is_editable = true;
  EXPECT_FALSE(shell().HandleContextMenu(*frame->GetPrimaryMainFrame(), params));
  params.is_editable = false;
  EXPECT_TRUE(shell().HandleContextMenu(*frame->GetPrimaryMainFrame(), params));
}

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest, CluShellClickingARowActivatesTheTab) {
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title2.html"),
                            ui::PAGE_TRANSITION_TYPED));
  ASSERT_EQ(tab_strip().active_index(), 1);
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, RowSelector(0));

  const base::ListValue center =
      content::EvalJs(frame, content::JsReplace(kRowCenter, RowSelector(0)))
          .ExtractList()
          .Clone();
  ASSERT_EQ(center.size(), 2u);
  content::SimulateMouseClickAt(
      frame, 0, blink::WebMouseEvent::Button::kLeft,
      gfx::Point(center[0].GetInt(), center[1].GetInt()));
  EXPECT_TRUE(base::test::RunUntil([&] { return tab_strip().active_index() == 0; }));
}

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellPageBoundsFollowThePageRect) {
  ExpectContentsAtPageRect();
  EXPECT_EQ(shell().layout().page_rect.x(), shell().layout().effective_width);

  const gfx::Size before = shell().layout().content_size;
  browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 900, 640));
  EXPECT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(&browser_view());
    return shell().layout().content_size != before;
  }));
  ExpectContentsAtPageRect();

  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  EXPECT_TRUE(base::test::RunUntil([&] { return shell().chrome_state().fullscreen; }));
  EXPECT_EQ(shell().layout().traffic_spacer, kTrafficSpacer);
  ExpectContentsAtPageRect();
}

// A toast showing through a resize moves with the window, at the rect the published state names for it.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest, CluShellAToastFollowsAResize) {
  browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1100, 800));
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(&browser_view());
    return !shell().layout().content_size.IsEmpty();
  }));
  window_state().ShowToast("Cleared", std::string("archive.undoClear"),
                           base::Seconds(5));
  ASSERT_TRUE(shell().IsPassiveVisible(0));
  const gfx::Size before = shell().layout().content_size;
  EXPECT_EQ(shell().passive_rect_for_testing(0),
            ToastRect(shell().layout().page_rect, 1));

  browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1100, 500));
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(&browser_view());
    return shell().layout().content_size != before;
  }));
  EXPECT_EQ(shell().passive_rect_for_testing(0),
            ToastRect(shell().layout().page_rect, 1));
}

// Advanced, "Show full URL when Toolbar is enabled": off, the toolbar's pill shows the host; on, the whole URL; the
// sidebar's pill keeps the host either way.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellTheToolbarPillShowsTheFullUrlWhenAsked) {
  const GURL url = embedded_test_server()->GetURL("/title1.html?q=1");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  const std::string host_text =
      "document.querySelector('[data-shell=\"url-host\"]').textContent.trim()";
  auto pill_shows = [&](const std::string& text) {
    return base::test::RunUntil([&] {
      return content::EvalJs(shell().frame_contents(), host_text)
                 .ExtractString() == text;
    });
  };
  const std::string host = base::StrCat({url.host(), ":", url.port()});
  EXPECT_TRUE(pill_shows(host));
  CluAppSettings::Get()->SetGlobal(kToolbarFullUrlKey, base::Value(true));
  EXPECT_TRUE(pill_shows(host));

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), FindCommand("shell.toolbar")->idc));
  WaitForSelector(shell().frame_contents(),
                  "[data-shell=\"toolbar\"] [data-shell=\"url-pill\"]");
  EXPECT_TRUE(pill_shows(url.spec()));
  CluAppSettings::Get()->SetGlobal(kToolbarFullUrlKey, base::Value(false));
  EXPECT_TRUE(pill_shows(host));
}

// A toast sits inside the page card's top right corner, also when toolbar mode moves the card under the band
// while it shows, and the copy toast lands at the same spot as any other.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest, CluShellEveryToastSitsInsideThePageCard) {
  window_state().ShowToast("Copied Text", std::nullopt, base::Seconds(5));
  ASSERT_TRUE(shell().IsPassiveVisible(0));
  const gfx::Rect plain = shell().passive_rect_for_testing(0);
  EXPECT_EQ(plain, ToastRect(shell().layout().page_rect, 1));
  window_state().ShowToast("Copied Current URL",
                           std::string(kShareToastAction), base::Seconds(5));
  EXPECT_EQ(shell().passive_rect_for_testing(0), plain);

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), FindCommand("shell.toolbar")->idc));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell().layout().page_rect.y() == kCardInset + kToolbarHeight &&
           shell().passive_rect_for_testing(0) ==
               ToastRect(shell().layout().page_rect, 1);
  }));
  const gfx::Rect card = shell().layout().page_rect;
  gfx::Rect toast = shell().passive_rect_for_testing(0);
  EXPECT_EQ(toast, ToastRect(card, 1));
  toast.Inset(kToastPad);
  EXPECT_TRUE(card.Contains(toast));
}

// Cmd+T in an undocked DevTools window opens the bar in the Clu window it belongs to, never a blank tab there.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellNewTabFromDevToolsOpensTheBarInTheCluWindow) {
  DevToolsWindow* devtools = DevToolsWindowTesting::OpenDevToolsWindowSync(
      tab_strip().GetActiveWebContents(), /*is_docked=*/false);
  BrowserWindowInterface* devtools_browser =
      DevToolsWindowTesting::Get(devtools)->browser();
  ASSERT_TRUE(devtools_browser);
  ASSERT_FALSE(CluShell::From(devtools_browser));
  const int pages = Pages();
  ASSERT_TRUE(chrome::ExecuteCommand(devtools_browser, IDC_NEW_TAB));
  EXPECT_TRUE(window_state().command_session());
  EXPECT_EQ(Pages(), pages);
  DevToolsWindowTesting::CloseDevToolsWindowSync(devtools);
}

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellNewTabOpensTheCommandBarAndEnterNavigates) {
  tab_strip().GetActiveWebContents()->Focus();
  // Cmd+T is IDC_NEW_TAB; the Clu hook opens the bar instead of a tab.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  EXPECT_EQ(Pages(), 1);
  ASSERT_TRUE(window_state().command_session());
  EXPECT_EQ(window_state().command_session()->mode, "new-tab");
  EXPECT_TRUE(shell().IsOverlayVisible());

  content::WebContents* overlay = shell().overlay_contents();
  WaitForSelector(overlay, "[data-surface=\"command\"] input");
  const GURL url = embedded_test_server()->GetURL("/title1.html");
  EXPECT_EQ(true, content::EvalJs(overlay, content::JsReplace(
                                               kTypeIntoCommandBar, url.spec())));

  content::TestNavigationObserver navigation(url);
  navigation.StartWatchingNewWebContents();
  content::SimulateKeyPress(overlay, ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, /*control=*/false,
                            /*shift=*/false, /*alt=*/false,
                            /*command=*/false);
  navigation.Wait();
  EXPECT_EQ(Pages(), 2);
  EXPECT_EQ(tab_strip().GetActiveWebContents()->GetLastCommittedURL(), url);
  EXPECT_FALSE(window_state().command_session());
  EXPECT_FALSE(shell().IsOverlayVisible());
}

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellEditUrlTogglesAndSwitchesInPlace) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_FOCUS_LOCATION));
  ASSERT_TRUE(window_state().command_session());
  EXPECT_EQ(window_state().command_session()->mode, "edit-url");
  EXPECT_EQ(window_state().command_session()->initial_text,
            embedded_test_server()->GetURL("/title1.html").spec());
  // The other mode switches the open session in place; the same mode again closes it.
  const std::string session = window_state().command_session()->session_id;
  ASSERT_TRUE(HandleBrowserCommand(browser(), IDC_NEW_TAB));
  ASSERT_TRUE(window_state().command_session());
  EXPECT_EQ(window_state().command_session()->session_id, session);
  EXPECT_EQ(window_state().command_session()->mode, "new-tab");
  ASSERT_TRUE(HandleBrowserCommand(browser(), IDC_NEW_TAB));
  EXPECT_FALSE(window_state().command_session());
  EXPECT_FALSE(shell().IsOverlayVisible());
}

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellSidebarToggleTweensThePageCard) {
  ExpectContentsAtPageRect();
  // Cmd+S is the sidebar key; its main menu item carries sidebar.toggle's IDC_CLU_* id (docs/chromium/m5.md).
  const int toggle = FindCommand("sidebar.toggle")->idc;
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), toggle));
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  EXPECT_EQ(shell().layout().page_rect.x(), kCardInset);
  // Hiding tweens the card to the full width over kSidebarHideMs (80 ms, 5 ticks).
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_EQ(shell().sidebar_anim()->ms, kSidebarHideMs);
  EXPECT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  ExpectContentsAtPageRect();
  EXPECT_EQ(browser_view().multi_contents_view()->x(), kCardInset);

  const base::DictValue state = window_state().CurrentState();
  const base::DictValue* window =
      state.FindDict("windows")->FindDict(window_state().window_id());
  ASSERT_TRUE(window);
  EXPECT_EQ(window->FindBoolByDottedPath("shell.sidebar.hidden"), true);

  ASSERT_TRUE(HandleBrowserCommand(browser(), toggle));
  // Showing docks it over kSidebarShowMs (85 ms, 6 ticks).
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_EQ(shell().sidebar_anim()->ms, kSidebarShowMs);
  EXPECT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  ExpectContentsAtPageRect();
  EXPECT_EQ(shell().layout().page_rect.x(), shell().layout().effective_width);
}

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellHidesChromiumChromeAndAnchorsBubbles) {
  views::test::RunScheduledLayout(&browser_view());
  EXPECT_FALSE(browser_view().top_container()->GetVisible());

  views::View* anchor = GetBubbleAnchorView(browser());
  ASSERT_TRUE(anchor);
  EXPECT_TRUE(anchor->IsDrawn());
  EXPECT_EQ(anchor->parent(), &browser_view());
  const gfx::Rect card = shell().shown_page_rect();
  EXPECT_TRUE(card.Contains(anchor->bounds().origin()));
  // It spans the card's top edge, so trailing-arrow bubbles hang from the card's trailing corner.
  EXPECT_EQ(anchor->bounds().height(), 0);
  EXPECT_GT(anchor->bounds().width(), card.width() / 2);
  EXPECT_TRUE(card.Contains(anchor->bounds().top_right() - gfx::Vector2d(1, 0)));

  // Password and other toolbar bubbles, extension dialogs, page info and permission prompts.
  ToolbarButtonProvider* provider = browser_view().toolbar_button_provider();
  EXPECT_EQ(provider->GetBubbleAnchor(std::nullopt).GetIfView(), anchor);
  EXPECT_EQ(provider->GetDefaultExtensionDialogAnchor().GetIfView(), anchor);
  EXPECT_EQ(bubble_anchor_util::GetPageInfoAnchorConfiguration(browser())
                .anchor.GetIfView(),
            anchor);
}

IN_PROC_BROWSER_TEST_F(CluShellBrowserTest, CluShellFrameInATabIsNotASurface) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kCluFrameUrl)));
  content::WebContents* tab = tab_strip().GetActiveWebContents();
  // Without a window the bridge sends no state, so the sidebar never renders, and every command is refused.
  EXPECT_EQ(false, content::EvalJs(tab, R"(
      new Promise(resolve => setTimeout(
          () => resolve(!!document.querySelector('nav.clu-sidebar')), 500)))"));
  // The window's own frame is unaffected.
  WaitForSelector(shell().frame_contents(), "nav.clu-sidebar");
}

// A bar at the top of the card keeps all four of the card's corners round. The infobar takes the top
// two, the page under it the bottom two.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest,
                       CluShellAnInfobarKeepsTheCardsCornersRound) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  content::WebContents* page = tab_strip().GetActiveWebContents();
  CreateSimpleAlertInfoBar(
      infobars::ContentInfoBarManager::FromWebContents(page),
      infobars::InfoBarDelegate::TEST_INFOBAR, /*vector_icon=*/nullptr,
      u"Clu tests", /*auto_expire=*/false, /*should_animate=*/false);
  views::View* const infobars = browser_view().infobar_container();
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(&browser_view());
    return infobars->GetVisible() && infobars->height() > 0;
  }));

  const float radius = shell().layout().radius;
  ASSERT_GT(radius, 0.f);
  const gfx::Rect card = shell().shown_page_rect();
  EXPECT_EQ(infobars->bounds().origin(), card.origin());
  EXPECT_EQ(infobars->width(), card.width());
  ASSERT_TRUE(infobars->layer());
  EXPECT_EQ(infobars->layer()->rounded_corner_radii(),
            gfx::RoundedCornersF(radius, radius, 0, 0));
  EXPECT_TRUE(infobars->layer()->GetMasksToBounds());
  EXPECT_EQ(browser_view().multi_contents_view()->GetBackgroundRadii(),
            gfx::RoundedCornersF(0, 0, radius, radius));
  ExpectContentsAtPageRect();
  EXPECT_EQ(browser_view().multi_contents_view()->y(), infobars->bounds().bottom());
}

// A page that goes full screen (a video) fills the screen: no sidebar, no inset, square corners, no reveal zone.
// Leaving full screen brings the sidebar and the card back.
IN_PROC_BROWSER_TEST_F(CluShellBrowserTest, CluShellAPageInFullScreenFillsTheWindow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  content::WebContents* page = tab_strip().GetActiveWebContents();
  FullscreenController* controller =
      ExclusiveAccessManager::From(browser())->fullscreen_controller();
  {
    ui_test_utils::FullscreenWaiter waiter(browser(), {.tab_fullscreen = true});
    controller->EnterFullscreenModeForTab(page->GetPrimaryMainFrame());
    waiter.Wait();
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(&browser_view());
    return shell().chrome_state().fullscreen &&
           shell().chrome_state().content_fullscreen;
  }));
  const ShellLayout& full = shell().layout();
  EXPECT_EQ(full.page_rect, gfx::Rect(full.content_size));
  EXPECT_EQ(full.radius, 0.f);
  EXPECT_FALSE(full.hot_zone);
  EXPECT_FALSE(full.traffic_lights);
  ExpectContentsAtPageRect();
  EXPECT_EQ(browser_view().multi_contents_view()->size(), full.content_size);

  {
    ui_test_utils::FullscreenWaiter waiter(
        browser(), ui_test_utils::FullscreenWaiter::kNoFullscreen);
    controller->ExitFullscreenModeForTab(page);
    waiter.Wait();
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(&browser_view());
    return !shell().chrome_state().content_fullscreen &&
           !shell().chrome_state().fullscreen;
  }));
  EXPECT_EQ(shell().layout().page_rect.x(), shell().layout().effective_width);
  EXPECT_EQ(shell().layout().radius, kCardRadius);
  ExpectContentsAtPageRect();
}

}  // namespace
}  // namespace clu
