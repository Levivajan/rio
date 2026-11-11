// Copyright 2026 The Rio Authors. All rights reserved.

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_platform_handler.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/browser/updater/clu_update_service.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"

// The CluUpdate* checks of the M12 "Done when" row (docs/chromium/PLAN.md section 6, docs/chromium/m12.md): what Clu
// does with Sparkle's events, in the real frame WebUI and command path. Sparkle itself is not in a test build (the
// package adds it), so these drive CluUpdateService through a fake driver; scripts/chromium/update-smoke.sh proves
// the real Sparkle update from one package to a newer one through a feed on 127.0.0.1.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

constexpr char kRelaunch[] = "[data-update-relaunch]";
constexpr char kDismiss[] = "[data-update-dismiss]";

class FakeUpdateDriver : public CluUpdateDriver {
 public:
  struct Log {
    int checks = 0;
    int installs = 0;
    // Every SetAutomatic() value, in order.
    std::vector<bool> automatic;
    raw_ptr<Client> client = nullptr;
  };

  explicit FakeUpdateDriver(Log* log) : log_(log) {}

  bool Start(Client* client) override {
    log_->client = client;
    return true;
  }
  void CheckNow() override { ++log_->checks; }
  bool InstallNow() override {
    ++log_->installs;
    return true;
  }
  void SetAutomatic(bool automatic) override {
    log_->automatic.push_back(automatic);
  }

 private:
  raw_ptr<Log> log_;
};

class CluUpdateBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    // The window posts the updater's start; let it run with the test build's (absent) Sparkle first.
    base::RunLoop().RunUntilIdle();
  }

  void TearDownOnMainThread() override {
    CluAppSettings::Get()->SetGlobal(kAutoUpdateKey, base::Value(true));
    CluUpdateService::Get().ResetForTesting();
    g_browser_process->local_state()->ClearPref(kCluUpdateDismissedVersionPref);
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  content::WebContents* frame() { return shell().frame_contents(); }

  void UseFakeDriver() {
    CluUpdateService::Get().SetDriverForTesting(
        std::make_unique<FakeUpdateDriver>(&log_));
    ASSERT_TRUE(log_.client);
  }

  base::DictValue Update(CluWindowState& state) {
    const base::DictValue current = state.CurrentState();
    const base::DictValue* update = current.FindDict("app")->FindDict("update");
    return update ? update->Clone() : base::DictValue();
  }

  std::string Status() {
    const base::DictValue update = Update(window());
    const std::string* status = update.FindString("status");
    return status ? *status : std::string();
  }

  // The newest toast's text, or empty. Toasts expire, so tests look at the newest right after each event.
  std::string LastToast() {
    const std::vector<std::string> toasts = Toasts();
    return toasts.empty() ? std::string() : toasts.back();
  }

  std::vector<std::string> Toasts() {
    std::vector<std::string> texts;
    const base::DictValue state = window().CurrentState();
    const base::ListValue* toasts = state.FindDict("windows")
                                        ->FindDict(window().window_id())
                                        ->FindList("toasts");
    for (const base::Value& toast : *toasts) {
      texts.push_back(*toast.GetDict().FindString("text"));
    }
    return texts;
  }

  bool FrameShowsRelaunch() {
    return content::EvalJs(frame(), content::JsReplace(
                                        "!!document.querySelector($1)",
                                        kRelaunch))
        .ExtractBool();
  }

  FakeUpdateDriver::Log log_;
};

// A test build has no Sparkle and no feed: no updater, no menu item or palette row, no button, and the bridge says so.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateABuildWithoutAFeedHasNoUpdater) {
  EXPECT_EQ("unavailable", Status());
  EXPECT_FALSE(CluUpdateService::Get().available());
  EXPECT_FALSE(window().CanRunCommand("platform.checkForUpdates"));
  EXPECT_FALSE(window().RunCommand("platform.checkForUpdates"));

  CluPlatformHandler handler(window().GetWeakPtr());
  CallFuture checked;
  handler.CheckForUpdates(checked.GetCallback());
  ASSERT_TRUE(checked.Get()->is_value_json());
  EXPECT_EQ(R"({"status":"unavailable"})", checked.Get()->get_value_json());
  CallFuture installed;
  handler.InstallUpdate(installed.GetCallback());
  EXPECT_EQ(R"({"status":"not-ready"})", installed.Get()->get_value_json());
  EXPECT_FALSE(FrameShowsRelaunch());
}

// A scheduled check finds an update, downloads it quietly (no toast) and every window offers Relaunch; clicking it in
// the sidebar installs through the updater.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateAReadyUpdateShowsRelaunchAndInstalls) {
  UseFakeDriver();
  EXPECT_EQ("idle", Status());
  EXPECT_TRUE(window().CanRunCommand("platform.checkForUpdates"));
  BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(CluWindowState::GetOrCreate(second));

  log_.client->OnCheckStarted();
  log_.client->OnUpdateFound("158.0.8127.19");
  EXPECT_EQ("downloading", Status());
  EXPECT_FALSE(FrameShowsRelaunch());
  log_.client->OnUpdateReady("158.0.8127.19");
  log_.client->OnCycleFinished();
  window().Flush();

  EXPECT_EQ("ready", Status());
  EXPECT_EQ("158.0.8127.19", *Update(window()).FindString("version"));
  EXPECT_EQ(Update(window()), Update(*CluWindowState::GetOrCreate(second)));
  EXPECT_TRUE(Toasts().empty());
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector, kRelaunch)));
  EXPECT_EQ("Restart to update",
            content::EvalJs(frame(), content::JsReplace(
                                         "document.querySelector($1)"
                                         ".textContent.trim()",
                                         kRelaunch)));

  EXPECT_TRUE(content::ExecJs(
      frame(), content::JsReplace("document.querySelector($1).click()",
                                  kRelaunch)));
  EXPECT_TRUE(base::test::RunUntil([&] { return log_.installs == 1; }));
  // A check now starts nothing: the update is ready.
  EXPECT_TRUE(window().RunCommand("platform.checkForUpdates"));
  EXPECT_EQ(0, log_.checks);
}

// The sidebar card's close button hides it for this version, stores the version in Local State, and the
// update still installs (on quit, or through the updater's install). A newer ready version shows the card again.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateClosingTheCardHidesItForThisVersionOnly) {
  UseFakeDriver();
  log_.client->OnCheckStarted();
  log_.client->OnUpdateReady("158.0.8127.19");
  log_.client->OnCycleFinished();
  window().Flush();
  EXPECT_EQ(false, *Update(window()).FindBool("dismissed"));
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector, kDismiss)));

  EXPECT_TRUE(content::ExecJs(
      frame(), content::JsReplace("document.querySelector($1).click()",
                                  kDismiss)));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return CluUpdateService::Get().Snapshot().dismissed;
  }));
  window().Flush();
  EXPECT_EQ(true, *Update(window()).FindBool("dismissed"));
  EXPECT_EQ("ready", Status());
  EXPECT_EQ("158.0.8127.19", g_browser_process->local_state()->GetString(
                                 kCluUpdateDismissedVersionPref));
  EXPECT_TRUE(base::test::RunUntil([&] { return !FrameShowsRelaunch(); }));
  // Still installs: Sparkle keeps its quit install, and the updater's install still runs.
  EXPECT_TRUE(CluUpdateService::Get().Install());
  EXPECT_EQ(1, log_.installs);

  log_.client->OnUpdateReady("158.0.8127.20");
  window().Flush();
  EXPECT_EQ(false, *Update(window()).FindBool("dismissed"));
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector, kRelaunch)));
}

// A dismissal stored by an earlier run hides the card for the same version; the bridge answers not-ready when
// nothing is ready.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateAStoredDismissalHidesTheSameVersion) {
  UseFakeDriver();
  CluPlatformHandler handler(window().GetWeakPtr());
  CallFuture early;
  handler.DismissUpdate(early.GetCallback());
  EXPECT_EQ(R"({"status":"not-ready"})", early.Get()->get_value_json());

  g_browser_process->local_state()->SetString(kCluUpdateDismissedVersionPref,
                                              "158.0.8127.19");
  EnsureUpdatesStarted();
  log_.client->OnCheckStarted();
  log_.client->OnUpdateReady("158.0.8127.19");
  window().Flush();
  EXPECT_EQ(true, *Update(window()).FindBool("dismissed"));
  EXPECT_FALSE(FrameShowsRelaunch());

  CallFuture again;
  handler.DismissUpdate(again.GetCallback());
  EXPECT_EQ(R"({"status":"dismissed"})", again.Get()->get_value_json());
}

// Check for Updates… from the menu or the palette asks the updater once and toasts what it found.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateTheMenuCheckToastsItsResult) {
  UseFakeDriver();
  EXPECT_TRUE(window().RunCommand("platform.checkForUpdates"));
  EXPECT_EQ(1, log_.checks);
  EXPECT_EQ("checking", Status());
  log_.client->OnCheckStarted();
  log_.client->OnUpdateNotFound();
  log_.client->OnCycleFinished();
  EXPECT_EQ("up-to-date", Status());
  EXPECT_EQ("Rio is up to date", LastToast());

  // A failure toasts for the user's check only.
  const size_t before = Toasts().size();
  log_.client->OnCheckStarted();
  log_.client->OnUpdateError("feed unreachable");
  EXPECT_LE(Toasts().size(), before);
  EXPECT_NE("Couldn't check for updates", LastToast());
  EXPECT_TRUE(window().RunCommand("platform.checkForUpdates"));
  log_.client->OnUpdateError("feed unreachable");
  EXPECT_EQ("error", Status());
  EXPECT_EQ("Couldn't check for updates", LastToast());
}

// General, "Automatically update Rio": on by default, and the switch turns Sparkle's scheduled checks and silent
// downloads off and on at once; "Check for Updates…" still checks with it off.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateTheGeneralSwitchDrivesAutomaticUpdates) {
  EXPECT_TRUE(CluAppSettings::Get()->GetBool(kAutoUpdateKey));
  UseFakeDriver();
  EXPECT_EQ(std::vector<bool>({true}), log_.automatic);

  CluAppSettings::Get()->SetGlobal(kAutoUpdateKey, base::Value(false));
  EXPECT_FALSE(CluUpdateService::Get().automatic());
  EXPECT_EQ(std::vector<bool>({true, false}), log_.automatic);
  EXPECT_TRUE(window().RunCommand("platform.checkForUpdates"));
  EXPECT_EQ(1, log_.checks);

  // The updater started later (the next launch) gets the stored value.
  log_ = FakeUpdateDriver::Log();
  UseFakeDriver();
  EXPECT_EQ(std::vector<bool>({false}), log_.automatic);

  CluAppSettings::Get()->SetGlobal(kAutoUpdateKey, base::Value(true));
  EXPECT_EQ(std::vector<bool>({false, true}), log_.automatic);
}

// With "Automatically update Rio" off, opening About starts no check, so nothing downloads or installs on quit; it
// shows what is known. "Check for Updates…" still checks, and with the switch on again About checks too.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateAboutNeverChecksWithAutomaticUpdatesOff) {
  UseFakeDriver();
  CluAppSettings::Get()->SetGlobal(kAutoUpdateKey, base::Value(false));
  std::vector<std::string> states;
  auto report = base::BindRepeating(
      [](std::vector<std::string>* out, const std::string& state,
         const std::string&) { out->push_back(state); },
      &states);
  ASSERT_TRUE(CheckForUpdatesFromAbout(report));
  EXPECT_EQ(0, log_.checks);
  EXPECT_EQ(states, (std::vector<std::string>{"disabled"}));
  EXPECT_NE("checking", Status());

  EXPECT_TRUE(window().RunCommand("platform.checkForUpdates"));
  EXPECT_EQ(1, log_.checks);
  log_.client->OnUpdateNotFound();
  log_.client->OnCycleFinished();
  states.clear();
  ASSERT_TRUE(CheckForUpdatesFromAbout(report));
  EXPECT_EQ(1, log_.checks);
  EXPECT_EQ(states, (std::vector<std::string>{"updated"}));

  CluAppSettings::Get()->SetGlobal(kAutoUpdateKey, base::Value(true));
  ASSERT_TRUE(CheckForUpdatesFromAbout(report));
  EXPECT_EQ(2, log_.checks);
}

// Starting Clu, its updater and an update cycle leave the default browser as it was (docs/PLAN.md 12.16).
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest,
                       CluUpdateNeverChangesTheDefaultBrowser) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const shell_integration::DefaultWebClientState before =
      shell_integration::GetDefaultBrowser();
  UseFakeDriver();
  EXPECT_TRUE(window().RunCommand("platform.checkForUpdates"));
  log_.client->OnUpdateFound("158.0.8127.19");
  log_.client->OnUpdateReady("158.0.8127.19");
  log_.client->OnCycleFinished();
  EXPECT_EQ(before, shell_integration::GetDefaultBrowser());
}

// An update of a self-signed build gives the Safe Storage entry a new per-build partition list, so macOS asks for the
// keychain once; the window publishes nothing about that list, and the sidebar shows no card offering to stop it.
IN_PROC_BROWSER_TEST_F(CluUpdateBrowserTest, CluUpdateShowsNoKeychainCard) {
  UseFakeDriver();
  log_.client->OnCheckStarted();
  log_.client->OnUpdateReady("158.0.8127.19");
  log_.client->OnCycleFinished();
  window().Flush();
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector, kRelaunch)));

  const base::DictValue state = window().CurrentState();
  EXPECT_FALSE(state.FindDict("app")->contains("keychainTrust"));
  EXPECT_EQ(false, content::EvalJs(frame(), content::JsReplace(
                                                "!!document.querySelector($1)",
                                                "[data-keychain]")));
  EXPECT_EQ(false,
            content::EvalJs(frame(), content::JsReplace(
                                         "document.body.innerText.includes($1)",
                                         "Stop the keychain prompt")));
}

}  // namespace
}  // namespace clu
