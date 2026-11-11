// Copyright 2026 The Rio Authors. All rights reserved.

// The app settings AI tab's browser-process features (docs/chromium/parity-command.md): Tidy Tab Titles renames a
// Today tab as it is pinned, and Tidy Downloads renames a finished download with a junk name after its page. Each
// follows its switch (ai.tidyTitles, ai.tidyDownloads, registered by the settings window's branch).
#include <memory>
#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/ai/clu_link_hover.h"
#include "clu/browser/ai/clu_shift_key.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/download/public/common/download_item.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "url/gurl.h"

namespace clu {
namespace {

// /ai/attachment?name=<file>: a small attachment with that name.
std::unique_ptr<net::test_server::HttpResponse> HandleAttachment(
    const net::test_server::HttpRequest& request) {
  const GURL url = request.GetURL();
  if (url.path() != "/ai/attachment") {
    return nullptr;
  }
  std::string name = "file.bin";
  for (const std::string_view pair :
       base::SplitStringPiece(url.query(), "&", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (pair.starts_with("name=")) {
      name = std::string(pair.substr(5));
    }
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("application/octet-stream");
  response->AddCustomHeader(
      "Content-Disposition",
      base::StrCat({"attachment; filename=\"", name, "\""}));
  response->set_content(std::string(512, 'c'));
  return response;
}

class CluAiBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // Made-up hosts reach the test server, so the tidy rules see a site's name. They end in .test: a real domain
    // would be upgraded to https (google.com is HSTS-preloaded) and never reach the http test server.
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleAttachment));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    // Files land in the test's download folder without a save dialog.
    browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kPromptForDownload,
                                                    false);
  }

  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  const CluTabTree& tree() { return window().model()->tree(); }

  // A Today row on `path` at `host`, shown, loaded and titled.
  std::string Open(const std::string& host, const std::string& path) {
    base::expected<std::string, std::string> id = window().CreateRow(
        embedded_test_server()->GetURL(host, path), std::nullopt,
        /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return std::string();
    }
    tabs::TabInterface* tab = window().FindTab(id.value());
    EXPECT_TRUE(tab && content::WaitForLoadStop(tab->GetContents()));
    if (!tab) {
      return std::string();
    }
    const std::string row = id.value();
    // The row shows the page's own <title> (before it arrives, a title made from the URL).
    content::NavigationEntry* entry =
        tab->GetContents()->GetController().GetLastCommittedEntry();
    EXPECT_TRUE(base::test::RunUntil([&] {
      return entry && !entry->GetTitle().empty() &&
             tree().Get(row)->title == base::UTF16ToUTF8(entry->GetTitle());
    }));
    return row;
  }

  // The switch, when the settings window's registry knows it (it refuses unknown keys).
  bool SetSwitch(const char* key, bool on) {
    if (!FindSetting(key) || !CluAppSettings::Get()) {
      return false;
    }
    CluAppSettings::Get()->SetGlobal(key, base::Value(on));
    return true;
  }

  download::DownloadItem* OnlyDownload() {
    download::SimpleDownloadManager::DownloadVector items;
    browser()->GetProfile()->GetDownloadManager()->GetAllDownloads(&items);
    return items.size() == 1u ? items[0].get() : nullptr;
  }
};

// Hover previews: Shift held over a link opens it in Peek, whether Shift went down before the pointer
// reached the link or after it.
IN_PROC_BROWSER_TEST_F(CluAiBrowserTest, CluAiShiftHeldOverALinkOpensItInPeek) {
  const std::string row = Open("docs.google.test", "/ai/pin.html");
  content::WebContents* page = window().FindTab(row)->GetContents();
  const GURL link =
      embedded_test_server()->GetURL("travel.example.test", "/ai/pin.html");
  auto wait = [](base::TimeDelta delay) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), delay);
    loop.Run();
  };

  // The pointer rests on the link with Shift up: nothing opens.
  SetShiftKeyDownForTesting(0);
  OnTargetUrl(page, link);
  wait(CluWindowState::kHoverPreviewDelay * 2);
  EXPECT_FALSE(window().multitask().peek().is_open());

  // Shift goes down while the pointer stays: Peek opens on the link.
  SetShiftKeyDownForTesting(1);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window().multitask().peek().is_open(); }));
  window().multitask().peek().Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window().multitask().peek().is_open(); }));

  // The pointer leaves the link before the delay: nothing opens.
  OnTargetUrl(page, link);
  OnTargetUrl(page, GURL());
  wait(CluWindowState::kHoverPreviewDelay * 2);
  EXPECT_FALSE(window().multitask().peek().is_open());
  SetShiftKeyDownForTesting(-1);
}

IN_PROC_BROWSER_TEST_F(CluAiBrowserTest, CluAiPinningATabGivesItATidyTitle) {
  const std::string row = Open("docs.google.test", "/ai/pin.html");
  EXPECT_EQ(tree().Get(row)->title, "(2) Interior Design Book - Google Docs");
  ASSERT_TRUE(window().TogglePin(row).has_value());
  EXPECT_EQ(tree().Get(row)->section, Section::kPinned);
  // The title comes a moment later: from the on-device model when the Mac has one, else (as in this test binary,
  // which carries no rio-ai) from the rules.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tree().Get(row)->title_override == "Interior Design Book";
  }));
  // The sidebar shows the tidy title: state carries it as the row's rename.
  const base::DictValue state = window().CurrentState();
  EXPECT_EQ(*state.FindDict("tabs")->FindDict(row)->FindString("titleOverride"),
            "Interior Design Book");

  // A rename the user made is never replaced: unpin, rename, pin again.
  ASSERT_TRUE(window().TogglePin(row).has_value());
  window().model()->mutable_tree().Mutable(row)->title_override = "Book";
  ASSERT_TRUE(window().TogglePin(row).has_value());
  EXPECT_EQ(tree().Get(row)->title_override, "Book");

  // With the switch off, a pinned tab keeps its own title.
  if (SetSwitch("ai.tidyTitles", false)) {
    const std::string other = Open("docs.google.test", "/ai/pin.html");
    ASSERT_TRUE(window().TogglePin(other).has_value());
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(tree().Get(other)->title_override.has_value());
  }
}

IN_PROC_BROWSER_TEST_F(CluAiBrowserTest, CluAiAJunkDownloadNameTakesThePageTitle) {
  const std::string row =
      Open("travel.example.test", "/ai/download.html?name=387494876.pdf");
  content::WebContents* page = window().FindTab(row)->GetContents();
  content::DownloadTestObserverTerminal done(
      browser()->GetProfile()->GetDownloadManager(), 1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  ASSERT_TRUE(content::ExecJs(page, "document.getElementById('file').click()"));
  done.WaitForFinished();
  ASSERT_TRUE(base::test::RunUntil([&] {
    download::DownloadItem* item = OnlyDownload();
    return item &&
           item->GetTargetFilePath().BaseName().AsUTF8Unsafe() ==
               "Navan Itinerary.pdf";
  }));
  base::ScopedAllowBlockingForTesting allow_blocking;
  EXPECT_TRUE(base::PathExists(OnlyDownload()->GetTargetFilePath()));
}

IN_PROC_BROWSER_TEST_F(CluAiBrowserTest, CluAiAGoodDownloadNameStays) {
  const std::string row =
      Open("travel.example.test", "/ai/download.html?name=Boarding%20pass.pdf");
  content::WebContents* page = window().FindTab(row)->GetContents();
  content::DownloadTestObserverTerminal done(
      browser()->GetProfile()->GetDownloadManager(), 1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  ASSERT_TRUE(content::ExecJs(page, "document.getElementById('file').click()"));
  done.WaitForFinished();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(OnlyDownload());
  EXPECT_EQ(OnlyDownload()->GetState(), download::DownloadItem::COMPLETE);
  EXPECT_EQ(OnlyDownload()->GetTargetFilePath().BaseName().AsUTF8Unsafe(),
            "Boarding pass.pdf");
}

IN_PROC_BROWSER_TEST_F(CluAiBrowserTest, CluAiTidyDownloadsFollowsItsSwitch) {
  if (!SetSwitch("ai.tidyDownloads", false)) {
    GTEST_SKIP() << "ai.tidyDownloads is registered by chromium/parity-settings";
  }
  const std::string row =
      Open("travel.example.test", "/ai/download.html?name=387494876.pdf");
  content::WebContents* page = window().FindTab(row)->GetContents();
  content::DownloadTestObserverTerminal done(
      browser()->GetProfile()->GetDownloadManager(), 1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  ASSERT_TRUE(content::ExecJs(page, "document.getElementById('file').click()"));
  done.WaitForFinished();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(OnlyDownload());
  EXPECT_EQ(OnlyDownload()->GetState(), download::DownloadItem::COMPLETE);
  EXPECT_EQ(OnlyDownload()->GetTargetFilePath().BaseName().AsUTF8Unsafe(),
            "387494876.pdf");
}

// General, Warn before quitting: on by default, and it drives Chromium's own "Hold ⌘Q
// to quit" pref both ways.
IN_PROC_BROWSER_TEST_F(CluAiBrowserTest, CluSettingsWarnBeforeQuittingDrivesConfirmToQuit) {
  PrefService* local_state = g_browser_process->local_state();
  EXPECT_TRUE(local_state->GetBoolean(prefs::kConfirmToQuitEnabled));
  CluAppSettings::Get()->SetGlobal(kWarnBeforeQuitKey, base::Value(false));
  EXPECT_FALSE(local_state->GetBoolean(prefs::kConfirmToQuitEnabled));
  CluAppSettings::Get()->SetGlobal(kWarnBeforeQuitKey, base::Value(true));
  EXPECT_TRUE(local_state->GetBoolean(prefs::kConfirmToQuitEnabled));
}

}  // namespace
}  // namespace clu
