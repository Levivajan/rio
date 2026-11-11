// Copyright 2026 The Rio Authors. All rights reserved.

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_row_stamp.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/sessions/content/content_serialized_navigation_builder.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/base/filename_util.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

// Restored tabs go back to their rows (docs/chromium/fixes.md): a page Chromium brings back through
// History > Recently Closed, session restore after a crash, or "Continue where you left off" carries its row id in its
// history (clu_row_stamp.h) and is that row's page again, never a second Today row. The relaunch tests are PRE_ pairs
// that share a profile directory.
namespace clu {
namespace {

class CluRestoreBrowserTest : public InProcessBrowserTest {
 protected:
  CluRestoreBrowserTest() {
    // No startup tab: the window shows what Clu and Chromium restore.
    set_open_about_blank_on_browser_launch(false);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    profile_ = browser()->GetProfile();
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
  }

  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluModelService& model() { return *window().model(); }
  const CluTabTree& tree() { return model().tree(); }
  TabStripModel& strip() { return *browser()->GetTabStripModel(); }
  std::string space() { return window().space_id(); }

  // Files, so the rows a PRE_ test saves still load after the relaunch.
  GURL Url(const std::string& page) {
    return net::FilePathToFileURL(
        base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
            .AppendASCII("clu/test/data/tabs")
            .AppendASCII(page));
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

  void FlushStore() {
    base::RunLoop loop;
    model().FlushForTesting(loop.QuitClosure());
    loop.Run();
  }

  ArchivePage Archive() {
    base::test::TestFuture<ArchivePage> page;
    model().ListArchive(std::string(), std::string(), 100, page.GetCallback());
    return page.Take();
  }

  // The row ids the strip's pages show, placeholders left out, in strip order (a row twice is a duplicate).
  std::vector<std::string> PageRows() {
    std::vector<std::string> ids;
    for (int i = 0; i < strip().count(); ++i) {
      content::WebContents* contents = strip().GetWebContentsAt(i);
      if (!CluPlaceholder::Is(contents) && CluTabId::Has(contents)) {
        ids.push_back(CluTabId::Get(contents));
      }
    }
    return ids;
  }

  // Every page shows a row of the tree, and no row shows twice.
  void ExpectEveryPageIsOneRow() {
    const std::vector<std::string> ids = PageRows();
    EXPECT_EQ(std::set<std::string>(ids.begin(), ids.end()).size(),
              ids.size());
    for (const std::string& id : ids) {
      EXPECT_TRUE(tree().Get(id)) << id;
    }
  }

  // History > Recently Closed: Chromium's own restore of its newest entry, as the menu item runs it
  // (app_controller_mac::TabRestorer::RestoreByID).
  void RestoreFromRecentlyClosed() {
    sessions::TabRestoreService* service =
        TabRestoreServiceFactory::GetForProfile(profile_);
    ASSERT_TRUE(service);
    service->LoadTabsFromLastSession();
    ASSERT_TRUE(base::test::RunUntil([&] { return service->IsLoaded(); }));
    ASSERT_FALSE(service->entries().empty());
    service->RestoreEntryById(BrowserLiveTabContext::From(browser()),
                              service->entries().front()->id,
                              WindowOpenDisposition::kUnknown);
  }

  // Three rows with pages: a favorite, a pinned row in a folder and a Today row, the Today row shown.
  void OpenThreeRows() {
    const std::string favorite = Open("a.html");
    ASSERT_TRUE(window()
                    .MoveRows({favorite}, DropTarget::Favorite(0), false)
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
    Open("c.html");
    ASSERT_EQ(PageRows().size(), 3u);
    FlushStore();
  }

  // What OpenThreeRows() left, read back after a relaunch.
  void ExpectThreeRowsInPlace() {
    ASSERT_EQ(tree().Favorites().size(), 1u);
    ASSERT_EQ(tree().Pinned(space()).size(), 1u);
    const TabNode* folder = tree().Get(tree().Pinned(space())[0]);
    ASSERT_TRUE(folder && folder->is_folder());
    ASSERT_EQ(tree().Children(folder->id).size(), 1u);
    EXPECT_EQ(GURL(tree().Get(tree().Children(folder->id)[0])->url),
              Url("b.html"));
    // Without the row ids every restored page was a new Today row: four here.
    ASSERT_EQ(tree().Today(space()).size(), 1u);
    EXPECT_EQ(GURL(tree().Get(tree().Today(space())[0])->url), Url("c.html"));
  }

  raw_ptr<Profile, AcrossTasksDanglingUntriaged> profile_ = nullptr;
};

// The id rides on every entry, so both restore files carry it, and an entry read back from them names the row.
IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       CluRestoreAPageCarriesItsRowIdIntoChromiumsRestoreFiles) {
  const std::string row = Open("a.html");
  content::WebContents* page = Contents(row);
  ASSERT_TRUE(content::NavigateToURL(page, Url("b.html")));
  content::NavigationController& controller = page->GetController();
  ASSERT_EQ(controller.GetEntryCount(), 2);
  std::vector<sessions::SerializedNavigationEntry> navigations;
  for (int i = 0; i < controller.GetEntryCount(); ++i) {
    navigations.push_back(
        sessions::ContentSerializedNavigationBuilder::FromNavigationEntry(
            i, controller.GetEntryAtIndex(i)));
    const auto& info = navigations.back().extended_info_map();
    ASSERT_TRUE(info.contains(kRowStampKey)) << i;
    EXPECT_EQ(info.at(kRowStampKey), row) << i;
  }

  // A page rebuilt from those entries, as session restore and Recently Closed build it, takes the row id.
  std::vector<std::unique_ptr<content::NavigationEntry>> entries =
      sessions::ContentSerializedNavigationBuilder::ToNavigationEntries(
          navigations, profile_);
  std::unique_ptr<content::WebContents> restored =
      content::WebContents::Create(
          content::WebContents::CreateParams(profile_));
  restored->GetController().Restore(1, content::RestoreType::kRestored,
                                    &entries);
  EXPECT_EQ(CluTabId::Get(restored.get()), row);
  EXPECT_TRUE(CluTabId::WasRestored(restored.get()));

  // A page whose history names no row gets a fresh id, and is not a restored one.
  std::unique_ptr<content::WebContents> fresh = content::WebContents::Create(
      content::WebContents::CreateParams(profile_));
  EXPECT_NE(CluTabId::Get(fresh.get()), row);
  EXPECT_FALSE(CluTabId::WasRestored(fresh.get()));
}

// History > Recently Closed after Cmd+W on a Today row: the page is that row again, once, and leaves the archive.
IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       CluRestoreRecentlyClosedBringsATodayRowBackOnce) {
  const std::string row = Open("a.html");
  const std::string other = Open("b.html");
  ASSERT_TRUE(window().ActivateRow(row));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(row); }));
  FlushStore();
  ASSERT_EQ(Archive().rows.size(), 1u);

  RestoreFromRecentlyClosed();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Get(row) && window().ActiveRowId() == row; }));
  FlushStore();
  EXPECT_EQ(tree().Today(space()).size(), 2u);
  EXPECT_EQ(tree().Today(space()).front(), row);
  EXPECT_TRUE(Archive().rows.empty());
  ASSERT_TRUE(content::WaitForLoadStop(Contents(row)));
  EXPECT_EQ(Contents(row)->GetLastCommittedURL(), Url("a.html"));
  ExpectEveryPageIsOneRow();
  EXPECT_EQ(PageRows().size(), 2u);

  // Cmd+Shift+T finds that close already undone: nothing comes back a second time.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_RESTORE_TAB));
  FlushStore();
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(tree().Today(space()).size(), 2u);
  EXPECT_TRUE(tree().Get(other));
}

// A restored page whose row is gone for good (its archive row deleted) is one new Today row.
IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       CluRestoreAPageWithNoSavedRowIsOneTodayRow) {
  const std::string row = Open("a.html");
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(row); }));
  base::test::TestFuture<bool> deleted;
  model().DeleteArchive(row, deleted.GetCallback());
  ASSERT_TRUE(deleted.Get());

  RestoreFromRecentlyClosed();
  ASSERT_TRUE(base::test::RunUntil([&] { return PageRows().size() == 1u; }));
  FlushStore();
  content::RunAllTasksUntilIdle();
  ASSERT_EQ(tree().Today(space()).size(), 1u);
  EXPECT_EQ(PageRows()[0], tree().Today(space())[0]);
  EXPECT_TRUE(Archive().rows.empty());
}

// Closing a pinned row's page keeps the row; Recently Closed puts the page back in that row, in its folder.
IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       CluRestoreRecentlyClosedPutsAPinnedPageBackInItsRow) {
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
  ASSERT_TRUE(window().ActivateRow(pinned));
  ASSERT_TRUE(content::NavigateToURL(Contents(pinned), Url("c.html")));
  const size_t rows = tree().size();
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return !Contents(pinned); }));
  ASSERT_TRUE(tree().Get(pinned));

  RestoreFromRecentlyClosed();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window().ActiveRowId() == pinned; }));
  ASSERT_TRUE(content::WaitForLoadStop(Contents(pinned)));
  EXPECT_EQ(Contents(pinned)->GetLastCommittedURL(), Url("c.html"));
  const TabNode* node = tree().Get(pinned);
  EXPECT_EQ(node->section, Section::kPinned);
  EXPECT_EQ(node->parent_id, folder);
  EXPECT_EQ(node->position, 0);
  EXPECT_EQ(tree().size(), rows);
  EXPECT_TRUE(tree().Today(space()).empty());
  ExpectEveryPageIsOneRow();
}

// The row came back through Cmd+Shift+T first: Chromium's own copy of the same page goes, and the row shows.
IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       CluRestoreACopyOfARowThatShowsGoes) {
  const std::string row = Open("a.html");
  Open("b.html");
  ASSERT_TRUE(window().ActivateRow(row));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(row); }));
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_RESTORE_TAB));
  ASSERT_TRUE(base::test::RunUntil([&] { return Contents(row) != nullptr; }));
  ASSERT_EQ(PageRows().size(), 2u);

  RestoreFromRecentlyClosed();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return PageRows().size() == 2u && window().ActiveRowId() == row;
  }));
  EXPECT_EQ(tree().Today(space()).size(), 2u);
  ExpectEveryPageIsOneRow();
}

// "Continue where you left off": Chromium restores every page at launch, and each is its row's page again.
IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       PRE_CluRestoreContinueWhereYouLeftOffKeepsEveryRowInPlace) {
  SessionStartupPref::SetStartupPref(
      profile_, SessionStartupPref(SessionStartupPref::LAST));
  OpenThreeRows();
}

IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       CluRestoreContinueWhereYouLeftOffKeepsEveryRowInPlace) {
  ASSERT_TRUE(base::test::RunUntil([&] { return PageRows().size() == 3u; }));
  ExpectThreeRowsInPlace();
  ExpectEveryPageIsOneRow();
}

// After a crash Chromium restores nothing by itself and Rio shows its rows from clu.db. Restoring the crashed session
// (the restore the crash bubble offered; History lists the same windows) brings each page back to its row, and a
// page Rio already loaded is not shown twice.
IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       PRE_CluRestoreAfterACrashKeepsEveryRowInPlace) {
  OpenThreeRows();
  // This session ends as a crash for the next launch.
  ExitTypeService::GetInstanceForProfile(profile_)
      ->SetWaitingForUserToAckCrashForTest(true);
}

IN_PROC_BROWSER_TEST_F(CluRestoreBrowserTest,
                       CluRestoreAfterACrashKeepsEveryRowInPlace) {
  ASSERT_EQ(ExitTypeService::GetInstanceForProfile(profile_)
                ->last_session_exit_type(),
            ExitType::kCrashed);
  ExpectThreeRowsInPlace();
  // Rio shows the space's last row, and only that page loads.
  ASSERT_TRUE(base::test::RunUntil([&] { return PageRows().size() == 1u; }));

  SessionRestore::RestoreSessionAfterCrash(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return PageRows().size() == 3u; }));
  ExpectThreeRowsInPlace();
  ExpectEveryPageIsOneRow();
}

}  // namespace
}  // namespace clu
