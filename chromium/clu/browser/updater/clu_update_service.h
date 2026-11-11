// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UPDATER_CLU_UPDATE_SERVICE_H_
#define CLU_BROWSER_UPDATER_CLU_UPDATE_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "clu/browser/updater/clu_update_rules.h"

namespace base {
class Clock;
}  // namespace base

namespace clu {

// What runs the updates: Sparkle on Mac (clu_sparkle_update_driver_mac.mm), a fake in tests.
class CluUpdateDriver {
 public:
  // Events, on the UI thread. A check Sparkle starts on its own schedule reports OnCheckStarted() too.
  class Client {
   public:
    virtual void OnCheckStarted() = 0;
    virtual void OnUpdateFound(std::string_view version) = 0;
    virtual void OnUpdateNotFound() = 0;
    // Downloaded, verified and waiting: it installs when Clu quits, or now through InstallNow().
    virtual void OnUpdateReady(std::string_view version) = 0;
    virtual void OnUpdateError(std::string_view message) = 0;
    virtual void OnCycleFinished() = 0;

   protected:
    virtual ~Client() = default;
  };

  virtual ~CluUpdateDriver() = default;

  // Starts the updater; false when this build cannot update (no Sparkle, no feed, a feed the rules refuse).
  virtual bool Start(Client* client) = 0;
  // A check now, without the updater's own windows: its outcome comes back as events.
  virtual void CheckNow() = 0;
  // Installs the ready update and relaunches Clu. False when none is ready.
  virtual bool InstallNow() = 0;
  // Whether the updater checks and downloads on its own schedule (General, "Automatically update Rio"). A check the
  // user asks for runs either way. Called after a successful Start().
  virtual void SetAutomatic(bool automatic) {}
  // Whether an update found by the next checks downloads silently and ends ready. On for a check the user asks for
  // while automatic updates are off, back to the setting when that cycle finishes.
  virtual void SetDownloads(bool downloads) {}
};

// Sparkle when the app bundle carries it, else null.
std::unique_ptr<CluUpdateDriver> CreatePlatformUpdateDriver();

// The browser's one updater (docs/chromium/m12.md): app.update for every window, the "Check for Updates…" command and
// Relaunch. It starts with the first Clu window and lives until the browser process ends. UI thread only.
class CluUpdateService : public CluUpdateDriver::Client {
 public:
  using ToastCallback = base::RepeatingCallback<void(const std::string&)>;
  // What About Rio shows: "checking", "updating", "nearly-updated", "updated" or "disabled", and the version found.
  using AboutCallback =
      base::RepeatingCallback<void(const std::string& state,
                                   const std::string& version)>;

  static CluUpdateService& Get();

  CluUpdateService(const CluUpdateService&) = delete;
  CluUpdateService& operator=(const CluUpdateService&) = delete;

  // Starts the platform driver once. Later calls do nothing.
  void EnsureStarted();

  // A check the user asked for. Its toasts (the outcome now, the result later) go to `toast`, the window it came
  // from; a later user check from another window takes over.
  UpdateCheckOutcome Check(ToastCallback toast);
  // Relaunches into the ready update. False when none is ready.
  bool Install();
  // The sidebar card's close button: hides the card for the ready version, which still installs on quit.
  // False when none is ready. The caller stores dismissed_version() (Local State) and hands it back at the next start
  // through LoadDismissedVersion().
  bool Dismiss();
  std::optional<std::string> dismissed_version() const {
    return model_->dismissed_version();
  }
  void LoadDismissedVersion(std::string_view stored);
  // About Rio (chrome://settings/help) opened: a quiet check (no toast), its state reported now and after every change
  // until it settles. A feed that cannot be reached settles on "disabled", so About shows no error while the feed is
  // not published. With automatic updates off it starts no check and reports the current state. False when this
  // build has no updater.
  bool CheckFromAbout(AboutCallback report);

  UpdateSnapshot Snapshot() const;
  bool available() const { return model_->available(); }

  // general.autoUpdate (CluAppSettings::ApplyAutoUpdate): handed to the driver now if it runs, else when it starts.
  // On by default; ResetForTesting() keeps it, since the setting owns it.
  void SetAutomatic(bool automatic);
  bool automatic() const { return automatic_; }

  // Runs `callback` after every change of Snapshot().
  base::CallbackListSubscription AddObserver(base::RepeatingClosure callback);

  // Test seams: a fake driver (started at once), a test clock. ResetForTesting() drops both and the state.
  void SetDriverForTesting(std::unique_ptr<CluUpdateDriver> driver);
  void SetClockForTesting(const base::Clock* clock);
  void ResetForTesting();

  // CluUpdateDriver::Client:
  void OnCheckStarted() override;
  void OnUpdateFound(std::string_view version) override;
  void OnUpdateNotFound() override;
  void OnUpdateReady(std::string_view version) override;
  void OnUpdateError(std::string_view message) override;
  void OnCycleFinished() override;

 private:
  friend class base::NoDestructor<CluUpdateService>;

  CluUpdateService();
  ~CluUpdateService() override;

  void Start(std::unique_ptr<CluUpdateDriver> driver);
  // Publishes the change and hands `toast` to the window that asked, if any.
  void Changed(std::optional<std::string> toast);
  void ReportToAbout();
  // Starts the driver's check. With automatic updates off (only "Check for Updates…" checks then), a found update
  // still downloads for this cycle, so it ends with Rio's Relaunch card rather than the updater's own alert.
  void RunCheck();

  SEQUENCE_CHECKER(sequence_checker_);
  bool started_ = false;
  bool automatic_ = true;
  // The running cycle downloads although automatic updates are off.
  bool downloads_for_check_ = false;
  std::unique_ptr<CluUpdateDriver> driver_;
  raw_ptr<const base::Clock> clock_;
  // Rebuilt by ResetForTesting().
  std::optional<CluUpdateModel> model_;
  ToastCallback toast_;
  std::vector<AboutCallback> about_;
  base::RepeatingClosureList observers_;
};

// The About page's hook, CluUpdateService::Get().CheckFromAbout(). chrome-browser-ui-webui-help-version_updater_mac.mm.patch
// declares it itself, so Chromium's help target needs no dependency on Rio.
bool CheckForUpdatesFromAbout(
    base::RepeatingCallback<void(const std::string&, const std::string&)> report);

}  // namespace clu

#endif  // CLU_BROWSER_UPDATER_CLU_UPDATE_SERVICE_H_
