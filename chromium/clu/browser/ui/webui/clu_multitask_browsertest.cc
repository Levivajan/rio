// Copyright 2026 The Rio Authors. All rights reserved.

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/safe_browsing/test_safe_browsing_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_outline.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_mini_toolbar.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/permissions/permission_request_manager_test_api.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/ui/views/frame/clu_pane_bar.h"
#include "clu/browser/ui/views/frame/clu_peek_view.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/download/public/common/download_item.h"
#include "components/find_in_page/find_tab_helper.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/resolvers/permission_prompt_options.h"
#include "components/permissions/test/permission_request_observer.h"
#include "components/safe_browsing/core/browser/db/fake_database_manager.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/tabs/public/tab_interface.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/public/test/hit_test_region_observer.h"
#include "content/public/test/media_start_stop_observer.h"
#include "content/public/test/test_navigation_observer.h"
#include "media/base/picture_in_picture_events_info.h"
#include "net/base/filename_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluSplit*, CluMedia* and CluPeek* checks of the M6 "Done when" row
// (docs/chromium/PLAN.md section 6): docs/PLAN.md 12.9 (two panes)
// and the first box of 12.10, on Chromium's own split tabs, auto
// picture-in-picture and media session, with real pages, the real frame and
// overlay WebUIs, and the handlers the bridge calls.
// docs/chromium/acceptance-map.md maps each box to these tests. The Mini
// window's are in clu_mini_browsertest.mm.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

std::string Reason(const mojom::CallResultPtr& result) {
  return result && result->is_error()
             ? result->get_error()->reason.value_or(std::string())
             : std::string();
}

// Counts the navigations a WebContents finishes while it lives.
class NavigationCounter : public content::WebContentsObserver {
 public:
  explicit NavigationCounter(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame()) {
      ++count_;
    }
  }
  int count() const { return count_; }

 private:
  int count_ = 0;
};

class CluMultitaskBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluWindowMultitask& multitask() { return window().multitask(); }
  CluModelService& model() { return *window().model(); }
  TabStripModel& strip() { return *browser()->GetTabStripModel(); }
  std::string space() { return window().space_id(); }

  GURL Url(const std::string& page, const std::string& host = "127.0.0.1") {
    return embedded_test_server()->GetURL(host, base::StrCat({"/", page}));
  }

  // Chromium's auto picture-in-picture takes https and file pages only.
  GURL FileUrl(const std::string& page) {
    return net::FilePathToFileURL(
        base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
            .AppendASCII("clu/test/data")
            .AppendASCII(page));
  }

  std::string Open(const GURL& url, bool activate = true) {
    base::expected<std::string, std::string> id =
        window().CreateRow(url, std::nullopt, activate);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return std::string();
    }
    if (content::WebContents* contents = Contents(id.value())) {
      EXPECT_TRUE(content::WaitForLoadStop(contents));
    }
    return id.value();
  }

  // A pinned row on `url`, shown and loaded.
  std::string OpenPinned(const GURL& url) {
    const std::string id = Open(url);
    EXPECT_TRUE(window()
                    .MoveRows({id}, DropTarget::Pinned(space(), "", 0), false)
                    .has_value());
    EXPECT_TRUE(window().ActivateRow(id));
    return id;
  }

  content::WebContents* Contents(const std::string& row_id) {
    tabs::TabInterface* tab = window().FindTab(row_id);
    return tab ? tab->GetContents() : nullptr;
  }

  const base::DictValue& WindowState() {
    state_ = window().CurrentState();
    return *state_.FindDict("windows")->FindDict(window().window_id());
  }

  void WaitForSelector(content::WebContents* contents,
                       const std::string& selector) {
    EXPECT_EQ(true, content::EvalJs(contents, content::JsReplace(
                                                  kWaitForSelector, selector)));
  }

  MultiContentsView* PaneView() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->multi_contents_view();
  }

  // The two row ids of the Chromium split `row_id`'s page is in, in pane order.
  std::vector<std::string> StripSplitOf(const std::string& row_id) {
    std::vector<std::string> ids;
    tabs::TabInterface* tab = window().FindTab(row_id);
    if (!tab || !tab->GetSplit()) {
      return ids;
    }
    for (tabs::TabInterface* member :
         strip().GetSplitData(*tab->GetSplit())->ListTabs()) {
      ids.push_back(CluTabId::Get(member->GetContents()));
    }
    return ids;
  }

  base::DictValue state_;
};

// 12.9 box 1 (two panes): drag to an edge, swap, resize and persist, keys,
// separate, junk rows ---------------------

IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitADropOnAnEdgeMakesAChromiumSplitAndOneRow) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  ASSERT_TRUE(window().ActivateRow(a));
  CluSplitHandler handler(window().GetWeakPtr());

  // The sidebar drag over the page: the preview is half of A toward the right
  // edge.
  CallFuture start;
  handler.DragState(
      mojom::SplitDragStateInput::New(mojom::SplitDragStateInputPhase::kStart,
                                      b, std::nullopt, std::nullopt),
      start.GetCallback());
  ASSERT_TRUE(IsOk(start.Get()));
  CallFuture hover;
  handler.DragState(mojom::SplitDragStateInput::New(
                        mojom::SplitDragStateInputPhase::kHover, b, a,
                        mojom::SplitDragStateInputEdge::kRight),
                    hover.GetCallback());
  ASSERT_TRUE(IsOk(hover.Get()));
  EXPECT_EQ(*WindowState().FindStringByDottedPath("splitDrop.edge"), "right");
  const gfx::Rect page = shell().shown_page_rect();
  EXPECT_EQ(WindowState().FindIntByDottedPath("splitDrop.zone.width"),
            page.width() / 2);
  WaitForSelector(shell().overlay_contents(),
                  "[data-split-drop-zone][data-edge=\"right\"]");

  CallFuture dropped;
  handler.DropTab(
      mojom::SplitDropTabInput::New(b, a, mojom::SplitDropTabInputEdge::kRight),
      dropped.GetCallback());
  ASSERT_TRUE(IsOk(dropped.Get()));
  CallFuture end;
  handler.DragState(
      mojom::SplitDragStateInput::New(mojom::SplitDragStateInputPhase::kEnd, b,
                                      std::nullopt, std::nullopt),
      end.GetCallback());
  EXPECT_TRUE(WindowState().Find("splitDrop")->is_none());

  // One model split [A, B], one Chromium split in that order, both panes on
  // screen, the dropped tab focused.
  const SplitRow* split = model().SplitOf(a);
  ASSERT_TRUE(split);
  EXPECT_EQ(split->tab_ids, (std::vector<std::string>{a, b}));
  EXPECT_EQ(StripSplitOf(a), (std::vector<std::string>{a, b}));
  EXPECT_TRUE(PaneView()->IsInSplitView());
  EXPECT_EQ(window().ActiveRowId(), b);
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::ListValue* panes =
        WindowState().FindListByDottedPath("splitView.panes");
    return panes && panes->size() == 2u;
  }));
  // The sidebar shows the split as one row with both members.
  WaitForSelector(shell().frame_contents(),
                  base::StrCat({"[data-split-id=\"", split->id,
                                "\"] [data-split-member=\"", b, "\"]"}));

  // Each pane has its own toolbar on top: its page's URL, reload only on the focused
  // pane, Chromium's mini toolbar hidden, and the page under the 32 pt bar. The top row keeps only the toggle.
  for (ContentsContainerView* container : PaneView()->contents_container_views()) {
    auto* bar = views::AsViewClass<CluPaneBar>(PaneBarOf(container));
    ASSERT_TRUE(bar);
    ASSERT_TRUE(base::test::RunUntil([&] { return bar->GetVisible(); }));
    // Shown, then laid out by the container's next layout.
    ASSERT_TRUE(base::test::RunUntil([&] {
      views::test::RunScheduledLayout(container);
      return bar->height() == CluPaneBar::kHeight;
    }));
    EXPECT_FALSE(container->mini_toolbar()->GetVisible());
    const bool focused = CluTabId::Get(container->contents_view()->web_contents()) == b;
    EXPECT_EQ(bar->active(), focused);
    EXPECT_EQ(bar->reload_visible(), focused);
    // The other pane keeps only back, forward, its address and close.
    EXPECT_EQ(bar->site_controls_visible(), focused);
    EXPECT_EQ(bar->split_menu_visible(), focused);
    // Its own page's URL without the scheme: 127.0.0.1:<port>/tabs/a.html or b.html.
    const std::u16string url = bar->url_text();
    EXPECT_TRUE(url.starts_with(u"127.0.0.1:")) << url;
    EXPECT_NE(url.find(focused ? u"/tabs/b.html" : u"/tabs/a.html"), std::u16string::npos) << url;
    EXPECT_EQ(container->contents_view()->y(), bar->bounds().bottom());
    EXPECT_EQ(bar->height(), CluPaneBar::kHeight);
    // Back centered 21 in from the pane's edge and close 21 in from its right edge, the edge being Chromium's 1 pt
    // outline at the container's bounds (both sat 4 pt inward).
    const int edge = 1;
    EXPECT_EQ(bar->x() + bar->back_bounds().CenterPoint().x() - edge, 21);
    EXPECT_EQ(container->width() - edge -
                  (bar->x() + bar->close_bounds().CenterPoint().x()),
              21);
    // The pane toolbar glyphs at their drawn sizes, give or take a pixel of
    // antialiasing: back and forward 13 x 10.5, reload 12.5 x 15, site controls 16, split 14.5, close 12, link 13.
    const std::vector<gfx::Size> glyphs = bar->glyph_sizes();
    ASSERT_EQ(glyphs.size(), 7u);
    const auto near = [](const gfx::Size& got, float width, float height) {
      return std::abs(got.width() - width) <= 1.5f && std::abs(got.height() - height) <= 1.5f;
    };
    EXPECT_TRUE(near(glyphs[0], 13, 10.5f)) << "back " << glyphs[0].ToString();
    EXPECT_TRUE(near(glyphs[1], 13, 10.5f)) << "forward " << glyphs[1].ToString();
    EXPECT_TRUE(near(glyphs[2], 12.5f, 15)) << "reload " << glyphs[2].ToString();
    EXPECT_TRUE(near(glyphs[3], 16, 16)) << "site controls " << glyphs[3].ToString();
    EXPECT_TRUE(near(glyphs[4], 14.5f, 14.5f)) << "split " << glyphs[4].ToString();
    EXPECT_TRUE(near(glyphs[5], 12, 12)) << "close " << glyphs[5].ToString();
    EXPECT_TRUE(near(glyphs[6], 13, 13)) << "link " << glyphs[6].ToString();
    // The host in the darker color, then the port and path in the lighter one.
    EXPECT_EQ(bar->host_text(), u"127.0.0.1");
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(shell().frame_contents(),
                           "!!document.querySelector('[data-shell=\"nav-back\"]')")
               .ExtractBool() == false;
  }));
}

IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest, CluSplitPanesSwap) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  ASSERT_TRUE(multitask().CreateSplit({a, b}, SplitDir::kRow, a).has_value());
  CluSplitHandler handler(window().GetWeakPtr());
  CallFuture swapped;
  handler.Swap(mojom::SplitSwapInput::New(a, b), swapped.GetCallback());
  ASSERT_TRUE(IsOk(swapped.Get()));
  EXPECT_EQ(model().SplitOf(a)->tab_ids, (std::vector<std::string>{b, a}));
  EXPECT_EQ(StripSplitOf(a), (std::vector<std::string>{b, a}));

  // Chromium's own swap (a double click on the divider) reaches the model.
  strip().ReverseTabsInSplit(*window().FindTab(a)->GetSplit());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model().SplitOf(a)->tab_ids == std::vector<std::string>{a, b};
  }));
}

// A split inside a folder stays in that folder, and the sidebar draws its one
// row at the folder's indent, 16 per level:
// in a folder and in a nested folder, and whole under a collapsed
// folder that holds the active split.
IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitInsideAFolderTakesTheFolderIndent) {
  const std::string outer =
      model()
          .mutable_tree()
          .CreateFolder(space(), std::string(), "Outer", std::nullopt,
                        model().NowMs())
          .value();
  const std::string nested =
      model()
          .mutable_tree()
          .CreateFolder(space(), outer, "Nested", std::nullopt, model().NowMs())
          .value();
  model().Commit();
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  const std::string c = Open(Url("tabs/c.html"));
  const std::string d = Open(Url("tabs/a.html", "localhost"));
  ASSERT_TRUE(window()
                  .MoveRows({a, b}, DropTarget::Pinned(space(), outer, 0), false)
                  .has_value());
  ASSERT_TRUE(window()
                  .MoveRows({c, d}, DropTarget::Pinned(space(), nested, 0),
                            false)
                  .has_value());
  base::expected<std::string, std::string> in_folder =
      multitask().CreateSplit({a, b}, SplitDir::kRow, a);
  ASSERT_TRUE(in_folder.has_value());
  base::expected<std::string, std::string> in_nested =
      multitask().CreateSplit({c, d}, SplitDir::kRow, c);
  ASSERT_TRUE(in_nested.has_value());

  // The model keeps every member in its folder.
  EXPECT_EQ(model().tree().Get(a)->parent_id, outer);
  EXPECT_EQ(model().tree().Get(b)->parent_id, outer);
  EXPECT_EQ(model().tree().Get(c)->parent_id, nested);
  EXPECT_EQ(model().tree().Get(d)->parent_id, nested);

  // The split row's level and its left edge from the folder row's.
  const auto indent_of = [&](const std::string& split_id) {
    return content::EvalJs(
               shell().frame_contents(),
               content::JsReplace(
                   R"((() => {
                     const row = document.querySelector(
                         `[data-split-id="${$1}"]`);
                     const folder = document.querySelector(
                         `[data-folder-id="${$2}"]`);
                     if (!row || !folder) return 'missing';
                     return row.getAttribute('aria-level') + ' ' +
                         Math.round(row.getBoundingClientRect().left -
                                    folder.getBoundingClientRect().left) +
                         ' ' + row.querySelectorAll('[data-split-member]')
                                   .length;
                   })())",
                   split_id, outer))
        .ExtractString();
  };
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return indent_of(in_folder.value()) == "2 16 2"; }))
      << indent_of(in_folder.value());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return indent_of(in_nested.value()) == "3 32 2"; }))
      << indent_of(in_nested.value());

  // Collapsed, the nested folder still shows the active split, whole, at its
  // child indent; the collapsed outer folder shows it at its own child indent.
  ASSERT_EQ(window().ActiveRowId(), c);
  CluFoldersHandler folders(window().GetWeakPtr());
  CallFuture collapse_nested;
  folders.SetCollapsed(mojom::FoldersSetCollapsedInput::New(nested, true),
                       collapse_nested.GetCallback());
  ASSERT_TRUE(IsOk(collapse_nested.Get()));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return indent_of(in_nested.value()) == "3 32 2"; }))
      << indent_of(in_nested.value());
  CallFuture collapse_outer;
  folders.SetCollapsed(mojom::FoldersSetCollapsedInput::New(outer, true),
                       collapse_outer.GetCallback());
  ASSERT_TRUE(IsOk(collapse_outer.Get()));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return indent_of(in_nested.value()) == "2 16 2"; }))
      << indent_of(in_nested.value());
  EXPECT_EQ(indent_of(in_folder.value()), "missing");
}

// A split holds up to 4 panes. A third and a
// fourth tab join (a drop on a pane edge, Open in Split View), each pane shows
// at an even share beside the others, a fifth is refused with the toast, and
// closing a pane keeps the rest a split.
IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitHoldsUpToFourPanes) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  const std::string c = Open(Url("tabs/c.html"));
  const std::string d = Open(Url("tabs/a.html?d"));
  const std::string e = Open(Url("tabs/b.html?e"));
  ASSERT_TRUE(multitask().CreateSplit({a, b}, SplitDir::kRow, a).has_value());
  CluSplitHandler handler(window().GetWeakPtr());

  // A tab dropped on b's right edge is the third pane, after b.
  CallFuture third;
  handler.DropTab(mojom::SplitDropTabInput::New(
                      c, b, mojom::SplitDropTabInputEdge::kRight),
                  third.GetCallback());
  ASSERT_TRUE(IsOk(third.Get()));
  EXPECT_EQ(model().SplitOf(a)->tab_ids, (std::vector<std::string>{a, b, c}));
  // Chromium's two-pane split lets go; the shell shows all three.
  EXPECT_FALSE(window().FindTab(a)->GetSplit());
  EXPECT_TRUE(shell().has_extra_panes());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().SplitPaneRects().size() == 3u; }));

  // Open in Split View on a fourth tab adds a pane instead of replacing one.
  ASSERT_TRUE(multitask().OpenRowInSplit(d).has_value());
  EXPECT_EQ(model().SplitOf(a)->tab_ids,
            (std::vector<std::string>{a, b, c, d}));
  EXPECT_EQ(window().ActiveRowId(), d);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().SplitPaneRects().size() == 4u; }));
  {
    const std::vector<CluShell::PaneRect> panes = shell().SplitPaneRects();
    for (size_t i = 0; i < panes.size(); ++i) {
      EXPECT_EQ(panes[i].contents, window().FindTab(
                                       model().SplitOf(a)->tab_ids[i])
                                       ->GetContents());
      EXPECT_FALSE(panes[i].rect.IsEmpty());
      if (i > 0) {
        // Side by side, left to right, even shares, none overlapping.
        EXPECT_GT(panes[i].rect.x(), panes[i - 1].rect.right());
        EXPECT_NEAR(panes[i].rect.width(), panes[0].rect.width(), 1);
        EXPECT_EQ(panes[i].rect.y(), panes[0].rect.y());
      }
    }
  }
  // The frame draws a bar above each hosted pane (back, forward, URL, close).
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::ListValue* panes =
        WindowState().FindListByDottedPath("splitView.panes");
    return panes && panes->size() == 4u &&
           panes->front().GetDict().FindIntByDottedPath("bar.height") ==
               CluShell::kPaneBarHeight;
  }));
  // That bar is the pane toolbar, as tall as the native two-pane bar.
  EXPECT_EQ(CluShell::kPaneBarHeight, CluPaneBar::kHeight);

  // A fifth is refused with the toast, nothing changes.
  CallFuture fifth;
  handler.DropTab(mojom::SplitDropTabInput::New(
                      e, a, mojom::SplitDropTabInputEdge::kLeft),
                  fifth.GetCallback());
  EXPECT_EQ(Reason(fifth.Get()), "split-full");
  EXPECT_EQ(model().SplitOf(a)->tab_ids.size(), 4u);
  EXPECT_FALSE(model().SplitOf(e));
  const base::ListValue* toasts = WindowState().FindList("toasts");
  ASSERT_FALSE(toasts->empty());
  EXPECT_EQ(*toasts->back().GetDict().FindString("text"), kSplitFullToast);

  // A divider drag resizes the panes it sits between; a double click evens
  // them out again.
  browser()->GetWindow()->SetBounds(gfx::Rect(0, 0, 1600, 1000));
  const std::string split_id = model().SplitOf(a)->id;
  const std::vector<double> wider = {0.28, 0.24, 0.24, 0.24};
  ASSERT_TRUE(multitask()
                  .Resize(split_id, base::span<const int32_t>(), wider)
                  .has_value());
  EXPECT_EQ(model().SplitOf(a)->sizes, wider);
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::vector<CluShell::PaneRect> panes = shell().SplitPaneRects();
    return panes.size() == 4u &&
           panes[0].rect.width() > panes[1].rect.width() + 20;
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::ListValue* dividers =
        WindowState().FindListByDottedPath("splitView.dividers");
    return dividers && dividers->size() == 3u;
  }));
  ASSERT_TRUE(
      multitask().Equalize(split_id, base::span<const int32_t>()).has_value());
  EXPECT_EQ(model().SplitOf(a)->sizes, std::vector<double>(4, 0.25));

  // Focusing another pane keeps all four on screen.
  ASSERT_TRUE(multitask().FocusPaneTab(b));
  EXPECT_EQ(window().ActiveRowId(), b);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().SplitPaneRects().size() == 4u; }));

  // Closing a pane keeps the other three a split.
  ASSERT_TRUE(multitask().RemovePane(c, /*close=*/true).has_value());
  ASSERT_TRUE(base::test::RunUntil([&] {
    const SplitRow* split = model().SplitOf(a);
    return split &&
           split->tab_ids == std::vector<std::string>{a, b, d} &&
           shell().SplitPaneRects().size() == 3u;
  }));

  // Separating one more leaves two panes in Chromium's own split.
  ASSERT_TRUE(multitask().RemovePane(d, /*close=*/false).has_value());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return StripSplitOf(a) == std::vector<std::string>{a, b} &&
           !shell().has_extra_panes();
  }));
  EXPECT_FALSE(model().SplitOf(d));
}

IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitShortcutsFocusPanesAndSeparate) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  ASSERT_TRUE(multitask().CreateSplit({a, b}, SplitDir::kRow, a).has_value());
  ASSERT_EQ(window().ActiveRowId(), a);
  // Ctrl+Shift+] and [ step with wrap; Ctrl+Shift+1 and 2 pick a pane; 3 does
  // nothing.
  EXPECT_TRUE(window().RunCommand("split.next"));
  EXPECT_EQ(window().ActiveRowId(), b);
  EXPECT_TRUE(window().RunCommand("split.next"));
  EXPECT_EQ(window().ActiveRowId(), a);
  EXPECT_TRUE(window().RunCommand("split.focus2"));
  EXPECT_EQ(window().ActiveRowId(), b);
  EXPECT_FALSE(window().CanRunCommand("split.focus3"));
  // The panes stay one Chromium split while the focus moves.
  EXPECT_EQ(StripSplitOf(a), (std::vector<std::string>{a, b}));
  // Ctrl+Shift+- separates the focused pane: two normal rows, the other pane on
  // screen.
  EXPECT_TRUE(window().RunCommand("split.separate"));
  EXPECT_FALSE(model().SplitOf(a));
  EXPECT_FALSE(model().tree().Get(b)->split_id);
  EXPECT_TRUE(StripSplitOf(a).empty());
  EXPECT_EQ(window().ActiveRowId(), a);
  EXPECT_FALSE(window().CanRunCommand("split.separate"));
}

// View ▸ Expand Current Split gives the focused pane all the room the
// other pane's minimum leaves; again, the panes are even. Without a split on
// screen the item is disabled.
IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitExpandCurrentSplitTogglesTheFocusedPane) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  EXPECT_FALSE(window().CanRunCommand("split.expand"));
  ASSERT_TRUE(multitask().CreateSplit({a, b}, SplitDir::kRow, a).has_value());
  ASSERT_EQ(window().ActiveRowId(), a);
  EXPECT_TRUE(window().CanRunCommand("split.expand"));
  ASSERT_TRUE(window().RunCommand("split.expand"));
  const SplitRow* split = model().SplitOf(a);
  ASSERT_TRUE(split);
  const int room = shell().layout().page_rect.width();
  EXPECT_GT(split->ratio, 0.5);
  EXPECT_GE((1 - split->ratio) * room, kMinPaneWidth - 1);
  ASSERT_TRUE(window().RunCommand("split.expand"));
  EXPECT_NEAR(model().SplitOf(a)->ratio, 0.5, 1e-9);
  // The second pane focused expands the other way.
  ASSERT_TRUE(window().RunCommand("split.focus2"));
  ASSERT_TRUE(window().RunCommand("split.expand"));
  EXPECT_LT(model().SplitOf(a)->ratio, 0.5);
  ASSERT_TRUE(window().RunCommand("split.separate"));
  EXPECT_FALSE(window().CanRunCommand("split.expand"));
}

IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitClosingAPaneOrUnsplittingInChromiumEndsIt) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  const std::string c = Open(Url("tabs/c.html"));
  ASSERT_TRUE(multitask().CreateSplit({a, b}, SplitDir::kRow, a).has_value());
  // Chromium's split menu ends the split: the model follows.
  strip().RemoveSplit(*window().FindTab(a)->GetSplit());
  ASSERT_TRUE(base::test::RunUntil([&] { return !model().SplitOf(a); }));
  // A split made in Chromium (a link dropped on the page's drop target) joins
  // the model.
  const int ia = strip().GetIndexOfWebContents(Contents(a));
  const int ic = strip().GetIndexOfWebContents(Contents(c));
  strip().AddToNewSplit({std::min(ia, ic), std::max(ia, ic)},
                        split_tabs::SplitTabVisualData(),
                        split_tabs::SplitTabCreatedSource::kDragAndDropLink);
  ASSERT_TRUE(base::test::RunUntil([&] { return model().SplitOf(c); }));
  // Closing a pane's tab ends the split; the other pane stays a normal tab.
  ASSERT_TRUE(window().ActivateRow(c));
  window().CloseRowCommand(c);
  ASSERT_TRUE(base::test::RunUntil([&] { return !model().SplitOf(a); }));
  EXPECT_TRUE(model().tree().Get(a));
  EXPECT_FALSE(model().tree().Get(a)->split_id);
}

// A split shown in another window moves there whole: the window it left must
// not end it when its strip lets go of the pages.
IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitMovesToAnotherWindowAndStaysOneSplit) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  const std::string id =
      multitask().CreateSplit({a, b}, SplitDir::kRow, a).value_or("");
  ASSERT_FALSE(id.empty());
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  CluWindowState* second = CluWindowState::GetOrCreate(other);
  ASSERT_TRUE(second);
  ASSERT_TRUE(base::test::RunUntil([&] { return second->model(); }));
  ASSERT_EQ(second->space_id(), window().space_id());
  ASSERT_TRUE(second->ActivateRow(a));
  ASSERT_TRUE(second->FindTab(a) && second->FindTab(b));
  EXPECT_FALSE(window().FindTab(a));
  // The first window's posted strip change has run.
  content::RunAllTasksUntilIdle();
  ASSERT_TRUE(model().SplitOf(a));
  EXPECT_EQ(model().SplitOf(a)->id, id);
  EXPECT_EQ(model().tree().Get(b)->split_id, id);
  ASSERT_TRUE(second->FindTab(a)->GetSplit());
  EXPECT_EQ(second->FindTab(a)->GetSplit(), second->FindTab(b)->GetSplit());
}

// A PRE_ test and its main test run in two processes, so the rows use file:
// URLs (the server's port changes).
// A pane keeps a 220 DIP minimum, and only the
// focused pane is outlined, in the space accent.
IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitKeepsA220MinimumAndOutlinesOnlyTheFocusedPane) {
  const std::string a = Open(Url("tabs/a.html"));
  const std::string b = Open(Url("tabs/b.html"));
  const std::string id =
      multitask().CreateSplit({a, b}, SplitDir::kRow, a).value_or("");
  ASSERT_FALSE(id.empty());
  EXPECT_EQ(kMinPaneWidth, 220);
  CluSplitHandler handler(window().GetWeakPtr());
  CallFuture resized;
  handler.Resize(mojom::SplitResizeInput::New(id, std::vector<int32_t>{},
                                              std::vector<double>{0.01, 0.99}),
                 resized.GetCallback());
  ASSERT_TRUE(IsOk(resized.Get()));
  const int room = shell().layout().page_rect.width();
  ASSERT_GE(room, 2 * kMinPaneWidth);
  EXPECT_NEAR(model().FindSplit(id)->ratio * room, kMinPaneWidth, 1.0);

  const ui::ColorProvider* colors = PaneView()->GetColorProvider();
  ASSERT_TRUE(colors);
  EXPECT_EQ(colors->GetColor(ContentsContainerOutline::GetColor(
                /*is_active=*/false, /*is_highlighted=*/false)),
            SK_ColorTRANSPARENT);
  EXPECT_NE(colors->GetColor(ContentsContainerOutline::GetColor(
                /*is_active=*/true, /*is_highlighted=*/false)),
            SK_ColorTRANSPARENT);
}

IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       PRE_CluSplitTheDividerResizesAndPersists) {
  const std::string a = Open(FileUrl("tabs/a.html"));
  const std::string b = Open(FileUrl("tabs/b.html"));
  const std::string id =
      multitask().CreateSplit({a, b}, SplitDir::kRow, a).value_or("");
  ASSERT_FALSE(id.empty());
  // split.resize from the bar: the share reaches Chromium's split.
  CluSplitHandler handler(window().GetWeakPtr());
  CallFuture resized;
  handler.Resize(mojom::SplitResizeInput::New(id, std::vector<int32_t>{},
                                              std::vector<double>{0.4, 0.6}),
                 resized.GetCallback());
  ASSERT_TRUE(IsOk(resized.Get()));
  split_tabs::SplitTabData* data =
      strip().GetSplitData(*window().FindTab(a)->GetSplit());
  EXPECT_NEAR(data->visual_data()->split_ratio(), 0.4, 1e-9);
  // Sizes that do not sum to 1 are refused.
  CallFuture junk;
  handler.Resize(mojom::SplitResizeInput::New(id, std::vector<int32_t>{},
                                              std::vector<double>{0.6, 0.6}),
                 junk.GetCallback());
  EXPECT_EQ(junk.Get()->get_error()->code, mojom::ErrorCode::kBadPayload);
  // Chromium's divider drag (MultiContentsViewDelegateImpl::ResizeWebContents)
  // sends each step as intermediate, and the release repeats the last ratio
  // with done, which UpdateSplitRatio drops as no change. So every step is
  // stored; the write to clu.db waits for the drag to rest.
  const split_tabs::SplitTabId chromium = *window().FindTab(a)->GetSplit();
  strip().UpdateSplitRatio(chromium, 0.35, /*is_intermediate=*/true);
  EXPECT_NEAR(model().FindSplit(id)->ratio, 0.35, 1e-9);
  strip().UpdateSplitRatio(chromium, 0.45, /*is_intermediate=*/true);
  strip().UpdateSplitRatio(chromium, 0.45, /*is_intermediate=*/false);
  EXPECT_NEAR(model().FindSplit(id)->ratio, 0.45, 1e-9);
  // Showing the other pane keeps the share.
  ASSERT_TRUE(window().ActivateRow(b));
  EXPECT_NEAR(strip().GetSplitData(chromium)->visual_data()->split_ratio(),
              0.45, 1e-9);
  // The share lands in clu.db 500 ms after the last change.
  base::RunLoop wait;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, wait.QuitClosure(),
      CluModelService::kSplitResizeWrite + base::Milliseconds(100));
  wait.Run();
  base::RunLoop flushed;
  model().FlushForTesting(flushed.QuitClosure());
  flushed.Run();
}

IN_PROC_BROWSER_TEST_F(CluMultitaskBrowserTest,
                       CluSplitTheDividerResizesAndPersists) {
  // After the relaunch the split comes back with its share, and showing it
  // makes the Chromium split again.
  ASSERT_EQ(model().splits().size(), 1u);
  const SplitRow split = model().splits().begin()->second;
  EXPECT_NEAR(split.ratio, 0.45, 1e-9);
  ASSERT_TRUE(window().ActivateRow(split.tab_ids[0]));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return StripSplitOf(split.tab_ids[0]).size() == 2u; }));
  EXPECT_NEAR(strip()
                  .GetSplitData(*window().FindTab(split.tab_ids[0])->GetSplit())
                  ->visual_data()
                  ->split_ratio(),
              0.45, 1e-9);
}

// 12.9 box 2: auto picture-in-picture ---------------------------------------------------------------------------------

class CluMediaBrowserTest : public CluMultitaskBrowserTest {
 protected:
  // A clip page, playing with sound from a click.
  std::string Play(const std::string& page) {
    const std::string id = Open(FileUrl(page));
    content::WebContents* contents = Contents(id);
    content::MediaStartStopObserver started(
        contents, content::MediaStartStopObserver::Type::kStart);
    EXPECT_TRUE(
        content::ExecJs(contents, "document.getElementById('play').click()"));
    started.Wait();
    return id;
  }

  // Chromium's auto-PiP takes a clip once it is playing, holds audio focus and
  // has been heard (its own browser tests wait for the same).
  void WaitUntilHeard(content::WebContents* contents) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      const media::PictureInPictureEventsInfo::AutoPipInfo info =
          AutoPictureInPictureTabHelper::FromWebContents(contents)
              ->GetAutoPipInfo();
      return info.has_audio_focus && info.is_playing &&
             info.was_recently_audible;
    }));
  }

  // Waits a bounded time for nothing to happen (the negative auto-PiP cases).
  void Settle() {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Seconds(1));
    loop.Run();
  }

  std::optional<std::string> Pip(const std::string& row_id) {
    const base::DictValue state = window().CurrentState();
    const base::Value* pip =
        state.FindDict("tabs")->FindDict(row_id)->Find("pip");
    return pip && pip->is_string() ? std::optional(pip->GetString())
                                   : std::nullopt;
  }
};

IN_PROC_BROWSER_TEST_F(CluMediaBrowserTest,
                       CluMediaAutoPipEntersOnLeavingAndExitsOnReturn) {
  const std::string video = Play("split/clip.html");
  const std::string other = Open(Url("tabs/a.html"), /*activate=*/false);
  content::WebContents* contents = Contents(video);
  WaitUntilHeard(contents);
  content::MediaStartStopObserver entered(
      contents, content::MediaStartStopObserver::Type::kEnterPictureInPicture);
  ASSERT_TRUE(window().ActivateRow(other));
  entered.Wait();
  EXPECT_TRUE(contents->HasPictureInPictureVideo());
  ASSERT_TRUE(base::test::RunUntil([&] { return Pip(video) == "auto"; }));

  content::MediaStartStopObserver left(
      contents, content::MediaStartStopObserver::Type::kExitPictureInPicture);
  ASSERT_TRUE(window().ActivateRow(video));
  left.Wait();
  EXPECT_FALSE(contents->HasPictureInPictureVideo());
  ASSERT_TRUE(base::test::RunUntil([&] { return !Pip(video); }));
}

IN_PROC_BROWSER_TEST_F(CluMediaBrowserTest,
                       CluMediaNeverForMutedTinyOrOptedOutVideo) {
  for (const char* page : {"split/clip-muted.html", "split/clip-tiny.html",
                           "split/clip-nopip.html"}) {
    const std::string video = Play(page);
    const std::string other = Open(Url("tabs/a.html"));
    Settle();
    EXPECT_FALSE(Contents(video)->HasPictureInPictureVideo()) << page;
    EXPECT_FALSE(Pip(video)) << page;
    ASSERT_TRUE(window().ActivateRow(video));
  }
}

IN_PROC_BROWSER_TEST_F(CluMediaBrowserTest,
                       CluMediaSwitchingPanesOfOneSplitNeverEntersPip) {
  const std::string video = Play("split/clip.html");
  WaitUntilHeard(Contents(video));
  const std::string other = Open(Url("tabs/a.html"), /*activate=*/false);
  ASSERT_TRUE(multitask()
                  .CreateSplit({video, other}, SplitDir::kRow, video)
                  .has_value());
  ASSERT_TRUE(multitask().FocusPaneTab(other));
  Settle();
  EXPECT_FALSE(Contents(video)->HasPictureInPictureVideo());
}

IN_PROC_BROWSER_TEST_F(CluMediaBrowserTest,
                       CluMediaThePipWindowsBackToTabActivatesTheTab) {
  const std::string video = Play("split/clip.html");
  WaitUntilHeard(Contents(video));
  const std::string other = Open(Url("tabs/a.html"), /*activate=*/false);
  content::MediaStartStopObserver entered(
      Contents(video),
      content::MediaStartStopObserver::Type::kEnterPictureInPicture);
  ASSERT_TRUE(window().ActivateRow(other));
  entered.Wait();
  // The PiP window's back-to-tab button.
  ASSERT_TRUE(PictureInPictureWindowManager::GetInstance()
                  ->ExitPictureInPictureViaWindowUi(
                      PictureInPictureWindowManager::UiBehavior::
                          kCloseWindowAndFocusOpener));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == video; }));
  EXPECT_EQ(*WindowState().FindString("activeTabId"), video);
}

// A site's playing video pops out when its tab stops showing, and General's "Picture in picture when
// leaving a tab" turns it off. The clip comes from an https server, as on a real site, so Chromium's Safe Browsing URL
// check runs before the auto-PiP; its verdicts come from a fake database, so the check answers at once.
class CluMediaAnySiteBrowserTest : public CluMediaBrowserTest {
 protected:
  void CreatedBrowserMainParts(content::BrowserMainParts* parts) override {
    CluMediaBrowserTest::CreatedBrowserMainParts(parts);
    factory_.SetTestDatabaseManager(
        base::MakeRefCounted<safe_browsing::FakeSafeBrowsingDatabaseManager>(
            content::GetUIThreadTaskRunner({})));
    safe_browsing::SafeBrowsingService::RegisterFactory(&factory_);
  }

  void TearDown() override {
    CluMediaBrowserTest::TearDown();
    safe_browsing::SafeBrowsingService::RegisterFactory(nullptr);
  }

  // The clip from the https server, playing with sound from a click.
  std::string PlayOverHttps() {
    const std::string id = Open(HttpsUrl("split/clip.html"));
    content::WebContents* contents = Contents(id);
    content::MediaStartStopObserver started(
        contents, content::MediaStartStopObserver::Type::kStart);
    EXPECT_TRUE(
        content::ExecJs(contents, "document.getElementById('play').click()"));
    started.Wait();
    return id;
  }

  ContentSetting AutoPipDefault() {
    return HostContentSettingsMapFactory::GetForProfile(browser()->GetProfile())
        ->GetDefaultContentSetting(ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
  }

  void SetUpOnMainThread() override {
    CluMediaBrowserTest::SetUpOnMainThread();
    https_.ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(https_.Start());
  }

  GURL HttpsUrl(const std::string& page) {
    return https_.GetURL(base::StrCat({"/", page}));
  }

  safe_browsing::TestSafeBrowsingServiceFactory factory_;
  net::EmbeddedTestServer https_{net::EmbeddedTestServer::TYPE_HTTPS};
};

IN_PROC_BROWSER_TEST_F(CluMediaAnySiteBrowserTest,
                       CluMediaAutoPipWorksOnAnHttpsSite) {
  // On by default: the profile allows auto-PiP, so there is no prompt.
  EXPECT_TRUE(CluAppSettings::Get()->GetBool(kAutoPictureInPictureKey));
  EXPECT_EQ(AutoPipDefault(), CONTENT_SETTING_ALLOW);
  const std::string video = PlayOverHttps();
  ASSERT_TRUE(Contents(video)->GetLastCommittedURL().SchemeIs("https"));
  const std::string other = Open(Url("tabs/a.html"), /*activate=*/false);
  content::WebContents* contents = Contents(video);
  WaitUntilHeard(contents);
  content::MediaStartStopObserver entered(
      contents, content::MediaStartStopObserver::Type::kEnterPictureInPicture);
  ASSERT_TRUE(window().ActivateRow(other));
  entered.Wait();
  EXPECT_TRUE(contents->HasPictureInPictureVideo());
  ASSERT_TRUE(base::test::RunUntil([&] { return Pip(video) == "auto"; }));

  content::MediaStartStopObserver left(
      contents, content::MediaStartStopObserver::Type::kExitPictureInPicture);
  ASSERT_TRUE(window().ActivateRow(video));
  left.Wait();
  EXPECT_FALSE(contents->HasPictureInPictureVideo());
}

// A playing https video in a split-view pane pops out when the window leaves the split for another tab, whichever
// pane had the focus, and goes back when the split shows again. (The 1142 check that saw nothing ran on an http page,
// which Chromium never pops out.)
class CluMediaSplitPaneBrowserTest : public CluMediaAnySiteBrowserTest,
                                     public testing::WithParamInterface<bool> {};

IN_PROC_BROWSER_TEST_P(CluMediaSplitPaneBrowserTest,
                       CluMediaAutoPipWorksWhenLeavingASplitPane) {
  const bool video_focused = GetParam();
  const std::string video = PlayOverHttps();
  content::WebContents* contents = Contents(video);
  WaitUntilHeard(contents);
  const std::string pane = Open(Url("tabs/a.html"), /*activate=*/false);
  ASSERT_TRUE(multitask()
                  .CreateSplit({video, pane}, SplitDir::kRow,
                               video_focused ? video : pane)
                  .has_value());
  ASSERT_TRUE(multitask().FocusPaneTab(video_focused ? video : pane));
  EXPECT_FALSE(contents->HasPictureInPictureVideo());
  const std::string other = Open(Url("tabs/b.html"), /*activate=*/false);
  content::MediaStartStopObserver entered(
      contents, content::MediaStartStopObserver::Type::kEnterPictureInPicture);
  ASSERT_TRUE(window().ActivateRow(other));
  entered.Wait();
  EXPECT_TRUE(contents->HasPictureInPictureVideo());
  ASSERT_TRUE(base::test::RunUntil([&] { return Pip(video) == "auto"; }));

  content::MediaStartStopObserver left(
      contents, content::MediaStartStopObserver::Type::kExitPictureInPicture);
  ASSERT_TRUE(window().ActivateRow(video));
  left.Wait();
  EXPECT_FALSE(contents->HasPictureInPictureVideo());
}

INSTANTIATE_TEST_SUITE_P(All, CluMediaSplitPaneBrowserTest, testing::Bool());

IN_PROC_BROWSER_TEST_F(CluMediaAnySiteBrowserTest,
                       CluMediaTheSettingOffKeepsTheVideoInItsTab) {
  CluAppSettings::Get()->SetGlobal(kAutoPictureInPictureKey,
                                   base::Value(false));
  EXPECT_EQ(AutoPipDefault(), CONTENT_SETTING_BLOCK);
  const std::string video = PlayOverHttps();
  WaitUntilHeard(Contents(video));
  const std::string other = Open(Url("tabs/a.html"));
  Settle();
  EXPECT_FALSE(Contents(video)->HasPictureInPictureVideo());
  EXPECT_FALSE(Pip(video));
  // Back on: allowed again.
  CluAppSettings::Get()->SetGlobal(kAutoPictureInPictureKey, base::Value(true));
  EXPECT_EQ(AutoPipDefault(), CONTENT_SETTING_ALLOW);
}

IN_PROC_BROWSER_TEST_F(CluMediaAnySiteBrowserTest,
                       CluMediaNeverForAPageSafeBrowsingFlags) {
  const GURL page = HttpsUrl("split/clip.html");
  static_cast<safe_browsing::FakeSafeBrowsingDatabaseManager*>(
      factory_.test_safe_browsing_service()->database_manager().get())
      ->AddDangerousUrl(page, safe_browsing::SBThreatType::SB_THREAT_TYPE_URL_MALWARE);
  // The flagged page shows Chromium's interstitial instead of the clip, so nothing plays and nothing pops out.
  const std::string video = Open(page);
  const std::string other = Open(Url("tabs/a.html"));
  Settle();
  EXPECT_FALSE(Contents(video)->HasPictureInPictureVideo());
  EXPECT_FALSE(Pip(video));
}

// 12.9 box 3: the media card ------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluMediaBrowserTest,
                       CluMediaTheCardShowsTogglesAndJumpsToABackgroundTab) {
  const std::string audio = Play("split/audio.html");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Contents(audio)->IsCurrentlyAudible(); }));
  const std::string other = Open(Url("tabs/a.html"));
  // Out of sight and heard: the card shows it, playing.
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::string* tab = WindowState().FindString("mediaTabId");
    return tab && *tab == audio;
  }));
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, base::StrCat({".clu-media-card[data-media-tab-id=\"",
                                       audio, "\"][data-state=\"playing\"]"}));
  CluMediaHandler handler(window().GetWeakPtr());
  // Pause from the card: the card stays, with Play.
  CallFuture paused;
  handler.Toggle(mojom::MediaToggleInput::New(audio), paused.GetCallback());
  ASSERT_TRUE(IsOk(paused.Get()));
  WaitForSelector(frame, ".clu-media-card[data-state=\"paused\"]");
  EXPECT_EQ(true, content::EvalJs(Contents(audio),
                                  "document.querySelector('audio').paused"));
  CallFuture played;
  handler.Toggle(mojom::MediaToggleInput::New(audio), played.GetCallback());
  ASSERT_TRUE(IsOk(played.Get()));
  WaitForSelector(frame, ".clu-media-card[data-state=\"playing\"]");
  // Previous and next go to the page's media session handlers.
  ASSERT_TRUE(content::ExecJs(
      Contents(audio),
      "navigator.mediaSession.setActionHandler('previoustrack', () => "
      "document.title = 'previous');"
      "navigator.mediaSession.setActionHandler('nexttrack', () => "
      "document.title = 'next');"));
  CallFuture previous;
  handler.Previous(mojom::MediaPreviousInput::New(audio),
                   previous.GetCallback());
  ASSERT_TRUE(IsOk(previous.Get()));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Contents(audio)->GetTitle() == u"previous"; }));
  CallFuture next;
  handler.Next(mojom::MediaNextInput::New(audio), next.GetCallback());
  ASSERT_TRUE(IsOk(next.Get()));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Contents(audio)->GetTitle() == u"next"; }));
  // The title brings the tab back, and the card goes.
  CallFuture focused;
  handler.Focus(mojom::MediaFocusInput::New(audio), focused.GetCallback());
  ASSERT_TRUE(IsOk(focused.Get()));
  EXPECT_EQ(window().ActiveRowId(), audio);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return WindowState().Find("mediaTabId")->is_none(); }));
  // A tab with no media has nothing to toggle.
  CallFuture none;
  handler.Toggle(mojom::MediaToggleInput::New(other), none.GetCallback());
  EXPECT_EQ(none.Get()->get_error()->code, mojom::ErrorCode::kNotFound);
}

// 12.10 box 1: Peek ---------------------------------------------------------------------------------------------------

class CluPeekBrowserTest : public CluMultitaskBrowserTest {
 protected:
  CluPeek& peek() { return multitask().peek(); }

  // Clicks the link `id` in `contents` like a user, with `modifiers`.
  void Click(content::WebContents* contents,
             const std::string& id,
             int modifiers = blink::WebInputEvent::kNoModifiers) {
    // Paint holding drops input right after a load (browser_test_utils.h), and so does a page whose hit-test data
    // has not reached viz yet (a row just activated); the click must reach the page.
    content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(contents);
    content::WaitForHitTestData(contents->GetPrimaryMainFrame());
    const gfx::Point at = gfx::ToFlooredPoint(
        content::GetCenterCoordinatesOfElementWithId(contents, id));
    content::SimulateMouseClickAt(contents, modifiers,
                                  blink::WebMouseEvent::Button::kLeft, at);
  }

  // A key chord from the keyboard into `contents`: down, char, up through its
  // RenderWidgetHost, which asks the browser first (PreHandleKeyboardEvent) as
  // for a key from the OS. content::SimulateKeyPress() marks its events
  // skip_if_unhandled, which skips the browser's pre-handling.
  void PressKey(content::WebContents* contents,
                ui::DomKey key,
                ui::DomCode code,
                ui::KeyboardCode key_code,
                int modifiers) {
    content::RenderWidgetHost* widget =
        contents->GetPrimaryMainFrame()->GetRenderWidgetHost();
    for (blink::WebInputEvent::Type type :
         {blink::WebInputEvent::Type::kRawKeyDown,
          blink::WebInputEvent::Type::kChar,
          blink::WebInputEvent::Type::kKeyUp}) {
      input::NativeWebKeyboardEvent event(type, modifiers,
                                          base::TimeTicks::Now());
      event.dom_key = key;
      event.dom_code = static_cast<int>(code);
      event.windows_key_code = key_code;
      if (type != blink::WebInputEvent::Type::kKeyUp && key.IsCharacter()) {
        event.text[0] = key.ToCharacter();
        event.unmodified_text[0] = key.ToCharacter();
      }
      widget->ForwardKeyboardEvent(event);
    }
  }

  void WaitForPeek() {
    ASSERT_TRUE(base::test::RunUntil([&] { return multitask().has_peek(); }));
    ASSERT_TRUE(content::WaitForLoadStop(peek().page()));
    ASSERT_TRUE(
        base::test::RunUntil([&] { return peek().attached_for_testing(); }));
  }

  content::WebContents* UnderPeek() {
    return shell().peek_view() ? shell().peek_view()->under_contents()
                               : nullptr;
  }

  // A Peek from pinned row `pinned` on `url`, attached and with focus in its
  // page, as after the open animation (CluPeek::Attach).
  void PeekWithFocus(const std::string& pinned, const GURL& url) {
    peek().Open(pinned, url, content::Referrer());
    WaitForPeek();
    shell().peek_view()->page_view()->RequestFocus();
    ASSERT_TRUE(
        base::test::RunUntil([&] { return peek().TakesPageCommands(); }));
  }

  FindBarController& Find() { return *FindBarController::From(browser()); }
};

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekOpensForAnotherHostFromAPinnedTabOnly) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  content::WebContents* source = Contents(pinned);
  const GURL before = source->GetLastCommittedURL();
  // Same host: the pinned tab navigates.
  content::TestNavigationObserver same(source);
  Click(source, "same");
  same.Wait();
  EXPECT_FALSE(multitask().has_peek());
  source->GetController().GoBack();
  EXPECT_TRUE(content::WaitForLoadStop(source));

  // Another host: the pinned tab stays, the Peek shows the target, and the
  // sidebar's URL pill shows its host (M11).
  Click(source, "cross");
  WaitForPeek();
  EXPECT_EQ(source->GetLastCommittedURL(), before);
  EXPECT_EQ(peek().page()->GetLastCommittedURL().host(), "localhost");
  EXPECT_EQ(*WindowState().FindStringByDottedPath("peek.host"), "localhost");
  WaitForSelector(UnderPeek(), "[data-peek-card]");
  WaitForSelector(shell().frame_contents(), "[data-shell=\"url-pill\"][data-peek]");
  EXPECT_EQ(base::StrCat({"localhost:",
                          peek().page()->GetLastCommittedURL().port()}),
            content::EvalJs(shell().frame_contents(),
                            "document.querySelector('[data-shell=\"url-host\"]')."
                            "textContent.trim()"));
  // The pinned row that opened it shows the peeked page's favicon at its right end (peek/peek-card.png).
  EXPECT_TRUE(base::StartsWith(*WindowState().FindStringByDottedPath("peek.favicon"),
                               "chrome://favicon2/"));
  WaitForSelector(shell().frame_contents(),
                  base::StrCat({"nav.clu-sidebar [data-tab-id=\"", pinned,
                                "\"] [data-peek-icon]"}));
  // A second link replaces the open Peek: one Peek per window, a new seq.
  const int seq = *WindowState().FindIntByDottedPath("peek.seq");
  peek().Close();
  ASSERT_TRUE(content::WaitForLoadStop(source));
  Click(source, "cross-2");
  WaitForPeek();
  EXPECT_GT(*WindowState().FindIntByDottedPath("peek.seq"), seq);

  // A normal Today tab's plain click navigates.
  peek().Close();
  const std::string today = Open(Url("multitask/links.html"));
  content::TestNavigationObserver plain(Contents(today));
  Click(Contents(today), "cross");
  plain.Wait();
  EXPECT_FALSE(multitask().has_peek());
}

// A window.open from the Peek whose row can't be made (here: its id is already a row) frees the new page, so the Peek
// hands Chromium no pointer to it.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekANewWindowWithoutARowReturnsNoContents) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  std::unique_ptr<content::WebContents> opened =
      content::WebContents::Create(
          content::WebContents::CreateParams(browser()->GetProfile()));
  CluTabId::Set(opened.get(), pinned);
  bool blocked = false;
  EXPECT_EQ(peek().AddNewContents(peek().page(), std::move(opened),
                                  Url("multitask/links.html"),
                                  WindowOpenDisposition::kNewForegroundTab,
                                  blink::mojom::WindowFeatures(),
                                  /*user_gesture=*/true, &blocked),
            nullptr);
}

// A new Peek over an open one whose page has committed: a link that serves a
// download closes the Peek, as it does for a first Peek.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekANewPeekOnADownloadCloses) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  peek().Open(pinned, Url("multitask/attachment.txt"), content::Referrer());
  ASSERT_TRUE(base::test::RunUntil([&] { return !multitask().has_peek(); }));
  // The file went to downloads. Cancelled, so quitting at the end of the test
  // asks nothing.
  content::DownloadManager* downloads =
      browser()->GetProfile()->GetDownloadManager();
  std::vector<raw_ptr<download::DownloadItem, VectorExperimental>> items;
  ASSERT_TRUE(base::test::RunUntil([&] {
    items.clear();
    downloads->GetAllDownloads(&items);
    return !items.empty();
  }));
  for (download::DownloadItem* item : items) {
    if (item->GetState() == download::DownloadItem::IN_PROGRESS) {
      item->Cancel(/*user_cancel=*/false);
    }
  }
}

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest, CluPeekNeverTakesAFormPost) {
  const std::string pinned = OpenPinned(Url("multitask/post.html"));
  content::WebContents* source = Contents(pinned);
  content::TestNavigationObserver posted(source);
  Click(source, "submit");
  posted.Wait();
  EXPECT_FALSE(multitask().has_peek());
  EXPECT_EQ(source->GetLastCommittedURL().host(), "localhost");
}

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest, CluPeekAShiftClickPeeksFromAnyTab) {
  const std::string today = Open(Url("multitask/links.html"));
  Click(Contents(today), "same", blink::WebInputEvent::kShiftKey);
  WaitForPeek();
  EXPECT_EQ(peek().source_row_id(), today);
  // No new window, and no new row.
  EXPECT_EQ(GetAllBrowserWindowInterfaces().size(), 1u);
}

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekEscTheScrimAndATabSwitchClose) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  // Esc in the Peek page.
  PressKey(peek().page(), ui::DomKey::ESCAPE, ui::DomCode::ESCAPE,
           ui::VKEY_ESCAPE, blink::WebInputEvent::kNoModifiers);
  ASSERT_TRUE(base::test::RunUntil([&] { return !multitask().has_peek(); }));
  EXPECT_TRUE(WindowState().Find("peek")->is_none());
  // A click on the scrim (the under-peek overlay sends peek.close).
  Click(Contents(pinned), "cross");
  WaitForPeek();
  WaitForSelector(UnderPeek(), "[data-peek-scrim]");
  EXPECT_TRUE(content::ExecJs(
      UnderPeek(),
      "document.querySelector('[data-peek-scrim]').dispatchEvent("
      "new PointerEvent('pointerdown', {bubbles: true}))"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !multitask().has_peek(); }));
  // Switching tabs closes it too.
  Click(Contents(pinned), "cross");
  WaitForPeek();
  Open(Url("tabs/a.html"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !multitask().has_peek(); }));
  // Cmd+W with a Peek open closes the Peek, not the tab.
  ASSERT_TRUE(window().ActivateRow(pinned));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  EXPECT_TRUE(window().RunCommand("tabs.close"));
  EXPECT_FALSE(multitask().has_peek());
  EXPECT_TRUE(Contents(pinned));
}

// A link's hover preview closes when another overlay (here the theme editor) opens; a Peek the
// user clicked open stays.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekAHoverPreviewYieldsToAnotherOverlay) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  EXPECT_FALSE(peek().from_hover());
  window().OpenThemePicker(space(), std::nullopt);
  EXPECT_TRUE(multitask().has_peek());

  // The theme editor opening again (it replaces itself) closes a hover preview.
  peek().Summarize();
  EXPECT_TRUE(peek().from_hover());
  window().OpenThemePicker(space(), std::nullopt);
  EXPECT_FALSE(multitask().has_peek());
}

// A link's hover preview closes when the pointer leaves the link; a Peek the user clicked open stays.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekAHoverPreviewClosesWhenThePointerLeavesTheLink) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  EXPECT_FALSE(peek().from_hover());
  const gfx::Point link(400, 300);
  window().CloseHoverPreviewIfLeft(link, gfx::Point(100, 300));
  EXPECT_TRUE(multitask().has_peek());

  peek().Summarize();
  EXPECT_TRUE(peek().from_hover());
  EXPECT_FALSE(window().CloseHoverPreviewIfLeft(link, gfx::Point(408, 305)));
  EXPECT_TRUE(multitask().has_peek());
  EXPECT_TRUE(window().CloseHoverPreviewIfLeft(link, gfx::Point(100, 300)));
  EXPECT_FALSE(multitask().has_peek());
}

// Opening the command bar (Cmd+T) closes any open Peek, a clicked one too, so the bar shows alone.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest, CluPeekClosesWhenTheCommandBarOpens) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  EXPECT_FALSE(peek().from_hover());
  window().OpenCommandBar(CommandBarMode::kNewTab);
  EXPECT_TRUE(window().command_session().has_value());
  EXPECT_FALSE(multitask().has_peek());
  window().CloseCommandBar();
}

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekOpenAsTabKeepsThePageWithoutAReload) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  content::WebContents* page = peek().page();
  // A second page in the Peek, so the history is worth keeping.
  content::TestNavigationObserver next(page);
  ASSERT_TRUE(content::ExecJs(page, "document.getElementById('next').click()"));
  next.Wait();
  const int entries = page->GetController().GetEntryCount();
  const std::string loads =
      content::EvalJs(page, "sessionStorage.getItem('loads')").ExtractString();
  NavigationCounter navigations(page);

  // Cmd+Shift+O, through Chromium's menu command as the key arrives.
  const CommandSpec* expand = FindCommand("peek.expand");
  ASSERT_TRUE(expand);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), MenuTagFor(*expand)));
  ASSERT_FALSE(multitask().has_peek());

  // The same WebContents is the new top Today row, selected; nothing reloaded,
  // the history came along.
  const std::string row = window().ActiveRowId();
  EXPECT_EQ(Contents(row), page);
  EXPECT_EQ(model().tree().Today(space()).front(), row);
  EXPECT_EQ(navigations.count(), 0);
  EXPECT_EQ(page->GetController().GetEntryCount(), entries);
  EXPECT_EQ(
      loads,
      content::EvalJs(page, "sessionStorage.getItem('loads')").ExtractString());
}

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest, CluPeekOpenInSplitMakesTwoPanes) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  content::WebContents* page = peek().page();
  CluPeekHandler handler(window().GetWeakPtr());
  CallFuture split;
  handler.ToSplit(split.GetCallback());
  ASSERT_TRUE(IsOk(split.Get()));
  EXPECT_FALSE(multitask().has_peek());
  const SplitRow* made = model().SplitOf(pinned);
  ASSERT_TRUE(made);
  ASSERT_EQ(made->tab_ids.size(), 2u);
  EXPECT_EQ(Contents(made->tab_ids[1]), page);
  EXPECT_EQ(StripSplitOf(pinned).size(), 2u);
  EXPECT_TRUE(PaneView()->IsInSplitView());
}

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekTheUnderPeekLayerStaysUnderToastsAndPrompts) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  Click(Contents(pinned), "cross");
  WaitForPeek();
  // The Peek layer is a view of the window; toasts and prompts are child
  // widgets over the window, so they show above it and the Peek stays.
  window().ShowToast("Copied URL", std::nullopt, base::Seconds(5));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return shell().IsPassiveVisible(0); }));
  window().OpenPrompt("pinned-url", base::DictValue());
  EXPECT_TRUE(shell().IsOverlayVisible());
  EXPECT_TRUE(multitask().has_peek());
  EXPECT_TRUE(shell().peek_shown());
  EXPECT_FALSE(WindowState().Find("peek")->is_none());
  // The under-peek surface renders in its own layer, never the top one.
  WaitForSelector(UnderPeek(),
                  "[data-surface=\"peek\"][data-layer=\"under-peek\"]");
  EXPECT_EQ(false, content::EvalJs(
                       shell().overlay_contents(),
                       "!!document.querySelector('[data-surface=\"peek\"]')"));
}

IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekPageCommandsActOnThePeekWhenItHasFocus) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  content::WebContents* source = Contents(pinned);
  Click(source, "cross");
  WaitForPeek();
  NavigationCounter under(source);
  // Cmd+R in the Peek page reloads the Peek, not the pinned tab under it.
  content::TestNavigationObserver reloaded(peek().page());
  PressKey(peek().page(), ui::DomKey::FromCharacter('r'), ui::DomCode::US_R,
           ui::VKEY_R, blink::WebInputEvent::kMetaKey);
  reloaded.Wait();
  EXPECT_EQ(under.count(), 0);
  EXPECT_TRUE(multitask().has_peek());
  // From the menu, nav.reload acts on the Peek while it has focus.
  if (peek().HasFocus()) {
    content::TestNavigationObserver again(peek().page());
    EXPECT_TRUE(window().RunCommand("nav.reload"));
    again.Wait();
    EXPECT_EQ(under.count(), 0);
  }
}

// docs/chromium/m6.md section 7: Chromium's own menu items for page commands,
// picked with the mouse, act on a focused Peek, and so does their enabled state:
// the History menu offers Back for the Peek's history, not the tab's.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekMenuBackForwardAndReloadActOnTheFocusedPeek) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  content::WebContents* source = Contents(pinned);
  ASSERT_FALSE(source->GetController().CanGoBack());
  PeekWithFocus(pinned, Url("multitask/target.html?n=1", "localhost"));
  peek().Open(pinned, Url("multitask/target.html?n=2", "localhost"),
              content::Referrer());
  ASSERT_TRUE(content::WaitForLoadStop(peek().page()));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().attached_for_testing(); }));
  shell().peek_view()->page_view()->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil([&] { return peek().TakesPageCommands(); }));
  ASSERT_TRUE(peek().page()->GetController().CanGoBack());
  NavigationCounter under(source);

  // The menu validates Back against the Peek, which has history; the tab under
  // it has none.
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_BACK));
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_FORWARD));
  {
    content::TestNavigationObserver back(peek().page());
    chrome::ExecuteCommand(browser(), IDC_BACK);
    back.Wait();
  }
  EXPECT_EQ(peek().page()->GetLastCommittedURL().query(), "n=1");
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FORWARD));
  {
    content::TestNavigationObserver forward(peek().page());
    chrome::ExecuteCommand(browser(), IDC_FORWARD);
    forward.Wait();
  }
  EXPECT_EQ(peek().page()->GetLastCommittedURL().query(), "n=2");
  {
    content::TestNavigationObserver reloaded(peek().page());
    chrome::ExecuteCommand(browser(), IDC_RELOAD);
    reloaded.Wait();
  }
  EXPECT_EQ(under.count(), 0);
  EXPECT_TRUE(multitask().has_peek());

  // Once the Peek closes, the items follow the tab again.
  peek().Close();
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_BACK));
  content::TestNavigationObserver tab_reloaded(source);
  chrome::ExecuteCommand(browser(), IDC_RELOAD);
  tab_reloaded.Wait();
  EXPECT_EQ(under.count(), 1);
}

// docs/chromium/m6.md section 7: Cmd+F and Chromium's find bar search the Peek
// page; closing the Peek hands the bar back to the tab.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest, CluPeekFindSearchesThePeekPage) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  content::WebContents* source = Contents(pinned);
  PeekWithFocus(pinned, Url("multitask/target.html?n=1", "localhost"));

  chrome::ExecuteCommand(browser(), IDC_FIND);
  EXPECT_EQ(Find().web_contents(), peek().page());
  EXPECT_TRUE(Find().find_bar()->IsFindBarVisible());
  EXPECT_TRUE(find_in_page::FindTabHelper::FromWebContents(peek().page())
                  ->find_ui_active());
  EXPECT_FALSE(
      find_in_page::FindTabHelper::FromWebContents(source)->find_ui_active());
  // The find bar's own text field has focus now; Find Next still goes to the
  // Peek.
  EXPECT_TRUE(peek().TakesPageCommands());
  chrome::ExecuteCommand(browser(), IDC_FIND_NEXT);
  EXPECT_EQ(Find().web_contents(), peek().page());

  // Closing the Peek ends its find session and gives the bar back to the tab.
  peek().Close();
  EXPECT_EQ(Find().web_contents(), source);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !Find().find_bar()->IsFindBarVisible(); }));
  // Cmd+F on the tab now finds in the tab.
  chrome::ExecuteCommand(browser(), IDC_FIND);
  EXPECT_EQ(Find().web_contents(), source);
  EXPECT_TRUE(
      find_in_page::FindTabHelper::FromWebContents(source)->find_ui_active());
}

// docs/chromium/m6.md section 7: Cmd+plus, Cmd+minus and Cmd+0 zoom the Peek
// page (another host than the tab, so Chromium's per-host zoom keeps the tab
// at 100%).
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest, CluPeekZoomZoomsThePeekPage) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  content::WebContents* source = Contents(pinned);
  PeekWithFocus(pinned, Url("multitask/target.html?n=1", "localhost"));
  zoom::ZoomController* zoom =
      zoom::ZoomController::FromWebContents(peek().page());
  ASSERT_TRUE(zoom);
  const int tab_percent =
      zoom::ZoomController::FromWebContents(source)->GetZoomPercent();

  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_ZOOM_PLUS));
  chrome::ExecuteCommand(browser(), IDC_ZOOM_PLUS);
  ASSERT_TRUE(base::test::RunUntil([&] { return zoom->GetZoomPercent() > 100; }));
  EXPECT_EQ(zoom::ZoomController::FromWebContents(source)->GetZoomPercent(),
            tab_percent);
  // Through Clu's own command path too (the key table, the command bar).
  EXPECT_TRUE(window().RunCommand("zoom.out"));
  ASSERT_TRUE(base::test::RunUntil([&] { return zoom->GetZoomPercent() == 100; }));
  chrome::ExecuteCommand(browser(), IDC_ZOOM_MINUS);
  ASSERT_TRUE(base::test::RunUntil([&] { return zoom->GetZoomPercent() < 100; }));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_ZOOM_NORMAL));
  chrome::ExecuteCommand(browser(), IDC_ZOOM_NORMAL);
  ASSERT_TRUE(base::test::RunUntil([&] { return zoom->GetZoomPercent() == 100; }));
  EXPECT_EQ(zoom::ZoomController::FromWebContents(source)->GetZoomPercent(),
            tab_percent);
}

// docs/chromium/m6.md section 7: a site in a Peek asks for a permission with
// Chromium's prompt, on the window, and the answer is stored for the site.
IN_PROC_BROWSER_TEST_F(CluPeekBrowserTest,
                       CluPeekShowsPermissionPromptsAndStoresTheAnswer) {
  const std::string pinned = OpenPinned(Url("multitask/links.html"));
  const GURL url = Url("capabilities/permissions.html", "localhost");
  PeekWithFocus(pinned, url);
  content::WebContents* page = peek().page();
  permissions::PermissionRequestManager* requests =
      permissions::PermissionRequestManager::FromWebContents(page);
  ASSERT_TRUE(requests);
  HostContentSettingsMap* map =
      HostContentSettingsMapFactory::GetForProfile(browser()->GetProfile());
  EXPECT_EQ(map->GetContentSetting(url, url, ContentSettingsType::NOTIFICATIONS),
            CONTENT_SETTING_ASK);

  permissions::PermissionRequestObserver observer(page);
  ASSERT_TRUE(content::ExecJs(
      page, "window.answer = Notification.requestPermission()",
      content::EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
  if (!requests->IsRequestInProgress()) {
    observer.Wait();
  }
  // The prompt is Chromium's bubble, made for a page with no tab.
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::Widget* prompt =
        test::PermissionRequestManagerTestApi(requests).GetPromptWindow();
    return prompt && prompt->IsVisible();
  }));
  EXPECT_TRUE(observer.request_shown());
  requests->Accept(PromptOptions(std::monostate()));
  EXPECT_EQ("granted", content::EvalJs(page, "window.answer"));
  EXPECT_EQ(map->GetContentSetting(url, url, ContentSettingsType::NOTIFICATIONS),
            CONTENT_SETTING_ALLOW);

  // Open as tab keeps the page and its prompt machinery: the tab's own
  // embedding context replaces the Peek's without a CHECK.
  const std::optional<std::string> row = peek().Expand();
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(Contents(*row), page);
  EXPECT_EQ(permissions::PermissionRequestManager::FromWebContents(page),
            requests);
  EXPECT_EQ("granted",
            content::EvalJs(page, "navigator.permissions.query({name: "
                                  "'notifications'}).then(s => s.state)"));
}

}  // namespace
}  // namespace clu
