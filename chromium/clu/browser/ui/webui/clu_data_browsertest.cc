// Copyright 2026 The Rio Authors. All rights reserved.

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/signin/chrome_signin_pref_names.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_import_service.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "clu/browser/importer/clu_site_storage.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/password_store_results_observer.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "components/services/storage/public/mojom/storage_usage_info.mojom.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_model_service_factory.h"
#include "clu/browser/model/clu_store.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_data_handlers.h"
#include "clu/browser/ui/webui/clu_settings_window.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/multitask/clu_multitask_hooks.h"
#include "clu/browser/settings/clu_settings_hooks.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/common/clu_urls.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_partition_key.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/test/test_cookie_manager.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/test/history_service_test_util.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_launcher.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluData* checks of docs/PLAN.md 12.13 (M8, docs/chromium/m8.md): the store's recovery, history and Cmd+Y, the
// settings page, clear browsing data and both imports, against real pages, Chromium's HistoryService and
// BrowsingDataRemover, and the handlers the bridge calls. The boxes each test covers are in
// docs/chromium/acceptance-map.md.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

// Every element named `tag` under `root`, across shadow roots (Chromium's history page is Lit).
constexpr char kDeepTexts[] = R"(
  (() => {
    const out = [];
    const walk = (root) => {
      for (const el of root.querySelectorAll('*')) {
        if (el.localName === $1) out.push(el.shadowRoot ? el.shadowRoot.textContent : el.textContent);
        if (el.shadowRoot) walk(el.shadowRoot);
      }
    };
    walk(document);
    return out.join('\n');
  })())";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

mojom::ErrorCode Code(const mojom::CallResultPtr& result) {
  return result->get_error()->code;
}

base::Value Json(const mojom::CallResultPtr& result) {
  return base::JSONReader::Read(result->get_value_json(), base::JSON_PARSE_RFC)
      .value();
}

class CluDataBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
  }

  void TearDownOnMainThread() override {
    if (CluImportService* import = CluImportService::Get()) {
      import->SetRootForTesting(std::nullopt);
    }
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluModelService& model() { return *window().model(); }
  TabStripModel& strip() { return *browser()->GetTabStripModel(); }
  std::string space() { return window().space_id(); }
  Profile* profile() { return browser()->GetProfile(); }
  history::HistoryService* history() {
    return HistoryServiceFactory::GetForProfile(
        profile(), ServiceAccessType::EXPLICIT_ACCESS);
  }

  GURL Url(const std::string& path) {
    return embedded_test_server()->GetURL(path);
  }

  std::string Open(const GURL& url) {
    base::expected<std::string, std::string> id =
        window().CreateRow(url, std::nullopt, /*activate=*/true);
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

  content::WebContents* ActiveContents() {
    tabs::TabInterface* tab = window().ActiveTab();
    return tab ? tab->GetContents() : nullptr;
  }

  void WaitForSelector(content::WebContents* contents,
                       const std::string& selector) {
    EXPECT_EQ(true, content::EvalJs(contents, content::JsReplace(
                                                  kWaitForSelector, selector)));
  }

  void FlushStore() {
    base::RunLoop loop;
    model().FlushForTesting(loop.QuitClosure());
    loop.Run();
  }

  history::QueryURLAndVisitsResult Visits(const GURL& url) {
    return Visits(history(), url);
  }

  history::QueryURLAndVisitsResult Visits(history::HistoryService* history,
                                          const GURL& url) {
    base::CancelableTaskTracker tracker;
    base::test::TestFuture<history::QueryURLAndVisitsResult> result;
    history->QueryURLAndVisits(url, history::VisitQuery404sPolicy::kInclude404s,
                               result.GetCallback(), &tracker);
    return result.Take();
  }

  ArchivePage Archive() {
    base::test::TestFuture<ArchivePage> page;
    model().ListArchive(std::string(), std::string(), 100, page.GetCallback());
    return page.Take();
  }

  // Rows of this window's space whose URL is on `host` of chrome://.
  std::vector<std::string> RowsOn(const std::string& host) {
    std::vector<std::string> out;
    for (const TabNode* node : model().tree().All()) {
      const GURL url(node->url);
      if (node->is_tab() && model().IsInSpace(node->id, space()) &&
          url.SchemeIs("chrome") && url.host() == host) {
        out.push_back(node->id);
      }
    }
    return out;
  }

  base::DictValue State() { return window().CurrentState(); }
};

// 12.13 box 2: visits, retention, rio://history and Cmd+Y ----------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluDataBrowserTest,
                       CluDataVisitsAreRecordedWithTheirTransitions) {
  // A typed visit: the command bar's navigation (TYPED | FROM_ADDRESS_BAR).
  const std::string row = Open(Url("/data/landing.html"));
  ASSERT_TRUE(content::NavigateToURL(Contents(row), Url("/data/harbor.html"),
                                     Url("/data/harbor.html")));
  // A link click.
  content::TestNavigationObserver link(Contents(row));
  ASSERT_TRUE(content::ExecJs(
      Contents(row), content::JsReplace("location.href = $1",
                                        Url("/data/kettle.html").spec())));
  link.Wait();
  history::BlockUntilHistoryProcessesPendingRequests(history());
  const history::QueryURLAndVisitsResult kettle = Visits(Url("/data/kettle.html"));
  ASSERT_TRUE(kettle.success);
  ASSERT_FALSE(kettle.visits.empty());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(kettle.visits.back().transition,
                                           ui::PAGE_TRANSITION_LINK));
  const history::QueryURLAndVisitsResult harbor = Visits(Url("/data/harbor.html"));
  ASSERT_TRUE(harbor.success);
  EXPECT_EQ(harbor.row.title(), u"Harbor lights");
  // Retention: Chromium's HistoryBackend expires visits after 90 days.
  static_assert(history::HistoryBackend::kExpireDaysThreshold == 90);
  // A private window records nothing.
  BrowserWindowInterface* incognito = CreateIncognitoBrowser(profile());
  CluWindowState* private_window = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(private_window);
  const base::expected<std::string, std::string> private_row =
      private_window->CreateRow(Url("/data/meadow.html"), std::nullopt, true);
  ASSERT_TRUE(private_row.has_value());
  ASSERT_TRUE(content::WaitForLoadStop(
      private_window->FindTab(private_row.value())->GetContents()));
  history::BlockUntilHistoryProcessesPendingRequests(history());
  EXPECT_FALSE(Visits(Url("/data/meadow.html")).success);
}

IN_PROC_BROWSER_TEST_F(CluDataBrowserTest,
                       CluDataCmdYFocusesOrOpensHistoryInTheSpace) {
  const std::string page = Open(Url("/data/harbor.html"));
  history::BlockUntilHistoryProcessesPendingRequests(history());
  // Cmd+Y reaches Clu as IDC_SHOW_HISTORY (Chromium's own History item) from a focused page.
  content::WebContents* page_contents = Contents(page);
  page_contents->Focus();
  chrome::ExecuteCommand(browser(), IDC_SHOW_HISTORY);
  ASSERT_TRUE(base::test::RunUntil([&] { return RowsOn("history").size() == 1; }));
  const std::string history_row = RowsOn("history")[0];
  EXPECT_EQ(window().ActiveRowId(), history_row);
  ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  // State shows the rio:// alias.
  EXPECT_EQ(*State().FindDict("tabs")->FindDict(history_row)->FindString("url"),
            "rio://history/");
  // Again from the page: the same row, no second history tab.
  window().ActivateRow(page);
  chrome::ExecuteCommand(browser(), IDC_SHOW_HISTORY);
  EXPECT_EQ(window().ActiveRowId(), history_row);
  EXPECT_EQ(RowsOn("history").size(), 1u);

  // Chromium's page lists the visit, searches it and drops it once deleted.
  content::WebContents* page_ui = ActiveContents();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page_ui, content::JsReplace(kDeepTexts, "history-item"))
               .ExtractString()
               .find("Harbor lights") != std::string::npos;
  }));
  ASSERT_TRUE(content::NavigateToURL(page_ui, GURL("chrome://history/?q=Harbor")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page_ui, content::JsReplace(kDeepTexts, "history-item"))
               .ExtractString()
               .find("Harbor lights") != std::string::npos;
  }));
  history()->DeleteURLs({Url("/data/harbor.html")});
  history::BlockUntilHistoryProcessesPendingRequests(history());
  EXPECT_FALSE(Visits(Url("/data/harbor.html")).success);
  ASSERT_TRUE(content::NavigateToURL(page_ui, GURL("chrome://history/?q=Harbor")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page_ui, content::JsReplace(kDeepTexts, "history-item"))
               .ExtractString()
               .find("Harbor lights") == std::string::npos;
  }));
  // A rio:// alias typed in the command bar opens the same page.
  EXPECT_TRUE(window().CreateRow(GURL("rio://history/"), std::nullopt, true).has_value());
}

// Rio has no Google account sign-in, sync or Chrome AI features: neither its own rio://settings nor Chromium's
// Settings (where Site permissions, Passwords and Performance lead) shows "You and Google" or "AI in Chrome", their
// sub-pages or their search hits. The profile and the bookmark and history import stay, under "Profile".
constexpr char kDeepText[] = R"(
  new Promise(resolve => {
    // Only what is on screen: Settings keeps some menu items in the page with the hidden attribute.
    const text = () => {
      const out = [];
      const walk = (node) => {
        if (node.nodeType === Node.ELEMENT_NODE &&
            (node.hidden || getComputedStyle(node).display === 'none')) return;
        if (node.shadowRoot) walk(node.shadowRoot);
        for (const child of node.childNodes) {
          if (child.nodeType === Node.TEXT_NODE) out.push(child.textContent);
          else walk(child);
        }
      };
      walk(document.documentElement);
      return out.join(' ').replace(/\s+/g, ' ');
    };
    const tags = () => {
      const found = [];
      const walk = (node) => {
        if (node.shadowRoot) walk(node.shadowRoot);
        for (const child of node.children || []) {
          found.push(child.localName);
          walk(child);
        }
      };
      walk(document.documentElement);
      return found;
    };
    // Text and element names, once the page shows $1 (Settings renders late).
    const all = () => text() + '\n' + tags().join(' ');
    const tick = () => all().includes($1) ? resolve(all()) : setTimeout(tick, 50);
    tick();
  }))";

std::string DeepText(content::WebContents* contents, const std::string& wait_for) {
  return content::EvalJs(contents, content::JsReplace(kDeepText, wait_for))
      .ExtractString();
}

IN_PROC_BROWSER_TEST_F(CluDataBrowserTest,
                       CluDataSettingsHaveNoGoogleOrChromeAiSection) {
  const char* gone[] = {"You and Google", "AI in Chrome", "AI in Rio",
                        "Sync and Google services", "Turn on sync",
                        "settings-sync-page", "settings-google-services-page",
                        "settings-ai-page", "settings-account-page"};
  // Rio's app settings window (docs/chromium/parity-settings.md).
  ASSERT_TRUE(window().OpenAppSettings("general"));
  content::WebContents* app = CluSettingsWindow::Get()->contents();
  ASSERT_TRUE(content::WaitForLoadStop(app));
  WaitForSelector(app, "[role=tablist]");
  const std::string own = DeepText(app, "Shortcuts");
  for (const char* text : gone) {
    EXPECT_EQ(own.find(text), std::string::npos) << "app settings: " << text;
  }
  CluSettingsWindow::Get()->Close();

  // Chromium's Settings, its removed sub-pages (they fall back to the first section) and a search.
  for (const char* url :
       {"chrome://settings/", "chrome://settings/people", "chrome://settings/syncSetup",
        "chrome://settings/googleServices", "chrome://settings/ai",
        "chrome://settings/?search=sync", "chrome://settings/?search=Gemini"}) {
    ASSERT_TRUE(window().CreateRow(GURL(url), std::nullopt, true).has_value());
    content::WebContents* page = ActiveContents();
    ASSERT_TRUE(content::WaitForLoadStop(page));
    const std::string text = DeepText(page, "Profile");
    for (const char* name : gone) {
      EXPECT_EQ(text.find(name), std::string::npos) << url << ": " << name;
    }
  }
  // What Rio uses from the old section is reachable under Profile.
  ASSERT_TRUE(window()
                  .CreateRow(GURL("chrome://settings/people"), std::nullopt, true)
                  .has_value());
  ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  const std::string people =
      DeepText(ActiveContents(), "Import bookmarks and settings");
  EXPECT_NE(people.find("Profile"), std::string::npos);
  ASSERT_TRUE(window()
                  .CreateRow(GURL("chrome://settings/manageProfile"),
                             std::nullopt, true)
                  .has_value());
  ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  EXPECT_NE(DeepText(ActiveContents(), "settings-manage-profile")
                .find("settings-manage-profile"),
            std::string::npos);
}

// Fix pass (docs/chromium/fixes.md, leftovers 2): Chromium's own pages keep no leftover that leads to Google. A
// settings search with no hits offers no "Google Chrome help" link, autofill offers no suggestions from Gemini, the
// Settings row shows the gear, and chrome://history shows no "Sync history" card (Rio has no account to sync to).
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest,
                       CluDataChromiumPagesHaveNoGoogleLeftovers) {
  // Rio's settings row shows Chromium's settings gear, not the globe of a page favicon2 doesn't know.
  const auto settings_row =
      window().CreateRow(GURL("rio://settings/"), std::nullopt, true);
  ASSERT_TRUE(settings_row.has_value());
  ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  window().Flush();
  const base::DictValue state = State();
  const std::string* icon = state.FindDict("tabs")
                                ->FindDict(*settings_row)
                                ->FindString("favicon");
  ASSERT_TRUE(icon);
  EXPECT_NE(icon->find("pageUrl=chrome%3A%2F%2Fsettings%2F&"),
            std::string::npos)
      << *icon;

  ASSERT_TRUE(window()
                  .CreateRow(GURL("chrome://settings/?search=zzqxvj"),
                             std::nullopt, true)
                  .has_value());
  content::WebContents* page = ActiveContents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(content::EvalJs(page, R"(
      import('chrome://resources/js/load_time_data.js').then(({loadTimeData}) =>
          [loadTimeData.getString('searchNoResultsHelp'),
           loadTimeData.getBoolean('showSuggestionsFromGeminiSettings'),
           loadTimeData.getBoolean('isAtMemoryEnabled')].join('|')))"),
            "|false|false");
  const std::string search = DeepText(page, "No search results found");
  EXPECT_EQ(search.find("Chrome help"), std::string::npos);
  ASSERT_TRUE(window()
                  .CreateRow(GURL("chrome://settings/autofill"), std::nullopt,
                             true)
                  .has_value());
  ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  const std::string autofill =
      DeepText(ActiveContents(), "settings-autofill-page");
  EXPECT_EQ(autofill.find("settings-suggestions-from-gemini-page"),
            std::string::npos);

  // The history page reads the promo's shown count without an account; Rio's default is past Chromium's limit of 5.
  EXPECT_EQ(browser()->GetProfile()->GetPrefs()->GetInteger(
                prefs::kHistoryPageHistorySyncPromoShownCountPerProfile),
            kHistorySyncPromoShownByDefault);
  ASSERT_TRUE(window()
                  .CreateRow(GURL("chrome://history/"), std::nullopt, true)
                  .has_value());
  ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  const std::string history = DeepText(ActiveContents(), "history-list");
  EXPECT_EQ(history.find("history-sync-promo"), std::string::npos);
  EXPECT_EQ(history.find("Sync history"), std::string::npos);
}

// Google's translate servers refuse a fork's key, so a new profile never offers to translate a page.
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluDataTranslateIsNotOffered) {
  EXPECT_FALSE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      translate::prefs::kOfferTranslateEnabled));
}

// 12.13 box 3: rio://settings --------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluDataBrowserTest,
                       CluDataSettingsShowsEverySectionAndJunkReadsAsDefaults) {
  // Junk stored values: they read as the defaults (and are kept, unknown keys too).
  {
    ScopedDictPrefUpdate update(g_browser_process->local_state(), kCluSettingsPref);
    update->Set("appearance.mode", "purple");
    update->Set("shell.lastSidebarWidth", "wide");
    update->Set("someone.else", 7);
  }
  window().MarkDirty();
  window().Flush();
  const base::DictValue state = State();
  const base::DictValue* global = state.FindDict("settings")->FindDict("global");
  EXPECT_EQ(*global->FindString("appearance.mode"), "auto");
  EXPECT_EQ(global->FindInt("shell.lastSidebarWidth"), 214);
  EXPECT_FALSE(global->contains("someone.else"));
  EXPECT_EQ(*g_browser_process->local_state()
                 ->GetDict(kCluSettingsPref)
                 .FindString("appearance.mode"),
            "purple");

  // settings.open shows the app settings window at a tab; a second open brings the same window to its tab. No row
  // opens for it (docs/chromium/parity-settings.md).
  CluSettingsHandler settings(window().GetWeakPtr());
  CallFuture opened;
  settings.Open(
      mojom::SettingsOpenInput::New(mojom::SettingsOpenInputSection::kShortcuts),
      opened.GetCallback());
  ASSERT_TRUE(IsOk(opened.Take()));
  CluSettingsWindow* settings_window = CluSettingsWindow::Get();
  ASSERT_TRUE(settings_window);
  content::WebContents* page = settings_window->contents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(page->GetLastCommittedURL(), GURL("chrome://clu-settings/#shortcuts"));
  EXPECT_TRUE(RowsOn(kCluSettingsHost).empty());
  WaitForSelector(page, "[role=tab][aria-selected=true]");
  EXPECT_EQ(content::EvalJs(page, "document.querySelector('[role=tab][aria-selected=true]').textContent"),
            "Shortcuts");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return settings_window->GetWindowTitle() == u"Shortcuts"; }));
  CallFuture again;
  settings.Open(
      mojom::SettingsOpenInput::New(mojom::SettingsOpenInputSection::kProfiles),
      again.GetCallback());
  ASSERT_TRUE(IsOk(again.Take()));
  EXPECT_EQ(CluSettingsWindow::Get(), settings_window);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return page->GetLastCommittedURL() == GURL("chrome://clu-settings/#profiles");
  }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return settings_window->GetWindowTitle() == u"Profiles"; }));

  // The page is the internal:settings surface of this window: it gets its state (the profile list fills).
  EXPECT_EQ(true, content::EvalJs(page, R"(
    new Promise(resolve => {
      const check = () => {
        if (document.querySelectorAll('[role=option]').length > 0) resolve(true);
        else setTimeout(check, 20);
      };
      check();
    }))"));

  // settings.set: global and profile keys, wrong scopes and junk refused.
  CallFuture dark;
  settings.Set(mojom::SettingsSetInput::New("appearance.mode", "global", "\"dark\""),
               dark.GetCallback());
  EXPECT_TRUE(IsOk(dark.Take()));
  CallFuture hours;
  settings.Set(mojom::SettingsSetInput::New("tabs.archiveAfterHours",
                                            model().profile_id(), "168"),
               hours.GetCallback());
  EXPECT_TRUE(IsOk(hours.Take()));
  EXPECT_EQ(model().archive_after_hours(), 168);
  for (auto [key, scope, value] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"tabs.archiveAfterHours", "global", "24"},
           {"appearance.mode", model().profile_id(), "\"dark\""},
           {"appearance.mode", "global", "\"neon\""},
           {"no.such.key", "global", "1"},
           {"tabs.archiveAfterHours", model().profile_id(), "13"}}) {
    CallFuture refused;
    settings.Set(mojom::SettingsSetInput::New(key, scope, value), refused.GetCallback());
    const mojom::CallResultPtr result = refused.Take();
    ASSERT_FALSE(IsOk(result)) << key;
    EXPECT_EQ(Code(result), mojom::ErrorCode::kBadPayload) << key;
  }
  EXPECT_EQ(model().archive_after_hours(), 168);
  window().Flush();
  EXPECT_EQ(*State().FindStringByDottedPath("app.appearance.mode"), "dark");
}

// Parity: Rio's two settings (docs/chromium/parity-settings.md) -------------------------------

// Cmd+, (Chromium's IDC_OPTIONS, the app menu's Settings…) opens the app settings window: 684 x 650, fixed, its title
// the tab's name, no row in the sidebar. Cmd+, again and the app menu's path with the window in front bring the same
// window up; Cmd+W closes it.
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluSettingsCmdCommaOpensTheAppSettingsWindow) {
  ASSERT_FALSE(CluSettingsWindow::Get());
  const size_t rows = model().tree().All().size();
  chrome::ExecuteCommand(browser(), IDC_OPTIONS);
  ASSERT_TRUE(base::test::RunUntil([] { return CluSettingsWindow::Get(); }));
  CluSettingsWindow* settings_window = CluSettingsWindow::Get();
  EXPECT_EQ(settings_window->browser(), browser());
  EXPECT_EQ(settings_window->widget()->GetWindowBoundsInScreen().size(),
            gfx::Size(CluSettingsWindow::kWidth, CluSettingsWindow::kHeight));
  EXPECT_FALSE(settings_window->CanResize());
  EXPECT_FALSE(settings_window->CanMinimize());
  EXPECT_FALSE(settings_window->CanMaximize());
  content::WebContents* page = settings_window->contents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(page->GetLastCommittedURL().host(), kCluSettingsHost);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return settings_window->GetWindowTitle() == u"General"; }));
  EXPECT_EQ(model().tree().All().size(), rows);
  EXPECT_TRUE(RowsOn(kCluSettingsHost).empty());
  // The page runs under the WebUI CSP and draws the seven tabs, with no account tab.
  WaitForSelector(page, "[role=tab]");
  EXPECT_EQ(content::EvalJs(page, "[...document.querySelectorAll('[role=tab]')]"
                                  ".map(t => t.textContent).join(',')"),
            "General,Profiles,AI features,Links,Shortcuts,Icon,Advanced");

  chrome::ExecuteCommand(browser(), IDC_OPTIONS);
  EXPECT_EQ(CluSettingsWindow::Get(), settings_window);
  EXPECT_TRUE(ShowAppSettingsForProfile(profile()));
  EXPECT_EQ(CluSettingsWindow::Get(), settings_window);

  // Clicking a tab: the window's title follows.
  EXPECT_TRUE(content::ExecJs(
      page, "document.getElementById('clu-sw-tab-shortcuts').click()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return settings_window->GetWindowTitle() == u"Shortcuts"; }));

  EXPECT_TRUE(settings_window->AcceleratorPressed(
      ui::Accelerator(ui::VKEY_W, ui::EF_COMMAND_DOWN)));
  ASSERT_TRUE(base::test::RunUntil([] { return !CluSettingsWindow::Get(); }));
}

// The settings window opens where it last stood. A junk or off-screen saved spot
// opens it centered on the asking window again.
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluSettingsWindowRemembersWhereItStood) {
  PrefService* local_state = g_browser_process->local_state();
  local_state->ClearPref(kCluSettingsWindowOriginPref);
  auto open = [&] {
    chrome::ExecuteCommand(browser(), IDC_OPTIONS);
    EXPECT_TRUE(base::test::RunUntil([] { return CluSettingsWindow::Get(); }));
    return CluSettingsWindow::Get()->widget()->GetWindowBoundsInScreen();
  };
  auto close = [] {
    CluSettingsWindow::Get()->Close();
    EXPECT_TRUE(base::test::RunUntil([] { return !CluSettingsWindow::Get(); }));
  };
  const gfx::Rect centered = open();
  const gfx::Rect moved = centered + gfx::Vector2d(-40, 30);
  CluSettingsWindow::Get()->widget()->SetBounds(moved);
  close();
  const base::DictValue& saved = local_state->GetDict(kCluSettingsWindowOriginPref);
  EXPECT_EQ(saved.FindInt("x"), moved.x());
  EXPECT_EQ(saved.FindInt("y"), moved.y());
  EXPECT_EQ(open().origin(), moved.origin());
  close();

  local_state->SetDict(kCluSettingsWindowOriginPref,
                       base::DictValue().Set("x", "left").Set("y", 10));
  EXPECT_EQ(open().origin(), centered.origin());
  close();
  local_state->SetDict(kCluSettingsWindowOriginPref,
                       base::DictValue().Set("x", -100000).Set("y", -100000));
  EXPECT_EQ(open().origin(), centered.origin());
  close();
}

// The Profiles tab's + button creates a profile from the settings window (profiles.create allows the
// internal:settings surface), and the new profile is the picked one.
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluSettingsProfilesAddButtonCreatesAProfile) {
  ASSERT_TRUE(ShowAppSettingsForProfile(profile()));
  ASSERT_TRUE(base::test::RunUntil([] { return CluSettingsWindow::Get(); }));
  content::WebContents* page = CluSettingsWindow::Get()->contents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  // The tabs render after the load stops.
  WaitForSelector(page, "#clu-sw-tab-profiles");
  EXPECT_TRUE(content::ExecJs(
      page, "document.getElementById('clu-sw-tab-profiles').click()"));
  WaitForSelector(page, "[role=option]");
  const size_t before = State().FindDict("profiles")->size();
  EXPECT_TRUE(content::ExecJs(page, "document.querySelector('[aria-label=Add]').click()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return State().FindDict("profiles")->size() == before + 1; }));
  EXPECT_EQ(true, content::EvalJs(page, R"(
    new Promise(resolve => {
      const check = () => {
        const picked = document.querySelector('[role=option][aria-selected=true]');
        if (picked && picked.textContent.startsWith('New Profile')) resolve(true);
        else setTimeout(check, 20);
      };
      check();
    }))"));
  CluSettingsWindow::Get()->Close();
  ASSERT_TRUE(base::test::RunUntil([] { return !CluSettingsWindow::Get(); }));
}

// Browser settings' "AI innovations" entry links to chrome://settings/ai-features: it opens the app
// settings window at its AI tab, and no row or tab opens for it.
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluSettingsAiInnovationsOpensTheAppSettingsAiTab) {
  ASSERT_FALSE(CluSettingsWindow::Get());
  const std::string settings_row = Open(GURL("chrome://settings/help"));
  const int tabs = browser()->tab_strip_model()->count();
  NavigateParams params(browser(), GURL("chrome://settings/ai-features"),
                        ui::PAGE_TRANSITION_LINK);
  params.source_contents = Contents(settings_row);
  params.disposition = WindowOpenDisposition::kNewForegroundTab;
  params.is_renderer_initiated = true;
  params.user_gesture = true;
  Navigate(&params);
  ASSERT_TRUE(base::test::RunUntil([] { return CluSettingsWindow::Get(); }));
  ASSERT_TRUE(base::test::RunUntil([] {
    return CluSettingsWindow::Get()->GetWindowTitle() == u"AI features";
  }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs);
  EXPECT_EQ(RowsOn("settings").size(), 1u);
  CluSettingsWindow::Get()->Close();
  ASSERT_TRUE(base::test::RunUntil([] { return !CluSettingsWindow::Get(); }));
}

// The settings window belongs to the window that opened it; when that window closes it moves to another Rio window,
// and closes with the last one.
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluSettingsWindowFollowsItsRioWindow) {
  ASSERT_TRUE(window().OpenAppSettings("links"));
  CluSettingsWindow* settings_window = CluSettingsWindow::Get();
  ASSERT_TRUE(content::WaitForLoadStop(settings_window->contents()));
  BrowserWindowInterface* second = CreateBrowser(profile());
  ASSERT_TRUE(base::test::RunUntil([&] { return CluShell::From(second); }));
  CloseBrowserSynchronously(browser());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return CluSettingsWindow::Get() &&
           CluSettingsWindow::Get()->browser() == second;
  }));
  CloseBrowserSynchronously(second);
  ASSERT_TRUE(base::test::RunUntil([] { return !CluSettingsWindow::Get(); }));
}

// rio://settings is Chromium's own settings again, as a normal row: settings.openBrowser opens it at a page in this
// space and moves the same row on; About says Rio. Rio's own page never loads in a tab.
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluSettingsBrowserSettingsAreChromiumsPageInARow) {
  CluSettingsHandler settings(window().GetWeakPtr());
  CallFuture privacy;
  settings.OpenBrowser(mojom::SettingsOpenBrowserInput::New(
                           mojom::SettingsOpenBrowserInputPage::kPrivacy),
                       privacy.GetCallback());
  ASSERT_TRUE(IsOk(privacy.Take()));
  ASSERT_EQ(RowsOn("settings").size(), 1u);
  const std::string row = RowsOn("settings")[0];
  content::WebContents* page = ActiveContents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(page->GetLastCommittedURL(), GURL("chrome://settings/privacy"));
  window().Flush();
  EXPECT_EQ(*State().FindDict("tabs")->FindDict(row)->FindString("url"),
            "rio://settings/privacy");

  CallFuture about;
  settings.OpenBrowser(mojom::SettingsOpenBrowserInput::New(
                           mojom::SettingsOpenBrowserInputPage::kAbout),
                       about.GetCallback());
  ASSERT_TRUE(IsOk(about.Take()));
  EXPECT_EQ(RowsOn("settings"), std::vector<std::string>{row});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return page->GetLastCommittedURL() == GURL("chrome://settings/help");
  }));
  const std::string text = DeepText(page, "About Rio");
  EXPECT_NE(text.find("About Rio"), std::string::npos);
  EXPECT_EQ(text.find("About Chromium"), std::string::npos);
  EXPECT_EQ(text.find("About Arc"), std::string::npos);
  EXPECT_EQ(text.find("Google Chrome"), std::string::npos);

  // The command bar's Browser Settings and a typed rio://about reach the same page.
  EXPECT_TRUE(window().RunCommand("settings.browser"));
  EXPECT_EQ(RowsOn("settings"), std::vector<std::string>{row});
  EXPECT_EQ(ResolveCluUrl(GURL("rio://about")), GURL("chrome://settings/help"));
  EXPECT_FALSE(
      window().CreateRow(GURL("chrome://clu-settings/"), std::nullopt, true).has_value());
}

// Settings, Links: off, links from other apps are Chromium's (no Mini window).
IN_PROC_BROWSER_TEST_F(CluDataBrowserTest, CluSettingsLinksSwitchesReachTheBrowser) {
  CluSettingsHandler settings(window().GetWeakPtr());
  CallFuture off;
  settings.Set(mojom::SettingsSetInput::New("links.miniFromOtherApps", "global", "false"),
               off.GetCallback());
  ASSERT_TRUE(IsOk(off.Take()));
  EXPECT_FALSE(CluAppSettings::Get()->GetBool("links.miniFromOtherApps"));
  EXPECT_FALSE(OpenUrlsFromOtherApp({Url("/data/kettle.html")}));
  for (const char* key : {"links.peekOnShiftClick", "links.peekOtherSites", "ai.tidyTabs",
                          "ai.askRow"}) {
    EXPECT_TRUE(CluAppSettings::Get()->GetBool(key)) << key;
    CallFuture set;
    settings.Set(mojom::SettingsSetInput::New(key, "global", "false"), set.GetCallback());
    ASSERT_TRUE(IsOk(set.Take())) << key;
    EXPECT_FALSE(CluAppSettings::Get()->GetBool(key)) << key;
  }
  window().Flush();
  EXPECT_EQ(State().FindDict("settings")->FindDict("global")->FindBool("ai.tidyTabs"),
            false);
}

// 12.13 box 4: clear browsing data -----------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluDataBrowserTest,
                       CluDataClearBrowsingDataByRangeAndTypeWithArchiveRows) {
  // A cookie, a visit and an archived row in this profile.
  const std::string jar = Open(Url("/data/cookie.html?set=1"));
  EXPECT_EQ(content::EvalJs(Contents(jar), "document.cookie"), "clu_jar=oatmeal");
  const std::string other = Open(Url("/data/kettle.html"));
  window().CloseRowCommand(other);
  ASSERT_TRUE(base::test::RunUntil([&] { return Archive().rows.size() == 1; }));
  history::BlockUntilHistoryProcessesPendingRequests(history());
  ASSERT_TRUE(Visits(Url("/data/kettle.html")).success);

  // The dialog opens in the top overlay with this profile picked.
  CluBrowsingDataHandler data(window().GetWeakPtr());
  CallFuture opened;
  data.Open(mojom::BrowsingDataOpenInput::New(std::nullopt), opened.GetCallback());
  ASSERT_TRUE(IsOk(opened.Take()));
  window().Flush();
  const base::DictValue state = State();
  const base::ListValue* surfaces = state.FindDict("windows")
                                         ->FindDict(window().window_id())
                                         ->FindDict("overlay")
                                         ->FindList("surfaces");
  bool dialog = false;
  for (const base::Value& s : *surfaces) {
    dialog |= *s.GetDict().FindString("surface") == "clear-data" &&
              *s.GetDict().FindStringByDottedPath("props.profileId") ==
                  window().state_profile_id();
  }
  EXPECT_TRUE(dialog);

  // Cookies only, last hour: history and the archive stay.
  CallFuture cookies;
  data.Clear(mojom::BrowsingDataClearInput::New(
                 window().state_profile_id(), mojom::BrowsingDataClearInputRange::kHour,
                 mojom::BrowsingDataClearInputTypes::New(false, true, false, false,
                                                         false, false)),
             cookies.GetCallback());
  ASSERT_TRUE(IsOk(cookies.Take()));
  ASSERT_TRUE(content::NavigateToURL(Contents(jar), Url("/data/cookie.html")));
  EXPECT_EQ(content::EvalJs(Contents(jar), "document.cookie"), "");
  EXPECT_TRUE(Visits(Url("/data/kettle.html")).success);
  EXPECT_EQ(Archive().rows.size(), 1u);

  // History and archived tabs, all time.
  CallFuture rest;
  data.Clear(mojom::BrowsingDataClearInput::New(
                 window().state_profile_id(), mojom::BrowsingDataClearInputRange::kAll,
                 mojom::BrowsingDataClearInputTypes::New(true, false, false, false,
                                                         false, true)),
             rest.GetCallback());
  ASSERT_TRUE(IsOk(rest.Take()));
  history::BlockUntilHistoryProcessesPendingRequests(history());
  EXPECT_FALSE(Visits(Url("/data/kettle.html")).success);
  EXPECT_TRUE(Archive().rows.empty());
  window().Flush();
  bool toast = false;
  for (const base::Value& t : *State()
                                   .FindDict("windows")
                                   ->FindDict(window().window_id())
                                   ->FindList("toasts")) {
    toast |= *t.GetDict().FindString("text") == "Browsing data cleared";
  }
  EXPECT_TRUE(toast);

  // A profile Clu does not know answers not-found and clears nothing.
  CallFuture unknown;
  data.Clear(mojom::BrowsingDataClearInput::New(
                 "00000000-0000-4000-8000-00000000dead",
                 mojom::BrowsingDataClearInputRange::kAll,
                 mojom::BrowsingDataClearInputTypes::New(true, true, true, true, true,
                                                         true)),
             unknown.GetCallback());
  EXPECT_EQ(Code(unknown.Take()), mojom::ErrorCode::kNotFound);
}

// 12.13 box 5: Chrome and sidebar-file import ------------------------------------------------------------------------

class CluImportBrowserTest : public CluDataBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    CluDataBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    ASSERT_TRUE(CopyImportFixture(home_.GetPath(),
                                  base::Time::Now().InMillisecondsSinceUnixEpoch()));
  }

  void TearDownOnMainThread() override {
    CluDataBrowserTest::TearDownOnMainThread();
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(home_.Delete());
  }

  CluImportService& import() { return *CluImportService::Get(); }

  // Bytes and mtime of every source file: the import only reads them.
  std::vector<std::pair<std::string, base::Time>> Sources() {
    base::ScopedAllowBlockingForTesting allow;
    std::vector<std::pair<std::string, base::Time>> out;
    for (const base::FilePath& file :
         {ChromeDataDir(home_.GetPath()).Append("Profile 1").Append("Bookmarks"),
          ChromeDataDir(home_.GetPath()).Append("Profile 1").Append("History"),
          SidebarFilePath(home_.GetPath()),
          SidebarUserDataDir(home_.GetPath()).Append("Default").Append("History"),
          SidebarUserDataDir(home_.GetPath()).Append("Profile 1").Append("History")}) {
      std::string bytes;
      base::File::Info info;
      EXPECT_TRUE(base::ReadFileToString(file, &bytes));
      EXPECT_TRUE(base::GetFileInfo(file, &info));
      out.emplace_back(bytes, info.last_modified);
    }
    return out;
  }

  // The window on screen: a sidebar import that lands on another profile's space closes the window it started in.
  CluWindowState* ShownWindow() {
    for (CluWindowState* w : space_actions::AllWindows()) {
      if (!w->hidden_by_switch() && !w->browser()->IsDeleteScheduled()) {
        return w;
      }
    }
    return nullptr;
  }

  // Windows still open, on screen or hidden.
  int OpenWindows() {
    int open = 0;
    for (CluWindowState* w : space_actions::AllWindows()) {
      open += w->browser()->IsDeleteScheduled() ? 0 : 1;
    }
    return open;
  }

  base::Value Run(mojom::ImportRunInputPtr input) {
    CluWindowState* shown = ShownWindow();
    EXPECT_TRUE(shown);
    if (!shown) {
      return base::Value();
    }
    CluImportHandler handler(shown->GetWeakPtr());
    CallFuture result;
    handler.Run(std::move(input), result.GetCallback());
    const mojom::CallResultPtr r = result.Take();
    EXPECT_TRUE(IsOk(r));
    return IsOk(r) ? Json(r) : base::Value();
  }

  // The sidebar import with the keychain items as given. Without `extras` it leaves out the passwords and cookies
  // (bookmarks, the search engine, the extension list), so the older tests count what they always did.
  static mojom::ImportRunInputArcPtr ArcInput(bool passwords, bool cookies, bool extras = false) {
    auto arc = mojom::ImportRunInputArc::New();
    arc->source = "arc";
    arc->passwords = passwords;
    arc->cookies = cookies;
    if (!extras) {
      arc->bookmarks = false;
      arc->search_engine = false;
      arc->extensions = false;
    }
    return arc;
  }

  // Without `signins` it leaves out the passwords and cookies, so the older tests count what they always did.
  // Without `extras` it leaves out the saved addresses and the default search engine.
  mojom::ImportRunInputPtr Chrome(bool signins = false, bool extras = true) {
    return mojom::ImportRunInput::NewChrome(mojom::ImportRunInputChrome::New(
        "chrome", "Profile 1", /*bookmarks=*/true, /*history=*/true,
        /*passwords=*/signins, /*cookies=*/signins, /*autofill=*/extras,
        /*search_engine=*/extras, space()));
  }

  base::ScopedTempDir home_;
};

IN_PROC_BROWSER_TEST_F(CluImportBrowserTest,
                       CluImportReadsNothingWithoutAFixtureRoot) {
  // Under --test-type there is no import root until a test sets one: real browser data is never read.
  CluImportHandler handler(window().GetWeakPtr());
  CallFuture detected;
  handler.Detect(detected.GetCallback());
  const base::Value none = Json(detected.Take());
  EXPECT_TRUE(none.GetDict().FindListByDottedPath("chrome.profiles")->empty());
  EXPECT_EQ(none.GetDict().FindBoolByDottedPath("arc.found"), false);
  CallFuture refused;
  handler.Run(Chrome(), refused.GetCallback());
  EXPECT_EQ(Code(refused.Take()), mojom::ErrorCode::kNotFound);

  import().SetRootForTesting(home_.GetPath());
  CallFuture found;
  handler.Detect(found.GetCallback());
  const base::Value detection = Json(found.Take());
  const base::ListValue* profiles =
      detection.GetDict().FindListByDottedPath("chrome.profiles");
  ASSERT_EQ(profiles->size(), 2u);
  EXPECT_EQ(*(*profiles)[0].GetDict().FindString("name"), "Personal");
  EXPECT_EQ(*(*profiles)[1].GetDict().FindString("dir"), "Profile 1");
  EXPECT_EQ((*profiles)[1].GetDict().FindBool("hasHistory"), true);
  EXPECT_EQ(detection.GetDict().FindBoolByDottedPath("arc.found"), true);
  EXPECT_EQ(*detection.GetDict().FindString("currentSpaceId"), space());
}

IN_PROC_BROWSER_TEST_F(CluImportBrowserTest,
                       CluImportChromeBookmarksAndHistoryOnceAndSourcesUntouched) {
  import().SetRootForTesting(home_.GetPath());
  const auto before = Sources();
  const base::Value first = Run(Chrome());
  EXPECT_EQ(first.GetDict().FindInt("bookmarks"), 13);
  EXPECT_EQ(first.GetDict().FindInt("history"), 4);
  EXPECT_EQ(first.GetDict().FindInt("skipped"), 2);
  EXPECT_TRUE(first.GetDict().Find("error")->is_none());

  // "Imported from Chrome" at the end of the space's pinned root, the tree below it.
  const CluTabTree& tree = model().tree();
  ASSERT_FALSE(tree.Pinned(space()).empty());
  const TabNode* root = tree.Get(tree.Pinned(space()).back());
  ASSERT_TRUE(root && root->is_folder());
  EXPECT_EQ(root->title_override, "Imported from Google Chrome");
  ASSERT_EQ(tree.Children(root->id).size(), 2u);
  const TabNode* bar = tree.Get(tree.Children(root->id)[0]);
  EXPECT_EQ(bar->title_override, "Bookmarks bar");
  const TabNode* harbor = tree.Get(tree.Children(bar->id)[0]);
  EXPECT_EQ(harbor->pinned_url, "https://harbor.example/");
  // History is the profile's own HistoryService now.
  history::BlockUntilHistoryProcessesPendingRequests(history());
  EXPECT_TRUE(Visits(GURL("https://harbor.example/")).success);
  EXPECT_FALSE(Visits(GURL("https://old.example/")).success);

  // Again: nothing new.
  const size_t rows = tree.size();
  const base::Value second = Run(Chrome());
  EXPECT_EQ(second.GetDict().FindInt("bookmarks"), 0);
  EXPECT_EQ(second.GetDict().FindInt("history"), 0);
  EXPECT_EQ(tree.size(), rows);
  EXPECT_EQ(Sources(), before);
  // The History copy was made in Rio's own data folder, not the system temp folder, and is gone.
  base::FilePath user_data;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data));
  {
    base::ScopedAllowBlockingForTesting allow;
    const base::FilePath temp = user_data.Append(kImportTempDirName);
    EXPECT_TRUE(base::DirectoryExists(temp));
    EXPECT_TRUE(base::IsDirectoryEmpty(temp));
  }
  FlushStore();
}

IN_PROC_BROWSER_TEST_F(CluImportBrowserTest,
                       CluImportSidebarFileMakesAProfilePerSourceProfileWithThemes) {
  import().SetRootForTesting(home_.GetPath());
  CluSpacesService* spaces = CluSpacesService::Get();
  const size_t profiles_before = spaces->list().profiles().size();
  const size_t spaces_before = spaces->list().spaces().size();
  const auto before = Sources();

  const base::Value first =
      Run(mojom::ImportRunInput::NewArc(ArcInput(false, false)));
  const base::DictValue& r = first.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("profiles"), 2);
  EXPECT_EQ(r.FindInt("spaces"), 4);
  EXPECT_EQ(r.FindInt("pinned"), 10);
  EXPECT_EQ(r.FindInt("favorites"), 3);
  EXPECT_EQ(r.FindIntByDottedPath("skipped.duplicates"), 1);
  // Today tabs come along, and each source profile's history (fixture History files under the import root).
  EXPECT_EQ(r.FindInt("today"), 3);
  EXPECT_EQ(r.FindInt("history"), 3);
  EXPECT_EQ(r.FindIntByDottedPath("skipped.historyFiles"), 0);
  ASSERT_EQ(spaces->list().profiles().size(), profiles_before + 2);
  ASSERT_EQ(spaces->list().spaces().size(), spaces_before + 4);

  // Each source profile is a Chromium profile of its own; the spaces keep their names, emoji and themes.
  const std::vector<SpaceEntry>& list = spaces->list().spaces();
  const SpaceEntry& work = list[spaces_before];
  const SpaceEntry& side = list[spaces_before + 2];
  EXPECT_EQ(work.name, "Work");
  EXPECT_EQ(work.icon_value, "\U0001F4BC");
  EXPECT_EQ(side.name, "Side project");
  EXPECT_NE(work.profile_id, side.profile_id);
  EXPECT_NE(work.profile_id, model().profile_id());
  const std::optional<SpaceTheme> work_theme = CluAppSettings::Get()->ShownTheme(work.id);
  ASSERT_TRUE(work_theme);
  EXPECT_EQ(work_theme->colors.size(), 2u);
  EXPECT_DOUBLE_EQ(work_theme->intensity, 0.6);
  EXPECT_FALSE(CluAppSettings::Get()->ShownTheme(list[spaces_before + 3].id));

  // The rows live in each profile's own model.
  CluModelService* work_model = spaces->LoadedModel(work.profile_id);
  ASSERT_TRUE(work_model);
  EXPECT_EQ(work_model->tree().Favorites().size(), 2u);
  const std::vector<std::string>& pinned = work_model->tree().Pinned(work.id);
  ASSERT_EQ(pinned.size(), 2u);
  EXPECT_EQ(work_model->tree().Get(pinned[0])->title_override, "Inbox");
  EXPECT_TRUE(work_model->tree().Get(pinned[1])->collapsed);
  const std::vector<std::string>& today = work_model->tree().Today(work.id);
  ASSERT_EQ(today.size(), 2u);
  EXPECT_EQ(work_model->tree().Get(today[0])->url, "https://today.example/1");
  EXPECT_EQ(work_model->tree().Get(today[1])->title, "Today 2");
  CluModelService* side_model = spaces->LoadedModel(side.profile_id);
  ASSERT_TRUE(side_model);
  EXPECT_EQ(side_model->tree().Today(list[spaces_before + 3].id).size(), 1u);
  // The default source profile's visits are in the Rio profile it became, and only there.
  history::HistoryService* work_history = HistoryServiceFactory::GetForProfile(
      spaces->LoadedProfile(work.profile_id), ServiceAccessType::EXPLICIT_ACCESS);
  history::HistoryService* side_history = HistoryServiceFactory::GetForProfile(
      spaces->LoadedProfile(side.profile_id), ServiceAccessType::EXPLICIT_ACCESS);
  history::BlockUntilHistoryProcessesPendingRequests(work_history);
  history::BlockUntilHistoryProcessesPendingRequests(side_history);
  EXPECT_TRUE(Visits(work_history, GURL("https://lantern.example/")).success);
  EXPECT_TRUE(Visits(work_history, GURL("https://orchard.example/plans")).success);
  EXPECT_FALSE(Visits(work_history, GURL("https://old.example/")).success);
  EXPECT_FALSE(Visits(work_history, GURL("https://workbench.example/")).success);
  EXPECT_TRUE(Visits(side_history, GURL("https://workbench.example/")).success);
  EXPECT_FALSE(Visits(history(), GURL("https://lantern.example/")).success);

  // Again: nothing new anywhere.
  const base::Value second =
      Run(mojom::ImportRunInput::NewArc(ArcInput(false, false)));
  EXPECT_EQ(second.GetDict().FindInt("profiles"), 0);
  EXPECT_EQ(second.GetDict().FindInt("spaces"), 0);
  EXPECT_EQ(second.GetDict().FindInt("pinned"), 0);
  EXPECT_EQ(second.GetDict().FindInt("favorites"), 0);
  EXPECT_EQ(second.GetDict().FindInt("today"), 0);
  EXPECT_EQ(second.GetDict().FindInt("history"), 0);
  EXPECT_EQ(work_model->tree().Today(work.id).size(), 2u);
  EXPECT_EQ(spaces->list().profiles().size(), profiles_before + 2);
  EXPECT_EQ(spaces->list().spaces().size(), spaces_before + 4);
  EXPECT_EQ(Sources(), before);
}

// A file too big for Rio writes nothing, also when imported spaces were moved to another profile since the last
// import: the cap check counts them as new, as the import then makes them again.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest,
                       CluImportCountsMovedSpacesTowardTheCap) {
  import().SetRootForTesting(home_.GetPath());
  CluSpacesService* spaces = CluSpacesService::Get();
  const size_t spaces_before = spaces->list().spaces().size();
  ASSERT_TRUE(Run(mojom::ImportRunInput::NewArc(ArcInput(false, false)))
                  .GetDict()
                  .Find("error")
                  ->is_none());
  // Work (first source profile) and Side project (second) move to this window's profile.
  const std::string work = spaces->list().spaces()[spaces_before].id;
  const std::string side = spaces->list().spaces()[spaces_before + 2].id;
  ASSERT_TRUE(spaces->SetSpaceProfile(work, model().profile_id()).has_value());
  ASSERT_TRUE(spaces->SetSpaceProfile(side, model().profile_id()).has_value());
  // Room for one more space, and the import would make two.
  while (spaces->list().spaces().size() < kMaxSpaces - 1) {
    ASSERT_TRUE(spaces
                    ->CreateSpace(std::nullopt, std::nullopt, std::nullopt,
                                  model().profile_id())
                    .has_value());
  }
  const base::Value again =
      Run(mojom::ImportRunInput::NewArc(ArcInput(false, false)));
  const std::string* error = again.GetDict().FindString("error");
  ASSERT_TRUE(error);
  EXPECT_NE(error->find("many spaces"), std::string::npos);
  EXPECT_EQ(again.GetDict().FindInt("spaces"), 0);
  EXPECT_EQ(spaces->list().spaces().size(), kMaxSpaces - 1);
}

// Saved passwords and cookies come over with the keychain secret (a fake one, the fixture's), into the
// Rio profile the source profile became, and the source files stay as they were.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest,
                       CluImportSidebarBringsPasswordsAndCookiesWithTheSafeStorageKey) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::FilePath source = SidebarUserDataDir(home_.GetPath()).Append("Default");
  std::string login_data_before, cookies_before;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::ReadFileToString(source.Append("Login Data"), &login_data_before));
    ASSERT_TRUE(base::ReadFileToString(source.Append("Cookies"), &cookies_before));
  }
  const base::Value result =
      Run(mojom::ImportRunInput::NewArc(ArcInput(true, true)));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  // Two logins and one site the user blocked; one login sealed with another key and one junk origin are skipped.
  EXPECT_EQ(r.FindInt("passwords"), 3);
  // Two live cookies; the expired one is left out, the one sealed with another key skipped.
  EXPECT_EQ(r.FindInt("cookies"), 2);
  EXPECT_EQ(r.FindBool("secretsDenied"), false);
  EXPECT_EQ(r.FindInt("secretsSkipped"), 3);
  // The sidebar import itself is unchanged.
  EXPECT_EQ(r.FindInt("spaces"), 4);
  EXPECT_EQ(r.FindInt("history"), 3);
  {
    base::ScopedAllowBlockingForTesting allow;
    std::string login_data_after, cookies_after;
    ASSERT_TRUE(base::ReadFileToString(source.Append("Login Data"), &login_data_after));
    ASSERT_TRUE(base::ReadFileToString(source.Append("Cookies"), &cookies_after));
    EXPECT_EQ(login_data_after, login_data_before);
    EXPECT_EQ(cookies_after, cookies_before);
    // The copies are gone.
    base::FilePath user_data;
    ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data));
    base::FileEnumerator left(user_data.Append(kImportTempDirName), false, base::FileEnumerator::DIRECTORIES);
    EXPECT_TRUE(left.Next().empty());
  }
  // The cookies are in the jar of the Rio profile the source profile with the Cookies file became (one of the two
  // the import made; the other has none).
  CluSpacesService* spaces = CluSpacesService::Get();
  const std::vector<ProfileEntry>& profiles = spaces->list().profiles();
  ASSERT_GE(profiles.size(), 2u);
  std::vector<std::string> values;
  for (size_t i = profiles.size() - 2; i < profiles.size(); ++i) {
    base::test::TestFuture<Profile*> loaded;
    spaces->LoadProfile(profiles[i].id, loaded.GetCallback());
    ASSERT_TRUE(loaded.Get());
    base::test::TestFuture<const net::CookieList&> all;
    loaded.Get()->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetAllCookies(
        all.GetCallback());
    for (const net::CanonicalCookie& c : all.Get()) {
      values.push_back(c.Value());
    }
  }
  EXPECT_THAT(values, testing::UnorderedElementsAre("fixture-cookie-1", "fixture-cookie-2"));
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// The fixture's never-save row is a legacy one that still carries a username and password. It comes over as a
// never-save form with neither, as PasswordStore::AddLogins CHECKs, so the site stays never-save and Rio lives.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportNeverSaveLoginComesOverWithoutUsernameOrPassword) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::Value result = Run(mojom::ImportRunInput::NewArc(ArcInput(true, false)));
  ASSERT_TRUE(result.GetDict().Find("error")->is_none());
  EXPECT_EQ(result.GetDict().FindInt("passwords"), 3);
  CluSpacesService* spaces = CluSpacesService::Get();
  const std::vector<ProfileEntry>& profiles = spaces->list().profiles();
  ASSERT_GE(profiles.size(), 2u);
  std::vector<password_manager::StoredCredential> forms;
  for (size_t i = profiles.size() - 2; i < profiles.size(); ++i) {
    base::test::TestFuture<Profile*> loaded;
    spaces->LoadProfile(profiles[i].id, loaded.GetCallback());
    ASSERT_TRUE(loaded.Get());
    scoped_refptr<password_manager::PasswordStoreInterface> store =
        ProfilePasswordStoreFactory::GetForProfile(loaded.Get(), ServiceAccessType::EXPLICIT_ACCESS);
    ASSERT_TRUE(store);
    password_manager::PasswordStoreResultsObserver observer;
    store->GetAllLogins(observer.GetWeakPtr());
    for (password_manager::StoredCredential& form : observer.WaitForResults()) {
      forms.push_back(std::move(form));
    }
  }
  int never_save = 0;
  for (const password_manager::StoredCredential& form : forms) {
    if (form.signon_realm != "https://never.example/") {
      continue;
    }
    ++never_save;
    EXPECT_TRUE(form.blocked_by_user);
    EXPECT_TRUE(form.username_value.empty());
    EXPECT_TRUE(form.password_value.empty());
  }
  EXPECT_EQ(never_save, 1);
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// The Chrome import brings the saved passwords and cookies of the Chrome profile, decrypted with the key
// from "Chrome Safe Storage" (here the fixture's fake secret), into the Rio profile of the space it imports into, and
// Chrome's files stay as they were.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest,
                       CluImportChromeBringsPasswordsAndCookiesWithTheSafeStorageKey) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::FilePath source = ChromeDataDir(home_.GetPath()).Append("Profile 1");
  std::string login_data_before, cookies_before;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::ReadFileToString(source.Append("Login Data"), &login_data_before));
    ASSERT_TRUE(base::ReadFileToString(source.Append("Cookies"), &cookies_before));
  }
  const base::Value result = Run(Chrome(/*signins=*/true));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("bookmarks"), 13);
  EXPECT_EQ(r.FindInt("history"), 4);
  // Two logins and one site the user blocked; two live cookies.
  EXPECT_EQ(r.FindInt("passwords"), 3);
  EXPECT_EQ(r.FindInt("cookies"), 2);
  EXPECT_EQ(r.FindBool("secretsDenied"), false);
  // The two bookmarks skips, plus a login and a cookie sealed with another key and a junk origin.
  EXPECT_EQ(r.FindInt("skipped"), 5);
  {
    base::ScopedAllowBlockingForTesting allow;
    std::string login_data_after, cookies_after;
    ASSERT_TRUE(base::ReadFileToString(source.Append("Login Data"), &login_data_after));
    ASSERT_TRUE(base::ReadFileToString(source.Append("Cookies"), &cookies_after));
    EXPECT_EQ(login_data_after, login_data_before);
    EXPECT_EQ(cookies_after, cookies_before);
    base::FilePath user_data;
    ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data));
    base::FileEnumerator left(user_data.Append(kImportTempDirName), false, base::FileEnumerator::DIRECTORIES);
    EXPECT_TRUE(left.Next().empty());
  }
  // The fixture's cookies are in this window's profile, the profile of the space the import went into.
  base::test::TestFuture<const net::CookieList&> all;
  browser()->GetProfile()->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetAllCookies(
      all.GetCallback());
  std::vector<std::string> values;
  for (const net::CanonicalCookie& c : all.Get()) {
    values.push_back(c.Domain() + "|" + c.Name() + "|" + c.Value());
  }
  EXPECT_THAT(values, testing::IsSupersetOf({".lantern.example|session|fixture-cookie-1",
                                             "orchard.example|sid|fixture-cookie-2"}));
  EXPECT_THAT(values, testing::Not(testing::Contains(testing::HasSubstr("not-ours"))));
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// Passwords saved to the Google account sit in Login Data For Account: with only that store they still come over.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeBringsTheAccountPasswordStore) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::FilePath source = ChromeDataDir(home_.GetPath()).Append("Profile 1");
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::Move(source.Append("Login Data"), source.Append("Login Data For Account")));
  }
  const base::Value result = Run(Chrome(/*signins=*/true));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  // Two logins and one site the user blocked, all from the account store.
  EXPECT_EQ(r.FindInt("passwords"), 3);
  EXPECT_EQ(r.FindInt("cookies"), 2);
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// A login in both of Chrome's password stores comes over once, and the result counts it once.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeCountsALoginInBothStoresOnce) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::FilePath source = ChromeDataDir(home_.GetPath()).Append("Profile 1");
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::CopyFile(source.Append("Login Data"), source.Append("Login Data For Account")));
  }
  const base::Value result = Run(Chrome(/*signins=*/true));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("passwords"), 3);
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// A partitioned cookie from Chrome arrives in its partition, never unpartitioned; a partition that doesn't parse makes
// the row junk, counted as skipped.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeKeepsAPartitionedCookiePartitioned) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::FilePath source = ChromeDataDir(home_.GetPath()).Append("Profile 1");
  {
    base::ScopedAllowBlockingForTesting allow;
    const int64_t now = base::Time::Now().InMillisecondsSinceUnixEpoch();
    ASSERT_TRUE(AddPartitionedCookie(source, "embed.example", "chip", "https://top.example", now));
    ASSERT_TRUE(AddPartitionedCookie(source, "embed.example", "chip", "not a site", now));
  }
  const base::Value result = Run(Chrome(/*signins=*/true));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("cookies"), 3);
  // The five skips of the plain import, plus the row whose partition doesn't parse.
  EXPECT_EQ(r.FindInt("skipped"), 6);
  base::test::TestFuture<const net::CookieList&> all;
  browser()->GetProfile()->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetAllCookies(
      all.GetCallback());
  int chips = 0;
  for (const net::CanonicalCookie& c : all.Get()) {
    if (c.Name() != "chip") {
      continue;
    }
    ++chips;
    EXPECT_EQ(c.Domain(), "embed.example");
    ASSERT_TRUE(c.IsPartitioned());
    EXPECT_EQ(c.PartitionKey()->site(), net::SchemefulSite(GURL("https://top.example")));
  }
  EXPECT_EQ(chips, 1);
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// A cookie manager whose pipe went away with every write pending (the network service restarting): no reply ever runs.
class DroppingCookieManager : public network::TestCookieManager {
 public:
  void SetCanonicalCookie(const net::CanonicalCookie& cookie,
                          const GURL& source_url,
                          const net::CookieOptions& cookie_options,
                          SetCanonicalCookieCallback callback) override {
    ++dropped;
  }
  int dropped = 0;
};

// Dropped cookie replies count as refused: the import still finishes, and the next one is not refused as busy.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeFinishesWhenTheCookieRepliesAreDropped) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  DroppingCookieManager cookies;
  CluImportService::SetCookieManagerForTesting(&cookies);
  const base::Value result = Run(Chrome(/*signins=*/true));
  CluImportService::SetCookieManagerForTesting(nullptr);
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(cookies.dropped, 2);
  EXPECT_EQ(r.FindInt("cookies"), 0);
  EXPECT_EQ(r.FindInt("passwords"), 3);
  EXPECT_FALSE(import().running());
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// A denied keychain prompt brings the bookmarks and history and says the passwords and sign-ins were skipped.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeWithoutTheKeychainKeySkipsTheSignins) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::nullopt);
  const base::Value result = Run(Chrome(/*signins=*/true));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("bookmarks"), 13);
  EXPECT_EQ(r.FindInt("history"), 4);
  EXPECT_EQ(r.FindInt("passwords"), 0);
  EXPECT_EQ(r.FindInt("cookies"), 0);
  EXPECT_EQ(r.FindBool("secretsDenied"), true);
  base::test::TestFuture<const net::CookieList&> all;
  browser()->GetProfile()->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetAllCookies(
      all.GetCallback());
  for (const net::CanonicalCookie& c : all.Get()) {
    EXPECT_NE(c.Value(), "fixture-cookie-1");
  }
}

// Each source profile's bookmarks land as a pinned "Imported bookmarks" folder in its first space, its
// default search engine becomes the Rio profile's default, and its Web Store extensions are listed (component
// extensions and junk ids are not); the files are only read.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportSidebarBringsBookmarksSearchEngineAndExtensions) {
  import().SetRootForTesting(home_.GetPath());
  const auto before = Sources();
  const base::Value result =
      Run(mojom::ImportRunInput::NewArc(ArcInput(false, false, /*extras=*/true)));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("spaces"), 4);
  EXPECT_EQ(r.FindInt("pinned"), 10);
  EXPECT_EQ(r.FindInt("bookmarks"), 3);
  ASSERT_TRUE(r.FindString("searchEngine"));
  EXPECT_EQ(*r.FindString("searchEngine"), "Kagi");
  std::vector<std::string> names;
  for (const base::Value& e : *r.FindList("extensions")) {
    names.push_back(*e.GetDict().FindString("name") + "|" + *e.GetDict().FindString("id"));
  }
  EXPECT_THAT(names, testing::UnorderedElementsAre("Fixture Reader|aaaabbbbccccddddeeeeffffgggghhhh",
                                                   "Fixture Notes|bbbbccccddddeeeeffffgggghhhhiiii"));
  EXPECT_EQ(Sources(), before);
  // The browser keeps the list for Reinstall, each with the imported Rio profile it installs into.
  for (const char* id : {"aaaabbbbccccddddeeeeffffgggghhhh", "bbbbccccddddeeeeffffgggghhhhiiii"}) {
    const SidebarExtension* listed = import().FindImportedExtension(id);
    ASSERT_TRUE(listed);
    ASSERT_TRUE(CluSpacesService::Get()->list().FindProfile(listed->profile_id));
    EXPECT_NE(listed->profile_id,
              CluSpacesService::Get()->ProfileIdOf(browser()->GetProfile()));
  }
  EXPECT_FALSE(import().FindImportedExtension("ccccddddeeeeffffgggghhhhiiiijjjj"));

  // The bookmarks folder is pinned in one of the imported profiles, and that profile searches with Kagi.
  CluSpacesService* spaces = CluSpacesService::Get();
  const std::vector<ProfileEntry>& profiles = spaces->list().profiles();
  bool found_folder = false;
  bool found_engine = false;
  for (const ProfileEntry& entry : profiles) {
    base::test::TestFuture<Profile*> loaded;
    spaces->LoadProfile(entry.id, loaded.GetCallback());
    ASSERT_TRUE(loaded.Get());
    if (CluModelService* model = CluModelService::Get(loaded.Get())) {
      for (const TabNode* node : model->tree().All()) {
        found_folder |= node->is_folder() &&
                        node->title_override.value_or(node->title) == "Imported bookmarks";
      }
    }
    TemplateURLService* urls = TemplateURLServiceFactory::GetForProfile(loaded.Get());
    found_engine |= urls && urls->loaded() && urls->GetDefaultSearchProvider() &&
                    urls->GetDefaultSearchProvider()->short_name() == u"Kagi";
  }
  EXPECT_TRUE(found_folder);
  EXPECT_TRUE(found_engine);
}

// An unticked box leaves its item behind; spaces and profiles always come over.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportSidebarLeavesUntickedItemsBehind) {
  import().SetRootForTesting(home_.GetPath());
  mojom::ImportRunInputArcPtr arc = ArcInput(false, false);
  arc->pinned = false;
  arc->today = false;
  arc->favorites = false;
  arc->history = false;
  const base::Value result = Run(mojom::ImportRunInput::NewArc(std::move(arc)));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("spaces"), 4);
  EXPECT_EQ(r.FindInt("pinned"), 0);
  EXPECT_EQ(r.FindInt("today"), 0);
  EXPECT_EQ(r.FindInt("favorites"), 0);
  EXPECT_EQ(r.FindInt("history"), 0);
  EXPECT_EQ(r.FindInt("bookmarks"), 0);
  EXPECT_TRUE(r.Find("searchEngine")->is_none());
}

// On the anonymized copy of a real sidebar file (clu/test/data/import-arc): a broken earlier run left
// "Imported" and "Imported 2"; the import asks (arc.previous), and with replace they go. Only the 4 profiles the
// spaces use come over, named as the source names them, with exactly the source's tabs, none of them loaded, and
// the window shows the main space. Running it again replaces that import: still 4 profiles.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportArcCopyReplacesThePreviousImport) {
  base::ScopedTempDir arc_home;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(arc_home.CreateUniqueTempDir());
    ASSERT_TRUE(base::CopyDirectory(
        ImportFixtureRoot().DirName().AppendASCII("import-arc").Append("Library"),
        arc_home.GetPath(), /*recursive=*/true));
  }
  import().SetRootForTesting(arc_home.GetPath());
  CluSpacesService* spaces = CluSpacesService::Get();
  for (const char* name : {"Imported", "Imported 2"}) {
    base::test::TestFuture<base::expected<std::string, SpacesError>> made;
    spaces->CreateProfile(std::string(name), made.GetCallback());
    ASSERT_TRUE(made.Get().has_value());
    ASSERT_TRUE(spaces->CreateSpace(std::string(name), std::nullopt, std::nullopt,
                                    made.Get().value())
                    .has_value());
  }
  const size_t profiles_before = spaces->list().profiles().size() - 2;
  const size_t spaces_before = spaces->list().spaces().size() - 2;
  auto named = [&](std::string_view name) {
    return std::count_if(spaces->list().profiles().begin(),
                         spaces->list().profiles().end(),
                         [&](const ProfileEntry& p) { return p.name == name; });
  };
  auto broken = [&] {
    return std::count_if(spaces->list().profiles().begin(),
                         spaces->list().profiles().end(),
                         [](const ProfileEntry& p) {
                           return p.name == "Imported" || p.name == "Imported 2";
                         });
  };

  {
    CluImportHandler handler(window().GetWeakPtr());
    CallFuture detected;
    handler.Detect(detected.GetCallback());
    const mojom::CallResultPtr r = detected.Take();
    ASSERT_TRUE(IsOk(r));
    EXPECT_EQ(Json(r).GetDict().FindIntByDottedPath("arc.previous"), 2);
  }

  for (int run = 0; run < 2; ++run) {
    mojom::ImportRunInputArcPtr arc = ArcInput(false, false);
    arc->replace = true;
    const base::Value result = Run(mojom::ImportRunInput::NewArc(std::move(arc)));
    const base::DictValue& r = result.GetDict();
    EXPECT_TRUE(r.Find("error")->is_none());
    EXPECT_EQ(r.FindInt("profiles"), 4);
    EXPECT_EQ(r.FindInt("spaces"), 4);
    EXPECT_EQ(r.FindInt("pinned"), 38);
    EXPECT_EQ(r.FindInt("favorites"), 29);
    EXPECT_EQ(r.FindInt("today"), 0);
    // The replaced profiles leave once their windows closed.
    ASSERT_TRUE(base::test::RunUntil([&] {
      return spaces->list().profiles().size() == profiles_before + 4;
    })) << "run " << run;
    EXPECT_EQ(broken(), 0);
    for (const char* name : {"Personal", "Work", "Applying", "Client"}) {
      EXPECT_EQ(named(name), 1) << name;
    }
    EXPECT_EQ(spaces->list().spaces().size(), spaces_before + 4);

    // Each space on its own profile, with the source's names and counts.
    const std::vector<SpaceEntry>& list = spaces->list().spaces();
    const SpaceEntry& main_space = list[spaces_before];
    EXPECT_EQ(main_space.name, "Main");
    EXPECT_EQ(list[spaces_before + 1].name, "work.example");
    EXPECT_EQ(list[spaces_before + 1].icon_value, "\U0001F9FC");
    EXPECT_EQ(list[spaces_before + 2].name, "side.example");
    EXPECT_EQ(list[spaces_before + 3].name, "client.example");
    std::set<std::string> profile_ids;
    for (size_t i = 0; i < 4; ++i) {
      profile_ids.insert(list[spaces_before + i].profile_id);
    }
    EXPECT_EQ(profile_ids.size(), 4u);
    CluModelService* personal = spaces->LoadedModel(main_space.profile_id);
    ASSERT_TRUE(personal);
    EXPECT_EQ(personal->tree().Favorites().size(), 6u);
    // Folder 1's split view of 2 tabs is a split of those 2 rows, side by side, in Main's space.
    ASSERT_EQ(personal->splits().size(), 1u) << "run " << run;
    const SplitRow& split = personal->splits().begin()->second;
    EXPECT_EQ(split.space_id, main_space.id);
    EXPECT_EQ(split.dir, SplitDir::kRow);
    ASSERT_EQ(split.tab_ids.size(), 2u);
    const TabNode* left = personal->tree().Get(split.tab_ids[0]);
    const TabNode* right = personal->tree().Get(split.tab_ids[1]);
    ASSERT_TRUE(left && right);
    EXPECT_EQ(left->url, "https://example.com/3");
    EXPECT_FALSE(left->parent_id.empty());
    EXPECT_EQ(left->parent_id, right->parent_id);

    // No imported tab is loaded, and one window is open and shows the main space: the window the import started in
    // closed rather than hid.
    ASSERT_TRUE(base::test::RunUntil([&] {
      CluWindowState* shown = ShownWindow();
      return OpenWindows() == 1 && shown && shown->space_id() == main_space.id;
    })) << "run " << run;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [](BrowserWindowInterface* browser) {
          TabStripModel* strip = browser->GetTabStripModel();
          for (int i = 0; strip && i < strip->count(); ++i) {
            EXPECT_NE(strip->GetWebContentsAt(i)->GetVisibleURL().host(),
                      "example.com");
          }
          return true;
        });
  }
  import().SetRootForTesting(std::nullopt);
}

// During first-run setup the window keeps the setup screen for the progress and the summary (no window
// opens or hides); finishing setup then shows the main space in one window, with no hidden one left.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportArcCopyDuringSetupMovesTheWindowWhenSetupEnds) {
  base::ScopedTempDir arc_home;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(arc_home.CreateUniqueTempDir());
    ASSERT_TRUE(base::CopyDirectory(
        ImportFixtureRoot().DirName().AppendASCII("import-arc").Append("Library"),
        arc_home.GetPath(), /*recursive=*/true));
  }
  import().SetRootForTesting(arc_home.GetPath());
  CluAppSettings* settings = CluAppSettings::Get();
  ASSERT_TRUE(settings);
  settings->SetFirstRunForTesting(true);
  ASSERT_TRUE(settings->onboarding_show());
  const std::string before = space();

  const base::Value result =
      Run(mojom::ImportRunInput::NewArc(ArcInput(false, false)));
  EXPECT_EQ(result.GetDict().FindInt("spaces"), 4);
  EXPECT_EQ(OpenWindows(), 1);
  EXPECT_EQ(ShownWindow(), &window());
  EXPECT_EQ(space(), before);

  CluSpacesService* spaces = CluSpacesService::Get();
  const auto main_entry = std::find_if(
      spaces->list().spaces().begin(), spaces->list().spaces().end(),
      [](const SpaceEntry& s) { return s.name == "Main"; });
  ASSERT_NE(main_entry, spaces->list().spaces().end());
  const std::string main_space = main_entry->id;
  {
    CluOnboardingHandler handler(window().GetWeakPtr());
    CallFuture done;
    handler.Complete(mojom::OnboardingCompleteInput::New(/*skipped=*/false),
                     done.GetCallback());
    EXPECT_TRUE(IsOk(done.Take()));
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    CluWindowState* shown = ShownWindow();
    return OpenWindows() == 1 && shown && shown->space_id() == main_space;
  }));
  settings->SetFirstRunForTesting(std::nullopt);
  import().SetRootForTesting(std::nullopt);
}

// Setup asks once to bring everything over. With another browser's sidebar file on the Mac it opens on the
// switch screen; after Not now (or an import) the Import step never shows, going on or back, and the dots count five.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportSetupAsksOnceToBringEverythingOver) {
  base::ScopedTempDir arc_home;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(arc_home.CreateUniqueTempDir());
    ASSERT_TRUE(base::CopyDirectory(
        ImportFixtureRoot().DirName().AppendASCII("import-arc").Append("Library"),
        arc_home.GetPath(), /*recursive=*/true));
  }
  import().SetRootForTesting(arc_home.GetPath());
  CluAppSettings* settings = CluAppSettings::Get();
  ASSERT_TRUE(settings);
  settings->SetFirstRunForTesting(true);
  window().MarkDirty();
  content::WebContents* frame = shell().frame_contents();
  const auto step = [&] {
    return content::EvalJs(frame, "document.querySelector('.clu-ob')?.dataset.step ?? ''")
        .ExtractString();
  };
  const auto title = [&] {
    return content::EvalJs(frame, "document.getElementById('clu-ob-title')?.textContent ?? ''")
        .ExtractString();
  };
  const auto click = [&](std::string_view text) {
    return content::EvalJs(frame, content::JsReplace(R"(
      (() => {
        const b = [...document.querySelectorAll('.clu-ob button')]
            .find(b => b.textContent.trim() === $1 && !b.disabled);
        if (b) b.click();
        return !!b;
      })())",
                                                     text))
        .ExtractBool();
  };
  const auto dots = [&] {
    return content::EvalJs(frame, "document.querySelector('.clu-ob-dots')?.getAttribute('aria-label') ?? ''")
        .ExtractString();
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return title() == "Bring everything over"; }));
  ASSERT_TRUE(click("Not now"));
  ASSERT_TRUE(base::test::RunUntil([&] { return title() == "Welcome to Rio"; }));
  EXPECT_EQ(dots(), "Step 1 of 5");
  for (const char* button : {"Get started", "Continue", "Continue"}) {
    const std::string before = step();
    ASSERT_TRUE(click(button)) << before;
    ASSERT_TRUE(base::test::RunUntil([&] { return step() != before; }));
  }
  ASSERT_EQ(step(), "default-browser");
  // Skip, or Continue when this Mac already has Rio as its default browser.
  ASSERT_TRUE(click("Skip") || click("Continue"));
  ASSERT_TRUE(base::test::RunUntil([&] { return step() != "default-browser"; }));
  EXPECT_EQ(step(), "done");
  EXPECT_EQ(dots(), "Step 5 of 5");
  ASSERT_TRUE(click("Back"));
  ASSERT_TRUE(base::test::RunUntil([&] { return step() == "default-browser"; }));
  settings->SetFirstRunForTesting(std::nullopt);
  import().SetRootForTesting(std::nullopt);
}

// A Replace once left "Imported 4" because a window of it was open. Replace moves that window to a space
// that stays and removes the profile anyway, and leaves no mark behind once it is gone.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportReplaceRemovesAProfileWithAnOpenWindow) {
  import().SetRootForTesting(home_.GetPath());
  CluSpacesService* spaces = CluSpacesService::Get();
  base::test::TestFuture<base::expected<std::string, SpacesError>> made;
  spaces->CreateProfile(std::string("Imported 4"), made.GetCallback());
  ASSERT_TRUE(made.Get().has_value());
  const std::string old_id = made.Get().value();
  base::expected<std::string, SpacesError> old_space =
      spaces->CreateSpace(std::string("Old"), std::nullopt, std::nullopt, old_id);
  ASSERT_TRUE(old_space.has_value());
  space_actions::SwitchWindowToSpace(&window(), old_space.value(), /*close_source=*/false);
  auto shown_on_old = [&] {
    for (CluWindowState* w : space_actions::AllWindows()) {
      if (!w->hidden_by_switch() && !w->browser()->IsDeleteScheduled() &&
          spaces->ProfileIdOf(w->profile()) == old_id) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(base::test::RunUntil(shown_on_old));

  mojom::ImportRunInputArcPtr arc = ArcInput(false, false);
  arc->replace = true;
  const base::Value result = Run(mojom::ImportRunInput::NewArc(std::move(arc)));
  EXPECT_TRUE(result.GetDict().Find("error")->is_none());
  ASSERT_TRUE(base::test::RunUntil([&] { return !spaces->list().FindProfile(old_id); }));
  EXPECT_FALSE(shown_on_old());
  EXPECT_TRUE(g_browser_process->local_state()->GetList(kCluProfilesToRemovePref).empty());
  import().SetRootForTesting(std::nullopt);
}

// The settings window was open on a window of the profile a Replace removes (Settings from the import step).
// The window goes, the settings window moves to a window that stays and its page with it, onto that window's profile,
// so the removed profile is destroyed with none of its pages left (else ~WebContentsOfBrowserContext CHECKs).
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportReplaceWithSettingsOpenOnTheRemovedProfile) {
  import().SetRootForTesting(home_.GetPath());
  CluSpacesService* spaces = CluSpacesService::Get();
  base::test::TestFuture<base::expected<std::string, SpacesError>> made;
  spaces->CreateProfile(std::string("Imported 4"), made.GetCallback());
  ASSERT_TRUE(made.Get().has_value());
  const std::string old_id = made.Get().value();
  base::expected<std::string, SpacesError> old_space =
      spaces->CreateSpace(std::string("Old"), std::nullopt, std::nullopt, old_id);
  ASSERT_TRUE(old_space.has_value());
  space_actions::SwitchWindowToSpace(&window(), old_space.value(), /*close_source=*/false);
  CluWindowState* old_window = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (CluWindowState* w : space_actions::AllWindows()) {
      if (!w->hidden_by_switch() && !w->browser()->IsDeleteScheduled() &&
          spaces->ProfileIdOf(w->profile()) == old_id) {
        old_window = w;
        return true;
      }
    }
    return false;
  }));
  base::WeakPtr<Profile> old_profile = old_window->profile()->GetWeakPtr();
  ASSERT_TRUE(CluSettingsWindow::Show(old_window->browser(), "import-arc"));
  CluSettingsWindow* settings_window = CluSettingsWindow::Get();
  ASSERT_TRUE(content::WaitForLoadStop(settings_window->contents()));
  ASSERT_EQ(settings_window->contents()->GetBrowserContext(), old_profile.get());

  mojom::ImportRunInputArcPtr arc = ArcInput(false, false);
  arc->replace = true;
  const base::Value result = Run(mojom::ImportRunInput::NewArc(std::move(arc)));
  EXPECT_TRUE(result.GetDict().Find("error")->is_none());
  ASSERT_TRUE(base::test::RunUntil([&] { return !spaces->list().FindProfile(old_id); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !old_profile; }));

  ASSERT_EQ(CluSettingsWindow::Get(), settings_window);
  ASSERT_TRUE(settings_window->browser());
  content::WebContents* page = settings_window->contents();
  ASSERT_TRUE(page);
  EXPECT_EQ(page->GetBrowserContext(),
            settings_window->browser()->GetProfile()->GetOriginalProfile());
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(page->GetLastCommittedURL(), GURL("chrome://clu-settings/#import-arc"));
  settings_window->Close();
  import().SetRootForTesting(std::nullopt);
}

// A second Replace started from the imported window, the only one on screen, left Rio
// with no window until Cmd+N. Replace always ends with a window on screen.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportReplaceFromTheOnlyWindowLeavesAWindow) {
  import().SetRootForTesting(home_.GetPath());
  CluSpacesService* spaces = CluSpacesService::Get();
  base::test::TestFuture<base::expected<std::string, SpacesError>> made;
  spaces->CreateProfile(std::string("Imported 4"), made.GetCallback());
  ASSERT_TRUE(made.Get().has_value());
  const std::string old_id = made.Get().value();
  base::expected<std::string, SpacesError> old_space =
      spaces->CreateSpace(std::string("Old"), std::nullopt, std::nullopt, old_id);
  ASSERT_TRUE(old_space.has_value());
  // The first import's end: the window moved to the imported profile and the one it left closed.
  space_actions::SwitchWindowToSpace(&window(), old_space.value(), /*close_source=*/true);
  auto shown = [&](bool on_old) {
    int count = 0;
    for (CluWindowState* w : space_actions::AllWindows()) {
      if (!w->is_private() && !w->hidden_by_switch() && !w->browser()->IsDeleteScheduled() &&
          (spaces->ProfileIdOf(w->profile()) == old_id) == on_old) {
        ++count;
      }
    }
    return count;
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return shown(true) == 1 && shown(false) == 0; }));

  // Run() binds the handler to the one window on screen: the imported one.
  mojom::ImportRunInputArcPtr arc = ArcInput(false, false);
  arc->replace = true;
  const base::Value result = Run(mojom::ImportRunInput::NewArc(std::move(arc)));
  EXPECT_TRUE(result.GetDict().Find("error")->is_none());
  ASSERT_TRUE(base::test::RunUntil([&] { return !spaces->list().FindProfile(old_id); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return shown(false) >= 1; }));
  import().SetRootForTesting(std::nullopt);
}

// A Replace a quit cut short leaves its marks, and one from before marks existed left an "Imported N" with
// no space. The next launch (Local State read again) removes both; the user's own profiles, a space-holding
// "Imported 7" and one the import map still owns stay, and junk marks are dropped.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportLaunchRemovesWhatAReplaceLeft) {
  CluSpacesService* spaces = CluSpacesService::Get();
  auto make = [&](const char* name) {
    base::test::TestFuture<base::expected<std::string, SpacesError>> made;
    spaces->CreateProfile(std::string(name), made.GetCallback());
    EXPECT_TRUE(made.Get().has_value());
    return made.Get().value_or(std::string());
  };
  const std::string leftover = make("Imported 4");
  const std::string marked = make("Old import");
  ASSERT_TRUE(spaces->CreateSpace(std::string("Old"), std::nullopt, std::nullopt, marked).has_value());
  const std::string mine = make("Work");
  const std::string mine_named = make("Imported 7");
  ASSERT_TRUE(spaces->CreateSpace(std::string("Mine"), std::nullopt, std::nullopt, mine_named).has_value());
  const std::string owned = make("Imported 5");
  PrefService* local_state = g_browser_process->local_state();
  local_state->SetDict(kCluImportMapPref, base::DictValue()
                                              .Set("sidebar|profile:Default", "a-new-profile")
                                              .Set("sidebar|profile:Profile 2", owned));
  spaces->MarkProfilesForRemoval({marked, marked, "junk", "", std::string(5000, 'x'),
                                  spaces->list().DefaultProfile()->id});
  EXPECT_EQ(local_state->GetList(kCluProfilesToRemovePref).size(), 1u);
  {
    ScopedListPrefUpdate update(local_state, kCluProfilesToRemovePref);
    update->Append("junk");
    update->Append(7);
  }

  spaces->ResetForTesting();
  ASSERT_TRUE(spaces->list().FindProfile(leftover));
  EXPECT_FALSE(std::ranges::any_of(spaces->list().spaces(), [&](const SpaceEntry& space) {
    return space.profile_id == marked;
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !spaces->list().FindProfile(leftover) && !spaces->list().FindProfile(marked);
  }));
  EXPECT_TRUE(spaces->list().FindProfile(mine));
  EXPECT_TRUE(spaces->list().FindProfile(mine_named));
  EXPECT_TRUE(spaces->list().FindProfile(owned));
  EXPECT_TRUE(spaces->list().DefaultProfile());
  EXPECT_TRUE(local_state->GetList(kCluProfilesToRemovePref).empty());
  local_state->ClearPref(kCluImportMapPref);
}

// What the settings page at #import-chrome shows once it has looked: the Chrome profiles in its picker
// (comma separated), or the card's status when there is no picker; "" until the Chrome import is on the page.
constexpr char kChromeImportShown[] = R"(
  (() => {
    const section = document.querySelector('[data-import-chrome]');
    const card = section?.querySelector('[role=group][aria-label="Google Chrome"]');
    if (!card) return '';
    const picker = card.querySelector('select');
    if (!picker) return card.querySelector('[role=status]')?.textContent ?? '';
    const boxes = [...card.querySelectorAll('input[type=checkbox]')].map(b => b.parentElement.textContent);
    const run = [...card.querySelectorAll('button')].some(b => b.textContent === 'Import');
    return [...picker.options].map(o => o.textContent).join(',') + '|' + boxes.join(',') + '|' + run;
  })())";

// The settings window, waited on until its page shows the Chrome import (#import-chrome) and what it found there.
std::string ChromeImportShown(CluSettingsWindow* settings_window) {
  EXPECT_TRUE(settings_window);
  if (!settings_window) {
    return std::string();
  }
  content::WebContents* page = settings_window->contents();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return page->GetLastCommittedURL() == GURL("chrome://clu-settings/#import-chrome");
  }));
  std::string shown;
  EXPECT_TRUE(base::test::RunUntil([&] {
    shown = content::EvalJs(page, kChromeImportShown).ExtractString();
    return !shown.empty();
  }));
  return shown;
}

// Every "Import from Google Chrome" opens the Chrome import itself (never General alone): the command bar's
// Import from Google Chrome and Import from Another Browser…, and the File menu's item. With the fixture home's two
// Chrome profiles (never the user's), the page offers both, what to bring and Import, and Import ends with a result.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeCommandsOpenTheChromeImport) {
  import().SetRootForTesting(home_.GetPath());
  const std::string offered = "Personal,Work|Bookmarks,History (last 90 days)|true";
  ASSERT_TRUE(window().RunCommand("settings.importChrome"));
  CluSettingsWindow* settings_window = CluSettingsWindow::Get();
  EXPECT_EQ(ChromeImportShown(settings_window), offered);
  settings_window->Close();
  ASSERT_TRUE(base::test::RunUntil([] { return !CluSettingsWindow::Get(); }));

  ASSERT_TRUE(window().RunCommand("settings.import"));
  settings_window = CluSettingsWindow::Get();
  EXPECT_EQ(ChromeImportShown(settings_window), offered);
  // An open settings window moves to the Chrome import too.
  ASSERT_TRUE(window().RunCommand("settings.open"));
  ASSERT_TRUE(window().RunCommand("settings.importChrome"));
  EXPECT_EQ(CluSettingsWindow::Get(), settings_window);
  EXPECT_EQ(ChromeImportShown(settings_window), offered);
  settings_window->Close();
  ASSERT_TRUE(base::test::RunUntil([] { return !CluSettingsWindow::Get(); }));

  // The File menu's Import from Google Chrome dispatches the command's id.
  const CommandSpec* spec = FindCommand("settings.importChrome");
  ASSERT_TRUE(spec);
  EXPECT_EQ(spec->menu, CommandMenu::kFile);
  EXPECT_STREQ(spec->title, "Import from Google Chrome");
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), spec->idc));
  settings_window = CluSettingsWindow::Get();
  EXPECT_EQ(ChromeImportShown(settings_window), offered);

  // Work (Profile 1) has bookmarks and history: Import brings them and says what came over.
  content::WebContents* page = settings_window->contents();
  ASSERT_TRUE(content::ExecJs(page, R"(
    (() => {
      const card = document.querySelector('[data-import-chrome] [role=group][aria-label="Google Chrome"]');
      const picker = card.querySelector('select');
      picker.value = 'Profile 1';
      picker.dispatchEvent(new Event('change', { bubbles: true }));
      [...card.querySelectorAll('button')].find(b => b.textContent === 'Import').click();
    })())"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, R"(
      document.querySelector('[data-import-chrome] [role=group][aria-label="Google Chrome"] [role=status]')
          ?.textContent.startsWith('Imported ') ?? false)")
        .ExtractBool();
  }));
  settings_window->Close();
  import().SetRootForTesting(std::nullopt);
}

// With no Google Chrome on the Mac, the Chrome import says so on its own page.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeSaysWhenChromeIsNotInstalled) {
  base::ScopedTempDir empty_home;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(empty_home.CreateUniqueTempDir());
  }
  import().SetRootForTesting(empty_home.GetPath());
  ASSERT_TRUE(window().RunCommand("settings.importChrome"));
  CluSettingsWindow* settings_window = CluSettingsWindow::Get();
  EXPECT_EQ(ChromeImportShown(settings_window),
            "Google Chrome isn\u2019t installed on this Mac, or has no profiles to import.");
  settings_window->Close();
  import().SetRootForTesting(std::nullopt);
}

// Onboarding's Import step "From Google Chrome" opened Settings at General. It opens the Chrome import with
// the fixture home's profiles and ends setup; "Switch from another browser" opens the Import from Arc card.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportSetupFromGoogleChromeOpensTheChromeImport) {
  {
    // No sidebar file, so setup asks at its Import step rather than on the switch screen.
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::DeleteFile(SidebarFilePath(home_.GetPath())));
  }
  import().SetRootForTesting(home_.GetPath());
  CluAppSettings* settings = CluAppSettings::Get();
  ASSERT_TRUE(settings);
  settings->SetFirstRunForTesting(true);
  window().MarkDirty();
  content::WebContents* frame = shell().frame_contents();
  const auto step = [&] {
    return content::EvalJs(frame, "document.querySelector('.clu-ob')?.dataset.step ?? ''")
        .ExtractString();
  };
  const auto click = [&](std::string_view text) {
    return content::EvalJs(frame, content::JsReplace(R"(
      (() => {
        const b = [...document.querySelectorAll('.clu-ob button')]
            .find(b => b.textContent.trim() === $1 && !b.disabled);
        if (b) b.click();
        return !!b;
      })())",
                                                     text))
        .ExtractBool();
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return step() == "welcome"; }));
  while (step() != "import") {
    const std::string before = step();
    ASSERT_NE(before, "done");
    // Get started, Continue twice, then Skip (or Continue when Rio is already the default browser).
    ASSERT_TRUE(click("Get started") || click("Continue") || click("Skip")) << before;
    ASSERT_TRUE(base::test::RunUntil([&] { return step() != before; }));
  }
  ASSERT_TRUE(click("From Google Chrome"));
  EXPECT_EQ(ChromeImportShown(CluSettingsWindow::Get()),
            "Personal,Work|Bookmarks,History (last 90 days)|true");
  EXPECT_TRUE(base::test::RunUntil([&] { return !settings->onboarding_show(); }));
  CluSettingsWindow::Get()->Close();
  settings->SetFirstRunForTesting(std::nullopt);
  import().SetRootForTesting(std::nullopt);
}

// The command bar's "Import from Arc" opens the app settings at General with its import card open
// (#import-arc), and moves an open settings window there too.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportArcCommandOpensTheImport) {
  ASSERT_TRUE(window().RunCommand("settings.importArc"));
  CluSettingsWindow* settings_window = CluSettingsWindow::Get();
  ASSERT_TRUE(settings_window);
  content::WebContents* page = settings_window->contents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(page->GetLastCommittedURL(),
            GURL("chrome://clu-settings/#import-arc"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page,
                           "!!document.querySelector('[data-switch-import]')")
        .ExtractBool();
  }));

  ASSERT_TRUE(window().RunCommand("settings.import"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return page->GetLastCommittedURL() == GURL("chrome://clu-settings/#import-chrome");
  }));
  ASSERT_TRUE(window().RunCommand("settings.importArc"));
  EXPECT_EQ(CluSettingsWindow::Get(), settings_window);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return page->GetLastCommittedURL() ==
           GURL("chrome://clu-settings/#import-arc");
  }));
  settings_window->Close();
}

// A denied keychain prompt skips passwords and cookies, says so, and imports everything else.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportSidebarWithADeniedKeychainSkipsSecrets) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::nullopt);
  const base::Value result =
      Run(mojom::ImportRunInput::NewArc(ArcInput(true, true)));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindBool("secretsDenied"), true);
  EXPECT_EQ(r.FindInt("passwords"), 0);
  EXPECT_EQ(r.FindInt("cookies"), 0);
  EXPECT_EQ(r.FindInt("spaces"), 4);
  EXPECT_EQ(r.FindInt("pinned"), 10);
}

// Site data ------------------------------------------------------------------------------------

// The fixture's address, by name, in `profile`'s Autofill once it has loaded.
bool HasFixtureAddress(Profile* profile) {
  autofill::PersonalDataManager* pdm = autofill::PersonalDataManagerFactory::GetForBrowserContext(profile);
  return pdm && std::ranges::any_of(pdm->address_data_manager().GetProfiles(), [](const autofill::AutofillProfile* p) {
           return p->GetRawInfo(autofill::NAME_FULL) == u"Ada Fixture" &&
                  p->GetRawInfo(autofill::ADDRESS_HOME_CITY) == u"Testville";
         });
}

ContentSetting FixtureNotifications(Profile* profile) {
  const GURL site(kFixturePermissionSite);
  return HostContentSettingsMapFactory::GetForProfile(profile)->GetContentSetting(
      site, site, ContentSettingsType::NOTIFICATIONS);
}

// The Chrome import brings the profile's site permissions, saved address and default search engine into the Rio
// profile of the space it imports into, and stages its Local Storage and IndexedDB there for the next launch; Chrome's
// files are only read.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, PRE_CluImportChromeBringsSiteData) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::FilePath source = ChromeDataDir(home_.GetPath()).Append("Profile 1");
  std::string prefs_before;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::ReadFileToString(source.Append("Preferences"), &prefs_before));
  }
  const base::Value result = Run(Chrome(/*signins=*/true));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  // One site with Local Storage and one with an IndexedDB database.
  EXPECT_EQ(r.FindInt("siteStorage"), 2);
  EXPECT_EQ(r.FindInt("permissions"), 1);
  EXPECT_EQ(r.FindInt("addresses"), 1);
  EXPECT_EQ(*r.FindString("searchEngine"), kFixtureSearchEngine);
  // The cookies test's count: site data added no skips.
  EXPECT_EQ(r.FindInt("skipped"), 5);
  Profile* profile = browser()->GetProfile();
  EXPECT_EQ(FixtureNotifications(profile), CONTENT_SETTING_ALLOW);
  EXPECT_TRUE(base::test::RunUntil([&] { return HasFixtureAddress(profile); }));
  const TemplateURL* engine = TemplateURLServiceFactory::GetForProfile(profile)->GetDefaultSearchProvider();
  ASSERT_TRUE(engine);
  EXPECT_EQ(engine->short_name(), u"Fixture Search");
  base::ScopedAllowBlockingForTesting allow;
  EXPECT_TRUE(base::DirectoryExists(profile->GetPath().Append(kPendingSiteStorageDirName)));
  std::string prefs_after;
  ASSERT_TRUE(base::ReadFileToString(source.Append("Preferences"), &prefs_after));
  EXPECT_EQ(prefs_after, prefs_before);
  // A second import adds the address and the permission once.
  const base::Value again = Run(Chrome(/*signins=*/true));
  EXPECT_EQ(again.GetDict().FindInt("addresses"), 0);
  EXPECT_EQ(again.GetDict().FindInt("permissions"), 0);
}

// At the next launch the staged Local Storage and IndexedDB are in the profile, before its storage opened.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeBringsSiteData) {
  Profile* profile = browser()->GetProfile();
  {
    base::ScopedAllowBlockingForTesting allow;
    EXPECT_FALSE(base::PathExists(profile->GetPath().Append(kPendingSiteStorageDirName)));
    EXPECT_TRUE(base::DirectoryExists(profile->GetPath().Append("IndexedDB").Append(kFixtureIndexedDbDir)));
  }
  base::test::TestFuture<std::vector<storage::mojom::StorageUsageInfoPtr>> usage;
  profile->GetDefaultStoragePartition()->GetLocalStorageControl()->GetUsage(usage.GetCallback());
  std::vector<std::string> origins;
  for (const storage::mojom::StorageUsageInfoPtr& info : usage.Get()) {
    origins.push_back(info->storage_key.origin().Serialize());
  }
  EXPECT_THAT(origins, testing::Contains(std::string(kFixturePermissionSite)));
  // What the first run brought stays.
  EXPECT_EQ(FixtureNotifications(profile), CONTENT_SETTING_ALLOW);
  EXPECT_TRUE(base::test::RunUntil([&] { return HasFixtureAddress(profile); }));
}

// The sidebar file import brings the same through the same code, into the Rio profile each source profile became.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportSidebarBringsSiteData) {
  import().SetRootForTesting(home_.GetPath());
  import().SetSafeStorageSecretForTesting(std::string(kFakeSafeStorageSecret));
  const base::Value result = Run(mojom::ImportRunInput::NewArc(ArcInput(true, true)));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  // The fixture's Default profile has the site data; its Profile 1 has none.
  EXPECT_EQ(r.FindInt("siteStorage"), 2);
  EXPECT_EQ(r.FindInt("permissions"), 1);
  EXPECT_EQ(r.FindInt("addresses"), 1);
  Profile* imported = nullptr;
  {
    base::ScopedAllowBlockingForTesting allow;
    for (Profile* p : g_browser_process->profile_manager()->GetLoadedProfiles()) {
      if (base::DirectoryExists(p->GetPath().Append(kPendingSiteStorageDirName))) {
        imported = p;
      }
    }
  }
  ASSERT_TRUE(imported);
  EXPECT_EQ(FixtureNotifications(imported), CONTENT_SETTING_ALLOW);
  EXPECT_TRUE(base::test::RunUntil([&] { return HasFixtureAddress(imported); }));
  import().SetSafeStorageSecretForTesting(std::nullopt);
}

// Without the sign-ins checkbox no site storage or permissions come over; the address still does.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportChromeWithoutSigninsLeavesSiteStorage) {
  import().SetRootForTesting(home_.GetPath());
  const base::Value result = Run(Chrome());
  const base::DictValue& r = result.GetDict();
  EXPECT_EQ(r.FindInt("siteStorage"), 0);
  EXPECT_EQ(r.FindInt("permissions"), 0);
  EXPECT_EQ(r.FindInt("addresses"), 1);
  EXPECT_NE(FixtureNotifications(browser()->GetProfile()), CONTENT_SETTING_ALLOW);
  base::ScopedAllowBlockingForTesting allow;
  EXPECT_FALSE(base::PathExists(browser()->GetProfile()->GetPath().Append(kPendingSiteStorageDirName)));
}

// The saved addresses and the default search engine come over only when ticked: unticked, Rio's own default engine
// stays and no address is added, from either import.
IN_PROC_BROWSER_TEST_F(CluImportBrowserTest, CluImportLeavesUntickedAddressesAndSearchEngine) {
  import().SetRootForTesting(home_.GetPath());
  TemplateURLService* urls = TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
  urls->Load();
  ASSERT_TRUE(base::test::RunUntil([&] { return urls->loaded(); }));
  const TemplateURL* before = urls->GetDefaultSearchProvider();
  ASSERT_TRUE(before);
  const std::u16string engine_before = before->short_name();
  const base::Value result = Run(Chrome(/*signins=*/false, /*extras=*/false));
  const base::DictValue& r = result.GetDict();
  EXPECT_TRUE(r.Find("error")->is_none());
  EXPECT_EQ(r.FindInt("addresses"), 0);
  EXPECT_TRUE(r.Find("searchEngine")->is_none());
  ASSERT_TRUE(urls->GetDefaultSearchProvider());
  EXPECT_EQ(urls->GetDefaultSearchProvider()->short_name(), engine_before);

  mojom::ImportRunInputArcPtr arc = ArcInput(false, false);
  arc->autofill = false;
  const base::Value sidebar = Run(mojom::ImportRunInput::NewArc(std::move(arc)));
  EXPECT_TRUE(sidebar.GetDict().Find("error")->is_none());
  EXPECT_EQ(sidebar.GetDict().FindInt("addresses"), 0);
}

// 12.13 box 1: an unreadable clu.db --------------------------------------------------------------------------------

class CluDataRecoveryBrowserTest : public CluDataBrowserTest {
 protected:
  // Before the browser starts: the main test finds a damaged clu.db, with the PRE_ test's file as its backup.
  bool SetUpUserDataDirectory() override {
    if (content::IsPreTest()) {
      return true;
    }
    base::FilePath data;
    if (!base::PathService::Get(chrome::DIR_USER_DATA, &data)) {
      return false;
    }
    const base::FilePath db = data.AppendASCII("Default")
                                  .AppendASCII(kCluStoreDirName)
                                  .AppendASCII(kCluStoreFileName);
    if (base::PathExists(db) &&
        !base::CopyFile(db, base::FilePath(db.value() + ".bak"))) {
      return false;
    }
    return base::CreateDirectory(db.DirName()) &&
           base::WriteFile(db, "not a database, junk written over clu.db");
  }
};

IN_PROC_BROWSER_TEST_F(CluDataRecoveryBrowserTest,
                       PRE_CluDataADamagedStoreComesBackFromItsBackup) {
  const std::string row = Open(Url("/data/harbor.html"));
  ASSERT_FALSE(row.empty());
  model().CommitSoon();
  FlushStore();
}

IN_PROC_BROWSER_TEST_F(CluDataRecoveryBrowserTest,
                       CluDataADamagedStoreComesBackFromItsBackup) {
  EXPECT_EQ(model().recovered(), "backup");
  EXPECT_TRUE(model().persistent());
  bool harbor = false;
  for (const TabNode* node : model().tree().All()) {
    harbor |= node->url.find("/data/harbor.html") != std::string::npos;
  }
  EXPECT_TRUE(harbor);
  window().Flush();
  const base::DictValue state = State();
  EXPECT_EQ(*state.FindStringByDottedPath("app.dataHealth.recovered"), "backup");
  bool toast = false;
  for (const base::Value& t :
       *state.FindDict("windows")->FindDict(window().window_id())->FindList("toasts")) {
    toast |= t.GetDict().FindString("text")->find("restored a backup") != std::string::npos;
  }
  EXPECT_TRUE(toast);
}

class CluDataFreshBrowserTest : public CluDataBrowserTest {
 protected:
  bool SetUpUserDataDirectory() override {
    base::FilePath data;
    if (!base::PathService::Get(chrome::DIR_USER_DATA, &data)) {
      return false;
    }
    const base::FilePath db = data.AppendASCII("Default")
                                  .AppendASCII(kCluStoreDirName)
                                  .AppendASCII(kCluStoreFileName);
    return base::CreateDirectory(db.DirName()) &&
           base::WriteFile(db, std::string(8192, '\x01'));
  }
};

IN_PROC_BROWSER_TEST_F(CluDataFreshBrowserTest,
                       CluDataADamagedStoreWithoutBackupStartsFresh) {
  EXPECT_EQ(model().recovered(), "fresh");
  EXPECT_TRUE(model().persistent());
  // One window, its space, nothing else: no dialog, the toast says it once.
  EXPECT_EQ(model().spaces().size(), 1u);
  window().Flush();
  EXPECT_EQ(*State().FindStringByDottedPath("app.dataHealth.recovered"), "fresh");
  const base::DictValue state = State();
  const base::ListValue* surfaces = state.FindDict("windows")
                                         ->FindDict(window().window_id())
                                         ->FindDict("overlay")
                                         ->FindList("surfaces");
  for (const base::Value& s : *surfaces) {
    EXPECT_NE(*s.GetDict().FindString("layer"), "top");
  }
}

}  // namespace
}  // namespace clu
