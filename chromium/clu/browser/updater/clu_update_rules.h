// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UPDATER_CLU_UPDATE_RULES_H_
#define CLU_BROWSER_UPDATER_CLU_UPDATE_RULES_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "url/gurl.h"

namespace base {
class Clock;
}  // namespace base

// The rules of Clu's updates (docs/chromium/m12.md), without Sparkle: which feeds and keys a package may name, what
// a feed's version string may show, and the status app.update shows (src/shared/state/platform.ts). Sparkle checks,
// downloads, verifies the EdDSA signature and installs; these rules decide what Clu starts and what the user sees.
namespace clu {

inline constexpr size_t kMaxFeedUrlLength = 2048;
inline constexpr size_t kMaxUpdateVersionLength = 64;
// An Ed25519 public key (SUPublicEDKey) is 32 bytes.
inline constexpr size_t kEdPublicKeyBytes = 32;

// What package.sh wrote into Info.plist (SUFeedURL, SUPublicEDKey).
struct UpdateConfig {
  GURL feed_url;
  std::string public_key;
};

// A feed is https, or http to this Mac only (127.0.0.1, ::1, localhost) for the local update test. No credentials,
// no fragment, at most kMaxFeedUrlLength. The key is base64 of 32 bytes. Anything else: the reason, and Clu runs
// without updates.
base::expected<UpdateConfig, std::string> ParseUpdateConfig(
    std::string_view feed_url,
    std::string_view public_key);
bool IsAllowedFeedUrl(const GURL& url);

// A version string from a feed as Clu may show it: 1 to kMaxUpdateVersionLength characters of [0-9A-Za-z.+-_ ],
// trimmed. Anything else (control characters, markup, empty) is junk: nullopt.
std::optional<std::string> CleanUpdateVersion(std::string_view raw);

enum class UpdateStatus {
  kUnavailable,
  kIdle,
  kChecking,
  kDownloading,
  kReady,
  kUpToDate,
  kError,
};
// The UpdateStatus string of src/shared/state/platform.ts.
std::string_view UpdateStatusName(UpdateStatus status);

// The answer to a check the user asked for (platform.checkForUpdates).
enum class UpdateCheckOutcome { kChecking, kBusy, kReady, kUnavailable };
std::string_view UpdateCheckOutcomeName(UpdateCheckOutcome outcome);
// The toast a user check gets at once: none while it runs (its result toasts later), else why nothing started.
std::optional<std::string> ToastForCheckOutcome(UpdateCheckOutcome outcome);

// app.update as state carries it.
struct UpdateSnapshot {
  UpdateSnapshot();
  UpdateSnapshot(const UpdateSnapshot&);
  UpdateSnapshot& operator=(const UpdateSnapshot&);
  ~UpdateSnapshot();

  UpdateStatus status = UpdateStatus::kUnavailable;
  std::optional<std::string> version;
  // Epoch ms of the last finished check.
  std::optional<double> checked_at;
  // The ready update is the version the user closed the sidebar card for: the card stays hidden, and the
  // update still installs on quit.
  bool dismissed = false;
};

// The status machine behind app.update. Sparkle's events move it; a check the user asked for also gets a toast for
// its outcome, a background check never does. A ready update stays ready until Clu relaunches: nothing after it
// (another check, an error) takes the Relaunch button away.
class CluUpdateModel {
 public:
  explicit CluUpdateModel(const base::Clock* clock);
  CluUpdateModel(const CluUpdateModel&) = delete;
  CluUpdateModel& operator=(const CluUpdateModel&) = delete;
  ~CluUpdateModel();

  void set_clock(const base::Clock* clock) { clock_ = clock; }

  // The updater started (the package has a feed and Sparkle loaded): idle.
  void SetAvailable();
  bool available() const { return status_ != UpdateStatus::kUnavailable; }
  bool ready() const { return status_ == UpdateStatus::kReady; }

  // A check starts. A user check while one runs or while an update is ready does not start another one. A
  // background check that Sparkle starts by itself never fails here.
  UpdateCheckOutcome BeginCheck(bool user);

  // Sparkle's events. Each returns the toast to show the user, if any.
  std::optional<std::string> OnFound(std::string_view version);
  std::optional<std::string> OnNotFound();
  std::optional<std::string> OnReady(std::string_view version);
  std::optional<std::string> OnError();
  // The cycle ended without an event above (Sparkle skipped or dismissed the update): back to idle.
  void OnCycleFinished();

  // The sidebar card's close button. Dismiss() hides the card for the ready version; false when nothing
  // is ready. A newer version that becomes ready shows the card again. dismissed_version() is what to store: the
  // cleaned version, or "" for a ready update whose version the feed did not name (hidden for this run only).
  bool Dismiss();
  const std::optional<std::string>& dismissed_version() const {
    return dismissed_version_;
  }
  // The stored dismissal, read once at start: junk or "" means none. A dismissal already made in this run wins.
  void LoadDismissedVersion(std::string_view stored);

  UpdateSnapshot Snapshot() const;

 private:
  // A check or download runs, or may run unseen: its result counts.
  bool InCycle() const;
  void Finish(UpdateStatus status);

  raw_ptr<const base::Clock> clock_;
  UpdateStatus status_ = UpdateStatus::kUnavailable;
  std::optional<std::string> version_;
  std::optional<base::Time> checked_at_;
  // The running check is one the user asked for.
  bool user_check_ = false;
  std::optional<std::string> dismissed_version_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UPDATER_CLU_UPDATE_RULES_H_
