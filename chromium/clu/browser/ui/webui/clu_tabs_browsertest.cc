// Copyright 2026 The Rio Authors. All rights reserved.

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/simple_test_clock.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/model/clu_menu_items.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_model_service_factory.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_tab_menu.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/ui/webui/clu_gmail_feed.h"
#include "clu/browser/ui/webui/clu_haptic.h"
#include "clu/browser/ui/webui/clu_live_folders.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "net/base/filename_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/clipboard/test/clipboard_test_util.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/base/data_transfer_policy/data_transfer_endpoint.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/vector2d.h"
#include "url/gurl.h"

// The CluTabs* checks of the M3 "Done when" row (docs/chromium/PLAN.md section 6): docs/PLAN.md 12.5 and 12.6 on
// Chromium, against real pages in real tab strips, the real frame WebUI, and the handlers the bridge calls. The
// boxes each test covers are in docs/chromium/acceptance-map.md.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

constexpr char kCenterOf[] = R"(
  (() => {
    const r = document.querySelector($1).getBoundingClientRect();
    return [Math.round(r.x + r.width / 2), Math.round(r.y + r.height / 2)];
  })())";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

std::string Reason(const mojom::CallResultPtr& result) {
  return result && result->is_error()
             ? result->get_error()->reason.value_or(std::string())
             : std::string();
}

mojom::ErrorCode Code(const mojom::CallResultPtr& result) {
  return result->get_error()->code;
}

mojom::DropTargetPtr Favorite(int index) {
  return mojom::DropTarget::NewFavorite(
      mojom::DropTargetFavorite::New("favorite", index));
}

mojom::DropTargetPtr TodayOf(const std::string& space, int index) {
  return mojom::DropTarget::NewToday(
      mojom::DropTargetToday::New("today", space, index));
}

mojom::DropTargetPtr PinnedOf(const std::string& space,
                              std::optional<std::string> parent,
                              int index) {
  return mojom::DropTarget::NewPinned(
      mojom::DropTargetPinned::New("pinned", space, std::move(parent), index));
}

// Records the privacy flags of the last write (TestClipboard drops them).
class PrivacyRecordingClipboard : public ui::TestClipboard {
 public:
  void WritePortableAndPlatformRepresentations(
      ui::ClipboardBuffer buffer,
      const ObjectMap& objects,
      const std::vector<RawData>& raw_objects,
      std::vector<PlatformRepresentation> platform_representations,
      std::unique_ptr<ui::DataTransferEndpoint> data_src,
      uint32_t privacy_types) override {
    last_privacy_types = privacy_types;
    ui::TestClipboard::WritePortableAndPlatformRepresentations(
        buffer, objects, raw_objects, std::move(platform_representations),
        std::move(data_src), privacy_types);
  }

  uint32_t last_privacy_types = 0;
};

class CluTabsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    profile_ = browser()->GetProfile();
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    // Copy Link and Copy URLs write here, never to the Mac's own pasteboard.
    ui::TestClipboard::CreateForCurrentThread();
  }

  void TearDownOnMainThread() override {
    // Through the profile: a test may have closed browser(), which then dangles. Closing the last window quits,
    // and the profile may be gone or shutting down; its model then never reads the clock again.
    if (g_browser_process->profile_manager()->IsValidProfile(profile_) &&
        !profile_->ShutdownStarted()) {
      if (CluModelService* model =
              CluModelServiceFactory::GetForProfile(profile_)) {
        model->SetClockForTesting(nullptr);
      }
    }
    ui::Clipboard::DestroyClipboardForCurrentThread();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluModelService& model() { return *window().model(); }
  const CluTabTree& tree() { return model().tree(); }
  TabStripModel& strip() { return *browser()->GetTabStripModel(); }
  std::string space() { return window().space_id(); }

  virtual GURL Url(const std::string& page) {
    return embedded_test_server()->GetURL(base::StrCat({"/tabs/", page}));
  }

  // A Today row on `page`, shown and loaded.
  std::string Open(const std::string& page) {
    base::expected<std::string, std::string> id =
        window().CreateRow(Url(page), std::nullopt, /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return std::string();
    }
    EXPECT_TRUE(content::WaitForLoadStop(Contents(id.value())));
    return id.value();
  }

  content::WebContents* Contents(const std::string& row_id) {
    tabs::TabInterface* tab = window().FindTab(row_id);
    return tab ? tab->GetContents() : nullptr;
  }

  // Every write posted so far has landed in clu.db.
  void FlushStore() {
    base::RunLoop loop;
    model().FlushForTesting(loop.QuitClosure());
    loop.Run();
  }

  ArchivePage Archive(const std::string& query = std::string()) {
    base::test::TestFuture<ArchivePage> page;
    model().ListArchive(query, std::string(), 100, page.GetCallback());
    return page.Take();
  }

  // Rows of the strip that are rows (the placeholder left out).
  int LivePages() {
    int count = 0;
    for (int i = 0; i < strip().count(); ++i) {
      if (!CluPlaceholder::Is(strip().GetWebContentsAt(i))) {
        ++count;
      }
    }
    return count;
  }

  void WaitForSelector(content::WebContents* contents,
                       const std::string& selector) {
    EXPECT_EQ(true, content::EvalJs(contents, content::JsReplace(
                                                  kWaitForSelector, selector)));
  }

  std::string RowSelector(const std::string& id) {
    return base::StrCat({"nav.clu-sidebar [data-tab-id=\"", id, "\"]"});
  }

  // Outlives every model call: TearDownOnMainThread() takes it back from the model.
  base::SimpleTestClock clock_;
  raw_ptr<Profile, AcrossTasksDanglingUntriaged> profile_ = nullptr;
};

// 12.5 box 1 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsMoveRulesHoldThroughTheTabsHandler) {
  CluTabsHandler tabs(window().GetWeakPtr());
  CluFoldersHandler folders(window().GetWeakPtr());
  std::vector<std::string> ids;
  for (int i = 0; i < kMaxFavorites + 1; ++i) {
    base::expected<std::string, std::string> id = window().CreateRow(
        Url("a.html"), std::nullopt, /*activate=*/false);
    ASSERT_TRUE(id.has_value());
    ids.push_back(id.value());
  }
  // Favorites take 12; the 13th is refused with its toast reason.
  for (int i = 0; i < kMaxFavorites; ++i) {
    CallFuture moved;
    tabs.Move(mojom::TabsMoveInput::New(ids[i], Favorite(i), std::nullopt,
                                        std::nullopt, std::nullopt),
              moved.GetCallback());
    ASSERT_TRUE(IsOk(moved.Get())) << i;
  }
  CallFuture full;
  tabs.Move(mojom::TabsMoveInput::New(ids.back(), Favorite(0), std::nullopt,
                                      /*ask=*/true, std::nullopt),
            full.GetCallback());
  EXPECT_EQ(Reason(full.Get()), "favorites-full");
  EXPECT_EQ(tree().Favorites().size(), static_cast<size_t>(kMaxFavorites));
  EXPECT_EQ(tree().Get(ids.back())->section, Section::kToday);
  // ask=true (a sidebar drop) shows the toast in this window.
  const base::DictValue state = window().CurrentState();
  const base::ListValue* toasts = state.FindDict("windows")
                                      ->FindDict(window().window_id())
                                      ->FindList("toasts");
  ASSERT_EQ(toasts->size(), 1u);
  EXPECT_EQ(*(*toasts)[0].GetDict().FindString("text"), "Favorites are full");

  // Folders nest 8 deep; a ninth level is refused, and a folder never goes to Today.
  std::optional<std::string> parent;
  std::string deepest;
  for (int level = 1; level <= kMaxFolderDepth; ++level) {
    CallFuture created;
    folders.Create(mojom::FoldersCreateInput::New(space(), parent),
                   created.GetCallback());
    mojom::CallResultPtr result = created.Take();
    ASSERT_TRUE(IsOk(result)) << level;
    deepest = *base::JSONReader::ReadDict(result->get_value_json(),
                                          base::JSON_PARSE_RFC)
                   ->FindString("folderId");
    parent = deepest;
  }
  CallFuture too_deep;
  folders.Create(mojom::FoldersCreateInput::New(space(), deepest),
                 too_deep.GetCallback());
  EXPECT_EQ(Reason(too_deep.Get()), "too-deep");
  const std::string root = tree().Pinned(space()).front();
  CallFuture folder_to_today;
  tabs.Move(mojom::TabsMoveInput::New(root, TodayOf(space(), 0), std::nullopt,
                                      std::nullopt, std::nullopt),
            folder_to_today.GetCallback());
  EXPECT_EQ(Reason(folder_to_today.Get()), "folder-not-allowed");

  // Custom icons: one emoji on a pinned row or folder; "ab" and Today rows are refused.
  CallFuture pin;
  tabs.Move(mojom::TabsMoveInput::New(ids.back(), PinnedOf(space(), deepest, 0),
                                      std::nullopt, std::nullopt, std::nullopt),
            pin.GetCallback());
  ASSERT_TRUE(IsOk(pin.Get()));
  CallFuture icon;
  tabs.SetIcon(mojom::TabsSetIconInput::New(ids.back(), "\xF0\x9F\x93\x8C"),
               icon.GetCallback());
  EXPECT_TRUE(IsOk(icon.Get()));
  EXPECT_EQ(tree().Get(ids.back())->custom_icon, "\xF0\x9F\x93\x8C");
  CallFuture two_letters;
  tabs.SetIcon(mojom::TabsSetIconInput::New(root, "ab"),
               two_letters.GetCallback());
  EXPECT_EQ(Reason(two_letters.Get()), "not-one-emoji");
  const std::string today =
      window().CreateRow(Url("b.html"), std::nullopt, false).value();
  CallFuture on_today;
  tabs.SetIcon(mojom::TabsSetIconInput::New(today, "\xF0\x9F\x93\x8C"),
               on_today.GetCallback());
  EXPECT_EQ(Reason(on_today.Get()), "bad-target");

  // A URL the policy refuses never becomes a row.
  const size_t rows = tree().size();
  CallFuture script;
  tabs.Create(mojom::TabsCreateInput::New(std::nullopt, "javascript:alert(1)",
                                          true),
              script.GetCallback());
  EXPECT_EQ(Reason(script.Get()), "blocked-url");
  EXPECT_EQ(tree().size(), rows);
}

// Advanced's "Haptic feedback when reordering tabs" (tabs.reorderHaptic): a sidebar drop (`drop`) that moves rows ticks
// the trackpad; a move from elsewhere, a refused drop and a drop with the switch off do not.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsASidebarDropTicksTheTrackpadUnlessTheSwitchIsOff) {
  CluTabsHandler tabs(window().GetWeakPtr());
  std::vector<std::string> ids;
  for (int i = 0; i < 3; ++i) {
    base::expected<std::string, std::string> id = window().CreateRow(
        Url("a.html"), std::nullopt, /*activate=*/false);
    ASSERT_TRUE(id.has_value());
    ids.push_back(id.value());
  }
  ASSERT_TRUE(CluAppSettings::Get()->GetBool(kReorderHapticKey));
  const int before = ReorderHapticsForTesting();

  CallFuture dropped;
  tabs.Move(mojom::TabsMoveInput::New(ids[2], TodayOf(space(), 0),
                                      std::nullopt, /*ask=*/true, /*drop=*/true),
            dropped.GetCallback());
  ASSERT_TRUE(IsOk(dropped.Get()));
  EXPECT_EQ(ReorderHapticsForTesting(), before + 1);
  CallFuture bulk;
  tabs.Bulk(mojom::TabsBulkInput::NewMove(mojom::TabsBulkInputMove::New(
                std::vector<std::string>{ids[0], ids[1]}, "move",
                TodayOf(space(), 0), std::nullopt, /*ask=*/true, /*drop=*/true)),
            bulk.GetCallback());
  ASSERT_TRUE(IsOk(bulk.Get()));
  EXPECT_EQ(ReorderHapticsForTesting(), before + 2);

  CallFuture elsewhere;
  tabs.Move(mojom::TabsMoveInput::New(ids[1], TodayOf(space(), 0),
                                      std::nullopt, std::nullopt, std::nullopt),
            elsewhere.GetCallback());
  ASSERT_TRUE(IsOk(elsewhere.Get()));
  // The command bar's Pin to <folder> asks for the cross-profile confirm but is no drop.
  CallFuture keyboard;
  tabs.Move(mojom::TabsMoveInput::New(ids[1], TodayOf(space(), 1),
                                      std::nullopt, /*ask=*/true, std::nullopt),
            keyboard.GetCallback());
  ASSERT_TRUE(IsOk(keyboard.Get()));
  EXPECT_EQ(ReorderHapticsForTesting(), before + 2);
  CallFuture refused;
  tabs.Move(mojom::TabsMoveInput::New("missing", TodayOf(space(), 0),
                                      std::nullopt, /*ask=*/true, /*drop=*/true),
            refused.GetCallback());
  EXPECT_FALSE(IsOk(refused.Get()));
  EXPECT_EQ(ReorderHapticsForTesting(), before + 2);

  CluAppSettings::Get()->SetGlobal(kReorderHapticKey, base::Value(false));
  CallFuture quiet;
  tabs.Move(mojom::TabsMoveInput::New(ids[0], TodayOf(space(), 0),
                                      std::nullopt, /*ask=*/true, /*drop=*/true),
            quiet.GetCallback());
  ASSERT_TRUE(IsOk(quiet.Get()));
  EXPECT_EQ(ReorderHapticsForTesting(), before + 2);
  CluAppSettings::Get()->SetGlobal(kReorderHapticKey, base::Value(true));
}

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsTheFrameRendersFavoritesPinnedFoldersAndToday) {
  const std::string favorite = Open("a.html");
  ASSERT_TRUE(window().MoveRows({favorite}, DropTarget::Favorite(0), false)
                  .has_value());
  const std::string folder =
      model()
          .mutable_tree()
          .CreateFolder(space(), std::string(), "Reading", std::nullopt,
                        model().NowMs())
          .value();
  model().Commit();
  const std::string pinned = Open("b.html");
  ASSERT_TRUE(window()
                  .MoveRows({pinned}, DropTarget::Pinned(space(), folder, 0),
                            false)
                  .has_value());
  const std::string today = Open("c.html");
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame,
                  base::StrCat({".clu-favorites [data-tab-id=\"", favorite, "\"]"}));
  WaitForSelector(frame, base::StrCat({"[data-folder-id=\"", folder, "\"]"}));
  WaitForSelector(frame, RowSelector(pinned));
  WaitForSelector(frame, RowSelector(today));
  // The active row is marked; navigating a pinned row off its pinned URL shows it.
  EXPECT_EQ("true", content::EvalJs(
                        frame, content::JsReplace(
                                   "document.querySelector($1).dataset.active",
                                   RowSelector(today))));
  ASSERT_TRUE(window().ActivateRow(pinned));
  ASSERT_TRUE(content::NavigateToURL(Contents(pinned), Url("a.html")));
  WaitForSelector(frame, RowSelector(pinned) + "[data-off-pinned=\"true\"]");
}

// 12.5 box 2 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsClosingTodayArchivesAndClosingPinnedResets) {
  const std::string c = Open("c.html");
  const std::string b = Open("b.html");
  const std::string a = Open("a.html");
  ASSERT_TRUE(window().ActivateRow(b));
  // Cmd+W is IDC_CLOSE_TAB: the Today row archives with reason close, and the row that takes its place shows.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(b); }));
  EXPECT_EQ(window().ActiveRowId(), c);
  EXPECT_FALSE(Contents(b));
  const ArchivePage page = Archive();
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].reason, ArchiveReason::kClose);
  EXPECT_EQ(GURL(page.rows[0].url), Url("b.html"));

  // A pinned row keeps its place, loses its page, and reads as its pinned URL again.
  ASSERT_TRUE(
      window().MoveRows({a}, DropTarget::Pinned(space(), "", 0), false).has_value());
  ASSERT_TRUE(window().ActivateRow(a));
  ASSERT_TRUE(content::NavigateToURL(Contents(a), Url("b.html")));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return GURL(tree().Get(a)->url) == Url("b.html"); }));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return !Contents(a); }));
  ASSERT_TRUE(tree().Get(a));
  EXPECT_EQ(GURL(tree().Get(a)->url), Url("a.html"));
  EXPECT_EQ(Archive().rows.size(), 1u);

  // Activating it again loads its pinned URL with no history.
  ASSERT_TRUE(window().ActivateRow(a));
  ASSERT_TRUE(content::WaitForLoadStop(Contents(a)));
  EXPECT_EQ(Contents(a)->GetLastCommittedURL(), Url("a.html"));
  EXPECT_EQ(Contents(a)->GetController().GetEntryCount(), 1);
}

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsCmdWNeverClosesTheWindowWhileARowShows) {
  // The startup tab is a row; closing every row leaves the window open on its empty state.
  while (!window().ActiveRowId().empty()) {
    const std::string active = window().ActiveRowId();
    ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
    ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(active); }));
  }
  EXPECT_EQ(LivePages(), 0);
  EXPECT_TRUE(window().HasPlaceholderForTesting());
  EXPECT_FALSE(shell().page_shown());
  // With no row showing, Cmd+W closes the window (F9.2).
  ui_test_utils::BrowserDestroyedObserver destroyed(browser());
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  destroyed.Wait();
}

// F9 for a close Clu did not start: a window showing one row keeps a background placeholder, so when that page closes
// itself the strip never empties (Browser::TabStripEmpty() would close the window). The row is archived and the
// window shows its empty state.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsAPageThatClosesItselfLeavesTheWindowOpen) {
  while (!window().ActiveRowId().empty()) {
    const std::string active = window().ActiveRowId();
    ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
    ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(active); }));
  }
  const std::string a = Open("a.html");
  ASSERT_TRUE(base::test::RunUntil([&] { return LivePages() == 1; }));
  content::RunAllTasksUntilIdle();
  EXPECT_TRUE(window().HasPlaceholderForTesting());

  // One history entry, so the page may close itself.
  ASSERT_TRUE(content::ExecJs(Contents(a), "window.close()"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(a); }));
  content::RunAllTasksUntilIdle();
  EXPECT_TRUE(std::ranges::contains(GetAllBrowserWindowInterfaces(),
                                    static_cast<BrowserWindowInterface*>(
                                        browser())));
  EXPECT_EQ(LivePages(), 0);
  EXPECT_EQ(Archive(Url("a.html").spec()).rows.size(), 1u);
  EXPECT_FALSE(shell().page_shown());
}

// A page moved into a window without Clu's shell (an extension's chrome.windows.create({tabId, type: 'popup'})) has
// left Clu: its row closes as F9 says, instead of loading a second page for it later.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsAPageMovedToAPopupClosesItsRow) {
  const std::string a = Open("a.html");
  BrowserWindowInterface* popup = CreateBrowserWindow(BrowserWindowCreateParams(
      BrowserWindowInterface::TYPE_POPUP, *profile_, /*from_user_gesture=*/true));
  ASSERT_TRUE(popup);
  ASSERT_FALSE(CluShell::From(popup));
  ASSERT_NE(strip().GetIndexOfWebContents(Contents(a)), TabStripModel::kNoTab);
  std::unique_ptr<tabs::TabModel> detached = strip().DetachTabAtForInsertion(
      strip().GetIndexOfWebContents(Contents(a)));
  popup->GetTabStripModel()->InsertDetachedTabAt(0, std::move(detached),
                                                 AddTabTypes::ADD_ACTIVE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(a); }));
  EXPECT_EQ(Archive(Url("a.html").spec()).rows.size(), 1u);
  EXPECT_NE(window().ActiveRowId(), a);
}

// 12.5 box 3 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsEveryMenuItemRuns) {
  CluTabMenu& menu = window().tab_menu();
  const std::string row = Open("a.html");
  CluTabMenu::Request request;
  request.menu = "row";
  request.node_id = row;

  // Move to lists the spaces, the row's own checked and enabled, then New Space….
  const std::vector<MenuItem> items = menu.Build(request, true);
  const auto move_to = std::ranges::find(items, std::string(kMenuMoveTo),
                                         &MenuItem::id);
  ASSERT_NE(move_to, items.end());
  const auto home = std::ranges::find(
      move_to->submenu,
      MoveToId({.kind = MoveToPick::Kind::kToday, .space_id = space()},
               kMenuMoveTo),
      &MenuItem::id);
  ASSERT_NE(home, move_to->submenu.end());
  EXPECT_TRUE(home->enabled);
  EXPECT_TRUE(home->checked);
  EXPECT_EQ(move_to->submenu.back().label, "New Space…");
  // Picking the checked space leaves the row where it is.
  const std::string home_id = home->id;
  const std::vector<std::string> before = tree().Today(space());
  menu.Run(request, home_id, std::string());
  EXPECT_EQ(tree().Today(space()), before);
  EXPECT_EQ(tree().Get(row)->space_id, space());

  // Copy Link puts the URL without tracking parameters on the clipboard.
  menu.Run(request, kMenuCopyLink, std::string());
  EXPECT_EQ(ui::clipboard_test_util::ReadText(
                ui::Clipboard::GetForCurrentThread(),
                ui::ClipboardBuffer::kCopyPaste, nullptr),
            base::UTF8ToUTF16(Url("a.html").spec()));
  // Rename answers the row to rename inline.
  EXPECT_EQ(menu.Run(request, kMenuRename, std::string()), row);
  // Mute is off while the page is silent: the pick does nothing.
  menu.Run(request, kMenuMute, std::string());
  EXPECT_FALSE(tree().Get(row)->muted);
  EXPECT_FALSE(Contents(row)->IsAudioMuted());
  // Duplicate lands below a Today source and shows.
  menu.Run(request, kMenuDuplicate, std::string());
  ASSERT_EQ(tree().Today(space()).size(), 3u);
  EXPECT_EQ(tree().Today(space())[0], row);
  const std::string copy = tree().Today(space())[1];
  EXPECT_EQ(window().ActiveRowId(), copy);
  // A Today row's menu has no Pin item (Cmd+D or a drag pins it).
  EXPECT_FALSE(menu.Run(request, kMenuPin, std::string()).has_value());
  EXPECT_EQ(tree().Get(row)->section, Section::kToday);
  ASSERT_TRUE(window().TogglePin(row).has_value());
  EXPECT_EQ(tree().Get(row)->section, Section::kPinned);
  menu.Run(request, kMenuPinnedUrlEnter, std::string());
  EXPECT_EQ(*window()
                 .CurrentState()
                 .FindDict("windows")
                 ->FindDict(window().window_id())
                 ->FindListByDottedPath("overlay.surfaces")
                 ->front()
                 .GetDict()
                 .FindString("surface"),
            "tabs.pinnedUrl");
  // A favorite's own item takes it back to Pinned.
  ASSERT_TRUE(
      window().MoveRows({row}, DropTarget::Favorite(0), /*toast=*/false).has_value());
  EXPECT_EQ(tree().Get(row)->section, Section::kFavorite);
  menu.Run(request, kMenuFavorite, std::string());
  EXPECT_EQ(tree().Get(row)->section, Section::kPinned);
  // Remove Pin closes a pinned tab into the archive.
  CluTabMenu::Request on_copy = request;
  on_copy.node_id = copy;
  ASSERT_TRUE(window().TogglePin(copy).has_value());
  ASSERT_EQ(tree().Get(copy)->section, Section::kPinned);
  menu.Run(on_copy, kMenuPin, std::string());
  EXPECT_FALSE(tree().Get(copy));
  // Cmd+D takes a pinned row back to Today.
  ASSERT_TRUE(window().TogglePin(row).has_value());
  EXPECT_EQ(tree().Get(row)->section, Section::kToday);

  // Archive Tabs Above, Below and Other take exactly those Today rows.
  const std::string top = Open("b.html");
  const std::string middle = Open("c.html");
  // Today is now [middle, top, row, startup tab].
  CluTabMenu::Request on_top = request;
  on_top.node_id = top;
  menu.Run(on_top, kMenuArchiveAbove, std::string());
  EXPECT_FALSE(tree().Get(middle));
  EXPECT_TRUE(tree().Get(top));
  menu.Run(on_top, kMenuArchiveBelow, std::string());
  EXPECT_EQ(tree().Today(space()), std::vector<std::string>{top});
  menu.Run(on_top, kMenuArchive, std::string());
  EXPECT_FALSE(tree().Get(top));

  // Folder menu: New Folder (+ menu), New Folder Inside, Set Icon, Delete Folder.
  CluTabMenu::Request plus;
  plus.menu = "new";
  const std::optional<std::string> folder =
      menu.Run(plus, kMenuNewFolder, std::string());
  ASSERT_TRUE(folder);
  CluTabMenu::Request on_folder;
  on_folder.menu = "row";
  on_folder.node_id = *folder;
  const std::optional<std::string> inner =
      menu.Run(on_folder, kMenuNewFolderInside, std::string());
  ASSERT_TRUE(inner);
  EXPECT_EQ(tree().Get(*inner)->parent_id, *folder);
  // The folder menu, item by item, and Duplicate puts a copy (with its nested folder)
  // right after it.
  auto labels = [](const std::vector<MenuItem>& items) {
    std::vector<std::string> out;
    for (const MenuItem& item : items) {
      out.push_back(item.separator ? "-" : item.label);
    }
    return out;
  };
  EXPECT_EQ(labels(menu.Build(on_folder, false)),
            (std::vector<std::string>{
                "Share Folder…", "-", "New Nested Folder…",
                "Turn “Untitled” into Space", "Copy All Links",
                "Copy All Links as Markdown", "-", "Change Icon…", "Rename…",
                "-", "Duplicate", "Move to", "-", "Delete"}));
  const size_t roots = tree().Pinned(space()).size();
  menu.Run(on_folder, kMenuFolderDuplicate, std::string());
  ASSERT_EQ(tree().Pinned(space()).size(), roots + 1);
  const auto folder_at = std::ranges::find(tree().Pinned(space()), *folder);
  ASSERT_NE(folder_at + 1, tree().Pinned(space()).end());
  const std::string folder_copy = *(folder_at + 1);
  EXPECT_TRUE(tree().Get(folder_copy)->is_folder());
  EXPECT_EQ(tree().Children(folder_copy).size(), 1u);
  // Empty sidebar space opens the space menu (the frame asks for menu "space" with the window's space).
  CluTabMenu::Request on_space;
  on_space.menu = "space";
  on_space.node_id = space();
  std::vector<std::string> space_labels = labels(menu.Build(on_space, false));
  ASSERT_EQ(space_labels.size(), 14u);
  EXPECT_TRUE(space_labels[9].starts_with("Export “"));
  EXPECT_TRUE(space_labels[9].ends_with("” into Folder"));
  space_labels[9] = "Export";
  EXPECT_EQ(space_labels,
            (std::vector<std::string>{
                "Change Space Icon…", "Rename Space…", "Edit Theme Color…",
                "Set Profile", "-", "New Folder", "Live Folders", "-",
                "Share Space…", "Export", "-", "Manage Spaces…", "-",
                "Delete Space"}));
  CluTabMenu::Request on_folder_copy = on_folder;
  on_folder_copy.node_id = folder_copy;
  menu.Run(on_folder_copy, kMenuDeleteFolder, std::string());
  menu.Run(on_folder, kMenuSetIcon, std::string());
  menu.Run(on_folder, kMenuDeleteFolder, std::string());
  EXPECT_FALSE(tree().Get(*folder));
  EXPECT_FALSE(tree().Get(*inner));
  EXPECT_FALSE(tree().Get(folder_copy));

  // Empty area: New Tab opens the command bar, Paste and Go opens what the clipboard holds.
  CluTabMenu::Request empty;
  empty.menu = "empty";
  menu.Run(empty, kMenuNewTab, std::string());
  ASSERT_TRUE(window().command_session());
  EXPECT_EQ(window().command_session()->mode, "new-tab");
  window().CloseCommandBar();
  menu.Run(empty, kMenuPasteAndGo, Url("c.html").spec());
  EXPECT_TRUE(base::test::RunUntil([&] {
    tabs::TabInterface* active = window().ActiveTab();
    return active && active->GetContents()->GetVisibleURL() == Url("c.html");
  }));

  // Multi-select: Copy URLs as Markdown, Move N Tabs To, Archive N Tabs.
  const std::string one = Open("a.html");
  const std::string two = Open("b.html");
  CluTabMenu::Request multi;
  multi.menu = "row";
  multi.tab_ids = {one, two};
  menu.Run(multi, kMenuBulkCopyMarkdown, std::string());
  EXPECT_EQ(ui::clipboard_test_util::ReadText(
                ui::Clipboard::GetForCurrentThread(),
                ui::ClipboardBuffer::kCopyPaste, nullptr),
            base::UTF8ToUTF16(base::StrCat({"[A](", Url("a.html").spec(),
                                            ")\n[B](", Url("b.html").spec(),
                                            ")"})));
  menu.Run(multi,
           MoveToId({.kind = MoveToPick::Kind::kPinned, .space_id = space()},
                    kMenuBulkMoveTo),
           std::string());
  EXPECT_EQ(tree().Get(one)->section, Section::kPinned);
  EXPECT_EQ(tree().Get(two)->section, Section::kPinned);
  // Add to Split View is enabled for two tabs of the window's space and makes them one split.
  const std::vector<MenuItem> multi_items = menu.Build(multi, false);
  const auto split_item = std::ranges::find(multi_items, std::string(kMenuBulkSplit), &MenuItem::id);
  ASSERT_NE(split_item, multi_items.end());
  EXPECT_TRUE(split_item->enabled);
  menu.Run(multi, kMenuBulkSplit, std::string());
  ASSERT_TRUE(model().SplitOf(one));
  EXPECT_EQ(model().SplitOf(one), model().SplitOf(two));
  menu.Run(multi, kMenuBulkArchive, std::string());
  EXPECT_FALSE(tree().Get(one));
  EXPECT_FALSE(tree().Get(two));

  // Move Tab to New Window (the Tabs menu; the row menu no longer has it) takes the same page to a new window.
  const std::string moving = Open("c.html");
  content::WebContents* page = Contents(moving);
  ui_test_utils::BrowserCreatedObserver created;
  CluTabMenu::Request on_moving = request;
  on_moving.node_id = moving;
  window().MoveRowToNewWindow(moving);
  BrowserWindowInterface* other = created.Wait();
  ASSERT_TRUE(other);
  EXPECT_NE(window().ActiveRowId(), moving);
  CluWindowState* other_state = CluWindowState::GetOrCreate(other);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return other_state->ActiveRowId() == moving; }));
  EXPECT_EQ(other_state->FindTab(moving)->GetContents(), page);
}

// 12.5 box 4 ---------------------------------------------------------------------------------------------------------

// A pinned row's Open in Split View puts it beside the page on screen.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsOpenInSplitViewFromAPinnedRow) {
  const std::string row = Open("a.html");
  const std::string shown = Open("b.html");
  ASSERT_TRUE(window().TogglePin(row).has_value());
  ASSERT_EQ(window().ActiveRowId(), shown);
  CluTabMenu& menu = window().tab_menu();
  CluTabMenu::Request request;
  request.menu = "row";
  request.node_id = row;
  menu.Run(request, kMenuOpenInSplit, std::string());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tree().Get(row)->split_id.has_value() &&
           tree().Get(row)->split_id == tree().Get(shown)->split_id;
  }));
}

// docs/chromium/motion.md: pinning slides the row once (110 ms), its favicon does not
// fade in again in its new list, a later state push that moves nothing starts no motion, and once the slide is done
// nothing in the sidebar animates at rest.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsPinSlidesOnceAndRestsStill) {
  const std::string other = Open("a.html");
  const std::string row = Open("b.html");
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, base::StrCat({".clu-today-divider ~ ", RowSelector(row)}));
  ASSERT_TRUE(content::ExecJs(frame, R"(
      window.cluAnimLog = [];
      const original = Element.prototype.animate;
      Element.prototype.animate = function (keyframes, options) {
        window.cluAnimLog.push({tab: this.getAttribute('data-tab-id'), id: options?.id ?? '',
                                ms: options?.duration});
        return original.call(this, keyframes, options);
      };)"));
  ASSERT_TRUE(window().TogglePin(row).has_value());
  WaitForSelector(frame, base::StrCat({RowSelector(row), " ~ .clu-today-divider"}));
  // Finite animations in the sidebar, by name (an equalizer that loops while audio plays is not motion at rest).
  static constexpr char kRunning[] = R"(
      (() => {
        const nav = document.querySelector('nav.clu-sidebar');
        return document.getAnimations()
            .filter((a) => a.playState === 'running' &&
                           a.effect?.getComputedTiming().iterations !== Infinity &&
                           nav.contains(a.effect?.target))
            .map((a) => a.animationName || a.id || 'script').join(',');
      })())";
  EXPECT_EQ(std::string::npos,
            content::EvalJs(frame, kRunning).ExtractString().find("clu-fade-in"));
  EXPECT_EQ("clu-row-flip:110", content::EvalJs(frame, content::JsReplace(R"(
      window.cluAnimLog.filter((a) => a.tab === $1).map((a) => a.id + ':' + a.ms).join(','))",
                                                     row)));
  // A push that moves nothing: the other row's title changes.
  ASSERT_TRUE(content::ExecJs(Contents(other), "document.title = 'Echo'"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame, content::JsReplace(
                                      "document.querySelector($1 + ' .clu-row-title')"
                                      "?.textContent === 'Echo'",
                                      RowSelector(other)))
        .ExtractBool();
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame, kRunning).ExtractString().empty();
  }));
  EXPECT_EQ(1, content::EvalJs(frame, content::JsReplace(
                   "window.cluAnimLog.filter((a) => a.tab === $1).length", row)));
}

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsDraggingATodayRowPinsIt) {
  const std::string row = Open("a.html");
  const std::string other = Open("b.html");
  ASSERT_TRUE(window()
                  .MoveRows({other}, DropTarget::Pinned(space(), "", 0), false)
                  .has_value());
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, RowSelector(row));
  // The frame has drawn the move: the pinned row sits above the Today divider (the sidebar labels neither section).
  // The rows' rects are read only then, or the press lands where the row was before.
  WaitForSelector(frame,
                  base::StrCat({RowSelector(other), " ~ .clu-today-divider"}));
  const auto center = [&](const std::string& selector) {
    const base::ListValue at =
        content::EvalJs(frame, content::JsReplace(kCenterOf, selector))
            .ExtractList()
            .Clone();
    return gfx::Point(at[0].GetInt(), at[1].GetInt());
  };
  // A real press: the left button held through the moves, as Blink needs for pointer events with buttons = 1
  // (content::SimulateMouseEvent sends no button modifiers and no click count). Each event is processed before the
  // next, as a hand's moves arrive over frames; unprocessed moves coalesce into one.
  content::RenderWidgetHost* widget =
      frame->GetRenderWidgetHostView()->GetRenderWidgetHost();
  const auto send = [&](blink::WebInputEvent::Type type, int modifiers,
                        const gfx::Point& at) {
    blink::WebMouseEvent event(type, modifiers, ui::EventTimeForNow());
    event.button = blink::WebMouseEvent::Button::kLeft;
    event.click_count = 1;
    event.SetPositionInWidget(at.x(), at.y());
    widget->ForwardMouseEvent(event);
    content::RunUntilInputProcessed(widget);
  };
  const gfx::Point start = center(RowSelector(row));
  send(blink::WebInputEvent::Type::kMouseDown,
       blink::WebInputEvent::kLeftButtonDown, start);
  // 10 px starts the drag; the sidebar then opens its Favorites drop zone and the rows below it move down, so the
  // pinned row is aimed at where it is drawn now.
  send(blink::WebInputEvent::Type::kMouseMove,
       blink::WebInputEvent::kLeftButtonDown, start - gfx::Vector2d(0, 10));
  WaitForSelector(frame, "[data-drag-layer]");
  ASSERT_TRUE(content::EvalJs(frame, "new Promise(r => requestAnimationFrame(() => r(true)))")
                  .ExtractBool());
  // The top third of the pinned row inserts before it.
  const gfx::Point to = center(RowSelector(other));
  const gfx::Point end(to.x(), to.y() - 8);
  const gfx::Point from = start - gfx::Vector2d(0, 10);
  for (int step = 1; step <= 8; ++step) {
    const gfx::Point at(from.x() + (end.x() - from.x()) * step / 8,
                        from.y() + (end.y() - from.y()) * step / 8);
    send(blink::WebInputEvent::Type::kMouseMove,
         blink::WebInputEvent::kLeftButtonDown, at);
  }
  send(blink::WebInputEvent::Type::kMouseUp, blink::WebInputEvent::kNoModifiers,
       end);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Get(row)->section == Section::kPinned; }));
  EXPECT_EQ(tree().Pinned(space()).front(), row);
  EXPECT_EQ(tree().Get(row)->pinned_url, Url("a.html").spec());
  // The page stayed the same page.
  EXPECT_TRUE(Contents(row));
}

// Pressing a row that is not selected activates it, and in an active window Chromium then focuses the page
// (BrowserView::OnActiveTabChanged calls RestoreFocus), so the frame blurs a few ms into the press. The drag still
// starts and drops.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsDraggingAnUnselectedRowSurvivesThePageTakingFocus) {
  const std::string row = Open("a.html");
  const std::string other = Open("b.html");
  ASSERT_TRUE(window()
                  .MoveRows({other}, DropTarget::Pinned(space(), "", 0), false)
                  .has_value());
  ASSERT_EQ(window().ActiveRowId(), other);
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame,
                  base::StrCat({RowSelector(other), " ~ .clu-today-divider"}));
  WaitForSelector(frame, RowSelector(row));
  const auto center = [&](const std::string& selector) {
    const base::ListValue at =
        content::EvalJs(frame, content::JsReplace(kCenterOf, selector))
            .ExtractList()
            .Clone();
    return gfx::Point(at[0].GetInt(), at[1].GetInt());
  };
  content::RenderWidgetHost* widget =
      frame->GetRenderWidgetHostView()->GetRenderWidgetHost();
  const auto send = [&](blink::WebInputEvent::Type type, int modifiers,
                        const gfx::Point& at) {
    blink::WebMouseEvent event(type, modifiers, ui::EventTimeForNow());
    event.button = blink::WebMouseEvent::Button::kLeft;
    event.click_count = 1;
    event.SetPositionInWidget(at.x(), at.y());
    widget->ForwardMouseEvent(event);
    content::RunUntilInputProcessed(widget);
  };
  frame->Focus();
  const gfx::Point start = center(RowSelector(row));
  send(blink::WebInputEvent::Type::kMouseDown,
       blink::WebInputEvent::kLeftButtonDown, start);
  // The press selected the row at once; the page it shows now takes focus, and the frame sees a window blur.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == row; }));
  // An inactive test window may not blur the frame on its own, so the frame also gets the blur it gets in an active
  // window.
  Contents(row)->Focus();
  ASSERT_TRUE(
      content::ExecJs(frame, "window.dispatchEvent(new Event('blur'))"));
  send(blink::WebInputEvent::Type::kMouseMove,
       blink::WebInputEvent::kLeftButtonDown, start - gfx::Vector2d(0, 10));
  WaitForSelector(frame, "[data-drag-layer]");
  ASSERT_TRUE(content::EvalJs(frame, "new Promise(r => requestAnimationFrame(() => r(true)))")
                  .ExtractBool());
  const gfx::Point to = center(RowSelector(other));
  const gfx::Point end(to.x(), to.y() - 8);
  const gfx::Point from = start - gfx::Vector2d(0, 10);
  for (int step = 1; step <= 8; ++step) {
    const gfx::Point at(from.x() + (end.x() - from.x()) * step / 8,
                        from.y() + (end.y() - from.y()) * step / 8);
    send(blink::WebInputEvent::Type::kMouseMove,
         blink::WebInputEvent::kLeftButtonDown, at);
  }
  send(blink::WebInputEvent::Type::kMouseUp, blink::WebInputEvent::kNoModifiers,
       end);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Get(row)->section == Section::kPinned; }));
  EXPECT_EQ(tree().Pinned(space()).front(), row);
}

// A real drag of a Today row into the 5 px gap between two pinned folders drops it there, between them, with
// the drop line drawn in that gap while the pointer is over it.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsDraggingATodayRowBetweenTwoPinnedFolders) {
  const std::string one =
      model()
          .mutable_tree()
          .CreateFolder(space(), std::string(), "One", std::nullopt,
                        model().NowMs())
          .value();
  const std::string two =
      model()
          .mutable_tree()
          .CreateFolder(space(), std::string(), "Two", std::nullopt,
                        model().NowMs())
          .value();
  model().Commit();
  const std::string row = Open("a.html");
  content::WebContents* frame = shell().frame_contents();
  const std::string one_selector =
      base::StrCat({"[data-folder-id=\"", one, "\"]"});
  const std::string two_selector =
      base::StrCat({"[data-folder-id=\"", two, "\"]"});
  WaitForSelector(frame, one_selector);
  WaitForSelector(frame, two_selector);
  WaitForSelector(frame, RowSelector(row));
  content::RenderWidgetHost* widget =
      frame->GetRenderWidgetHostView()->GetRenderWidgetHost();
  const auto send = [&](blink::WebInputEvent::Type type, int modifiers,
                        const gfx::Point& at) {
    blink::WebMouseEvent event(type, modifiers, ui::EventTimeForNow());
    event.button = blink::WebMouseEvent::Button::kLeft;
    event.click_count = 1;
    event.SetPositionInWidget(at.x(), at.y());
    widget->ForwardMouseEvent(event);
    content::RunUntilInputProcessed(widget);
  };
  const base::ListValue at_start =
      content::EvalJs(frame, content::JsReplace(kCenterOf, RowSelector(row)))
          .ExtractList()
          .Clone();
  const gfx::Point start(at_start[0].GetInt(), at_start[1].GetInt());
  send(blink::WebInputEvent::Type::kMouseDown,
       blink::WebInputEvent::kLeftButtonDown, start);
  send(blink::WebInputEvent::Type::kMouseMove,
       blink::WebInputEvent::kLeftButtonDown, start - gfx::Vector2d(0, 10));
  WaitForSelector(frame, "[data-drag-layer]");
  ASSERT_TRUE(content::EvalJs(frame, "new Promise(r => requestAnimationFrame(() => r(true)))")
                  .ExtractBool());
  // The middle of the gap, read after the drag started (the rows may have moved for the drop zones).
  const base::ListValue gap =
      content::EvalJs(frame, content::JsReplace(R"((() => {
        const a = document.querySelector($1).getBoundingClientRect();
        const b = document.querySelector($2).getBoundingClientRect();
        return [Math.round(a.x + a.width / 2), Math.round((a.bottom + b.top) / 2)];
      })())",
                                                one_selector, two_selector))
          .ExtractList()
          .Clone();
  const gfx::Point end(gap[0].GetInt(), gap[1].GetInt());
  const gfx::Point from = start - gfx::Vector2d(0, 10);
  for (int step = 1; step <= 8; ++step) {
    const gfx::Point at(from.x() + (end.x() - from.x()) * step / 8,
                        from.y() + (end.y() - from.y()) * step / 8);
    send(blink::WebInputEvent::Type::kMouseMove,
         blink::WebInputEvent::kLeftButtonDown, at);
  }
  // The drop line sits in the gap.
  EXPECT_EQ(true, content::EvalJs(frame, content::JsReplace(R"(
      new Promise(resolve => {
        const check = () => {
          const line = document.querySelector('[data-drop-line]');
          if (!line) return void setTimeout(check, 20);
          const a = document.querySelector($1).getBoundingClientRect();
          const b = document.querySelector($2).getBoundingClientRect();
          const r = line.getBoundingClientRect();
          const mid = (r.top + r.bottom) / 2;
          resolve(mid >= a.bottom - 1 && mid <= b.top + 1);
        };
        check();
      }))",
                                                          one_selector,
                                                          two_selector)));
  send(blink::WebInputEvent::Type::kMouseUp, blink::WebInputEvent::kNoModifiers,
       end);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Get(row)->section == Section::kPinned; }));
  EXPECT_EQ(tree().Pinned(space()),
            (std::vector<std::string>{one, row, two}));
}

// 12.5 box 5 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsRowKeysWorkFromAFocusedPage) {
  const std::string startup = window().ActiveRowId();
  const std::string c = Open("c.html");
  const std::string b = Open("b.html");
  const std::string a = Open("a.html");
  // Today is [a, b, c, startup]; the page has focus.
  Contents(a)->Focus();
  // Cmd+1..9 are reserved: a focused page never sees them.
  const input::NativeWebKeyboardEvent key(
      blink::WebInputEvent::Type::kRawKeyDown, blink::WebInputEvent::kMetaKey,
      base::TimeTicks::Now());
  chrome::BrowserCommandController* controller =
      chrome::BrowserCommandController::From(browser());
  EXPECT_TRUE(controller->IsReservedCommandOrKey(IDC_SELECT_TAB_2, key));
  EXPECT_TRUE(controller->IsReservedCommandOrKey(IDC_SELECT_LAST_TAB, key));

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_SELECT_TAB_2));
  EXPECT_EQ(window().ActiveRowId(), c);
  // Cmd+9 is the 9th row, with no jump to the last: four rows, nothing happens.
  chrome::ExecuteCommand(browser(), IDC_SELECT_LAST_TAB);
  EXPECT_EQ(window().ActiveRowId(), c);
  // Cmd+Option+Down and Up step through the same order without wrapping.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_FOCUS_NEXT_PANE));
  EXPECT_EQ(window().ActiveRowId(), startup);
  chrome::ExecuteCommand(browser(), IDC_FOCUS_NEXT_PANE);
  EXPECT_EQ(window().ActiveRowId(), startup);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_SELECT_TAB_0));
  EXPECT_EQ(window().ActiveRowId(), a);
  chrome::ExecuteCommand(browser(), IDC_FOCUS_PREVIOUS_PANE);
  EXPECT_EQ(window().ActiveRowId(), a);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_FOCUS_NEXT_PANE));
  EXPECT_EQ(window().ActiveRowId(), b);

  // Cmd+D pins and unpins; Cmd+Shift+D duplicates with history; Cmd+Shift+M mutes. Their main menu items carry
  // Clu's IDC_CLU_* ids (docs/chromium/m5.md).
  const int pin = FindCommand("tabs.togglePin")->idc;
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), pin));
  EXPECT_EQ(tree().Get(b)->section, Section::kPinned);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), pin));
  EXPECT_EQ(tree().Get(b)->section, Section::kToday);
  HandleBrowserCommand(browser(), FindCommand("tabs.toggleMute")->idc);
  EXPECT_TRUE(Contents(b)->IsAudioMuted());
  ASSERT_TRUE(content::NavigateToURL(Contents(b), Url("a.html")));
  HandleBrowserCommand(browser(), FindCommand("tabs.duplicate")->idc);
  const std::string copy = window().ActiveRowId();
  ASSERT_NE(copy, b);
  ASSERT_TRUE(content::WaitForLoadStop(Contents(copy)));
  EXPECT_TRUE(Contents(copy)->GetController().CanGoBack());
  EXPECT_TRUE(Contents(copy)->IsAudioMuted());
}

// Memory Saver's discard swaps a background page's WebContents (in place only where kWebContentsDiscard is on): the
// new page keeps the row's mute.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsADiscardedRowStaysMuted) {
  const std::string muted = Open("a.html");
  HandleBrowserCommand(browser(), FindCommand("tabs.toggleMute")->idc);
  ASSERT_TRUE(tree().Get(muted)->muted);
  Open("b.html");
  content::WebContents* before = Contents(muted);
  ASSERT_TRUE(resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
                  before)
                  ->DiscardTab(::mojom::LifecycleUnitDiscardReason::URGENT));
  ASSERT_TRUE(Contents(muted));
  EXPECT_TRUE(Contents(muted)->IsAudioMuted());
}

// 12.5 box 6 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsNoPageLeaksAfterTenOpenAndCloseCycles) {
  ASSERT_TRUE(content::WaitForLoadStop(strip().GetActiveWebContents()));
  const size_t baseline = content::GetAllWebContents().size();
  for (int i = 0; i < 10; ++i) {
    const std::string id = Open("a.html");
    content::WebContentsDestroyedWatcher destroyed(Contents(id));
    window().CloseRowCommand(id);
    destroyed.Wait();
    ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(id); }));
  }
  EXPECT_EQ(content::GetAllWebContents().size(), baseline);
  EXPECT_EQ(Archive().rows.size(), 10u);
}

// "−" on a loaded pinned row unloads it, and the published row says so at once, so the sidebar turns its
// "−" into "×" (rowModel.ts); the row stays, nothing is selected.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsUnloadingAPinnedRowPublishesItUnloaded) {
  const std::string id = Open("a.html");
  ASSERT_TRUE(window().TogglePin(id).has_value());
  ASSERT_EQ(tree().Get(id)->section, Section::kPinned);
  auto lifecycle = [&]() -> std::string {
    window().Flush();
    const base::DictValue state = window().CurrentState();
    const base::DictValue* tab = state.FindDict("tabs")->FindDict(id);
    const std::string* value = tab ? tab->FindString("lifecycle") : nullptr;
    return value ? *value : std::string();
  };
  ASSERT_EQ(lifecycle(), "live");
  content::WebContentsDestroyedWatcher destroyed(Contents(id));
  window().CloseRowCommand(id);
  destroyed.Wait();
  ASSERT_TRUE(base::test::RunUntil([&] { return lifecycle() == "unloaded"; }));
  EXPECT_TRUE(tree().Get(id));
  EXPECT_FALSE(window().FindTab(id));
  EXPECT_EQ(window().ActiveRowId(), std::string());
}

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsAPrivateWindowKeepsNoRows) {
  const size_t rows = tree().size();
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  CluWindowState* state = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->model());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, Url("a.html")));
  base::expected<std::string, std::string> id =
      state->CreateRow(Url("b.html"), std::nullopt, true);
  ASSERT_TRUE(id.has_value());
  EXPECT_TRUE(state->FindTab(id.value()));
  // The private tabs reached no row of the profile's model, and no archive.
  EXPECT_EQ(tree().size(), rows);
  state->CloseRowCommand(id.value());
  EXPECT_TRUE(Archive().rows.empty());
}

// F12 in a private window: its rows are its pages, so next and previous row step through them (Cmd+Option+Down,
// Ctrl+PageDown and the rest reach Clu as IDC_SELECT_NEXT_TAB and friends, which Clu claims).
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsAPrivateWindowStepsRows) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  CluWindowState* state = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(state);
  const std::string a =
      state->CreateRow(Url("a.html"), std::nullopt, true).value();
  const std::string b =
      state->CreateRow(Url("b.html"), std::nullopt, true).value();
  ASSERT_EQ(state->ActiveRowId(), b);
  ASSERT_TRUE(chrome::ExecuteCommand(incognito, IDC_SELECT_PREVIOUS_TAB));
  EXPECT_EQ(state->ActiveRowId(), a);
  ASSERT_TRUE(chrome::ExecuteCommand(incognito, IDC_SELECT_NEXT_TAB));
  EXPECT_EQ(state->ActiveRowId(), b);
  EXPECT_TRUE(state->RunCommand("tabs.prev"));
  EXPECT_EQ(state->ActiveRowId(), a);
}

// Copy URL as Markdown links a renamed row by its own name; a private window's copy is marked off the record (no
// Universal Clipboard, no clipboard history), a normal window's is not.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsCopyUrlUsesTheRowNameAndKeepsPrivateCopiesLocal) {
  ui::Clipboard::DestroyClipboardForCurrentThread();
  auto owned = std::make_unique<PrivacyRecordingClipboard>();
  PrivacyRecordingClipboard* clipboard = owned.get();
  ui::Clipboard::SetClipboardForCurrentThread(std::move(owned));

  const std::string row = Open("a.html");
  model().mutable_tree().Mutable(row)->title_override = "Q3 Board Deck";
  ASSERT_TRUE(window().RunCommand("edit.copyUrlMarkdown"));
  EXPECT_EQ(base::UTF16ToUTF8(ui::clipboard_test_util::ReadText(
                clipboard, ui::ClipboardBuffer::kCopyPaste, nullptr)),
            base::StrCat({"[Q3 Board Deck](", Url("a.html").spec(), ")"}));
  EXPECT_EQ(clipboard->last_privacy_types, 0u);

  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  CluWindowState* state = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(state);
  const std::string secret =
      state->CreateRow(Url("b.html"), std::nullopt, true).value();
  ASSERT_TRUE(content::WaitForLoadStop(state->FindTab(secret)->GetContents()));
  ASSERT_TRUE(state->RunCommand("edit.copyUrl"));
  EXPECT_TRUE(clipboard->last_privacy_types &
              ui::Clipboard::PrivacyTypes::kNoCloudClipboard);
  EXPECT_TRUE(clipboard->last_privacy_types &
              ui::Clipboard::PrivacyTypes::kNoLocalClipboardHistory);
}

// A private window keeps no rows: Cmd+D bookmarks the page as it did through M4 (the Pin Tab item stays disabled) and
// Duplicate is Chromium's own.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsAPrivateWindowBookmarksAndDuplicates) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  CluWindowState* state = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(state);
  const std::string row =
      state->CreateRow(Url("a.html"), std::nullopt, true).value();
  ASSERT_TRUE(content::WaitForLoadStop(state->FindTab(row)->GetContents()));
  bookmarks::BookmarkModel* bookmarks =
      BookmarkModelFactory::GetForBrowserContext(profile_);
  bookmarks::test::WaitForBookmarkModelToLoad(bookmarks);

  EXPECT_FALSE(state->CanRunCommand("tabs.togglePin"));
  EXPECT_TRUE(CluShell::From(incognito)->AcceleratorPressed(
      ui::Accelerator(ui::VKEY_D, ui::EF_COMMAND_DOWN)));
  EXPECT_TRUE(bookmarks->IsBookmarked(Url("a.html")));

  const int pages = incognito->GetTabStripModel()->count();
  EXPECT_TRUE(state->RunCommand("tabs.duplicate"));
  EXPECT_EQ(incognito->GetTabStripModel()->count(), pages + 1);
}

// A private window's rows move within its one list, take a name, and
// have a row menu (Copy Link, Rename, Duplicate, Mute, Close); pinning stays off, so private
// windows stay flat.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsAPrivateWindowMovesRenamesAndHasARowMenu) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  CluWindowState* state = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(state);
  const std::string a =
      state->CreateRow(Url("a.html"), std::nullopt, true).value();
  const std::string b =
      state->CreateRow(Url("b.html"), std::nullopt, true).value();
  const std::string c =
      state->CreateRow(Url("c.html"), std::nullopt, true).value();
  ASSERT_TRUE(content::WaitForLoadStop(state->FindTab(c)->GetContents()));
  // The window opened on a page of its own; only the three rows made here are under test.
  TabStripModel* private_pages = incognito->GetTabStripModel();
  for (int i = private_pages->count() - 1; i >= 0; --i) {
    content::WebContents* page = private_pages->GetWebContentsAt(i);
    if (page != state->FindTab(a)->GetContents() &&
        page != state->FindTab(b)->GetContents() &&
        page != state->FindTab(c)->GetContents()) {
      private_pages->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
    }
  }
  ASSERT_EQ(private_pages->count(), 3);
  const auto today = [&] {
    std::vector<std::string> ids;
    const base::DictValue current = state->CurrentState();
    for (const base::Value& id : *current.FindDict("spaces")
                                      ->FindDict(state->space_id())
                                      ->FindList("today")) {
      ids.push_back(id.GetString());
    }
    return ids;
  };
  ASSERT_EQ(today(), (std::vector<std::string>{a, b, c}));

  // Moves: c to the top, then a to the end; indices count the list without the moved row.
  CluTabsHandler tabs(state->GetWeakPtr());
  CallFuture top;
  tabs.Move(mojom::TabsMoveInput::New(c, TodayOf(state->space_id(), 0),
                                      std::nullopt, true, std::nullopt),
            top.GetCallback());
  ASSERT_TRUE(IsOk(top.Get()));
  EXPECT_EQ(today(), (std::vector<std::string>{c, a, b}));
  CallFuture end;
  tabs.Move(mojom::TabsMoveInput::New(a, TodayOf(state->space_id(), 2),
                                      std::nullopt, true, std::nullopt),
            end.GetCallback());
  ASSERT_TRUE(IsOk(end.Get()));
  EXPECT_EQ(today(), (std::vector<std::string>{c, b, a}));
  // Pinning stays off: Pinned and Favorites refuse, and junk indices too.
  std::vector<mojom::DropTargetPtr> refused_targets;
  refused_targets.push_back(PinnedOf(state->space_id(), std::nullopt, 0));
  refused_targets.push_back(Favorite(0));
  refused_targets.push_back(TodayOf(state->space_id(), 7));
  for (mojom::DropTargetPtr& target : refused_targets) {
    CallFuture refused;
    tabs.Move(mojom::TabsMoveInput::New(b, std::move(target), std::nullopt,
                                        true, std::nullopt),
              refused.GetCallback());
    EXPECT_FALSE(IsOk(refused.Get()));
  }
  EXPECT_EQ(today(), (std::vector<std::string>{c, b, a}));
  EXPECT_FALSE(state->CanRunCommand("tabs.togglePin"));

  // Rename: the row shows the name, cleaned and capped as a row's; an empty name shows the page title again.
  CallFuture renamed;
  tabs.Rename(mojom::TabsRenameInput::New(b, "  Secret plan  "),
              renamed.GetCallback());
  ASSERT_TRUE(IsOk(renamed.Get()));
  EXPECT_EQ(*state->CurrentState()
                 .FindDict("tabs")
                 ->FindDict(b)
                 ->FindString("titleOverride"),
            "Secret plan");
  CallFuture cleared;
  tabs.Rename(mojom::TabsRenameInput::New(b, ""), cleared.GetCallback());
  ASSERT_TRUE(IsOk(cleared.Get()));
  EXPECT_TRUE(state->CurrentState()
                  .FindDict("tabs")
                  ->FindDict(b)
                  ->Find("titleOverride")
                  ->is_none());

  // The row menu: its five items, nothing that pins, moves to a space or archives; each runs.
  CluTabMenu& menu = state->tab_menu();
  CluTabMenu::Request request;
  request.menu = "row";
  request.node_id = c;
  const std::vector<MenuItem> items = menu.Build(request, false);
  const std::vector<std::string> leaves = EnabledLeaves(items);
  EXPECT_EQ(leaves, (std::vector<std::string>{kMenuCopyLink, kMenuRename,
                                              kMenuDuplicate, kMenuMute,
                                              kMenuClose}));
  EXPECT_FALSE(menu.Run(request, kMenuPin, std::string()).has_value());
  EXPECT_EQ(menu.Run(request, kMenuRename, std::string()), c);
  menu.Run(request, kMenuCopyLink, std::string());
  EXPECT_EQ(ui::clipboard_test_util::ReadText(
                ui::Clipboard::GetForCurrentThread(),
                ui::ClipboardBuffer::kCopyPaste, nullptr),
            base::UTF8ToUTF16(Url("c.html").spec()));
  menu.Run(request, kMenuMute, std::string());
  EXPECT_TRUE(state->FindTab(c)->GetContents()->IsAudioMuted());
  const int pages = incognito->GetTabStripModel()->count();
  menu.Run(request, kMenuDuplicate, std::string());
  EXPECT_EQ(incognito->GetTabStripModel()->count(), pages + 1);
  menu.Run(request, kMenuClose, std::string());
  ASSERT_TRUE(base::test::RunUntil([&] { return !state->FindTab(c); }));
}

// The + and "..." menus and the folder copies: + lists New
// Space, New Folder, New Split and New Tab; Copy All Links copies the folder's tabs and is off while it has none;
// "..." holds the two controls the narrow top row folds away, and they run.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsPlusFolderAndMoreMenusRun) {
  CluTabMenu& menu = window().tab_menu();
  auto labels = [](const std::vector<MenuItem>& items) {
    std::vector<std::string> out;
    for (const MenuItem& item : items) {
      out.push_back(item.separator ? "-" : item.label);
    }
    return out;
  };
  CluTabMenu::Request plus;
  plus.menu = "new";
  EXPECT_EQ(labels(menu.Build(plus, false)),
            (std::vector<std::string>{"New Space", "New Folder", "-",
                                      "New Split", "New Tab"}));
  const std::optional<std::string> folder =
      menu.Run(plus, kMenuNewFolder, std::string());
  ASSERT_TRUE(folder);
  CluTabMenu::Request on_folder;
  on_folder.menu = "row";
  on_folder.node_id = *folder;
  EXPECT_FALSE(std::ranges::contains(EnabledLeaves(menu.Build(on_folder, false)),
                                     std::string(kMenuFolderCopyLinks)));
  const std::string a = Open("a.html");
  ASSERT_TRUE(window()
                  .MoveRows({a}, DropTarget::Pinned(space(), *folder, 0), false)
                  .has_value());
  menu.Run(on_folder, kMenuFolderCopyLinks, std::string());
  EXPECT_EQ(ui::clipboard_test_util::ReadText(
                ui::Clipboard::GetForCurrentThread(),
                ui::ClipboardBuffer::kCopyPaste, nullptr),
            base::UTF8ToUTF16(Url("a.html").spec()));

  CluTabMenu::Request more;
  more.menu = "more";
  EXPECT_EQ(labels(menu.Build(more, false)),
            (std::vector<std::string>{"Reload", "Toggle Sidebar"}));
  ASSERT_FALSE(shell().chrome_state().sidebar_hidden);
  menu.Run(more, kMenuToggleSidebar, std::string());
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  // Nothing outside the menu runs through it.
  menu.Run(more, "tabs.close", std::string());
  EXPECT_TRUE(tree().Get(a));
}

// Cmd+Shift+M with nothing to mute (the empty state) is consumed: let go, Chromium's key lookup opens the avatar menu.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsMuteWithNothingToMuteKeepsItsChord) {
  while (!window().ActiveRowId().empty()) {
    const std::string active = window().ActiveRowId();
    ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
    ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(active); }));
  }
  EXPECT_FALSE(window().CanRunCommand("tabs.toggleMute"));
  EXPECT_TRUE(shell().AcceleratorPressed(
      ui::Accelerator(ui::VKEY_M, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)));
}

// 12.6 box 1 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsAutoArchiveAtThirteenHoursOnTheTestClock) {
  clock_.SetNow(base::Time::Now());
  model().SetClockForTesting(&clock_);
  base::SimpleTestClock& clock = clock_;
  const std::string idle = Open("a.html");
  const std::string audible = Open("audio.html");
  EXPECT_EQ("running", content::EvalJs(Contents(audible), "startTone()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Contents(audible)->IsCurrentlyAudible(); }));
  const std::string active = Open("b.html");
  content::WebContents* idle_page = Contents(idle);
  content::WebContentsDestroyedWatcher idle_gone(idle_page);

  clock.Advance(base::Hours(11));
  EXPECT_TRUE(model().Sweep(false).empty());
  clock.Advance(base::Hours(2));
  const std::vector<std::string> archived = model().Sweep(false);
  // The startup row and the idle row go; the active and the audible rows stay.
  EXPECT_TRUE(std::ranges::contains(archived, idle));
  EXPECT_FALSE(std::ranges::contains(archived, active));
  EXPECT_FALSE(std::ranges::contains(archived, audible));
  idle_gone.Wait();
  EXPECT_FALSE(tree().Get(idle));
  EXPECT_EQ(window().ActiveRowId(), active);
  for (const ArchiveRow& row : Archive().rows) {
    EXPECT_EQ(row.reason, ArchiveReason::kAuto);
  }
  // Cmd+Shift+T never brings back an auto-archived tab.
  chrome::ExecuteCommand(browser(), IDC_RESTORE_TAB);
  FlushStore();
  EXPECT_FALSE(tree().Get(idle));
}

// Every new tab lands at the top of Today, under New Tab: a link opened from the last
// Today row, and then one opened from that new tab, each go to the top, never under their opener.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsALinkTabGoesToTheTopOfToday) {
  const std::string a = Open("a.html");
  Open("b.html");
  Open("c.html");
  ASSERT_TRUE(window().ActivateRow(a));
  ASSERT_NE(tree().Today(space()).front(), a);
  content::WebContents* from = Contents(a);
  ASSERT_TRUE(from);
  for (const char* page : {"a.html?one", "b.html?two"}) {
    const GURL url = Url(page);
    content::TestNavigationObserver opened(url);
    opened.StartWatchingNewWebContents();
    ASSERT_TRUE(content::ExecJs(
        from, content::JsReplace("window.open($1, '_blank')", url)));
    opened.Wait();
    ASSERT_TRUE(base::test::RunUntil([&] {
      const std::vector<std::string>& today = tree().Today(space());
      const TabNode* top = today.empty() ? nullptr : tree().Get(today.front());
      return top && top->url == url.spec();
    }));
    from = Contents(tree().Today(space()).front());
    ASSERT_TRUE(from);
  }
  EXPECT_EQ(tree().Today(space()).back(), a);
}

// 12.6 box 2 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsReopenClosedRestoresHistoryAndClearBatches) {
  const std::string row = Open("a.html");
  ASSERT_TRUE(content::NavigateToURL(Contents(row), Url("b.html")));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(row); }));
  // Cmd+Shift+T: back at the top of Today with its history.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_RESTORE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] {
    tabs::TabInterface* active = window().ActiveTab();
    return active && active->GetContents()->GetVisibleURL() == Url("b.html");
  }));
  const std::string back = window().ActiveRowId();
  EXPECT_EQ(tree().Today(space()).front(), back);
  ASSERT_TRUE(content::WaitForLoadStop(Contents(back)));
  EXPECT_TRUE(Contents(back)->GetController().CanGoBack());

  // Clear takes the Today rows off the list with an Undo toast; Undo puts the same pages back.
  Open("b.html");
  Open("c.html");
  const std::vector<std::string> cleared = tree().Today(space());
  std::vector<content::WebContents*> pages;
  std::vector<std::string> urls;
  for (const std::string& id : cleared) {
    pages.push_back(Contents(id));
    urls.push_back(tree().Get(id)->url);
  }
  EXPECT_EQ(window().ClearToday(std::nullopt),
            static_cast<int>(cleared.size()));
  {
    const base::DictValue state = window().CurrentState();
    EXPECT_TRUE(state.FindDict("spaces")
                    ->FindDict(space())
                    ->FindList("today")
                    ->empty());
    const base::ListValue* toasts = state.FindDict("windows")
                                        ->FindDict(window().window_id())
                                        ->FindList("toasts");
    ASSERT_EQ(toasts->size(), 1u);
    EXPECT_EQ(*(*toasts)[0].GetDict().FindStringByDottedPath("action.commandId"),
              "archive.undoClear");
  }
  EXPECT_TRUE(shell().IsPassiveVisible(0));
  ASSERT_TRUE(window().RunCommand("archive.undoClear"));
  EXPECT_EQ(tree().Today(space()), cleared);
  for (size_t i = 0; i < cleared.size(); ++i) {
    if (pages[i]) {
      EXPECT_EQ(model().FindContents(cleared[i]), pages[i]);
    }
  }

  // Once the toast ends the batch archives; Cmd+Shift+T brings all of it back in order.
  window().ClearToday(std::nullopt);
  model().CommitClear();
  EXPECT_TRUE(tree().Today(space()).empty());
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_RESTORE_TAB));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Today(space()).size() == cleared.size(); }));
  for (size_t i = 0; i < cleared.size(); ++i) {
    EXPECT_EQ(tree().Get(tree().Today(space())[i])->url, urls[i]) << i;
  }
}

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsTheArchivePanelRestoresAndSearchesLiterally) {
  const std::string percent = Open("a.html");
  model().mutable_tree().Mutable(percent)->title_override = "50% off";
  model().Commit();
  const std::string plain = Open("b.html");
  model().ArchiveRows({percent, plain}, ArchiveReason::kClose);

  CluArchiveHandler archive(window().GetWeakPtr());
  CallFuture opened;
  archive.Open(opened.GetCallback());
  ASSERT_TRUE(IsOk(opened.Get()));
  EXPECT_TRUE(window().archive_panel());
  CallFuture listed;
  archive.List(mojom::ArchiveListInput::New(model().profile_id(), "50%",
                                            std::nullopt, std::nullopt),
               listed.GetCallback());
  mojom::CallResultPtr result = listed.Take();
  ASSERT_TRUE(IsOk(result));
  const std::optional<base::DictValue> page =
      base::JSONReader::ReadDict(result->get_value_json(), base::JSON_PARSE_RFC);
  const base::ListValue* rows = page->FindList("rows");
  ASSERT_EQ(rows->size(), 1u);
  const std::string archive_id = *(*rows)[0].GetDict().FindString("id");
  EXPECT_EQ(*(*rows)[0].GetDict().FindString("title"), "50% off");

  // Another profile's archive is not this window's to read.
  CallFuture foreign;
  archive.List(mojom::ArchiveListInput::New(
                   "00000000-0000-4000-8000-000000000001", std::nullopt,
                   std::nullopt, std::nullopt),
               foreign.GetCallback());
  EXPECT_EQ(foreign.Get()->get_value_json(), R"({"nextCursor":null,"rows":[]})");

  // Restore: the top of Today in its space, shown, and the archive row is gone.
  CallFuture restored;
  archive.Restore(mojom::ArchiveRestoreInput::New(archive_id),
                  restored.GetCallback());
  ASSERT_TRUE(IsOk(restored.Get()));
  EXPECT_EQ(window().ActiveRowId(), tree().Today(space()).front());
  ASSERT_TRUE(content::WaitForLoadStop(Contents(window().ActiveRowId())));
  EXPECT_EQ(Contents(window().ActiveRowId())->GetLastCommittedURL(),
            Url("a.html"));
  FlushStore();
  EXPECT_EQ(Archive().rows.size(), 1u);

  CallFuture missing;
  archive.Restore(mojom::ArchiveRestoreInput::New(archive_id),
                  missing.GetCallback());
  EXPECT_EQ(Code(missing.Get()), mojom::ErrorCode::kNotFound);
}

// 12.6 box 3 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsCtrlTabTapAndHoldFromAFocusedPage) {
  const std::string first = Open("a.html");
  const std::string second = Open("b.html");
  const std::string third = Open("c.html");
  Contents(third)->Focus();
  // A tap: Ctrl+Tab then the Control key up within 180 ms switches to the last tab, with no overlay.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CYCLE_TO_NEXT_TAB));
  window().OnEvent(ui::KeyEvent(ui::EventType::kKeyReleased, ui::VKEY_CONTROL,
                                ui::EF_NONE));
  EXPECT_EQ(window().ActiveRowId(), second);
  EXPECT_FALSE(shell().IsPassiveVisible(1));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CYCLE_TO_NEXT_TAB));
  window().OnEvent(ui::KeyEvent(ui::EventType::kKeyReleased, ui::VKEY_CONTROL,
                                ui::EF_NONE));
  EXPECT_EQ(window().ActiveRowId(), third);

  // A hold: the switcher shows after 180 ms; Tab again steps; the release commits the third most recent.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CYCLE_TO_NEXT_TAB));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return shell().IsPassiveVisible(1); }));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CYCLE_TO_NEXT_TAB));
  window().OnEvent(ui::KeyEvent(ui::EventType::kKeyReleased, ui::VKEY_CONTROL,
                                ui::EF_NONE));
  EXPECT_EQ(window().ActiveRowId(), first);
  EXPECT_FALSE(shell().IsPassiveVisible(1));
  // The switcher never took focus from the page.
  EXPECT_TRUE(Contents(first)->GetRenderWidgetHostView()->HasFocus() ||
              !browser()->IsActive());
}

// Tabs > Next Recent Tab, or the palette row, switches at once: no Control key is held, so no key-up would ever end
// a switcher session.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsNextRecentTabFromTheMenuSwitchesAtOnce) {
  const std::string first = Open("a.html");
  const std::string second = Open("b.html");
  HandleBrowserCommand(browser(), FindCommand("archive.mruNext")->idc);
  EXPECT_FALSE(window().switcher().active());
  EXPECT_EQ(window().ActiveRowId(), first);
  EXPECT_TRUE(window().RunCommand("archive.mruNext"));
  EXPECT_FALSE(window().switcher().active());
  EXPECT_EQ(window().ActiveRowId(), second);
  content::RunAllTasksUntilIdle();
  EXPECT_FALSE(shell().IsPassiveVisible(1));
}

// The hover card is a passive surface hung from the pointer, which the anchor carries (tabs-t5).
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsTheHoverCardHangsFromThePointer) {
  const std::string row = Open("a.html");
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  EXPECT_TRUE(shell().IsPassiveVisible(2));
  const base::DictValue state = window().CurrentState();
  const base::ListValue* surfaces =
      state.FindDict("windows")
          ->FindDict(window().window_id())
          ->FindListByDottedPath("overlay.surfaces");
  ASSERT_EQ(surfaces->size(), 1u);
  const base::DictValue& card = (*surfaces)[0].GetDict();
  EXPECT_EQ(*card.FindString("surface"), "tab-hover");
  EXPECT_EQ(*card.FindString("layer"), "passive");
  EXPECT_EQ(card.FindDoubleByDottedPath("rect.x").value_or(0), 60 + 11 - 8);
  EXPECT_EQ(card.FindDoubleByDottedPath("rect.y").value_or(0), 135 + 12 - 8);
  window().Hover(std::nullopt, std::nullopt);
  EXPECT_FALSE(shell().IsPassiveVisible(2));
}

// The pointer leaving a row fades the title pill out over kHoverCardOut: the card
// stays marked closing for the overlay's fade, then goes, once. A new hover during the fade keeps the card up; Rio's
// own reasons to hide it (here a second leave) end the fade at once.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsTheHoverCardFadesOutOnce) {
  const std::string row = Open("a.html");
  base::DictValue state;
  auto card = [&]() -> const base::DictValue* {
    state = window().CurrentState();
    const base::ListValue* surfaces =
        state.FindDict("windows")
            ->FindDict(window().window_id())
            ->FindListByDottedPath("overlay.surfaces");
    return surfaces && surfaces->size() == 1u ? &(*surfaces)[0].GetDict()
                                              : nullptr;
  };
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  ASSERT_TRUE(shell().IsPassiveVisible(2));
  ASSERT_TRUE(card());
  EXPECT_FALSE(card()->FindBoolByDottedPath("props.closing").value_or(false));

  const base::TimeTicks left = base::TimeTicks::Now();
  window().Hover(std::nullopt, std::nullopt, /*fade=*/true);
  EXPECT_TRUE(shell().IsPassiveVisible(2));
  ASSERT_TRUE(card());
  EXPECT_TRUE(card()->FindBoolByDottedPath("props.closing").value_or(false));
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().IsPassiveVisible(2); }));
  EXPECT_GE(base::TimeTicks::Now() - left, CluWindowState::kHoverCardOut);
  EXPECT_FALSE(card());

  // Back on the row mid-fade: the same card shows again, not closing, and nothing hides it after the fade's time.
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  const std::string id = *card()->FindString("instanceId");
  window().Hover(std::nullopt, std::nullopt, /*fade=*/true);
  window().Hover(row, gfx::Rect(60, 140, 0, 0));
  ASSERT_TRUE(card());
  EXPECT_EQ(*card()->FindString("instanceId"), id);
  EXPECT_FALSE(card()->FindBoolByDottedPath("props.closing").value_or(false));
  const base::TimeTicks back = base::TimeTicks::Now();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return base::TimeTicks::Now() - back > CluWindowState::kHoverCardOut * 3;
  }));
  EXPECT_TRUE(shell().IsPassiveVisible(2));

  // A second leave during the fade ends it at once; the internal hide never fades.
  window().Hover(std::nullopt, std::nullopt, /*fade=*/true);
  window().Hover(std::nullopt, std::nullopt, /*fade=*/true);
  EXPECT_FALSE(shell().IsPassiveVisible(2));
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  window().Hover(std::nullopt, std::nullopt);
  EXPECT_FALSE(shell().IsPassiveVisible(2));
}

// An open hover card follows the page's title as it changes ("Settings" became "Settings - About Rio"), and
// it closes when another overlay opens.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsTheHoverCardFollowsTheTitleAndYieldsToOverlays) {
  const std::string row = Open("a.html");
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  ASSERT_TRUE(shell().IsPassiveVisible(2));
  auto card_title = [&]() -> std::string {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* surfaces =
        state.FindDict("windows")
            ->FindDict(window().window_id())
            ->FindListByDottedPath("overlay.surfaces");
    if (!surfaces || surfaces->empty()) {
      return std::string();
    }
    const std::string* title =
        (*surfaces)[0].GetDict().FindStringByDottedPath("props.title");
    return title ? *title : std::string();
  };
  ASSERT_TRUE(content::ExecJs(Contents(row), "document.title = 'Renamed - About'"));
  EXPECT_TRUE(base::test::RunUntil([&] { return card_title() == "Renamed - About"; }));
  // Another overlay opening (here the theme editor) closes the card.
  window().OpenThemePicker(space(), std::nullopt);
  EXPECT_FALSE(shell().IsPassiveVisible(2));
  EXPECT_TRUE(card_title().empty());
}

// Live Folders ▸ RSS or Atom Feed…: a pinned folder named after the feed, holding its entries as pinned rows
// without pages, newest first; a source that names no feed is refused and a page that is no feed leaves the folder
// empty with a toast. The folder is recorded in Local State for the half-hourly refresh.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsALiveFolderFollowsAFeed) {
  CluFoldersHandler folders(window().GetWeakPtr());
  CallFuture refused;
  folders.CreateLive(mojom::FoldersCreateLiveInput::New(
                         space(), mojom::FoldersCreateLiveInputKind::kFeed,
                         "javascript:alert(1)"),
                     refused.GetCallback());
  ASSERT_TRUE(refused.Get()->is_error());
  EXPECT_EQ(Code(refused.Get()), mojom::ErrorCode::kBadPayload);
  auto folder_of = [](const mojom::CallResultPtr& result) {
    return *base::JSONReader::ReadDict(result->get_value_json(),
                                       base::JSON_PARSE_RFC)
                ->FindString("folderId");
  };

  CallFuture made;
  folders.CreateLive(
      mojom::FoldersCreateLiveInput::New(
          space(), mojom::FoldersCreateLiveInputKind::kFeed,
          embedded_test_server()->GetURL("/live/feed.xml").spec()),
      made.GetCallback());
  ASSERT_TRUE(IsOk(made.Get()));
  const std::string folder = folder_of(made.Get());
  EXPECT_NE(std::ranges::find(tree().Pinned(space()), folder),
            tree().Pinned(space()).end());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Children(folder).size() == 2u; }));
  const TabNode* first = tree().Get(tree().Children(folder)[0]);
  const TabNode* second = tree().Get(tree().Children(folder)[1]);
  EXPECT_EQ(first->title, "Boats are back");
  EXPECT_EQ(first->pinned_url, Url("a.html").spec());
  EXPECT_EQ(second->title, "Fish & chips");
  EXPECT_EQ(tree().Get(folder)->title_override, "Harbor News");
  EXPECT_FALSE(window().FindTab(first->id));
  EXPECT_TRUE(g_browser_process->local_state()
                  ->GetDict(kCluLiveFoldersPref)
                  .FindDict(folder));

  // A page that is no feed: the folder stays empty and the window says why.
  CallFuture empty;
  folders.CreateLive(
      mojom::FoldersCreateLiveInput::New(
          space(), mojom::FoldersCreateLiveInputKind::kFeed,
          embedded_test_server()->GetURL("/live/not-a-feed.html").spec()),
      empty.GetCallback());
  ASSERT_TRUE(IsOk(empty.Get()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* toasts =
        state.FindDict("windows")->FindDict(window().window_id())->FindList("toasts");
    return toasts && !toasts->empty() &&
           *toasts->back().GetDict().FindString("text") ==
               "Couldn't read this feed";
  }));
  EXPECT_TRUE(tree().Children(folder_of(empty.Get())).empty());

  // A deleted live folder leaves Local State at the next refresh.
  CallFuture deleted;
  folders.Delete(mojom::FoldersDeleteInput::New(folder), deleted.GetCallback());
  ASSERT_TRUE(IsOk(deleted.Get()));
  CluLiveFolders::Get().RefreshAll();
  EXPECT_FALSE(g_browser_process->local_state()
                   ->GetDict(kCluLiveFoldersPref)
                   .FindDict(folder));
}

// A title that changes while the card shows is the card's title at once ("Settings" became "Settings - About Rio" and the
// card kept "Settings").
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsTheHoverCardFollowsTheTitle) {
  const std::string row = Open("a.html");
  window().Hover(row, gfx::Rect(60, 135, 0, 0));
  auto card_title = [&] {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* surfaces =
        state.FindDict("windows")
            ->FindDict(window().window_id())
            ->FindListByDottedPath("overlay.surfaces");
    const std::string* title =
        surfaces && surfaces->size() == 1u
            ? (*surfaces)[0].GetDict().FindStringByDottedPath("props.title")
            : nullptr;
    return title ? *title : std::string();
  };
  ASSERT_FALSE(card_title().empty());
  ASSERT_TRUE(content::ExecJs(Contents(row), "document.title = 'A renamed page'"));
  EXPECT_TRUE(base::test::RunUntil([&] { return card_title() == "A renamed page"; }))
      << card_title();
  EXPECT_TRUE(shell().IsPassiveVisible(2));
  window().Hover(std::nullopt, std::nullopt);
}

// A pinned Rio page shows and copies as its rio:// URL everywhere, and sits on its pinned page: no offPinned, the hover
// card reads rio://history, Copy Link pastes into the command bar as rio://history/, and Change Pinned URL takes a
// typed rio:// URL.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsAPinnedRioPageKeepsItsAlias) {
  base::expected<std::string, std::string> made = window().CreateRow(
      GURL("rio://history/"), std::nullopt, /*activate=*/true);
  ASSERT_TRUE(made.has_value());
  const std::string row = made.value();
  ASSERT_TRUE(content::WaitForLoadStop(Contents(row)));
  ASSERT_TRUE(window().TogglePin(row).has_value());
  ASSERT_EQ(tree().Get(row)->pinned_url, "chrome://history/");
  auto tab_state = [&] {
    return window().CurrentState().FindDict("tabs")->FindDict(row)->Clone();
  };
  EXPECT_EQ(*tab_state().FindString("url"), "rio://history/");
  EXPECT_EQ(tab_state().FindBool("offPinned"), false);

  window().Hover(row, gfx::Rect(8, 120, 240, 30));
  const base::DictValue state = window().CurrentState();
  const base::DictValue& card = (*state.FindDict("windows")
                                      ->FindDict(window().window_id())
                                      ->FindListByDottedPath(
                                          "overlay.surfaces"))[0]
                                    .GetDict();
  EXPECT_EQ(*card.FindStringByDottedPath("props.location"), "rio://history");
  window().Hover(std::nullopt, std::nullopt);

  CluTabMenu& menu = window().tab_menu();
  CluTabMenu::Request request;
  request.menu = "row";
  request.node_id = row;
  menu.Run(request, kMenuCopyLink, std::string());
  EXPECT_EQ(ui::clipboard_test_util::ReadText(
                ui::Clipboard::GetForCurrentThread(),
                ui::ClipboardBuffer::kCopyPaste, nullptr),
            u"rio://history/");
  menu.Run(request, kMenuPinnedUrlEnter, std::string());
  EXPECT_EQ(*window()
                 .CurrentState()
                 .FindDict("windows")
                 ->FindDict(window().window_id())
                 ->FindListByDottedPath("overlay.surfaces")
                 ->front()
                 .GetDict()
                 .FindStringByDottedPath("props.url"),
            "rio://history/");

  CluTabsHandler tabs(window().GetWeakPtr());
  CallFuture set;
  tabs.SetPinnedUrl(mojom::TabsSetPinnedUrlInput::New(row, "rio://settings"),
                    set.GetCallback());
  ASSERT_TRUE(set.Get()->is_value_json());
  EXPECT_EQ(tree().Get(row)->pinned_url, "chrome://settings/");
}

// Relaunch (PLAN M3 "Relaunch restores the tree, and only the active tab loads") ---------------------------------------

class CluTabsRelaunchBrowserTest : public CluTabsBrowserTest {
 protected:
  CluTabsRelaunchBrowserTest() {
    // No startup tab: the window shows what Clu restores.
    set_open_about_blank_on_browser_launch(false);
  }

  // A PRE_ test and its main test run in two processes, and the test server's port changes between them, so the
  // saved rows point at files, which stay where they are.
  GURL Url(const std::string& page) override {
    return net::FilePathToFileURL(
        base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
            .AppendASCII("clu/test/data/tabs")
            .AppendASCII(page));
  }
};

IN_PROC_BROWSER_TEST_F(CluTabsRelaunchBrowserTest,
                       PRE_CluTabsRelaunchRestoresTheTreeAndLoadsOnlyTheActiveTab) {
  const std::string favorite = Open("a.html");
  ASSERT_TRUE(window().MoveRows({favorite}, DropTarget::Favorite(0), false)
                  .has_value());
  const std::string pinned = Open("b.html");
  ASSERT_TRUE(window()
                  .MoveRows({pinned}, DropTarget::Pinned(space(), "", 0), false)
                  .has_value());
  ASSERT_TRUE(content::NavigateToURL(Contents(pinned), Url("c.html")));
  const std::string folder =
      model()
          .mutable_tree()
          .CreateFolder(space(), std::string(), "Reading", std::nullopt,
                        model().NowMs())
          .value();
  model().mutable_tree().Mutable(folder)->collapsed = true;
  model().Commit();
  Open("c.html");
  const std::string active = Open("a.html");
  ASSERT_TRUE(content::NavigateToURL(Contents(active), Url("b.html")));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Get(active)->nav_state.has_value(); }));
  model().CommitSoon();
  FlushStore();
}

IN_PROC_BROWSER_TEST_F(CluTabsRelaunchBrowserTest,
                       CluTabsRelaunchRestoresTheTreeAndLoadsOnlyTheActiveTab) {
  // The tree is back: one favorite, a pinned row and a collapsed folder, two Today rows.
  EXPECT_EQ(tree().Favorites().size(), 1u);
  ASSERT_EQ(tree().Pinned(space()).size(), 2u);
  ASSERT_EQ(tree().Today(space()).size(), 2u);
  const TabNode* pinned = nullptr;
  const TabNode* folder = nullptr;
  for (const std::string& id : tree().Pinned(space())) {
    (tree().Get(id)->is_folder() ? folder : pinned) = tree().Get(id);
  }
  ASSERT_TRUE(pinned);
  ASSERT_TRUE(folder);
  EXPECT_TRUE(folder->collapsed);
  // A pinned row is back at its pinned URL (F2).
  EXPECT_EQ(GURL(pinned->url), Url("b.html"));

  // Only the restored active row has a page; every other row is unloaded.
  const std::string active = tree().Today(space()).front();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window().ActiveRowId() == active; }));
  EXPECT_EQ(LivePages(), 1);
  EXPECT_FALSE(Contents(pinned->id));
  // Its history came back: Back works.
  content::WebContents* page = Contents(active);
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(page->GetLastCommittedURL(), Url("b.html"));
  ASSERT_TRUE(page->GetController().CanGoBack());
  content::TestNavigationObserver back(page);
  page->GetController().GoBack();
  back.Wait();
  EXPECT_EQ(page->GetLastCommittedURL(), Url("a.html"));

  // The frame lists the unloaded rows too.
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, RowSelector(pinned->id));
}

IN_PROC_BROWSER_TEST_F(CluTabsRelaunchBrowserTest,
                       PRE_CluTabsTheLaunchSweepArchivesIdleRows) {
  const std::string idle = Open("a.html");
  const std::string active = Open("b.html");
  // Both idle for 13 hours; the active one is the space's restored row.
  const int64_t thirteen_hours_ago =
      (base::Time::Now() - base::Hours(13)).InMillisecondsSinceUnixEpoch();
  model().mutable_tree().Mutable(idle)->last_active_at = thirteen_hours_ago;
  model().mutable_tree().Mutable(active)->last_active_at = thirteen_hours_ago;
  model().mutable_tree().MutableSpaceState(space()).active_tab_id = active;
  model().Commit();
  FlushStore();
}

IN_PROC_BROWSER_TEST_F(CluTabsRelaunchBrowserTest,
                       CluTabsTheLaunchSweepArchivesIdleRows) {
  // The launch sweep ran before the window showed its rows: the idle row is in the archive, the restored one shows.
  ASSERT_EQ(tree().Today(space()).size(), 1u);
  EXPECT_EQ(GURL(tree().Get(tree().Today(space())[0])->url), Url("b.html"));
  const ArchivePage page = Archive();
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].reason, ArchiveReason::kAuto);
  EXPECT_EQ(GURL(page.rows[0].url), Url("a.html"));
}

// The collapsed-folder popover: opens in the hover card's passive slot, holds while the pointer is over it, and closes
// when the row is left unheld or the pointer leaves it.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsFolderPeekHoldsAndCloses) {
  CluFoldersHandler folders(window().GetWeakPtr());
  CallFuture created;
  folders.Create(mojom::FoldersCreateInput::New(space(), std::nullopt),
                 created.GetCallback());
  mojom::CallResultPtr result = created.Take();
  ASSERT_TRUE(IsOk(result));
  const std::string folder = *base::JSONReader::ReadDict(
                                  result->get_value_json(), base::JSON_PARSE_RFC)
                                  ->FindString("folderId");
  auto surfaces = [&] {
    return window()
        .CurrentState()
        .FindDict("windows")
        ->FindDict(window().window_id())
        ->FindListByDottedPath("overlay.surfaces")
        ->Clone();
  };
  auto peek = [&](std::optional<std::string> id, mojom::RectPtr rect,
                  std::optional<bool> held) {
    CallFuture done;
    folders.Peek(mojom::FoldersPeekInput::New(std::move(id), std::move(rect),
                                              held),
                 done.GetCallback());
    return done.Take();
  };
  ASSERT_TRUE(IsOk(peek(folder, mojom::Rect::New(264, 300, 256, 117),
                        std::nullopt)));
  ASSERT_EQ(surfaces().size(), 1u);
  EXPECT_EQ(*surfaces()[0].GetDict().FindString("surface"), "folder-peek");
  EXPECT_EQ(*surfaces()[0].GetDict().FindString("layer"), "passive");
  // Held: leaving the row does not close it; leaving the popover does.
  ASSERT_TRUE(IsOk(peek(folder, nullptr, true)));
  ASSERT_TRUE(IsOk(peek(std::nullopt, nullptr, std::nullopt)));
  EXPECT_EQ(surfaces().size(), 1u);
  ASSERT_TRUE(IsOk(peek(folder, nullptr, false)));
  EXPECT_TRUE(surfaces().empty());
  // Unheld, leaving the row closes it; an unknown folder is refused.
  ASSERT_TRUE(IsOk(peek(folder, mojom::Rect::New(264, 300, 256, 117),
                        std::nullopt)));
  ASSERT_TRUE(IsOk(peek(std::nullopt, nullptr, std::nullopt)));
  EXPECT_TRUE(surfaces().empty());
  EXPECT_EQ(peek(base::Uuid::GenerateRandomV4().AsLowercaseString(),
                 mojom::Rect::New(0, 0, 10, 10), std::nullopt)
                ->get_error()
                ->code,
            mojom::ErrorCode::kNotFound);
}

// The popover takes the keyboard for its search field, and hovering another folder shows that folder's
// popover as a new instance (fresh search and highlight), never the last folder's tabs.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsFolderPeekFocusesAndFollowsTheHoveredFolder) {
  CluFoldersHandler folders(window().GetWeakPtr());
  auto create = [&] {
    CallFuture created;
    folders.Create(mojom::FoldersCreateInput::New(space(), std::nullopt),
                   created.GetCallback());
    mojom::CallResultPtr result = created.Take();
    return *base::JSONReader::ReadDict(result->get_value_json(),
                                       base::JSON_PARSE_RFC)
                ->FindString("folderId");
  };
  const std::string yc = create();
  const std::string providers = create();
  auto surfaces = [&] {
    return window()
        .CurrentState()
        .FindDict("windows")
        ->FindDict(window().window_id())
        ->FindListByDottedPath("overlay.surfaces")
        ->Clone();
  };
  auto open = [&](const std::string& id) {
    CallFuture done;
    folders.Peek(mojom::FoldersPeekInput::New(
                     id, mojom::Rect::New(264, 300, 256, 117), std::nullopt),
                 done.GetCallback());
    return IsOk(done.Take());
  };
  ASSERT_TRUE(open(yc));
  ASSERT_EQ(surfaces().size(), 1u);
  const base::DictValue first = surfaces()[0].GetDict().Clone();
  EXPECT_EQ(first.FindBool("focus"), true);
  EXPECT_EQ(*first.FindStringByDottedPath("props.folderId"), yc);
  EXPECT_TRUE(shell().IsPassiveVisible(2));
  // The same folder again keeps its instance; another folder replaces it with a new one.
  ASSERT_TRUE(open(yc));
  EXPECT_EQ(*surfaces()[0].GetDict().FindString("instanceId"),
            *first.FindString("instanceId"));
  ASSERT_TRUE(open(providers));
  ASSERT_EQ(surfaces().size(), 1u);
  EXPECT_EQ(*surfaces()[0].GetDict().FindStringByDottedPath("props.folderId"),
            providers);
  EXPECT_NE(*surfaces()[0].GetDict().FindString("instanceId"),
            *first.FindString("instanceId"));
  // A press on a row closes it with the folder it shows, even after the frame's unheld close request came and went.
  CallFuture closed;
  folders.Peek(mojom::FoldersPeekInput::New(providers, nullptr, false),
               closed.GetCallback());
  ASSERT_TRUE(IsOk(closed.Take()));
  EXPECT_TRUE(surfaces().empty());
  EXPECT_FALSE(shell().IsPassiveVisible(2));
}

// A favorite selects on the press, before any release, and its tile carries neither a
// title tooltip nor a speaker badge.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsFavoriteSelectsOnPressWithNoTooltipOrBadge) {
  const std::string first = Open("a.html");
  ASSERT_TRUE(window().MoveRows({first}, DropTarget::Favorite(0), false)
                  .has_value());
  const std::string second = Open("b.html");
  ASSERT_TRUE(window().MoveRows({second}, DropTarget::Favorite(1), false)
                  .has_value());
  ASSERT_TRUE(window().ActivateRow(second));
  content::WebContents* frame = shell().frame_contents();
  const std::string tile =
      base::StrCat({".clu-favorites [data-tab-id=\"", first, "\"]"});
  WaitForSelector(frame, tile);
  EXPECT_EQ(0, content::EvalJs(frame,
                               "document.querySelectorAll('.clu-favorites "
                               ".clu-tooltip-anchor, .clu-favorites "
                               ".clu-favorite-badge').length"));
  EXPECT_TRUE(content::ExecJs(
      frame, content::JsReplace(
                 "document.querySelector($1).dispatchEvent(new PointerEvent("
                 "'pointerdown', {bubbles: true, button: 0, pointerId: 1}))",
                 tile)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == first; }));
}

// The selected favorite's ring takes its site icon's color, its fill is the selected
// row's (not a hard-coded white), and the unread dot is 5 pt, centered 13 right and 12 below the favicon's center.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsSelectedFavoriteRingAndUnreadDot) {
  const std::string mail = Open("icon.html");
  ASSERT_TRUE(window().MoveRows({mail}, DropTarget::Favorite(0), false)
                  .has_value());
  ASSERT_TRUE(window().ActivateRow(mail));
  content::WebContents* frame = shell().frame_contents();
  const std::string tile =
      base::StrCat({".clu-favorites [data-tab-id=\"", mail,
                    "\"][data-active=\"true\"][data-unread=\"true\"]"});
  WaitForSelector(frame, tile);
  // The icon arrives after the load; the ring color follows it.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(
               frame,
               content::JsReplace("document.querySelector($1).style."
                                  "getPropertyValue('--favorite-ring')",
                                  tile))
               .ExtractString()
               .size() == 7u;
  }));
  // A solid gold icon: red over green over blue.
  EXPECT_EQ(true, content::EvalJs(
                      frame, content::JsReplace(
                                 R"((() => {
        const c = document.querySelector($1).style
            .getPropertyValue('--favorite-ring');
        const [r, g, b] = [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16));
        return r > g && g > b;
      })())",
                                 tile)));
  EXPECT_NE("rgb(255, 255, 255)",
            content::EvalJs(
                frame, content::JsReplace(
                           "getComputedStyle(document.querySelector($1)."
                           "querySelector('.clu-favorite-tile'))."
                           "backgroundColor",
                           tile)));
  // At rest, and with the tile forced to 45 and 59 pt: the dot keeps 10 pt from the tile's
  // corner, so its offset from the favicon grows with the tile.
  for (const char* width : {"", "45px", "59px"}) {
    SCOPED_TRACE(width);
    EXPECT_EQ("5,10,10,true",
              content::EvalJs(
                  frame, content::JsReplace(
                             R"((() => {
        const t = document.querySelector($1);
        const b = t.querySelector('.clu-favorite-tile');
        if ($2) { t.style.width = $2; t.style.flex = 'none'; b.style.flex = 'none'; b.style.width = $2; }
        const tile = b.getBoundingClientRect();
        const icon = t.querySelector('.clu-row-favicon, .clu-letter-tile')
            .getBoundingClientRect();
        const dot = t.querySelector('.clu-favorite-unread')
            .getBoundingClientRect();
        const cx = (r) => r.left + r.width / 2;
        const cy = (r) => r.top + r.height / 2;
        const clear = dot.left >= icon.right && dot.top >= icon.bottom;
        return [dot.width, Math.round(tile.right - cx(dot)),
                Math.round(tile.bottom - cy(dot)), clear].join();
      })())",
                             tile, width)));
  }
}

// Starting a drag with no favorites and an empty Pinned moves nothing (the drop zones are
// zero-height markers), and the ghost of a Today row keeps its close x while the rows under it show none.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsDragStartMovesNothing) {
  const std::string today = Open("a.html");
  content::WebContents* frame = shell().frame_contents();
  WaitForSelector(frame, RowSelector(today));
  const std::string row_top = content::JsReplace(
      "Math.round(document.querySelector($1).getBoundingClientRect().top)",
      RowSelector(today));
  const int before = content::EvalJs(frame, row_top).ExtractInt();
  EXPECT_TRUE(content::ExecJs(
      frame, content::JsReplace(
                 R"((() => {
        const row = document.querySelector($1);
        const r = row.getBoundingClientRect();
        const at = (dy) => ({bubbles: true, button: 0, pointerId: 1,
                             clientX: r.left + 40, clientY: r.top + 10 + dy});
        row.dispatchEvent(new PointerEvent('pointerdown', at(0)));
        window.dispatchEvent(new PointerEvent('pointermove', at(12)));
      })())",
                 RowSelector(today))));
  WaitForSelector(frame, ".clu-drag-ghost .clu-row-close");
  EXPECT_EQ(before, content::EvalJs(frame, row_top));
  EXPECT_EQ(true, content::EvalJs(
                      frame,
                      "document.documentElement.hasAttribute('data-sidebar-"
                      "dragging') && getComputedStyle(document.querySelector("
                      "'.clu-drag-ghost .clu-row-close')).opacity === '1' && "
                      "!document.querySelector('.clu-drop-zone')"));
  EXPECT_TRUE(content::ExecJs(
      frame,
      "window.dispatchEvent(new PointerEvent('pointercancel', {pointerId: 1}))"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame,
                           "!document.querySelector('.clu-drag-ghost')")
        .ExtractBool();
  }));
}

// A favorite with an unread count ("(3) Inbox") has Show Unread Badge, checked; turning it off
// hides the tile's unread dot and is stored, and turning it on brings the dot back.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsFavoriteShowUnreadBadge) {
  const std::string mail = Open("icon.html");
  ASSERT_TRUE(window().MoveRows({mail}, DropTarget::Favorite(0), false)
                  .has_value());
  CluTabMenu& menu = window().tab_menu();
  CluTabMenu::Request request;
  request.menu = "row";
  request.node_id = mail;
  const auto check = [&] {
    const std::vector<MenuItem> items = menu.Build(request, true);
    const auto item = std::ranges::find(items, std::string(kMenuUnreadBadge),
                                        &MenuItem::id);
    EXPECT_NE(item, items.end());
    return item != items.end() && item->checked;
  };
  content::WebContents* frame = shell().frame_contents();
  const std::string tile =
      base::StrCat({".clu-favorites [data-tab-id=\"", mail, "\"]"});
  // The dot shows once the page's "(3) Inbox" title is in; the menu reads the same title.
  WaitForSelector(frame, tile + "[data-unread=\"true\"] .clu-favorite-unread");
  EXPECT_TRUE(check());

  menu.Run(request, kMenuUnreadBadge, std::string());
  EXPECT_FALSE(model().UnreadBadgeShown(mail));
  EXPECT_FALSE(check());
  WaitForSelector(frame, tile + "[data-unread=\"false\"]");
  EXPECT_EQ(0, content::EvalJs(
                   frame, content::JsReplace(
                              "document.querySelectorAll($1).length",
                              tile + " .clu-favorite-unread")));
  FlushStore();

  menu.Run(request, kMenuUnreadBadge, std::string());
  EXPECT_TRUE(model().UnreadBadgeShown(mail));
  WaitForSelector(frame, tile + "[data-unread=\"true\"] .clu-favorite-unread");
  // A row that is not a favorite has no such setting.
  const std::string plain = Open("a.html");
  EXPECT_FALSE(model().SetUnreadBadgeShown(plain, false));
  // A favorite whose title has no unread count ("A") has no Show Unread Badge in its menu.
  ASSERT_TRUE(window().MoveRows({plain}, DropTarget::Favorite(1), false)
                  .has_value());
  request.node_id = plain;
  const std::vector<MenuItem> plain_items = menu.Build(request, true);
  EXPECT_EQ(std::ranges::find(plain_items, std::string(kMenuUnreadBadge),
                              &MenuItem::id),
            plain_items.end());
  EXPECT_NE(std::ranges::find(plain_items, std::string(kMenuRename),
                              &MenuItem::id),
            plain_items.end());
}

// A favorite this profile never opened (an import or a restore leaves one) shows its site's
// icon instead of the globe: its page declares none (it is a 404), so /favicon.ico, fetched once for the site.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsUnopenedFavoriteGetsItsSiteIcon) {
  // Counted on the server's thread.
  std::atomic<int> icon_requests = 0;
  net::EmbeddedTestServer site;
  site.RegisterRequestHandler(base::BindRepeating(
      [](std::atomic<int>* requests,
         const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/favicon.ico") {
          return nullptr;
        }
        ++*requests;
        std::string png;
        EXPECT_TRUE(base::Base64Decode(
            "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAAAFklEQVR4nGP4edqUJM"
            "QwqmFUw/DVAAA8avkQHT4J9QAAAABJRU5ErkJggg==",
            &png));
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_content_type("image/png");
        response->set_content(png);
        return response;
      },
      base::Unretained(&icon_requests)));
  ASSERT_TRUE(site.Start());

  NewTab init;
  init.url = site.GetURL("/inbox").spec();
  init.title = "Inbox";
  const std::string favorite =
      model()
          .mutable_tree()
          .InsertTab(init, DropTarget::Favorite(0), model().NowMs())
          .value();
  NewTab other = init;
  other.url = site.GetURL("/calendar").spec();
  ASSERT_TRUE(model()
                  .mutable_tree()
                  .InsertTab(other, DropTarget::Favorite(1), model().NowMs())
                  .has_value());
  model().Commit();
  ASSERT_FALSE(Contents(favorite));
  WaitForSelector(shell().frame_contents(),
                  base::StrCat({".clu-favorites [data-tab-id=\"", favorite,
                                "\"] img[src*=\"cluIcon=f\"]"}));
  // One fetch for the site, not one per row.
  EXPECT_EQ(icon_requests.load(), 1);
}

// A Today row that never loaded gets the icon its page declares, read from the page's HTML without loading it,
// and /favicon.ico is not asked when a declared icon decodes. Cookies never go out.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsUnloadedRowGetsItsDeclaredIcon) {
  std::atomic<int> declared_requests = 0;
  std::atomic<int> favicon_ico_requests = 0;
  std::atomic<int> cookie_requests = 0;
  net::EmbeddedTestServer site;
  site.RegisterRequestHandler(base::BindRepeating(
      [](std::atomic<int>* declared, std::atomic<int>* favicon_ico,
         std::atomic<int>* cookies,
         const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.headers.contains("Cookie")) {
          ++*cookies;
        }
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        if (request.relative_url == "/home") {
          response->set_content_type("text/html");
          response->set_content(
              "<html><head><title>Home</title>"
              "<link rel=\"icon\" type=\"image/svg+xml\" href=\"/i.svg\">"
              "<link rel=\"icon\" sizes=\"32x32\" href=\"/static/icon-32.png\">"
              "<script>document.title = 'ran';</script></head></html>");
          return response;
        }
        if (request.relative_url == "/favicon.ico") {
          ++*favicon_ico;
          return nullptr;
        }
        if (request.relative_url != "/static/icon-32.png") {
          return nullptr;
        }
        ++*declared;
        std::string png;
        EXPECT_TRUE(base::Base64Decode(
            "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAAAFklEQVR4nGP4edqUJM"
            "QwqmFUw/DVAAA8avkQHT4J9QAAAABJRU5ErkJggg==",
            &png));
        response->set_content_type("image/png");
        response->set_content(png);
        return response;
      },
      base::Unretained(&declared_requests),
      base::Unretained(&favicon_ico_requests),
      base::Unretained(&cookie_requests)));
  ASSERT_TRUE(site.Start());

  NewTab init;
  init.url = site.GetURL("/home").spec();
  init.title = "Home";
  const std::string today =
      model()
          .mutable_tree()
          .InsertTab(init, DropTarget::Today(space(), 0), model().NowMs())
          .value();
  model().Commit();
  ASSERT_FALSE(Contents(today));
  WaitForSelector(shell().frame_contents(),
                  base::StrCat({RowSelector(today), " img[src*=\"cluIcon=f\"]"}));
  EXPECT_EQ(declared_requests.load(), 1);
  EXPECT_EQ(favicon_ico_requests.load(), 0);
  EXPECT_EQ(cookie_requests.load(), 0);
  // Nothing was loaded: the row still has no page.
  EXPECT_FALSE(Contents(today));
}

// A restored Gmail favorite and restored Calendar and Meet pinned rows are asleep (no page). Hovering each
// opens the 239 x 256 live card with the site's compact page loaded in the window's profile, the rows' own, with the
// phone user agent; never the "No unread emails" text card or the title pill, and the row stays asleep. Every Google
// host resolves to the test server, so nothing reaches Google and nothing is signed in to.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsAsleepPreviewRowsOpenTheLiveCard) {
  host_resolver()->AddRule("*.google.com", "127.0.0.1");
  auto insert = [&](const std::string& url, const std::string& title,
                    const DropTarget& target) {
    NewTab init;
    init.url = url;
    init.title = title;
    const std::string id =
        model().mutable_tree().InsertTab(init, target, model().NowMs()).value();
    model().Commit();
    return id;
  };
  const std::string calendar =
      insert("https://calendar.google.com/calendar/u/0/r/week",
             "Google Calendar", DropTarget::Pinned(space(), "", 0));
  const std::string meet = insert("https://meet.google.com/", "Google Meet",
                                  DropTarget::Pinned(space(), "", 1));
  struct Case {
    std::string row;
    std::string site;
    GURL live;
  };
  for (const Case& c :
       {Case{calendar, "Google Calendar",
             GURL("https://calendar.google.com/calendar/u/0/r/day")},
        Case{meet, "Google Meet",
             GURL("https://meet.google.com/landing?authuser=0")}}) {
    SCOPED_TRACE(c.site);
    ASSERT_FALSE(model().FindContents(c.row));
    window().Hover(c.row, gfx::Rect(240, 100, 0, 0));
    ASSERT_TRUE(shell().IsPassiveVisible(2));
    const base::DictValue state = window().CurrentState();
    const base::DictValue* card = nullptr;
    for (const base::Value& s :
         *state.FindDict("windows")
              ->FindDict(window().window_id())
              ->FindListByDottedPath("overlay.surfaces")) {
      if (*s.GetDict().FindString("surface") == "tab-hover") {
        card = &s.GetDict();
      }
    }
    ASSERT_TRUE(card);
    EXPECT_EQ(*card->FindStringByDottedPath("props.kind"), "preview");
    EXPECT_EQ(*card->FindStringByDottedPath("props.site"), c.site);
    EXPECT_FALSE(card->FindStringByDottedPath("props.image"));
    EXPECT_EQ(card->FindDoubleByDottedPath("rect.width").value_or(0), 239 + 16);
    EXPECT_EQ(card->FindDoubleByDottedPath("rect.height").value_or(0),
              256 + 16);
    content::WebContents* live = shell().live_preview_contents_for_testing();
    ASSERT_TRUE(live);
    EXPECT_EQ(live->GetBrowserContext(), browser()->GetProfile());
    EXPECT_EQ(live->GetVisibleURL(), c.live);
    EXPECT_NE(live->GetUserAgentOverride().ua_string_override.find("iPhone"),
              std::string::npos);
    // The card never wakes the row.
    EXPECT_FALSE(model().FindContents(c.row));
  }
  window().Hover(std::nullopt, std::nullopt);
  EXPECT_FALSE(shell().IsPassiveVisible(2));
  EXPECT_FALSE(shell().live_preview_contents_for_testing());
}

// Hovering a Gmail favorite or pinned Gmail row opens the mail card drawn from that row's own account's
// unread mail, read from Gmail's Atom feed (/mail/u/N/feed/atom) with the row's profile and its cookies, without
// loading any Gmail page and without waking the row. A fake feed server stands in for Gmail (never a real account):
// account 0 has no unread mail (the 64 tall card), account 1 has 3 (listed newest first), account 2 has 40 (the
// count and the 5 newest), and account 3's feed fails, so its card falls back to the title's count. Nothing shows
// before the feed answers, so the card never shows a guess first.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsGmailCardReadsTheAccountsFeed) {
  host_resolver()->AddRule("*.google.com", "127.0.0.1");
  std::atomic<int> feed_requests = 0;
  std::atomic<int> feed_with_cookie = 0;
  std::atomic<int> page_requests = 0;
  net::EmbeddedTestServer gmail;
  gmail.RegisterRequestHandler(base::BindRepeating(
      [](std::atomic<int>* feeds, std::atomic<int>* with_cookie,
         std::atomic<int>* pages,
         const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        const std::string& url = request.relative_url;
        if (!url.ends_with("/feed/atom")) {
          ++*pages;
          return nullptr;
        }
        ++*feeds;
        const auto cookie = request.headers.find("Cookie");
        if (cookie != request.headers.end() &&
            cookie->second.find("SID=fake-session") != std::string::npos) {
          ++*with_cookie;
        }
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        if (url == "/mail/u/3/feed/atom") {
          response->set_code(net::HTTP_INTERNAL_SERVER_ERROR);
          return response;
        }
        auto entry = [](int n, const std::string& issued) {
          return base::StrCat(
              {"<entry><title>Subject ", base::NumberToString(n),
               "</title><summary>s</summary><link rel=\"alternate\" "
               "href=\"https://mail.google.com/mail?message_id=",
               base::NumberToString(n),
               "&amp;view=conv\" type=\"text/html\"/><modified>", issued,
               "</modified><issued>", issued,
               "</issued><id>tag:mail,2004:", base::NumberToString(n),
               "</id><author><name>Sender ", base::NumberToString(n),
               "</name><email>s@mail.test</email></author></entry>"});
        };
        int count = 0;
        std::string entries;
        if (url == "/mail/u/1/feed/atom") {
          count = 3;
          entries = entry(1, "2026-11-10T08:00:00Z") +
                    entry(2, "2026-11-10T19:59:00Z") +
                    entry(3, "2026-11-10T12:00:00Z");
        } else if (url == "/mail/u/work@company.com/feed/atom") {
          count = 2;
          entries = entry(70, "2026-11-10T09:00:00Z") +
                    entry(71, "2026-11-10T10:00:00Z");
        } else if (url == "/mail/u/2/feed/atom") {
          count = 40;
          for (int i = 10; i < 30; ++i) {
            entries += entry(i, base::StrCat({"2026-11-10T12:",
                                              base::NumberToString(i),
                                              ":00Z"}));
          }
        }
        response->set_content_type("application/atom+xml");
        response->set_content(base::StrCat(
            {"<?xml version=\"1.0\" encoding=\"UTF-8\"?><feed version=\"0.3\" "
             "xmlns=\"http://purl.org/atom/ns#\"><title>Gmail - Inbox for "
             "test@mail.test</title><fullcount>",
             base::NumberToString(count), "</fullcount>", entries,
             "</feed>"}));
        return response;
      },
      base::Unretained(&feed_requests), base::Unretained(&feed_with_cookie),
      base::Unretained(&page_requests)));
  ASSERT_TRUE(gmail.Start());
  CluGmailFeed::SetFeedOriginForTesting(gmail.base_url());
  // The profile's own sign-in, as a Gmail tab would have it.
  ASSERT_TRUE(content::SetCookie(browser()->GetProfile(), gmail.base_url(),
                                 "SID=fake-session"));

  auto insert = [&](int account, const std::string& title,
                    const DropTarget& target) {
    NewTab init;
    init.url = base::StrCat({"https://mail.google.com/mail/u/",
                             base::NumberToString(account), "/#inbox"});
    init.title = title;
    const std::string id =
        model().mutable_tree().InsertTab(init, target, model().NowMs()).value();
    model().Commit();
    return id;
  };
  // Stored titles that disagree with the feed: the feed wins.
  const std::string none =
      insert(0, "Inbox (9) - Mail", DropTarget::Favorite(0));
  const std::string three =
      insert(1, "Inbox - Mail", DropTarget::Favorite(1));
  const std::string forty =
      insert(2, "Inbox (2) - Mail", DropTarget::Pinned(space(), "", 0));
  const std::string failing =
      insert(3, "Inbox (7) - Mail", DropTarget::Favorite(2));

  auto card_for = [&](const std::string& row) -> std::optional<base::DictValue> {
    window().Hover(row, gfx::Rect(60, 130, 0, 0));
    if (!base::test::RunUntil([&] { return shell().IsPassiveVisible(2); })) {
      return std::nullopt;
    }
    const base::DictValue state = window().CurrentState();
    for (const base::Value& s :
         *state.FindDict("windows")
              ->FindDict(window().window_id())
              ->FindListByDottedPath("overlay.surfaces")) {
      if (*s.GetDict().FindString("surface") == "tab-hover" &&
          *s.GetDict().FindStringByDottedPath("props.tabId") == row) {
        return s.GetDict().Clone();
      }
    }
    return std::nullopt;
  };
  auto close = [&] {
    window().Hover(std::nullopt, std::nullopt);
    ASSERT_TRUE(
        base::test::RunUntil([&] { return !shell().IsPassiveVisible(2); }));
  };

  // No unread mail: the two-line card, 239 x 64, though the stored title says 9.
  std::optional<base::DictValue> card = card_for(none);
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.kind"), "mail");
  EXPECT_EQ(*card->FindStringByDottedPath("props.site"), "Gmail");
  EXPECT_EQ(*card->FindStringByDottedPath("props.mailSource"), "feed");
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 0);
  EXPECT_TRUE(card->FindListByDottedPath("props.mailItems")->empty());
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.width").value_or(0), 239 + 16);
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.height").value_or(0), 64 + 16);
  EXPECT_FALSE(shell().live_preview_contents_for_testing());
  close();

  // 3 unread: the three, newest first, with sender, subject, time and the message's Gmail link.
  card = card_for(three);
  ASSERT_TRUE(card);
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 3);
  const base::ListValue* items = card->FindListByDottedPath("props.mailItems");
  ASSERT_EQ(items->size(), 3u);
  EXPECT_EQ(*(*items)[0].GetDict().FindString("author"), "Sender 2");
  EXPECT_EQ(*(*items)[0].GetDict().FindString("title"), "Subject 2");
  EXPECT_EQ(*(*items)[0].GetDict().FindString("issued"),
            "2026-11-10T19:59:00Z");
  EXPECT_EQ(*(*items)[0].GetDict().FindString("url"),
            "https://mail.google.com/mail?message_id=2&view=conv");
  EXPECT_EQ(*(*items)[1].GetDict().FindString("title"), "Subject 3");
  EXPECT_EQ(*(*items)[2].GetDict().FindString("title"), "Subject 1");
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.height").value_or(0),
            6 + 30 + 3 * 44 + 6 + 16);
  close();

  // 40 unread on a pinned row: the count and the 5 newest.
  card = card_for(forty);
  ASSERT_TRUE(card);
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 40);
  items = card->FindListByDottedPath("props.mailItems");
  ASSERT_EQ(items->size(), 5u);
  EXPECT_EQ(*(*items)[0].GetDict().FindString("title"), "Subject 29");
  EXPECT_EQ(*(*items)[4].GetDict().FindString("title"), "Subject 25");
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.height").value_or(0),
            6 + 30 + 5 * 44 + 6 + 16);
  close();

  // A failing feed: the count-only card from the title, never "No unread emails".
  card = card_for(failing);
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.kind"), "mail");
  EXPECT_EQ(*card->FindStringByDottedPath("props.mailSource"), "title");
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 7);
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.height").value_or(0), 64 + 16);
  close();

  // A second hover inside a minute reuses the answer: the same card at once, no new read.
  const int reads = feed_requests.load();
  card = card_for(three);
  ASSERT_TRUE(card);
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 3);
  EXPECT_EQ(feed_requests.load(), reads);
  close();

  // Every read carried the profile's cookie, no Gmail page was loaded, and no row woke.
  EXPECT_EQ(feed_requests.load(), 4);
  EXPECT_EQ(feed_with_cookie.load(), 4);
  EXPECT_EQ(page_requests.load(), 0);
  for (const std::string& row : {none, three, forty, failing}) {
    EXPECT_FALSE(model().FindContents(row));
  }

  // A failing feed and a title with no count (a message view, a label, a page still loading):
  // the plain card, never "No unread emails" while mail may be unread.
  auto insert_url = [&](const std::string& url, const std::string& title,
                        int favorite) {
    NewTab init;
    init.url = url;
    init.title = title;
    const std::string id = model()
                               .mutable_tree()
                               .InsertTab(init, DropTarget::Favorite(favorite),
                                          model().NowMs())
                               .value();
    model().Commit();
    return id;
  };
  const std::string reading = insert_url(
      "https://mail.google.com/mail/u/3/#inbox/FMfcgz",
      "Re: invoice - test@mail.test - Gmail", 3);
  card = card_for(reading);
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.kind"), "title");
  EXPECT_FALSE(card->FindStringByDottedPath("props.mailSource"));
  EXPECT_FALSE(card->FindStringByDottedPath("props.site"));
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.height").value_or(0), 28 + 16);
  close();

  // A row addressed by email reads that account's feed; ?authuser= reads its account's.
  const std::string work = insert_url(
      "https://mail.google.com/mail/u/work@company.com/#inbox", "Inbox - Mail",
      4);
  card = card_for(work);
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.mailSource"), "feed");
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 2);
  items = card->FindListByDottedPath("props.mailItems");
  ASSERT_EQ(items->size(), 2u);
  EXPECT_EQ(*(*items)[0].GetDict().FindString("title"), "Subject 71");
  close();
  const std::string authuser = insert_url(
      "https://mail.google.com/mail/?authuser=1#inbox", "Inbox - Mail", 5);
  card = card_for(authuser);
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.mailSource"), "feed");
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 3);
  close();

  // An account selector that cannot be read reads no feed, never account 0's: the plain card.
  const int before_junk = feed_requests.load();
  const std::string junk = insert_url(
      "https://mail.google.com/mail/u/not-an-account/#inbox", "Inbox - Mail",
      6);
  card = card_for(junk);
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.kind"), "title");
  EXPECT_FALSE(card->FindStringByDottedPath("props.mailSource"));
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 0);
  close();
  EXPECT_EQ(feed_requests.load(), before_junk);
  CluGmailFeed::SetFeedOriginForTesting(GURL());
}

// Gmail's card is usable. The overlay holds it open while the pointer is on it (tabs.hover held),
// so the tile's leave keeps it; leaving the card closes it with the 75 ms fade. A click on a message opens that
// message (its feed link) in the row's own tab, waking the asleep row, and closes the card; a live row loads it in
// place. Only a Gmail link into a Gmail row opens. A fake feed server stands in for Gmail (never a real account).
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsGmailCardHoldsAndOpensTheMessage) {
  host_resolver()->AddRule("*.google.com", "127.0.0.1");
  net::EmbeddedTestServer gmail;
  gmail.RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/mail/u/1/feed/atom") {
          return nullptr;
        }
        auto entry = [](int n) {
          return base::StrCat(
              {"<entry><title>Fake ", base::NumberToString(n),
               "</title><link rel=\"alternate\" "
               "href=\"https://mail.google.com/mail/u/1/#inbox/fake",
               base::NumberToString(n),
               "\" type=\"text/html\"/><issued>2026-11-10T1",
               base::NumberToString(n), ":00:00Z</issued><author><name>Fake "
               "Sender</name></author></entry>"});
        };
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_content_type("application/atom+xml");
        response->set_content(base::StrCat(
            {"<?xml version=\"1.0\" encoding=\"UTF-8\"?><feed version=\"0.3\" "
             "xmlns=\"http://purl.org/atom/ns#\"><fullcount>2</fullcount>",
             entry(1), entry(2), "</feed>"}));
        return response;
      }));
  ASSERT_TRUE(gmail.Start());
  CluGmailFeed::SetFeedOriginForTesting(gmail.base_url());

  NewTab init;
  init.url = "https://mail.google.com/mail/u/1/#inbox";
  init.title = "Inbox (2) - Mail";
  const std::string row =
      model()
          .mutable_tree()
          .InsertTab(init, DropTarget::Favorite(0), model().NowMs())
          .value();
  NewTab other_init;
  other_init.url = "https://example.com/";
  other_init.title = "Example";
  const std::string other =
      model()
          .mutable_tree()
          .InsertTab(other_init, DropTarget::Favorite(1), model().NowMs())
          .value();
  model().Commit();

  CluTabsHandler tabs(window().GetWeakPtr());
  auto hover = [&](std::optional<std::string> id, mojom::RectPtr anchor,
                   std::optional<bool> held) {
    CallFuture done;
    tabs.Hover(mojom::TabsHoverInput::New(std::move(id), std::move(anchor),
                                          held),
               done.GetCallback());
    return done.Take();
  };
  auto open_mail = [&](const std::string& id, const std::string& url) {
    CallFuture done;
    tabs.OpenMail(mojom::TabsOpenMailInput::New(id, url), done.GetCallback());
    return done.Take();
  };
  auto card = [&]() -> std::optional<base::DictValue> {
    const base::DictValue state = window().CurrentState();
    for (const base::Value& s :
         *state.FindDict("windows")
              ->FindDict(window().window_id())
              ->FindListByDottedPath("overlay.surfaces")) {
      if (*s.GetDict().FindString("surface") == "tab-hover") {
        return s.GetDict().Clone();
      }
    }
    return std::nullopt;
  };
  auto open_card = [&] {
    ASSERT_TRUE(IsOk(hover(row, mojom::Rect::New(60, 130, 0, 0), std::nullopt)));
    ASSERT_TRUE(
        base::test::RunUntil([&] { return shell().IsPassiveVisible(2); }));
    ASSERT_TRUE(card());
    ASSERT_EQ(card()->FindListByDottedPath("props.mailItems")->size(), 2u);
  };

  // Held: the tile's leave keeps the card, not even closing; a hold for another row changes nothing.
  open_card();
  ASSERT_TRUE(IsOk(hover(row, nullptr, true)));
  ASSERT_TRUE(IsOk(hover(other, nullptr, false)));
  ASSERT_TRUE(IsOk(hover(std::nullopt, nullptr, std::nullopt)));
  ASSERT_TRUE(card());
  EXPECT_FALSE(card()->FindBoolByDottedPath("props.closing").value_or(false));
  EXPECT_TRUE(shell().IsPassiveVisible(2));
  // Leaving the card fades it out, then it goes.
  ASSERT_TRUE(IsOk(hover(row, nullptr, false)));
  ASSERT_TRUE(card());
  EXPECT_TRUE(card()->FindBoolByDottedPath("props.closing").value_or(false));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !shell().IsPassiveVisible(2); }));
  EXPECT_FALSE(card());

  // Unheld, the tile's leave closes it as before.
  open_card();
  ASSERT_TRUE(IsOk(hover(std::nullopt, nullptr, std::nullopt)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !shell().IsPassiveVisible(2); }));

  // The pointer crossed the card and came back to the tile: the card released its hold and started to fade, and the
  // tile asking for it again brings it back, no longer closing.
  open_card();
  ASSERT_TRUE(IsOk(hover(row, nullptr, true)));
  ASSERT_TRUE(IsOk(hover(row, nullptr, false)));
  ASSERT_TRUE(card());
  ASSERT_TRUE(card()->FindBoolByDottedPath("props.closing").value_or(false));
  ASSERT_TRUE(IsOk(hover(row, mojom::Rect::New(60, 130, 0, 0), std::nullopt)));
  ASSERT_TRUE(card());
  EXPECT_FALSE(card()->FindBoolByDottedPath("props.closing").value_or(false));
  EXPECT_EQ(card()->FindListByDottedPath("props.mailItems")->size(), 2u);
  EXPECT_TRUE(shell().IsPassiveVisible(2));
  ASSERT_TRUE(IsOk(hover(std::nullopt, nullptr, std::nullopt)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !shell().IsPassiveVisible(2); }));

  // A title with no unread count (an open message, or an asleep row's stored "Gmail") still gets the feed's message
  // list, so the card holds as any Gmail card does.
  for (const char* title : {"Re: invoice - test@mail.test - Gmail", "Gmail"}) {
    model().mutable_tree().Mutable(row)->title = title;
    model().Commit();
    open_card();
    EXPECT_EQ(*card()->FindStringByDottedPath("props.kind"), "mail") << title;
    ASSERT_TRUE(IsOk(hover(row, nullptr, true)));
    ASSERT_TRUE(IsOk(hover(std::nullopt, nullptr, std::nullopt)));
    ASSERT_TRUE(card()) << title;
    EXPECT_FALSE(card()->FindBoolByDottedPath("props.closing").value_or(false));
    ASSERT_TRUE(IsOk(hover(row, nullptr, false)));
    ASSERT_TRUE(
        base::test::RunUntil([&] { return !shell().IsPassiveVisible(2); }));
  }
  model().mutable_tree().Mutable(row)->title = "Inbox (2) - Mail";
  model().Commit();

  // Refused: a link that is not Gmail's, an http one, and a Gmail link into a row that is not Gmail. Nothing opens.
  open_card();
  for (const std::string& url :
       {std::string("https://example.com/mail"),
        std::string("http://mail.google.com/mail/u/1/#inbox/fake1"),
        std::string("https://mail.google.com.evil.test/x")}) {
    mojom::CallResultPtr refused = open_mail(row, url);
    ASSERT_TRUE(refused->is_error()) << url;
    EXPECT_EQ(Code(refused), mojom::ErrorCode::kRejected) << url;
  }
  EXPECT_EQ(Code(open_mail(other,
                           "https://mail.google.com/mail/u/1/#inbox/fake1")),
            mojom::ErrorCode::kRejected);
  EXPECT_EQ(Code(open_mail(base::Uuid::GenerateRandomV4().AsLowercaseString(),
                           "https://mail.google.com/mail/u/1/#inbox/fake1")),
            mojom::ErrorCode::kNotFound);
  EXPECT_FALSE(model().FindContents(row));
  ASSERT_TRUE(card());

  // A click on the newest message: the asleep row wakes on it, activates, and the card closes at once.
  const std::string newest =
      *(*card()->FindListByDottedPath("props.mailItems"))[0].GetDict().FindString(
          "url");
  EXPECT_EQ(newest, "https://mail.google.com/mail/u/1/#inbox/fake2");
  ASSERT_TRUE(IsOk(hover(row, nullptr, true)));
  ASSERT_TRUE(IsOk(open_mail(row, newest)));
  EXPECT_FALSE(card());
  EXPECT_FALSE(shell().IsPassiveVisible(2));
  EXPECT_EQ(window().ActiveRowId(), row);
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::WebContents* contents = model().FindContents(row);
    return contents && contents->GetVisibleURL() == GURL(newest);
  }));

  // The row is live now: the other message loads in place, in the same page.
  content::WebContents* page = model().FindContents(row);
  open_card();
  ASSERT_TRUE(IsOk(open_mail(row, "https://mail.google.com/mail/u/1/#inbox/fake1")));
  EXPECT_FALSE(card());
  EXPECT_EQ(model().FindContents(row), page);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return page->GetVisibleURL() ==
           GURL("https://mail.google.com/mail/u/1/#inbox/fake1");
  }));

  // An asleep Today row with saved history (the inbox, then a message) wakes on the clicked message, loaded on top of
  // that history as the live row does: Back still reaches the old message and the inbox.
  NewTab today_init;
  today_init.url = "https://mail.google.com/mail/u/1/#inbox/fake1";
  today_init.title = "Gmail";
  today_init.nav_state =
      R"({"index":1,"entries":[{"url":"https://mail.google.com/mail/u/1/#inbox","title":"Inbox"},)"
      R"({"url":"https://mail.google.com/mail/u/1/#inbox/fake1","title":"Fake 1"}]})";
  const std::string today =
      model()
          .mutable_tree()
          .InsertTab(today_init, DropTarget::Today(space(), 0), model().NowMs())
          .value();
  model().Commit();
  ASSERT_TRUE(model().tree().Get(today)->nav_state.has_value());
  ASSERT_FALSE(model().FindContents(today));
  ASSERT_TRUE(IsOk(open_mail(today, newest)));
  EXPECT_EQ(window().ActiveRowId(), today);
  ASSERT_TRUE(model().tree().Get(today)->nav_state.has_value());
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::WebContents* contents = model().FindContents(today);
    return contents && contents->GetVisibleURL() == GURL(newest);
  }));
  content::NavigationController& history =
      model().FindContents(today)->GetController();
  ASSERT_EQ(history.GetEntryCount(), 3);
  EXPECT_EQ(history.GetEntryAtIndex(0)->GetURL(),
            GURL("https://mail.google.com/mail/u/1/#inbox"));
  EXPECT_EQ(history.GetEntryAtIndex(1)->GetURL(),
            GURL("https://mail.google.com/mail/u/1/#inbox/fake1"));
  EXPECT_EQ(history.GetEntryAtIndex(2)->GetURL(), GURL(newest));
  ASSERT_TRUE(base::test::RunUntil([&] { return history.CanGoBack(); }));
  CluGmailFeed::SetFeedOriginForTesting(GURL());
}

// The frame hangs an asleep pinned Calendar row's card centered on the row, as it does a loaded one's,
// because the card the browser draws for it is the 256 tall live view, not the 28 tall title pill (hoverCardModel()
// in src/shared/tabs.ts mirrors BuildHoverCard()). Hovers the real row in the frame; the row stays asleep.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest,
                       CluTabsAsleepPinnedPreviewCardIsCenteredOnItsRow) {
  host_resolver()->AddRule("*.google.com", "127.0.0.1");
  NewTab init;
  init.url = "https://calendar.google.com/calendar/u/0/r/week";
  init.title = "Google Calendar";
  const std::string calendar =
      model()
          .mutable_tree()
          .InsertTab(init, DropTarget::Pinned(space(), "", 0), model().NowMs())
          .value();
  model().Commit();
  ASSERT_FALSE(model().FindContents(calendar));
  content::WebContents* frame = shell().frame_contents();
  const std::string row = RowSelector(calendar);
  WaitForSelector(frame, row);
  // Where the card's 272 tall view (the card plus its 8 pt margin) sits when centered on the row, kept in the window.
  const int expected_y =
      content::EvalJs(frame, content::JsReplace(R"((() => {
        const r = document.querySelector($1).getBoundingClientRect();
        const y = Math.round(r.top + r.height / 2 - 128 - 8);
        return Math.max(0, Math.min(y, window.innerHeight - 272));
      })())",
                                                row))
          .ExtractInt();
  ASSERT_TRUE(content::ExecJs(
      frame, content::JsReplace(R"((() => {
        const row = document.querySelector($1);
        const r = row.getBoundingClientRect();
        row.dispatchEvent(new PointerEvent('pointerover', {bubbles: true,
            pointerId: 1, clientX: r.left + 20, clientY: r.top + 10}));
      })())",
                                row)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return shell().IsPassiveVisible(2); }));
  const base::DictValue state = window().CurrentState();
  const base::DictValue* card = nullptr;
  for (const base::Value& s : *state.FindDict("windows")
                                   ->FindDict(window().window_id())
                                   ->FindListByDottedPath("overlay.surfaces")) {
    if (*s.GetDict().FindString("surface") == "tab-hover") {
      card = &s.GetDict();
    }
  }
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.kind"), "preview");
  EXPECT_EQ(card->FindDoubleByDottedPath("rect.height").value_or(0), 256 + 16);
  EXPECT_NEAR(card->FindDoubleByDottedPath("rect.y").value_or(-1000),
              expected_y, 1);
  EXPECT_FALSE(model().FindContents(calendar));
  window().Hover(std::nullopt, std::nullopt);
}

// Hovering a live Calendar row opens the 239 x 256 preview card and lays a live view of Calendar's compact
// day view over it, its own page in the tab's profile, while the tab itself stays as it was (no navigation, reload or
// unload). Every Google host resolves to the test server, so nothing reaches Google and nothing is signed in to.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsCalendarRowHasALivePreview) {
  host_resolver()->AddRule("*.google.com", "127.0.0.1");
  base::expected<std::string, std::string> calendar = window().CreateRow(
      embedded_test_server()->GetURL("calendar.google.com",
                                     "/tabs/a.html"),
      std::nullopt, /*activate=*/true);
  ASSERT_TRUE(calendar.has_value());
  content::WebContents* tab = Contents(calendar.value());
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  Open("b.html");
  const int entries = tab->GetController().GetEntryCount();
  const int entry_id =
      tab->GetController().GetLastCommittedEntry()->GetUniqueID();
  const GURL tab_url = tab->GetLastCommittedURL();
  auto card = [&]() -> std::optional<base::DictValue> {
    const base::DictValue state = window().CurrentState();
    for (const base::Value& s : *state.FindDict("windows")
                                     ->FindDict(window().window_id())
                                     ->FindListByDottedPath("overlay.surfaces")) {
      if (*s.GetDict().FindString("surface") == "tab-hover") {
        return s.GetDict().Clone();
      }
    }
    return std::nullopt;
  };
  window().Hover(calendar.value(), gfx::Rect(240, 100, 0, 0));
  ASSERT_TRUE(shell().IsPassiveVisible(2));
  std::optional<base::DictValue> open = card();
  ASSERT_TRUE(open);
  EXPECT_EQ(*open->FindStringByDottedPath("props.kind"), "preview");
  EXPECT_EQ(*open->FindStringByDottedPath("props.site"), "Google Calendar");
  EXPECT_FALSE(open->FindStringByDottedPath("props.image"));
  EXPECT_EQ(open->FindDoubleByDottedPath("rect.width").value_or(0), 239 + 16);
  EXPECT_EQ(open->FindDoubleByDottedPath("rect.height").value_or(0), 256 + 16);
  content::WebContents* live = shell().live_preview_contents_for_testing();
  ASSERT_TRUE(live);
  EXPECT_NE(live, tab);
  EXPECT_EQ(live->GetBrowserContext(), tab->GetBrowserContext());
  // The test URL's path is a.html, so the compact page is on the first account.
  EXPECT_EQ(live->GetVisibleURL(),
            GURL("https://calendar.google.com/calendar/u/0/r/day"));
  EXPECT_TRUE(live->IsAudioMuted());
  // The card's page asks for the phone view, so Calendar serves its narrow day, not its cropped desktop page; the tab
  // keeps its own user agent.
  EXPECT_NE(live->GetUserAgentOverride().ua_string_override.find("iPhone"),
            std::string::npos);
  EXPECT_TRUE(
      live->GetController().GetPendingEntry()
          ? live->GetController().GetPendingEntry()->GetIsOverridingUserAgent()
          : live->GetController().GetVisibleEntry()->GetIsOverridingUserAgent());
  EXPECT_TRUE(tab->GetUserAgentOverride().ua_string_override.empty());
  // A redraw of the same card (a title change) keeps the same live page.
  window().Hover(calendar.value(), gfx::Rect(240, 100, 0, 0));
  EXPECT_EQ(shell().live_preview_contents_for_testing(), live);
  // The tab itself never moved.
  EXPECT_EQ(tab->GetController().GetEntryCount(), entries);
  EXPECT_EQ(tab->GetController().GetLastCommittedEntry()->GetUniqueID(),
            entry_id);
  EXPECT_EQ(tab->GetLastCommittedURL(), tab_url);
  EXPECT_FALSE(tab->WasDiscarded());
  window().Hover(std::nullopt, std::nullopt);
  EXPECT_FALSE(shell().IsPassiveVisible(2));
  EXPECT_FALSE(card());
  EXPECT_FALSE(shell().live_preview_contents_for_testing());
}

// A live Gmail row's card is the mail card, never a live page; with its feed unavailable (the test server has none) it shows the count in the tab's title. Moving to a
// Calendar row switches the card and gives that row its own live page at once.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsGmailCardIsNeverAPageAndFollowsThePointer) {
  host_resolver()->AddRule("*.google.com", "127.0.0.1");
  CluGmailFeed::SetFeedOriginForTesting(embedded_test_server()->base_url());
  base::expected<std::string, std::string> mail = window().CreateRow(
      embedded_test_server()->GetURL("mail.google.com", "/tabs/a.html"),
      std::nullopt, /*activate=*/true);
  ASSERT_TRUE(mail.has_value());
  ASSERT_TRUE(content::WaitForLoadStop(Contents(mail.value())));
  ASSERT_TRUE(content::ExecJs(Contents(mail.value()),
                              "document.title = 'Inbox (12) - Mail'"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Contents(mail.value())->GetTitle() == u"Inbox (12) - Mail";
  }));
  base::expected<std::string, std::string> calendar = window().CreateRow(
      embedded_test_server()->GetURL("calendar.google.com", "/tabs/a.html"),
      std::nullopt, /*activate=*/false);
  ASSERT_TRUE(calendar.has_value());
  window().Hover(mail.value(), gfx::Rect(240, 100, 0, 0));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return shell().IsPassiveVisible(2); }));
  EXPECT_FALSE(shell().live_preview_contents_for_testing());
  const base::DictValue state = window().CurrentState();
  const base::DictValue* card = nullptr;
  for (const base::Value& s :
       *state.FindDict("windows")
            ->FindDict(window().window_id())
            ->FindListByDottedPath("overlay.surfaces")) {
    if (*s.GetDict().FindString("surface") == "tab-hover") {
      card = &s.GetDict();
    }
  }
  ASSERT_TRUE(card);
  EXPECT_EQ(*card->FindStringByDottedPath("props.kind"), "mail");
  EXPECT_EQ(*card->FindStringByDottedPath("props.mailSource"), "title");
  EXPECT_EQ(card->FindIntByDottedPath("props.unread"), 12);
  window().Hover(calendar.value(), gfx::Rect(240, 140, 0, 0));
  content::WebContents* next = shell().live_preview_contents_for_testing();
  ASSERT_TRUE(next);
  EXPECT_EQ(next->GetVisibleURL(),
            GURL("https://calendar.google.com/calendar/u/0/r/day"));
  window().Hover(std::nullopt, std::nullopt);
  EXPECT_FALSE(shell().live_preview_contents_for_testing());
  CluGmailFeed::SetFeedOriginForTesting(GURL());
}

// A Gmail favorite's card sits on the sidebar just under its own tile for every column, starting where the
// first tile's card starts (the design's left 65), never hung from a later tile's corner out over the page.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsGmailCardStartsInsideTheSidebarForEveryTile) {
  host_resolver()->AddRule("*.google.com", "127.0.0.1");
  CluGmailFeed::SetFeedOriginForTesting(embedded_test_server()->base_url());
  CluTabsHandler tabs(window().GetWeakPtr());
  std::vector<std::string> ids;
  for (int i = 0; i < 3; ++i) {
    base::expected<std::string, std::string> id = window().CreateRow(
        embedded_test_server()->GetURL(
            "mail.google.com",
            base::StrCat({"/tabs/a.html?u=", base::NumberToString(i)})),
        std::nullopt, /*activate=*/false);
    ASSERT_TRUE(id.has_value());
    CallFuture moved;
    tabs.Move(mojom::TabsMoveInput::New(id.value(), Favorite(i), std::nullopt,
                                        std::nullopt, std::nullopt),
              moved.GetCallback());
    ASSERT_TRUE(IsOk(moved.Get())) << i;
    ids.push_back(id.value());
  }
  content::WebContents* frame = shell().frame_contents();
  const auto tile = [](const std::string& id) {
    return base::StrCat({".clu-favorite[data-tab-id=\"", id, "\"]"});
  };
  WaitForSelector(frame, tile(ids[2]));
  const auto rect_of = [&](const std::string& id) {
    const content::EvalJsResult r = content::EvalJs(
        frame, content::JsReplace(R"(
          (() => {
            const r = document.querySelector($1).getBoundingClientRect();
            return [r.right, r.bottom];
          })())",
                                  tile(id)));
    const base::ListValue& list = r.ExtractList();
    return std::make_pair(list[0].GetDouble(), list[1].GetDouble());
  };
  const double first_right = rect_of(ids[0]).first;
  const int sidebar_right = shell().layout().effective_width;
  for (const std::string& id : ids) {
    const auto [right, bottom] = rect_of(id);
    // A real hover over the tile: React's enter comes from the pointerover.
    ASSERT_TRUE(content::ExecJs(
        frame, content::JsReplace(
                   "document.querySelector($1).dispatchEvent("
                   "new PointerEvent('pointerover', {bubbles: true}))",
                   tile(id))));
    std::optional<gfx::Rect> card;
    ASSERT_TRUE(base::test::RunUntil([&] {
      const base::DictValue state = window().CurrentState();
      for (const base::Value& s :
           *state.FindDict("windows")
                ->FindDict(window().window_id())
                ->FindListByDottedPath("overlay.surfaces")) {
        const base::DictValue& d = s.GetDict();
        if (*d.FindString("surface") == "tab-hover" &&
            *d.FindStringByDottedPath("props.tabId") == id) {
          card = gfx::Rect(*d.FindIntByDottedPath("rect.x"),
                           *d.FindIntByDottedPath("rect.y"),
                           *d.FindIntByDottedPath("rect.width"),
                           *d.FindIntByDottedPath("rect.height"));
          return true;
        }
      }
      return false;
    })) << id;
    // The card's box (inside its 8 pt shadow margin) starts at the first tile's corner, 3.5 over it, inside the
    // sidebar, and its top is 3.5 over this tile's bottom.
    EXPECT_NEAR(card->x() + 8, first_right - 3.5, 1) << id;
    EXPECT_LT(card->x() + 8, sidebar_right) << id;
    EXPECT_LE(first_right, right + 0.5) << id;
    EXPECT_NEAR(card->y() + 8, bottom - 3.5, 1) << id;
    ASSERT_TRUE(content::ExecJs(
        frame, content::JsReplace(
                   "document.querySelector($1).dispatchEvent("
                   "new PointerEvent('pointerout', {bubbles: true}))",
                   tile(id))));
    window().Hover(std::nullopt, std::nullopt);
  }
  CluGmailFeed::SetFeedOriginForTesting(GURL());
}

// Any other site keeps the title card, with no live page and no snapshot.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsOtherSiteRowHasNoLivePreview) {
  host_resolver()->AddRule("github.com", "127.0.0.1");
  base::expected<std::string, std::string> row = window().CreateRow(
      embedded_test_server()->GetURL("github.com", "/tabs/a.html"),
      std::nullopt, /*activate=*/true);
  ASSERT_TRUE(row.has_value());
  ASSERT_TRUE(content::WaitForLoadStop(Contents(row.value())));
  Open("b.html");
  window().Hover(row.value(), gfx::Rect(240, 100, 0, 0));
  ASSERT_TRUE(shell().IsPassiveVisible(2));
  EXPECT_FALSE(shell().live_preview_contents_for_testing());
  const base::DictValue state = window().CurrentState();
  for (const base::Value& s : *state.FindDict("windows")
                                   ->FindDict(window().window_id())
                                   ->FindListByDottedPath("overlay.surfaces")) {
    if (*s.GetDict().FindString("surface") == "tab-hover") {
      EXPECT_EQ(*s.GetDict().FindStringByDottedPath("props.kind"), "title");
      EXPECT_FALSE(s.GetDict().FindStringByDottedPath("props.image"));
    }
  }
  window().Hover(std::nullopt, std::nullopt);
}

// A loaded Google Calendar pinned row shows its next event under the title, read from that tab's own page
// without reloading it; a page with no event and a Today row show nothing (a row with no page has no contents and
// gets no subtitle). A fake calendar page
// served at calendar.google.com, never a real calendar.
IN_PROC_BROWSER_TEST_F(CluTabsBrowserTest, CluTabsCalendarRowShowsItsNextEvent) {
  net::EmbeddedTestServer https(net::EmbeddedTestServer::TYPE_HTTPS);
  net::EmbeddedTestServer::ServerCertificateConfig cert;
  cert.dns_names = {"calendar.google.com"};
  https.SetSSLConfig(cert);
  https.RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        auto response =
            std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_content_type("text/html");
        // Each load counts itself, so a reload would show.
        const std::string count =
            "<script>sessionStorage.loads = "
            "Number(sessionStorage.loads || 0) + 1</script>";
        if (request.relative_url == "/day") {
          // Midnight has passed (12:00am), 11:59pm is next, and a chip with no time is junk.
          response->set_content(
              "<title>Google Calendar</title>" + count +
              "<div data-eventchip>Standup 12:00am to 12:15am</div>"
              "<div data-eventchip>Lunch Q3 2026</div>"
              "<div data-eventchip>Late call 11:59pm</div>");
        } else {
          response->set_content("<title>Google Calendar</title>" + count +
                                "<p>No events</p>");
        }
        return response;
      }));
  ASSERT_TRUE(https.Start());
  host_resolver()->AddRule("calendar.google.com", "127.0.0.1");

  base::expected<std::string, std::string> made = window().CreateRow(
      https.GetURL("calendar.google.com", "/day"), std::nullopt,
      /*activate=*/true);
  ASSERT_TRUE(made.has_value());
  const std::string row = made.value();
  content::WebContents* page = Contents(row);
  ASSERT_TRUE(content::WaitForLoadStop(page));
  const int entry = page->GetController().GetLastCommittedEntry()->GetUniqueID();
  auto subtitle = [&]() -> std::optional<std::string> {
    const base::DictValue state = window().CurrentState();
    const std::string* value =
        state.FindDict("tabs")->FindDict(row)->FindString("subtitle");
    return value ? std::optional<std::string>(*value) : std::nullopt;
  };
  // A Today row shows none.
  EXPECT_EQ(subtitle(), std::nullopt);

  ASSERT_TRUE(window().TogglePin(row).has_value());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return subtitle() == "Event at 11:59 PM"; }));
  // Read from the page as it was: same entry, loaded once.
  EXPECT_EQ(page->GetController().GetLastCommittedEntry()->GetUniqueID(),
            entry);
  EXPECT_EQ(content::EvalJs(page, "sessionStorage.loads"), "1");

  // A page with no event clears it.
  ASSERT_TRUE(content::NavigateToURL(
      page, https.GetURL("calendar.google.com", "/empty")));
  ASSERT_TRUE(base::test::RunUntil([&] { return !subtitle().has_value(); }));

  // Back on a page with an event, it shows again.
  ASSERT_TRUE(content::NavigateToURL(
      page, https.GetURL("calendar.google.com", "/day")));
  ASSERT_TRUE(base::test::RunUntil([&] { return subtitle().has_value(); }));
}

}  // namespace
}  // namespace clu
