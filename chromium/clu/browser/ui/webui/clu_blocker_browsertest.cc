// Copyright 2026 The Rio Authors. All rights reserved.

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/thread_annotations.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/site/clu_blocker.h"
#include "clu/browser/site/clu_site_extension.h"
#include "clu/browser/site/clu_site_profile.h"
#include "clu/browser/site/clu_site_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_site_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/ui_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "url/gurl.h"

// The CluBlocker* checks of the M7 "Done when" row (docs/chromium/PLAN.md section 6) and docs/PLAN.md 12.14's first
// box on Chromium: the fixture lists compiled to declarativeNetRequest rules on Clu's component extension, per-tab
// counts from Chromium's action tracker, hiding CSS per page, the per-site switch, and the first page after launch.
// The fixture server records every request it gets, so "blocked" means "never reached the server".
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kAds[] = "/blocker/ads.html";
constexpr char kBanner[] = "/blocker/ads/banner.js";
constexpr char kPixel[] = "/blocker/pixel.gif";
constexpr char kOk[] = "/blocker/ok.js";

std::string ReadFixture(const char* name) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string text;
  CHECK(base::ReadFileToString(root.AppendASCII("clu")
                                   .AppendASCII("test")
                                   .AppendASCII("data")
                                   .AppendASCII("blocker")
                                   .AppendASCII(name),
                               &text));
  return text;
}

class CluBlockerBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    // Everything the server serves is registered before it listens; it listens before the browser starts, so a
    // test may open a fixture page at launch.
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    embedded_test_server()->RegisterRequestMonitor(base::BindRepeating(
        &CluBlockerBrowserTest::OnRequest, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->InitializeAndListen());
    // A page on the command line loads before SetUpOnMainThread(), and the browser waits for it to stop loading.
    if (accept_before_launch_) {
      embedded_test_server()->StartAcceptingConnections();
    }
    InProcessBrowserTest::SetUp();
  }

  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    CluBlocker::SetResourcesForTesting(ReadFixture("rules.json"),
                                       ReadFixture("cosmetic.json"));
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    if (!accept_before_launch_) {
      embedded_test_server()->StartAcceptingConnections();
    }
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(base::test::RunUntil([&] { return blocker().ready(); }));
    handler_ = std::make_unique<CluBlockingHandler>(window().GetWeakPtr());
  }

  void TearDownOnMainThread() override {
    handler_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void OnRequest(const net::test_server::HttpRequest& request) {
    const auto host = request.headers.find("Host");
    std::string name = host == request.headers.end() ? "" : host->second;
    name = name.substr(0, name.find(':'));
    base::AutoLock lock(lock_);
    requests_.push_back(base::StrCat({name, request.GetURL().path()}));
  }

  // Requests of `path` from any host (`host` set: that host only) the server has seen.
  int Seen(const std::string& path, const std::string& host = "") {
    base::AutoLock lock(lock_);
    int count = 0;
    for (const std::string& r : requests_) {
      if (r.ends_with(path) && (host.empty() || r == host + path)) {
        ++count;
      }
    }
    return count;
  }

  void ForgetRequests() {
    base::AutoLock lock(lock_);
    requests_.clear();
  }

  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluBlocker& blocker() { return *GetCluBlocker(browser()->GetProfile()); }
  CluSiteService& site() { return *GetCluSiteService(browser()->GetProfile()); }

  std::string Open(const std::string& path, const std::string& host = "") {
    base::expected<std::string, std::string> id = window().CreateRow(
        host.empty() ? embedded_test_server()->GetURL(path)
                     : embedded_test_server()->GetURL(host, path),
        std::nullopt, /*activate=*/true);
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

  // TabState field `key` of row `row_id` as published.
  base::Value TabField(const std::string& row_id, const char* key) {
    const base::DictValue state = window().CurrentState();
    const base::DictValue* tab = state.FindDict("tabs")->FindDict(row_id);
    const base::Value* value = tab ? tab->Find(key) : nullptr;
    return value ? value->Clone() : base::Value();
  }

  std::string Display(content::WebContents* contents, const char* selector) {
    return content::EvalJs(
               contents,
               content::JsReplace(
                   "getComputedStyle(document.querySelector($1)).display",
                   selector))
        .ExtractString();
  }

  // Waits for the page's subresources: the image and both scripts have answered or failed.
  void WaitForSubresources(content::WebContents* contents) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      return content::EvalJs(
                 contents,
                 "performance.getEntriesByType('resource').length >= 1 && "
                 "document.readyState === 'complete'")
          .ExtractBool();
    }));
  }

  mojom::CallResultPtr SetSiteEnabled(const std::string& row_id, bool enabled) {
    CallFuture done;
    handler_->SetSiteEnabled(
        mojom::BlockingSetSiteEnabledInput::New(row_id, enabled),
        done.GetCallback());
    return done.Take();
  }

  // The server accepts before the browser starts (CluBlockerLaunchBrowserTest).
  bool accept_before_launch_ = false;
  base::Lock lock_;
  std::vector<std::string> requests_ GUARDED_BY(lock_);
  std::unique_ptr<CluBlockingHandler> handler_;
};

// 12.14: "Blocker blocks fixture trackers per tab with counts ... cosmetic hiding".
IN_PROC_BROWSER_TEST_F(CluBlockerBrowserTest,
                       CluBlockerBlocksFixtureTrackersAndCountsThemPerTab) {
  const std::string row = Open(kAds);
  content::WebContents* page = Contents(row);
  WaitForSubresources(page);
  EXPECT_EQ(Seen(kOk), 1);
  EXPECT_EQ(Seen(kBanner), 0);
  EXPECT_EQ(Seen(kPixel, "tracker.localhost"), 0);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabField(row, "blockedCount") == base::Value(2); }));
  EXPECT_EQ(TabField(row, "blockingEnabled"), base::Value(true));
  EXPECT_EQ(TabField(row, "site"), base::Value("127.0.0.1"));
  // The generic rule and the rule for this host both hide their slot.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Display(page, ".ad-slot") == "none"; }));
  EXPECT_EQ(Display(page, ".host-slot"), "none");
  EXPECT_EQ(content::EvalJs(page, "!!window.okLoaded").ExtractBool(), true);
  EXPECT_EQ(content::EvalJs(page, "!!window.bannerLoaded").ExtractBool(),
            false);

  // Counts are per tab and per page: a second tab counts its own, a plain page counts none.
  const std::string other = Open("/tabs/a.html");
  EXPECT_EQ(TabField(other, "blockedCount"), base::Value(0));
  EXPECT_EQ(TabField(row, "blockedCount"), base::Value(2));
}

// Turning blocking off on a site excepts that site only: a subdomain is still blocked, as its state says.
IN_PROC_BROWSER_TEST_F(CluBlockerBrowserTest,
                       CluBlockerTheSiteSwitchLeavesSubdomainsBlocked) {
  const std::string row = Open(kAds, "site.localhost");
  WaitForSubresources(Contents(row));
  content::TestNavigationObserver reload(Contents(row));
  ASSERT_TRUE(SetSiteEnabled(row, false)->is_value_json());
  reload.Wait();
  WaitForSubresources(Contents(row));
  EXPECT_EQ(Seen(kBanner, "site.localhost"), 1);

  const std::string sub = Open(kAds, "mail.site.localhost");
  WaitForSubresources(Contents(sub));
  EXPECT_EQ(Seen(kBanner, "mail.site.localhost"), 0);
  EXPECT_EQ(TabField(sub, "blockingEnabled"), base::Value(true));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabField(sub, "blockedCount") == base::Value(2); }));
}

// 12.14: "per-site toggle reloads and persists". The exception survives a restart.
IN_PROC_BROWSER_TEST_F(CluBlockerBrowserTest,
                       PRE_CluBlockerTheSiteSwitchReloadsAndPersists) {
  const std::string row = Open(kAds);
  WaitForSubresources(Contents(row));
  ASSERT_EQ(Seen(kBanner), 0);
  ForgetRequests();
  content::TestNavigationObserver reload(Contents(row));
  ASSERT_TRUE(SetSiteEnabled(row, false)->is_value_json());
  // The tab reloads by itself once the exception is in place.
  reload.Wait();
  WaitForSubresources(Contents(row));
  EXPECT_EQ(Seen(kBanner), 1);
  EXPECT_EQ(Seen(kPixel, "tracker.localhost"), 1);
  EXPECT_EQ(TabField(row, "blockingEnabled"), base::Value(false));
  EXPECT_EQ(TabField(row, "blockedCount"), base::Value(0));
  EXPECT_EQ(Display(Contents(row), ".ad-slot"), "block");
  EXPECT_FALSE(site().IsBlockingEnabled("127.0.0.1"));
}

IN_PROC_BROWSER_TEST_F(CluBlockerBrowserTest,
                       CluBlockerTheSiteSwitchReloadsAndPersists) {
  EXPECT_FALSE(site().IsBlockingEnabled("127.0.0.1"));
  const std::string row = Open(kAds);
  WaitForSubresources(Contents(row));
  EXPECT_EQ(Seen(kBanner), 1);
  // Back on: the next page is blocked again.
  ForgetRequests();
  content::TestNavigationObserver reload(Contents(row));
  ASSERT_TRUE(SetSiteEnabled(row, true)->is_value_json());
  reload.Wait();
  WaitForSubresources(Contents(row));
  EXPECT_EQ(Seen(kBanner), 0);
  EXPECT_TRUE(site().IsBlockingEnabled("127.0.0.1"));
}

// A row that does not exist, or a page with no site, changes nothing.
IN_PROC_BROWSER_TEST_F(CluBlockerBrowserTest,
                       CluBlockerTheSwitchNeedsARowWithASite) {
  mojom::CallResultPtr unknown =
      SetSiteEnabled("00000000-0000-4000-8000-0000000000aa", false);
  ASSERT_TRUE(unknown->is_error());
  EXPECT_EQ(unknown->get_error()->code, mojom::ErrorCode::kNotFound);
  const std::string blank = window().CreateRow(GURL("about:blank"),
                                               std::nullopt, true).value_or("");
  ASSERT_FALSE(blank.empty());
  mojom::CallResultPtr no_site = SetSiteEnabled(blank, false);
  ASSERT_TRUE(no_site->is_error());
  EXPECT_EQ(no_site->get_error()->reason, "no-site");
  EXPECT_TRUE(site().exceptions().empty());
}

// The M7 "Done when": the first page after launch is already blocked. The page is on the command line, so it starts
// loading while the profile and its blocker are still coming up; the navigation throttle holds it until the rules
// are in place.
class CluBlockerLaunchBrowserTest : public CluBlockerBrowserTest {
 protected:
  CluBlockerLaunchBrowserTest() {
    set_open_about_blank_on_browser_launch(false);
    accept_before_launch_ = true;
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    CluBlockerBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendArg(embedded_test_server()->GetURL(kAds).spec());
  }
};

IN_PROC_BROWSER_TEST_F(CluBlockerLaunchBrowserTest,
                       CluBlockerTheFirstPageAfterLaunchIsBlocked) {
  content::WebContents* page = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    page = browser()->tab_strip_model()->GetActiveWebContents();
    return page && page->GetLastCommittedURL().path() == kAds;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(page));
  WaitForSubresources(page);
  EXPECT_EQ(Seen(kOk), 1);
  EXPECT_EQ(Seen(kBanner), 0);
  EXPECT_EQ(Seen(kPixel, "tracker.localhost"), 0);
  EXPECT_EQ(Display(page, ".ad-slot"), "none");
}

// Private windows block with the profile's lists (docs/PLAN.md 3.15: blocking still applies there).
IN_PROC_BROWSER_TEST_F(CluBlockerBrowserTest, CluBlockerBlocksInAPrivateWindow) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      incognito, embedded_test_server()->GetURL(kAds)));
  content::WebContents* page =
      incognito->GetTabStripModel()->GetActiveWebContents();
  WaitForSubresources(page);
  EXPECT_EQ(Seen(kOk), 1);
  EXPECT_EQ(Seen(kBanner), 0);
  EXPECT_EQ(Seen(kPixel, "tracker.localhost"), 0);
}

// The blocker's host is invisible: Chromium's extensions page and Clu's action row never list it.
IN_PROC_BROWSER_TEST_F(CluBlockerBrowserTest,
                       CluBlockerTheComponentExtensionStaysOutOfSight) {
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(browser()->GetProfile())
          ->enabled_extensions()
          .GetByID(kCluExtensionId);
  ASSERT_TRUE(extension);
  EXPECT_FALSE(extensions::ui_util::ShouldDisplayInExtensionSettings(*extension));
  const base::DictValue state = window().CurrentState();
  const base::Value* row = state.FindDict("windows")
                               ->FindDict(window().window_id())
                               ->Find("actionRow");
  EXPECT_TRUE(!row || row->is_none());
  EXPECT_FALSE(blocker().rules_version().empty());
}

}  // namespace
}  // namespace clu
