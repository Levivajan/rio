// Copyright 2026 The Rio Authors. All rights reserved.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/base64url.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/thread_annotations.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/autofill/autofill_uitest_util.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/interstitials/security_interstitial_page_test_utils.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/password_manager/passwords_navigation_observer.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/safe_browsing/test_safe_browsing_service.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/passwords/manage_passwords_ui_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/download/bubble/download_toolbar_ui_controller.h"
#include "chrome/browser/ui/views/passwords/password_bubble_view_base.h"
#include "chrome/browser/webauthn/chrome_authenticator_request_delegate.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/permissions/permission_request_manager_test_api.h"
#include "clu/browser/capabilities/clu_downloads.h"
#include "clu/browser/capabilities/clu_keep_live.h"
#include "clu/browser/capabilities/clu_library_files.h"
#include "clu/browser/capabilities/clu_screenshot_watcher.h"
#include "clu/browser/ui/views/frame/clu_anchor_view.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "clu/browser/ui/webui/clu_capabilities_handlers.h"
#include "clu/browser/ui/webui/clu_data_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/common/mojom/bridge.mojom.h"
#include "clu/common/mojom/capabilities.mojom.h"
#include "components/autofill/content/browser/content_autofill_driver.h"
#include "components/autofill/core/browser/autofill_trigger_source.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/foundations/browser_autofill_manager.h"
#include "components/autofill/core/browser/foundations/test_autofill_manager_waiter.h"
#include "components/autofill/core/browser/test_utils/autofill_test_util.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/download/public/common/download_item.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_form_manager.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/password_store_change.h"
#include "components/password_manager/core/browser/password_store/password_store_results_observer.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/resolvers/permission_prompt_options.h"
#include "components/permissions/test/permission_request_observer.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/content/browser/safe_browsing_blocking_page.h"
#include "components/safe_browsing/core/browser/db/fake_database_manager.h"
#include "components/safe_browsing/core/browser/db/sb_protocol_manager_util.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/safe_browsing/core/common/safebrowsing_switches.h"
#include "components/security_interstitials/content/security_interstitial_page.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "components/security_interstitials/core/controller_client.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/scoped_authenticator_environment_for_testing.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "content/public/test/slow_download_http_response.h"
#include "content/public/test/test_navigation_observer.h"
#include "device/fido/virtual_ctap2_device.h"
#include "device/fido/virtual_fido_device_factory.h"
#include "google_apis/google_api_keys.h"
#include "net/base/url_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/gfx/geometry/point.h"
#include "ui/shell_dialogs/fake_select_file_dialog.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/any_widget_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"
#include "url/origin.h"

// The CluNative* checks of the M9 "Done when" row (docs/chromium/PLAN.md section 6) and docs/PLAN.md 12.12 restated
// for Chromium: Chromium's own password manager, address autofill, Safe Browsing, downloads bubble, permission
// prompts, passkeys and Memory Saver, running inside a Clu window whose toolbar is hidden. No test signs in to a real
// account: logins, addresses and passkeys are fixture pages from the test server and a virtual authenticator. The
// boxes each test covers are in docs/chromium/acceptance-map.md.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

// Resolves once the element `id` holds `value`. Autofilled values reach page script only after a user gesture, so
// callers click the page first.
constexpr char kWaitForValue[] = R"(
  new Promise(resolve => {
    const check = () => {
      const el = document.getElementById($1);
      if (el && el.value === $2) {
        resolve(true);
      } else {
        setTimeout(check, 20);
      }
    };
    check();
  }))";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

std::optional<mojom::ErrorCode> Code(const mojom::CallResultPtr& result) {
  if (!result || !result->is_error()) {
    return std::nullopt;
  }
  return result->get_error()->code;
}

// /capabilities/attachment?name=<file>&size=<bytes>: a plain attachment.
std::unique_ptr<net::test_server::HttpResponse> HandleAttachment(
    const net::test_server::HttpRequest& request) {
  const GURL url = request.GetURL();
  if (url.path() != "/capabilities/attachment") {
    return nullptr;
  }
  std::string name = "file.bin";
  size_t size = 1000;
  for (const std::string_view pair :
       base::SplitStringPiece(url.query(), "&", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (pair.starts_with("name=")) {
      name = std::string(pair.substr(5, 200));
    } else if (pair.starts_with("size=")) {
      base::StringToSizeT(pair.substr(5), &size);
      size = std::min<size_t>(size, 20'000'000);
    }
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("application/octet-stream");
  response->AddCustomHeader("Content-Disposition",
                            base::StrCat({"attachment; filename=\"", name, "\""}));
  response->set_content(std::string(size, 'c'));
  return response;
}

class CluNativeBrowserTest : public InProcessBrowserTest {
 protected:
  CluNativeBrowserTest() {
    // Fill as soon as the form is parsed; the test server has no prediction server to wait for.
    password_manager::PasswordFormManager::
        set_wait_for_server_predictions_for_filling(false);
  }

  void SetUpOnMainThread() override {
    // Pages load from localhost only, so no host resolver rule is set here: the network fixture's direct lookups
    // would never match behind a catch-all rule (the first matching rule wins).
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleAttachment));
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &content::SlowDownloadHttpResponse::HandleSlowDownloadRequest));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    PrefService* prefs = browser()->GetProfile()->GetPrefs();
    // Files land in InProcessBrowserTest's temporary download folder without a save dialog, and the bubble opens
    // only when asked, so each test decides when it shows.
    prefs->SetBoolean(prefs::kPromptForDownload, false);
    prefs->SetBoolean(prefs::kDownloadBubblePartialViewEnabled, false);
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  TabStripModel& strip() { return *browser()->GetTabStripModel(); }
  Profile* profile() { return browser()->GetProfile(); }
  views::View* anchor() { return GetBubbleAnchorView(browser()); }

  // `path` on localhost: a secure context, which password filling and WebAuthn need, unlike 127.0.0.1 for WebAuthn.
  GURL Url(const std::string& path) {
    return embedded_test_server()->GetURL("localhost", "/" + path);
  }

  // A Today row on `url`, shown and loaded; returns its page.
  content::WebContents* Open(const GURL& url) {
    base::expected<std::string, std::string> id =
        window().CreateRow(url, std::nullopt, /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return nullptr;
    }
    tabs::TabInterface* tab = window().FindTab(id.value());
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    EXPECT_TRUE(contents && content::WaitForLoadStop(contents));
    return contents;
  }

  content::WebContents* Shown() { return strip().GetActiveWebContents(); }

  // The bubble `widget` hangs from the window's CluAnchorView (the toolbar is hidden).
  void ExpectAnchoredToTheCard(views::Widget* widget) {
    ASSERT_TRUE(widget);
    ASSERT_TRUE(widget->widget_delegate());
    views::BubbleDialogDelegate* bubble =
        widget->widget_delegate()->AsBubbleDialogDelegate();
    ASSERT_TRUE(bubble);
    ASSERT_TRUE(anchor());
    EXPECT_EQ(bubble->GetAnchorView(), anchor());
    // The bubble's window sits over the card, whichever corner its arrow takes: never over the sidebar. The anchor
    // spans the card's top edge 12 pt inside it; the bubble's shadow may reach a little further.
    const gfx::Rect on_screen = widget->GetWindowBoundsInScreen();
    const gfx::Rect edge = anchor()->GetBoundsInScreen();
    EXPECT_GE(on_screen.x(), edge.x() - 40) << on_screen.ToString();
    EXPECT_LE(on_screen.right(), edge.right() + 40) << on_screen.ToString();
    EXPECT_GE(on_screen.y(), edge.y() - 40) << on_screen.ToString();
  }

  base::ListValue Downloads() {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* rows = state.FindList("downloads");
    return rows ? rows->Clone() : base::ListValue();
  }

  content::DownloadManager* download_manager() {
    return profile()->GetDownloadManager();
  }
};

// docs/chromium/PLAN.md M9: native password save and fill --------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativePasswordsSaveFromTheBubbleAndFillOnReturn) {
  content::WebContents* page = Open(Url("capabilities/login.html"));
  ASSERT_TRUE(page);
  {
    PasswordsNavigationObserver submitted(page);
    ASSERT_TRUE(content::ExecJs(page, R"(
        document.getElementById('username').value = 'ada@example.com';
        document.getElementById('password').value = 'correct horse';
        document.getElementById('submit').click();)"));
    ASSERT_TRUE(submitted.Wait());
  }

  // Chromium offers to save, in its own bubble, hung from CluAnchorView.
  ManagePasswordsUIController* passwords =
      ManagePasswordsUIController::FromWebContents(page);
  ASSERT_TRUE(passwords);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return passwords->GetState() == password_manager::ui::PENDING_PASSWORD_STATE;
  }));
  ASSERT_TRUE(base::test::RunUntil([] {
    PasswordBubbleViewBase* bubble =
        PasswordBubbleViewBase::manage_password_bubble();
    return bubble && bubble->GetWidget() && bubble->GetWidget()->IsVisible();
  }));
  ExpectAnchoredToTheCard(
      PasswordBubbleViewBase::manage_password_bubble()->GetWidget());
  const password_manager::PasswordForm& pending =
      passwords->GetPendingPassword();
  EXPECT_EQ(pending.username_value, u"ada@example.com");
  passwords->SavePassword(pending.username_value, pending.password_value);

  // The login is in the profile's store ("Clu Safe Storage" on a real Mac; the mock keychain here).
  password_manager::PasswordStoreResultsObserver stored;
  ProfilePasswordStoreFactory::GetForProfile(profile(),
                                             ServiceAccessType::IMPLICIT_ACCESS)
      ->GetAutofillableLogins(stored.GetWeakPtr());
  const std::vector<password_manager::StoredCredential> logins =
      stored.WaitForResults();
  ASSERT_EQ(logins.size(), 1u);
  EXPECT_EQ(logins[0].username_value, u"ada@example.com");

  // Back on the form, Chromium fills it.
  {
    PasswordsNavigationObserver loaded(page);
    ASSERT_TRUE(content::ExecJs(page, "location.href = 'login.html'"));
    ASSERT_TRUE(loaded.Wait());
  }
  content::SimulateMouseClickAt(page, 0, blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(1, 1));
  EXPECT_EQ(true, content::EvalJs(page, content::JsReplace(
                                            kWaitForValue, "username",
                                            "ada@example.com")));
  EXPECT_EQ(true, content::EvalJs(page, content::JsReplace(
                                            kWaitForValue, "password",
                                            "correct horse")));
}

IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativePasswordsCommandOpensChromiumsPasswordManager) {
  ASSERT_TRUE(window().CanRunCommand("passwords.open"));
  ASSERT_TRUE(window().RunCommand("passwords.open"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::WebContents* shown = Shown();
    return shown &&
           shown->GetVisibleURL().host() == "password-manager";
  }));
}

// Notes each change to a password store.
class LoginsChanged : public password_manager::PasswordStoreInterface::Observer {
 public:
  void OnLoginsChanged(password_manager::PasswordStoreInterface* store,
                       const password_manager::PasswordStoreChangeList& changes) override {
    changed = true;
  }
  void OnLoginsRetained(
      password_manager::PasswordStoreInterface* store,
      const std::vector<password_manager::StoredCredential>& retained) override {
    changed = true;
  }

  bool changed = false;
};

// The Import step's "Import passwords from a CSV file" (docs/chromium/fixes.md): Chromium's own password
// page opens in the window's space and asks for a CSV file; the file's login lands in the profile's store. A second
// press reuses that page's row.
IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativeImportPasswordsFromACsvFileUsesChromiumsImport) {
  // No file dialog on screen: the fake one answers with a fixture file.
  ui::FakeSelectFileDialog::Factory* dialogs =
      ui::FakeSelectFileDialog::RegisterFactory();
  base::RunLoop asked;
  dialogs->SetOpenCallback(asked.QuitClosure());

  CluImportHandler handler(window().GetWeakPtr());
  base::test::TestFuture<mojom::CallResultPtr> opened;
  handler.OpenPasswords(opened.GetCallback());
  ASSERT_TRUE(opened.Get()->is_value_json());
  const std::string row = window().ActiveRowId();
  ASSERT_FALSE(row.empty());
  ASSERT_TRUE(Shown());
  EXPECT_EQ(Shown()->GetVisibleURL().host(), "password-manager");
  asked.Run();
  ui::FakeSelectFileDialog* dialog = dialogs->GetLastDialog();
  ASSERT_TRUE(dialog);
  ASSERT_FALSE(dialog->file_types().extensions.empty());
  EXPECT_EQ(dialog->file_types().extensions[0],
            std::vector<base::FilePath::StringType>{FILE_PATH_LITERAL("csv")});

  scoped_refptr<password_manager::PasswordStoreInterface> store =
      ProfilePasswordStoreFactory::GetForProfile(
          profile(), ServiceAccessType::IMPLICIT_ACCESS);
  LoginsChanged changes;
  base::ScopedObservation<password_manager::PasswordStoreInterface,
                          password_manager::PasswordStoreInterface::Observer>
      observing(&changes);
  observing.Observe(store.get());
  ASSERT_TRUE(dialog->CallFileSelected(
      base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
          .AppendASCII("clu/test/data/capabilities/passwords.csv"),
      "csv"));
  ASSERT_TRUE(base::test::RunUntil([&] { return changes.changed; }));
  observing.Reset();
  password_manager::PasswordStoreResultsObserver stored;
  store->GetAutofillableLogins(stored.GetWeakPtr());
  const std::vector<password_manager::StoredCredential> logins =
      stored.WaitForResults();
  ASSERT_EQ(logins.size(), 1u);
  EXPECT_EQ(logins[0].username_value, u"grace@example.com");

  // Again: the same row goes back to the import, no second password page.
  const size_t rows = window().model()->tree().size();
  base::test::TestFuture<mojom::CallResultPtr> again;
  handler.OpenPasswords(again.GetCallback());
  ASSERT_TRUE(again.Get()->is_value_json());
  EXPECT_EQ(window().ActiveRowId(), row);
  EXPECT_EQ(window().model()->tree().size(), rows);
}

// docs/chromium/PLAN.md M9: address autofill ----------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest, CluNativeAutofillFillsAnAddressForm) {
  // A made-up address saved the way Chromium's settings save one.
  autofill::AddTestProfile(profile(), autofill::test::GetFullProfile());
  autofill::PersonalDataManager* addresses =
      autofill::PersonalDataManagerFactory::GetForBrowserContext(profile());
  ASSERT_TRUE(addresses);
  const std::vector<const autofill::AutofillProfile*> saved =
      addresses->address_data_manager().GetProfiles();
  ASSERT_EQ(saved.size(), 1u);
  const autofill::AutofillProfile& address = *saved[0];

  content::WebContents* page = Open(Url("capabilities/address.html"));
  ASSERT_TRUE(page);
  autofill::ContentAutofillDriver* driver =
      autofill::ContentAutofillDriver::GetForRenderFrameHost(
          page->GetPrimaryMainFrame());
  ASSERT_TRUE(driver);
  auto& manager =
      static_cast<autofill::BrowserAutofillManager&>(driver->GetAutofillManager());
  const autofill::FormStructure* form = autofill::WaitForMatchingForm(
      &manager, base::BindRepeating([](const autofill::FormStructure& form) {
        return form.field_count() >= 6;
      }));
  ASSERT_TRUE(form);

  // What a pick in Chromium's autofill popup runs.
  autofill::TestAutofillManagerWaiter filled(
      manager, {autofill::AutofillManagerEvent::kDidAutofillForm});
  manager.FillOrPreviewForm(autofill::mojom::ActionPersistence::kFill,
                            form->global_id(), form->field(0)->global_id(),
                            &address, autofill::AutofillTriggerSource::kPopup,
                            /*require_user_confirmation=*/false,
                            /*blocked_fields=*/{});
  ASSERT_TRUE(filled.Wait(1));

  content::SimulateMouseClickAt(page, 0, blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(1, 1));
  for (const auto& [id, type] :
       std::vector<std::pair<std::string, autofill::FieldType>>{
           {"name", autofill::NAME_FULL},
           {"city", autofill::ADDRESS_HOME_CITY},
           {"zip", autofill::ADDRESS_HOME_ZIP}}) {
    EXPECT_EQ(true,
              content::EvalJs(page, content::JsReplace(
                                        kWaitForValue, id,
                                        base::UTF16ToUTF8(address.GetRawInfo(type)))))
        << id;
  }
}

// docs/PLAN.md 12.12 box 1 on Chromium: downloads ------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativeDownloadsReachStateAndTheBubbleHangsFromTheButton) {
  // Nothing to list yet: Cmd+Shift+J opens chrome://downloads, as Chromium's toolbar button does.
  EXPECT_TRUE(Downloads().empty());
  ASSERT_TRUE(window().RunCommand("downloads.open"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Shown() && Shown()->GetVisibleURL().host() == "downloads";
  }));

  content::WebContents* page = Open(
      Url("capabilities/downloads.html?same=/capabilities/"
          "attachment%3Fname%3Dq3-revenue.txt%26size%3D2000"));
  ASSERT_TRUE(page);
  content::DownloadTestObserverTerminal done(
      download_manager(), 1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  ASSERT_TRUE(content::ExecJs(page, "document.getElementById('same').click()"));
  done.WaitForFinished();

  // The finished file reaches state for the sidebar's downloads button.
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::ListValue rows = Downloads();
    return rows.size() == 1u &&
           *rows[0].GetDict().FindString("state") == "completed";
  }));
  const base::ListValue rows = Downloads();
  // A name Tidy Downloads keeps (it would rename a junk one such as "report.txt" after the page).
  EXPECT_EQ(*rows[0].GetDict().FindString("filename"), "q3-revenue.txt");
  EXPECT_EQ(rows[0].GetDict().FindDouble("receivedBytes"), 2000);
  EXPECT_EQ(rows[0].GetDict().FindBool("exists"), true);

  // Cmd+Shift+J, the sidebar button (download.openPopover) and Window > Downloads now open Chromium's bubble. The
  // docked sidebar shows the downloads button, so the bubble hangs from it; the next test covers the card with the
  // sidebar hidden.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().DownloadsAnchorRect().has_value(); }));
  views::NamedWidgetShownWaiter shown(views::test::AnyWidgetTestPasskey{},
                                      DownloadToolbarUIController::kBubbleName);
  CluDownloadHandler handler(window().GetWeakPtr());
  CallFuture opened;
  handler.OpenPopover(opened.GetCallback());
  ASSERT_TRUE(IsOk(opened.Get()));
  views::Widget* bubble = shown.WaitIfNeededAndGet();
  ASSERT_TRUE(bubble && bubble->widget_delegate());
  views::BubbleDialogDelegate* delegate =
      bubble->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(delegate);
  EXPECT_EQ(delegate->GetAnchorView(), shell().downloads_anchor_view());
  EXPECT_TRUE(DownloadToolbarUIController::From(browser())->IsShowingDetails());
}

// docs/chromium/fixes.md (m9.md section 9): while the sidebar shows its downloads button, Chromium's bubble hangs from
// that button, wherever it opens from; with the sidebar hidden it hangs from the card again.
IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativeDownloadsBubbleHangsFromTheSidebarButton) {
  content::WebContents* page = Open(
      Url("capabilities/downloads.html?same=/capabilities/"
          "attachment%3Fname%3Dq3-revenue.txt%26size%3D2000"));
  ASSERT_TRUE(page);
  content::DownloadTestObserverTerminal done(
      download_manager(), 1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  ASSERT_TRUE(content::ExecJs(page, "document.getElementById('same').click()"));
  done.WaitForFinished();

  // The frame's downloads button shows and reports where it is, inside the docked sidebar.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell().DownloadsAnchorRect().has_value();
  }));
  const gfx::Rect button = *shell().DownloadsAnchorRect();
  EXPECT_TRUE(shell().layout().sidebar_rect.Contains(button));
  EXPECT_EQ(true, content::EvalJs(shell().frame_contents(), content::JsReplace(R"(
      (() => {
        const r = document.querySelector('.clu-downloads-button').getBoundingClientRect();
        return Math.abs(r.x - $1) <= 1 && Math.abs(r.y - $2) <= 1;
      })())",
                                                                    button.x(),
                                                                    button.y())));

  // Cmd+Shift+J: the bubble hangs from the button, not the card.
  {
    views::NamedWidgetShownWaiter shown(
        views::test::AnyWidgetTestPasskey{},
        DownloadToolbarUIController::kBubbleName);
    ASSERT_TRUE(window().RunCommand("downloads.open"));
    views::Widget* bubble = shown.WaitIfNeededAndGet();
    ASSERT_TRUE(bubble && bubble->widget_delegate());
    views::BubbleDialogDelegate* delegate =
        bubble->widget_delegate()->AsBubbleDialogDelegate();
    ASSERT_TRUE(delegate);
    EXPECT_EQ(delegate->GetAnchorView(), shell().downloads_anchor_view());
    EXPECT_NE(delegate->GetAnchorView(), anchor());
    EXPECT_EQ(shell().downloads_anchor_view()->bounds(), button);
    // On screen it sits by the button: it starts over the sidebar, not at the card's far corner.
    const gfx::Rect on_screen = bubble->GetWindowBoundsInScreen();
    const gfx::Rect at = shell().downloads_anchor_view()->GetBoundsInScreen();
    EXPECT_LE(on_screen.x(), at.right() + 40) << on_screen.ToString();
    views::test::WidgetDestroyedWaiter closed(bubble);
    bubble->Close();
    closed.Wait();
  }

  // The sidebar hides: no button shows, so the bubble hangs from the card.
  shell().ToggleSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell().sidebar_anim() && !shell().layout().traffic_lights;
  }));
  EXPECT_FALSE(shell().DownloadsAnchorRect().has_value());
  views::NamedWidgetShownWaiter again(views::test::AnyWidgetTestPasskey{},
                                      DownloadToolbarUIController::kBubbleName);
  ASSERT_TRUE(window().RunCommand("downloads.open"));
  ExpectAnchoredToTheCard(again.WaitIfNeededAndGet());
}

IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativeDownloadsActOnRowsByIdOnly) {
  content::WebContents* page = Open(Url("capabilities/downloads.html"));
  ASSERT_TRUE(page);
  content::DownloadTestObserverInProgress started(download_manager(), 1);
  ASSERT_TRUE(content::ExecJs(
      page, content::JsReplace(
                "location.href = $1",
                Url(std::string(content::SlowDownloadHttpResponse::kKnownSizeUrl)
                        .substr(1)))));
  started.WaitForFinished();
  download::SimpleDownloadManager::DownloadVector items;
  download_manager()->GetAllDownloads(&items);
  ASSERT_EQ(items.size(), 1u);
  download::DownloadItem* item = items[0];
  const std::string id = item->GetGuid();
  ASSERT_TRUE(base::test::RunUntil([&] { return Downloads().size() == 1u; }));
  EXPECT_EQ(*Downloads()[0].GetDict().FindString("state"), "progressing");

  CluDownloadHandler handler(window().GetWeakPtr());
  auto act = [&](const std::string& row, mojom::DownloadActInputAction action) {
    CallFuture result;
    handler.Act(mojom::DownloadActInput::New(row, action), result.GetCallback());
    return result.Take();
  };

  // A row that does not exist changes nothing, even with a well-formed id.
  const std::string unknown = base::Uuid::GenerateRandomV4().AsLowercaseString();
  EXPECT_EQ(Code(act(unknown, mojom::DownloadActInputAction::kCancel)),
            mojom::ErrorCode::kNotFound);
  EXPECT_EQ(item->GetState(), download::DownloadItem::IN_PROGRESS);
  // A running file cannot open.
  EXPECT_EQ(Code(act(id, mojom::DownloadActInputAction::kOpen)),
            mojom::ErrorCode::kRejected);

  ASSERT_TRUE(IsOk(act(id, mojom::DownloadActInputAction::kPause)));
  EXPECT_TRUE(item->IsPaused());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return *Downloads()[0].GetDict().FindString("state") == "paused";
  }));
  // Pausing twice is refused.
  EXPECT_EQ(Code(act(id, mojom::DownloadActInputAction::kPause)),
            mojom::ErrorCode::kRejected);
  ASSERT_TRUE(IsOk(act(id, mojom::DownloadActInputAction::kResume)));
  EXPECT_FALSE(item->IsPaused());

  ASSERT_TRUE(IsOk(act(id, mojom::DownloadActInputAction::kCancel)));
  EXPECT_EQ(item->GetState(), download::DownloadItem::CANCELLED);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return *Downloads()[0].GetDict().FindString("state") == "cancelled";
  }));

  // Remove takes the row away; Clear on an empty list is fine.
  ASSERT_TRUE(IsOk(act(id, mojom::DownloadActInputAction::kRemove)));
  ASSERT_TRUE(base::test::RunUntil([&] { return Downloads().empty(); }));
  CallFuture cleared;
  handler.Clear(cleared.GetCallback());
  EXPECT_TRUE(IsOk(cleared.Get()));
}

// The Library lists the Downloads and screenshot folders (temp folders here, never the real ones) and acts
// on a file only by its bare name inside them. Open and reveal are left out: they would start Finder or an app.
IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluLibraryListsTheFoldersAndRefusesPathsOutsideThem) {
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
    ASSERT_TRUE(base::WriteFile(downloads.GetPath().AppendASCII("invoice.pdf"), "pdf"));
    ASSERT_TRUE(base::WriteFile(downloads.GetPath().AppendASCII(".DS_Store"), "x"));
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("Screenshot 1.png"), "png"));
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("notes.txt"), "txt"));
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());

  CluDownloadHandler handler(window().GetWeakPtr());
  auto files = [&](mojom::DownloadFilesInputSource source) {
    CallFuture result;
    handler.Files(mojom::DownloadFilesInput::New(source), result.GetCallback());
    mojom::CallResultPtr taken = result.Take();
    EXPECT_TRUE(IsOk(taken));
    std::vector<std::string> names;
    if (IsOk(taken)) {
      std::optional<base::Value> rows =
          base::JSONReader::Read(taken->get_value_json(), base::JSON_PARSE_RFC);
      for (const base::Value& row : rows->GetList()) {
        names.push_back(*row.GetDict().FindString("name"));
      }
    }
    return names;
  };
  EXPECT_EQ(files(mojom::DownloadFilesInputSource::kDownloads),
            std::vector<std::string>{"invoice.pdf"});
  EXPECT_EQ(files(mojom::DownloadFilesInputSource::kMedia),
            std::vector<std::string>{"Screenshot 1.png"});

  for (const char* junk : {"../invoice.pdf", "/etc/hosts", "missing.pdf", ".DS_Store", ""}) {
    CallFuture result;
    handler.FileAct(mojom::DownloadFileActInput::New(
                        mojom::DownloadFileActInputSource::kDownloads, junk,
                        mojom::DownloadFileActInputAction::kReveal,
                        std::nullopt, nullptr),
                    result.GetCallback());
    EXPECT_EQ(Code(result.Take()), mojom::ErrorCode::kNotFound) << junk;
  }
  // A media file is not reachable through the Downloads source.
  CallFuture crossed;
  handler.FileAct(mojom::DownloadFileActInput::New(
                      mojom::DownloadFileActInputSource::kDownloads, "Screenshot 1.png",
                      mojom::DownloadFileActInputAction::kOpen,
                      std::nullopt, nullptr),
                  crossed.GetCallback());
  EXPECT_EQ(Code(crossed.Take()), mojom::ErrorCode::kNotFound);

  SetLibraryFoldersForTesting(base::FilePath(), base::FilePath());
  base::ScopedAllowBlockingForTesting allow_blocking;
  EXPECT_TRUE(downloads.Delete());
  EXPECT_TRUE(media.Delete());
}

// A screenshot written into the watched folder (a temp folder, never the real one) becomes an arrival at
// once; what was there before, hidden temp files and non-media files never do.
IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest, CluScreenshotLandingShowsAsAnArrival) {
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(media.CreateUniqueTempDir());
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("Old shot.png"), "png"));
  }
  int changes = 0;
  CluScreenshotWatcher watcher(media.GetPath(),
                               base::BindLambdaForTesting([&] { ++changes; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return watcher.primed(); }));
  EXPECT_TRUE(watcher.arrivals().empty());
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII(".Screenshot temp.png"), "png"));
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("notes.txt"), "txt"));
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("Screenshot 2.png"), "png"));
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return !watcher.arrivals().empty(); }));
  ASSERT_EQ(watcher.arrivals().size(), 1u);
  EXPECT_EQ(watcher.arrivals()[0].name, "Screenshot 2.png");
  EXPECT_EQ(watcher.arrivals()[0].kind, "image");
  EXPECT_GE(changes, 1);

  base::ScopedAllowBlockingForTesting allow_blocking;
  EXPECT_TRUE(media.Delete());
}

// docs/chromium/PLAN.md M9 and docs/PLAN.md 12.12 box 2 on Chromium: permission prompts ---------------------------

class CluNativePermissionsBrowserTest : public CluNativeBrowserTest {
 protected:
  permissions::PermissionRequestManager* requests(content::WebContents* page) {
    return permissions::PermissionRequestManager::FromWebContents(page);
  }

  // Asks for notifications without waiting for the answer.
  void Ask(content::WebContents* page) {
    // The answer waits for the prompt, so the script must not wait for it.
    ASSERT_TRUE(content::ExecJs(
        page, "window.answer = Notification.requestPermission()",
        content::EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
  }

  // Waits for Chromium's prompt, checks it is its bubble on CluAnchorView.
  void WaitForPrompt(content::WebContents* page) {
    permissions::PermissionRequestObserver observer(page);
    if (!requests(page)->IsRequestInProgress()) {
      observer.Wait();
    }
    ASSERT_TRUE(base::test::RunUntil([&] {
      views::Widget* prompt =
          test::PermissionRequestManagerTestApi(browser()).GetPromptWindow();
      return prompt && prompt->IsVisible();
    }));
    ExpectAnchoredToTheCard(
        test::PermissionRequestManagerTestApi(browser()).GetPromptWindow());
  }

  ContentSetting Stored(const GURL& url) {
    return HostContentSettingsMapFactory::GetForProfile(profile())
        ->GetContentSetting(url, url, ContentSettingsType::NOTIFICATIONS);
  }
};

IN_PROC_BROWSER_TEST_F(CluNativePermissionsBrowserTest,
                       CluNativePermissionsPromptOnTheCardAndRememberAllow) {
  const GURL url = Url("capabilities/permissions.html");
  content::WebContents* page = Open(url);
  ASSERT_TRUE(page);
  EXPECT_EQ(Stored(url), CONTENT_SETTING_ASK);
  Ask(page);
  WaitForPrompt(page);
  requests(page)->Accept(PromptOptions(std::monostate()));
  EXPECT_EQ("granted", content::EvalJs(page, "window.answer"));
  EXPECT_EQ(Stored(url), CONTENT_SETTING_ALLOW);

  // Remembered: the next ask answers at once, and the permissions API agrees with the decision.
  permissions::PermissionRequestObserver again(page);
  EXPECT_EQ("granted",
            content::EvalJs(page, "Notification.requestPermission()"));
  EXPECT_FALSE(again.request_shown());
  EXPECT_EQ("granted",
            content::EvalJs(page, "navigator.permissions.query({name: "
                                  "'notifications'}).then(s => s.state)"));
}

IN_PROC_BROWSER_TEST_F(CluNativePermissionsBrowserTest,
                       CluNativePermissionsRememberBlock) {
  const GURL url = Url("capabilities/permissions.html");
  content::WebContents* page = Open(url);
  ASSERT_TRUE(page);
  Ask(page);
  WaitForPrompt(page);
  requests(page)->Deny(PromptOptions(std::monostate()));
  EXPECT_EQ("denied", content::EvalJs(page, "window.answer"));
  EXPECT_EQ(Stored(url), CONTENT_SETTING_BLOCK);

  permissions::PermissionRequestObserver again(page);
  EXPECT_EQ("denied", content::EvalJs(page, "Notification.requestPermission()"));
  EXPECT_FALSE(again.request_shown());
  EXPECT_EQ("denied",
            content::EvalJs(page, "navigator.permissions.query({name: "
                                  "'notifications'}).then(s => s.state)"));
}

IN_PROC_BROWSER_TEST_F(CluNativePermissionsBrowserTest,
                       CluNativePermissionsDismissDeniesOnceThenChromiumEmbargoes) {
  const GURL url = Url("capabilities/permissions.html");
  content::WebContents* page = Open(url);
  ASSERT_TRUE(page);
  // Closing the prompt denies this request only and stores nothing; Chromium
  // asks again, up to its embargo after three closes.
  for (int i = 0; i < 3; ++i) {
    Ask(page);
    WaitForPrompt(page);
    requests(page)->Dismiss(PromptOptions(std::monostate()));
    EXPECT_EQ("default", content::EvalJs(page, "window.answer")) << i;
    EXPECT_EQ(Stored(url), CONTENT_SETTING_ASK) << i;
  }
  permissions::PermissionRequestObserver embargoed(page);
  EXPECT_NE("granted",
            content::EvalJs(page, "Notification.requestPermission()"));
  EXPECT_FALSE(embargoed.request_shown());
}

// docs/chromium/fixes.md (m7.md and m8.md section 7): Site controls and Rio's settings read and write the decisions
// in Chromium's own site settings, the store its prompts and chrome://settings/content use.

class CluNativeSitePermissionsBrowserTest
    : public CluNativePermissionsBrowserTest {
 protected:
  HostContentSettingsMap* map() {
    return HostContentSettingsMapFactory::GetForProfile(profile());
  }

  mojom::CallResultPtr Set(const std::string& origin,
                           const std::string& permission,
                           mojom::PermissionSetInputDecision decision,
                           std::optional<std::string> profile_id = {}) {
    CluPermissionHandler handler(window().GetWeakPtr());
    CallFuture result;
    handler.Set(mojom::PermissionSetInput::New(
                    profile_id.value_or(window().state_profile_id()), origin,
                    permission, decision),
                result.GetCallback());
    return result.Take();
  }

  mojom::CallResultPtr Clear(std::optional<std::string> origin,
                             std::optional<std::string> permission) {
    CluPermissionHandler handler(window().GetWeakPtr());
    CallFuture result;
    handler.Clear(mojom::PermissionClearInput::New(window().state_profile_id(),
                                                   std::move(origin),
                                                   std::move(permission)),
                  result.GetCallback());
    return result.Take();
  }

  // permission.list's rows as "origin permission decision", for one origin or every site.
  std::vector<std::string> List(std::optional<std::string> origin) {
    CluPermissionHandler handler(window().GetWeakPtr());
    CallFuture result;
    handler.List(mojom::PermissionListInput::New(window().state_profile_id(),
                                                 std::move(origin)),
                 result.GetCallback());
    mojom::CallResultPtr listed = result.Take();
    std::vector<std::string> rows;
    EXPECT_TRUE(IsOk(listed));
    if (!IsOk(listed)) {
      return rows;
    }
    std::optional<base::Value> value =
        base::JSONReader::Read(listed->get_value_json(), base::JSON_PARSE_RFC);
    EXPECT_TRUE(value && value->is_list());
    if (!value || !value->is_list()) {
      return rows;
    }
    for (const base::Value& row : value->GetList()) {
      const base::DictValue& dict = row.GetDict();
      EXPECT_EQ(*dict.FindString("profileId"), window().state_profile_id());
      rows.push_back(base::StrCat({*dict.FindString("origin"), " ",
                                   *dict.FindString("permission"), " ",
                                   *dict.FindString("decision")}));
    }
    return rows;
  }

  // The rule chrome://settings/content lists for `url` (a prefs rule of the user), or none.
  std::optional<ContentSetting> SiteSettingsRule(const GURL& url,
                                                 ContentSettingsType type) {
    for (const ContentSettingPatternSource& rule :
         map()->GetSettingsForOneType(type)) {
      if (rule.source == content_settings::mojom::ProviderType::kPrefProvider &&
          rule.primary_pattern != ContentSettingsPattern::Wildcard() &&
          rule.primary_pattern.Matches(url)) {
        return rule.GetContentSetting();
      }
    }
    return std::nullopt;
  }
};

IN_PROC_BROWSER_TEST_F(CluNativeSitePermissionsBrowserTest,
                       PRE_CluNativeSitePermissionsAChangeSticksAndReachesChromium) {
  const GURL url = Url("capabilities/permissions.html");
  content::WebContents* page = Open(url);
  ASSERT_TRUE(page);
  const std::string origin = url::Origin::Create(url).Serialize();
  EXPECT_TRUE(List(origin).empty());

  // Site controls' Allow and Block land in Chromium's site settings.
  ASSERT_TRUE(IsOk(Set(origin, "notifications",
                       mojom::PermissionSetInputDecision::kAllow)));
  ASSERT_TRUE(
      IsOk(Set(origin, "camera", mojom::PermissionSetInputDecision::kBlock)));
  EXPECT_EQ(Stored(url), CONTENT_SETTING_ALLOW);
  EXPECT_EQ(SiteSettingsRule(url, ContentSettingsType::NOTIFICATIONS),
            CONTENT_SETTING_ALLOW);
  EXPECT_EQ(SiteSettingsRule(url, ContentSettingsType::MEDIASTREAM_CAMERA),
            CONTENT_SETTING_BLOCK);
  EXPECT_EQ(List(origin), (std::vector<std::string>{
                              origin + " camera deny",
                              origin + " notifications allow"}));
  // The page gets the answer from Chromium without a prompt.
  permissions::PermissionRequestObserver observer(page);
  EXPECT_EQ("granted",
            content::EvalJs(page, "Notification.requestPermission()"));
  EXPECT_FALSE(observer.request_shown());
}

IN_PROC_BROWSER_TEST_F(CluNativeSitePermissionsBrowserTest,
                       CluNativeSitePermissionsAChangeSticksAndReachesChromium) {
  // After a relaunch (the test server has a new port, so the old origin is another site now): Rio's settings list
  // both decisions from Chromium's store.
  std::vector<std::string> sticky;
  for (const std::string& row : List(std::nullopt)) {
    if (row.starts_with("http://localhost:")) {
      sticky.push_back(row.substr(row.find(' ') + 1));
    }
  }
  EXPECT_EQ(sticky, (std::vector<std::string>{"camera deny",
                                              "notifications allow"}));

  // A decision made in Chromium's prompt shows in Site controls, and Ask (clear) takes it back out of Chromium.
  const GURL url = Url("capabilities/permissions.html");
  const std::string origin = url::Origin::Create(url).Serialize();
  content::WebContents* page = Open(url);
  ASSERT_TRUE(page);
  Ask(page);
  WaitForPrompt(page);
  requests(page)->Deny(PromptOptions(std::monostate()));
  EXPECT_EQ("denied", content::EvalJs(page, "window.answer"));
  EXPECT_EQ(List(origin),
            (std::vector<std::string>{origin + " notifications deny"}));
  ASSERT_TRUE(IsOk(Clear(origin, "notifications")));
  EXPECT_EQ(Stored(url), CONTENT_SETTING_ASK);
  EXPECT_EQ(SiteSettingsRule(url, ContentSettingsType::NOTIFICATIONS),
            std::nullopt);
  EXPECT_TRUE(List(origin).empty());
  EXPECT_EQ("prompt",
            content::EvalJs(page, "navigator.permissions.query({name: "
                                  "'notifications'}).then(s => s.state)"));

  // Reset everything clears the old site's decisions too.
  ASSERT_TRUE(IsOk(Clear(std::nullopt, std::nullopt)));
  EXPECT_TRUE(List(std::nullopt).empty());
}

IN_PROC_BROWSER_TEST_F(CluNativeSitePermissionsBrowserTest,
                       CluNativeSitePermissionsRefuseJunk) {
  const std::string origin =
      url::Origin::Create(Url("capabilities/permissions.html")).Serialize();
  // A page with no web origin has nothing to list.
  EXPECT_TRUE(List("chrome://settings/content").empty());
  EXPECT_TRUE(List("file:///etc/hosts").empty());
  for (const char* junk : {"file:///etc/hosts", "javascript:alert(1)",
                           "chrome://clu-frame/", "not a url"}) {
    EXPECT_EQ(Code(Set(junk, "camera", mojom::PermissionSetInputDecision::kAllow)),
              mojom::ErrorCode::kBadPayload)
        << junk;
    EXPECT_EQ(Code(Clear(std::string(junk), std::nullopt)),
              mojom::ErrorCode::kBadPayload)
        << junk;
  }
  EXPECT_EQ(Code(Set(origin, "junk", mojom::PermissionSetInputDecision::kAllow)),
            mojom::ErrorCode::kBadPayload);
  EXPECT_EQ(Code(Clear(origin, "Camera")), mojom::ErrorCode::kBadPayload);
  EXPECT_EQ(Code(Set(origin, "camera", mojom::PermissionSetInputDecision::kAllow,
                     "6f1c3a52-9a51-4d8f-9a9e-0c7a2b4e5d61")),
            mojom::ErrorCode::kNotFound);
  // Nothing was stored.
  EXPECT_TRUE(List(std::nullopt).empty());
}

// docs/chromium/PLAN.md M9: Safe Browsing ------------------------------------------------------------------------

// Safe Browsing's verdicts from a fake database, so the interstitial runs without Google's servers.
class CluNativeSafeBrowsingBrowserTest : public CluNativeBrowserTest {
 protected:
  void CreatedBrowserMainParts(content::BrowserMainParts* parts) override {
    CluNativeBrowserTest::CreatedBrowserMainParts(parts);
    factory_.SetTestUIManager(new safe_browsing::TestSafeBrowsingUIManager());
    factory_.SetTestDatabaseManager(
        base::MakeRefCounted<safe_browsing::FakeSafeBrowsingDatabaseManager>(
            content::GetUIThreadTaskRunner({})));
    safe_browsing::SafeBrowsingService::RegisterFactory(&factory_);
  }

  void TearDown() override {
    CluNativeBrowserTest::TearDown();
    safe_browsing::SafeBrowsingService::RegisterFactory(nullptr);
  }

  void MarkDangerous(const GURL& url) {
    static_cast<safe_browsing::FakeSafeBrowsingDatabaseManager*>(
        factory_.test_safe_browsing_service()->database_manager().get())
        ->AddDangerousUrl(url, safe_browsing::SBThreatType::SB_THREAT_TYPE_URL_PHISHING);
  }

  safe_browsing::TestSafeBrowsingServiceFactory factory_;
};

IN_PROC_BROWSER_TEST_F(CluNativeSafeBrowsingBrowserTest,
                       CluNativeSafeBrowsingBlocksADangerousPageInTheCard) {
  const GURL safe = Url("tabs/a.html");
  const GURL dangerous = Url("capabilities/login.html");
  MarkDangerous(dangerous);
  content::WebContents* page = Open(safe);
  ASSERT_TRUE(page);
  const size_t rows_before = window().CurrentState().FindDict("tabs")->size();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), dangerous));
  ASSERT_EQ(Shown(), page);
  ASSERT_TRUE(chrome_browser_interstitials::IsShowingInterstitial(page));
  security_interstitials::SecurityInterstitialPage* interstitial =
      security_interstitials::SecurityInterstitialTabHelper::FromWebContents(page)
          ->GetBlockingPageForCurrentlyCommittedNavigationForTesting();
  ASSERT_TRUE(interstitial);
  EXPECT_EQ(interstitial->GetTypeForTesting(),
            safe_browsing::SafeBrowsingBlockingPage::kTypeForTesting);
  // The warning is the row's page, in the card: no new row, no new window.
  EXPECT_EQ(window().CurrentState().FindDict("tabs")->size(), rows_before);

  // "Back to safety" returns to the page before.
  content::TestNavigationObserver back(page);
  interstitial->CommandReceived(
      base::NumberToString(security_interstitials::CMD_DONT_PROCEED));
  back.Wait();
  EXPECT_EQ(page->GetLastCommittedURL(), safe);
  EXPECT_FALSE(chrome_browser_interstitials::IsShowingInterstitial(page));
}

// The real service, with no fake database. The list update goes to the test server, which records the lists it
// asks for: only the public ones a Safe Browsing key outside Google Chrome gets, since one Chrome-only list fails the
// whole update with 404. A local page marked as phishing through Chromium's --mark_as_phishing hook shows the
// interstitial from the real list database.
class CluNativeSafeBrowsingListsBrowserTest : public CluNativeBrowserTest {
 protected:
  CluNativeSafeBrowsingListsBrowserTest() {
    // What Rio's builds get from Chromium's field trial testing config.
    features_.InitAndEnableFeature(safe_browsing::kLocalListsUseSBv5);
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    CluNativeBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(safe_browsing::switches::kMarkAsPhishing,
                                    "http://localhost/capabilities/login.html");
  }

  void SetUpOnMainThread() override {
    CluNativeBrowserTest::SetUpOnMainThread();
    lists_server_.RegisterRequestHandler(base::BindRepeating(
        &CluNativeSafeBrowsingListsBrowserTest::HandleLists,
        base::Unretained(this)));
    ASSERT_TRUE(lists_server_.Start());
    lists_prefix_ = lists_server_.GetURL("/v5").spec();
    safe_browsing::SetSbV5UrlPrefixForTesting(lists_prefix_.c_str());
  }

  void TearDownOnMainThread() override {
    safe_browsing::SetSbV5UrlPrefixForTesting(nullptr);
    CluNativeBrowserTest::TearDownOnMainThread();
  }

  // Runs on the server's thread: keeps the decoded request of each list update, answers with no lists.
  std::unique_ptr<net::test_server::HttpResponse> HandleLists(
      const net::test_server::HttpRequest& request) {
    if (!request.GetURL().path().ends_with("/hashLists:batchGet")) {
      return nullptr;
    }
    std::string req;
    std::string decoded;
    if (net::GetValueForKeyInQuery(request.GetURL(), "$req", &req) &&
        base::Base64UrlDecode(req, base::Base64UrlDecodePolicy::IGNORE_PADDING,
                              &decoded)) {
      base::AutoLock lock(lock_);
      update_requests_.push_back(decoded);
    }
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_code(net::HTTP_OK);
    response->set_content_type("application/x-protobuf");
    return response;
  }

  std::vector<std::string> UpdateRequests() {
    base::AutoLock lock(lock_);
    return update_requests_;
  }

  base::test::ScopedFeatureList features_;
  net::EmbeddedTestServer lists_server_;
  std::string lists_prefix_;
  base::Lock lock_;
  std::vector<std::string> update_requests_ GUARDED_BY(lock_);
};

IN_PROC_BROWSER_TEST_F(CluNativeSafeBrowsingListsBrowserTest,
                       CluNativeSafeBrowsingAsksOnlyForThePublicListsEarly) {
  const PrefService& prefs = *browser()->GetProfile()->GetPrefs();
  EXPECT_EQ(safe_browsing::GetSafeBrowsingState(prefs),
            safe_browsing::SafeBrowsingState::STANDARD_PROTECTION);
  safe_browsing::SafeBrowsingService* service =
      g_browser_process->safe_browsing_service();
  ASSERT_TRUE(service);
  EXPECT_TRUE(service->enabled_by_prefs());
  // Rio asks for the first update 10 to 20 seconds in, not Chromium's 1 to 5 minutes.
  EXPECT_TRUE(base::CommandLine::ForCurrentProcess()->HasSwitch(
      safe_browsing::switches::kSbFastInitialListsUpdate));
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(60));
  ASSERT_TRUE(base::test::RunUntil([&] { return !UpdateRequests().empty(); }));
  const std::string request = UpdateRequests().front();
  for (const char* list : {"se-4b", "mw-4b", "uws-4b"}) {
    EXPECT_NE(request.find(list), std::string::npos) << list;
  }
  for (const char* list : {"mwb-4b", "mcrx-16b", "bi-4b", "csdda-32b",
                           "csda-32b", "gc-32b", "srf-4b", "sus-4b"}) {
    EXPECT_EQ(request.find(list), std::string::npos) << list;
  }
}

IN_PROC_BROWSER_TEST_F(CluNativeSafeBrowsingListsBrowserTest,
                       CluNativeSafeBrowsingWarnsOnAPageTheListsMark) {
  safe_browsing::SafeBrowsingService* service =
      g_browser_process->safe_browsing_service();
  ASSERT_TRUE(service && service->database_manager());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service->database_manager()->IsDatabaseReady(); }));
  content::WebContents* page = Open(Url("tabs/a.html"));
  ASSERT_TRUE(page);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           Url("capabilities/login.html")));
  ASSERT_TRUE(chrome_browser_interstitials::IsShowingInterstitial(page));
  security_interstitials::SecurityInterstitialPage* interstitial =
      security_interstitials::SecurityInterstitialTabHelper::FromWebContents(page)
          ->GetBlockingPageForCurrentlyCommittedNavigationForTesting();
  ASSERT_TRUE(interstitial);
  EXPECT_EQ(interstitial->GetTypeForTesting(),
            safe_browsing::SafeBrowsingBlockingPage::kTypeForTesting);
}

// Google's own test page against Google's servers. It runs only with a Safe Browsing key in the build
// (~/.clu/keys.gn) or in GOOGLE_API_KEY, which non-branded builds read at startup (docs/chromium/m9.md section 3).
class CluNativeSafeBrowsingNetworkBrowserTest : public CluNativeBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    CluNativeBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(
        safe_browsing::switches::kSbFastInitialListsUpdate);
  }

  void SetUpOnMainThread() override {
    CluNativeBrowserTest::SetUpOnMainThread();
    for (const char* host :
         {"testsafebrowsing.appspot.com", "safebrowsing.googleapis.com",
          "*.googleapis.com", "*.gstatic.com"}) {
      host_resolver()->AllowDirectLookup(host);
    }
  }
};

IN_PROC_BROWSER_TEST_F(CluNativeSafeBrowsingNetworkBrowserTest,
                       CluNativeSafeBrowsingBlocksGooglesTestPage) {
  if (!google_apis::HasAPIKeyConfigured()) {
    GTEST_SKIP() << "no Google API key: see docs/chromium/m9.md section 3";
  }
  content::WebContents* page = Open(Url("tabs/a.html"));
  ASSERT_TRUE(page);
  const GURL phishing("https://testsafebrowsing.appspot.com/s/phishing.html");
  // The first list update can take a while on a fresh profile; navigate again until the lists know the page.
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Minutes(3));
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (chrome_browser_interstitials::IsShowingInterstitial(page)) {
      return true;
    }
    std::ignore = ui_test_utils::NavigateToURL(browser(), phishing);
    return chrome_browser_interstitials::IsShowingInterstitial(page);
  }));
  security_interstitials::SecurityInterstitialPage* interstitial =
      security_interstitials::SecurityInterstitialTabHelper::FromWebContents(page)
          ->GetBlockingPageForCurrentlyCommittedNavigationForTesting();
  ASSERT_TRUE(interstitial);
  EXPECT_EQ(interstitial->GetTypeForTesting(),
            safe_browsing::SafeBrowsingBlockingPage::kTypeForTesting);
}

// docs/chromium/PLAN.md M9 and docs/PLAN.md 12.12 box 3 on Chromium: passkeys -----------------------------------

class CluNativePasskeyBrowserTest : public CluNativeBrowserTest,
                                    public ChromeAuthenticatorRequestDelegate::TestObserver {
 protected:
  void SetUpOnMainThread() override {
    CluNativeBrowserTest::SetUpOnMainThread();
    // A virtual security key that holds passkeys and verifies the user, so no real device, account or iCloud
    // Keychain is touched (a test override also turns off the platform authenticators).
    auto device = std::make_unique<device::test::VirtualFidoDeviceFactory>();
    device->mutable_state()->fingerprints_enrolled = true;
    device::VirtualCtap2Device::Config config;
    config.resident_key_support = true;
    config.internal_uv_support = true;
    device->SetCtap2Config(config);
    environment_ =
        std::make_unique<content::ScopedAuthenticatorEnvironmentForTesting>(
            std::move(device));
    ChromeAuthenticatorRequestDelegate::SetGlobalObserverForTesting(this);
  }

  void TearDownOnMainThread() override {
    ChromeAuthenticatorRequestDelegate::SetGlobalObserverForTesting(nullptr);
    environment_.reset();
    CluNativeBrowserTest::TearDownOnMainThread();
  }

  // ChromeAuthenticatorRequestDelegate::TestObserver:
  void UIShown(ChromeAuthenticatorRequestDelegate* delegate) override {
    shown_ = delegate->AsWeakPtr();
  }

  // The newest result the fixture recorded, as "kind:ok:type" or "kind:fail:name".
  std::string LastResult(content::WebContents* page) {
    return content::EvalJs(page, R"(
        new Promise(resolve => {
          const check = () => {
            const r = window.results[window.results.length - 1];
            if (r) {
              resolve(`${r.kind}:${r.ok ? 'ok' : 'fail'}:${r.ok ? r.type : r.name}`);
            } else {
              setTimeout(check, 20);
            }
          };
          check();
        }))")
        .ExtractString();
  }

  std::unique_ptr<content::ScopedAuthenticatorEnvironmentForTesting>
      environment_;
  base::WeakPtr<ChromeAuthenticatorRequestDelegate> shown_;
};

IN_PROC_BROWSER_TEST_F(CluNativePasskeyBrowserTest,
                       CluNativePasskeysCreateAndSignInOnTheFixture) {
  content::WebContents* page = Open(Url("capabilities/passkey.html"));
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::ExecJs(page, "window.createPasskey()"));
  EXPECT_EQ(LastResult(page), "create:ok:public-key");
  ASSERT_TRUE(content::ExecJs(page, "window.results = []; window.getPasskey()"));
  EXPECT_EQ(LastResult(page), "get:ok:public-key");
}

IN_PROC_BROWSER_TEST_F(CluNativePasskeyBrowserTest,
                       CluNativePasskeysAFailedRequestLeavesTheSiteUsable) {
  content::WebContents* page = Open(Url("capabilities/passkey.html"));
  ASSERT_TRUE(page);
  // A credential the key does not hold: Chromium's dialog says so, and closing it rejects the request.
  // The request waits for the dialog, so the script must not wait for it.
  ASSERT_TRUE(content::ExecJs(page, "window.getUnknownPasskey()",
                              content::EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
  ASSERT_TRUE(base::test::RunUntil([&] { return !!shown_; }));
  shown_->dialog_model()->CancelAuthenticatorRequest();
  EXPECT_EQ(LastResult(page), "unknown:fail:NotAllowedError");

  // The password form still signs in.
  ASSERT_TRUE(content::ExecJs(page, R"(
      document.getElementById('user').value = 'ada@example.com';
      document.getElementById('pass').value = 'pw';
      document.querySelector('#password button').click();)"));
  EXPECT_EQ("Signed in as ada@example.com with a password",
            content::EvalJs(page, "document.getElementById('status').textContent"));
}

// docs/PLAN.md 12.12 box 4 on Chromium: Memory Saver --------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativeMemorySaverIsOnAndKeepsClusExemptionsLive) {
  using resource_coordinator::TabLifecycleUnitExternal;
  // On by default; a user value still wins.
  auto* tuning =
      performance_manager::user_tuning::UserPerformanceTuningManager::GetInstance();
  ASSERT_TRUE(tuning);
  ASSERT_TRUE(base::test::RunUntil([&] { return tuning->IsMemorySaverModeActive(); }));
  ASSERT_TRUE(CluKeepLive::Get());

  // A plain background tab of a Clu window stays auto-discardable (Chromium's to decide); a discard keeps its row.
  content::WebContents* background = Open(Url("capabilities/hibernate.html?n=1"));
  ASSERT_TRUE(background);
  content::WebContents* front = Open(Url("capabilities/hibernate.html?n=2"));
  ASSERT_TRUE(front);
  ASSERT_TRUE(base::test::RunUntil([&] {
    TabLifecycleUnitExternal* unit =
        TabLifecycleUnitExternal::FromWebContents(background);
    return unit && unit->IsAutoDiscardable();
  }));
  EXPECT_FALSE(CluKeepLive::Get()->IsHeld(background));
  const size_t rows = window().CurrentState().FindDict("tabs")->size();
  const int index = strip().GetIndexOfWebContents(background);
  ASSERT_NE(index, TabStripModel::kNoTab);
  // An external discard, which Chromium's policy always allows: Memory Saver's own timer waits for hours and skips
  // recently seen tabs, so it cannot run inside a test. The row must survive whatever discards the page.
  ASSERT_TRUE(TabLifecycleUnitExternal::FromWebContents(background)->DiscardTab(
      ::mojom::LifecycleUnitDiscardReason::EXTERNAL));
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::WebContents* now = strip().GetWebContentsAt(index);
    return now && now->WasDiscarded();
  }));
  EXPECT_EQ(window().CurrentState().FindDict("tabs")->size(), rows);

  // An extension's own choice is left alone.
  TabLifecycleUnitExternal* front_unit =
      TabLifecycleUnitExternal::FromWebContents(front);
  ASSERT_TRUE(front_unit);
  front_unit->SetAutoDiscardable(false);

  // A popup window's page is held live.
  ASSERT_TRUE(content::ExecJs(
      front, content::JsReplace("window.open($1, '_blank', 'popup,width=420,height=320')",
                                Url("tabs/b.html"))));
  BrowserWindowInterface* popup = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* each) {
          if (each->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
            popup = each;
          }
          return true;
        });
    return popup && popup->GetTabStripModel()->GetActiveWebContents();
  }));
  content::WebContents* popup_page =
      popup->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(base::test::RunUntil([&] {
    TabLifecycleUnitExternal* unit =
        TabLifecycleUnitExternal::FromWebContents(popup_page);
    return CluKeepLive::Get()->IsHeld(popup_page) && unit &&
           !unit->IsAutoDiscardable();
  }));
  EXPECT_FALSE(front_unit->IsAutoDiscardable());
  EXPECT_FALSE(CluKeepLive::Get()->IsHeld(front));
}

// docs/PLAN.md 8: Site Permissions… --------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluNativeBrowserTest,
                       CluNativeSitePermissionsOpensChromiumsSiteSettings) {
  content::WebContents* page = Open(Url("tabs/a.html"));
  ASSERT_TRUE(page);
  ASSERT_TRUE(window().CanRunCommand("permissions.openSite"));
  ASSERT_TRUE(window().RunCommand("permissions.openSite"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::WebContents* shown = Shown();
    return shown && shown->GetVisibleURL().host() == "settings" &&
           shown->GetVisibleURL().path() == "/content/siteDetails";
  }));
  // Chromium's own pages have no site settings to show.
  EXPECT_FALSE(window().CanRunCommand("permissions.openSite"));
}

}  // namespace
}  // namespace clu
