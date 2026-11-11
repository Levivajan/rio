// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/updater/clu_update_rules.h"

#include <utility>

#include "base/base64.h"
#include "base/strings/string_util.h"
#include "base/time/clock.h"

namespace clu {

namespace {

bool IsVersionChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '.' || c == '+' || c == '-' ||
         c == '_' || c == ' ';
}

std::string Downloading(const std::optional<std::string>& version) {
  return version ? "Downloading Rio " + *version + "…"
                 : std::string("Downloading the update…");
}

}  // namespace

UpdateSnapshot::UpdateSnapshot() = default;
UpdateSnapshot::UpdateSnapshot(const UpdateSnapshot&) = default;
UpdateSnapshot& UpdateSnapshot::operator=(const UpdateSnapshot&) = default;
UpdateSnapshot::~UpdateSnapshot() = default;

bool IsAllowedFeedUrl(const GURL& url) {
  if (!url.is_valid() || url.spec().size() > kMaxFeedUrlLength ||
      url.has_username() || url.has_password() || url.has_ref() ||
      url.host().empty()) {
    return false;
  }
  if (url.SchemeIs("https")) {
    return true;
  }
  // The local update test serves its feed on this Mac only.
  const std::string_view host = url.host();
  return url.SchemeIs("http") &&
         (host == "127.0.0.1" || host == "[::1]" || host == "localhost");
}

base::expected<UpdateConfig, std::string> ParseUpdateConfig(
    std::string_view feed_url,
    std::string_view public_key) {
  if (feed_url.empty()) {
    return base::unexpected("no update feed (SUFeedURL)");
  }
  if (feed_url.size() > kMaxFeedUrlLength) {
    return base::unexpected("the update feed URL is too long");
  }
  const GURL url(feed_url);
  if (!IsAllowedFeedUrl(url)) {
    return base::unexpected(
        "the update feed must be https, or http to this Mac");
  }
  const std::string_view key = base::TrimWhitespaceASCII(public_key,
                                                         base::TRIM_ALL);
  std::string bytes;
  if (key.empty() || !base::Base64Decode(key, &bytes) ||
      bytes.size() != kEdPublicKeyBytes) {
    return base::unexpected(
        "the update key (SUPublicEDKey) must be base64 of 32 bytes");
  }
  return UpdateConfig{url, std::string(key)};
}

std::optional<std::string> CleanUpdateVersion(std::string_view raw) {
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(raw, base::TRIM_ALL);
  if (trimmed.empty() || trimmed.size() > kMaxUpdateVersionLength) {
    return std::nullopt;
  }
  for (char c : trimmed) {
    if (!IsVersionChar(c)) {
      return std::nullopt;
    }
  }
  return std::string(trimmed);
}

std::string_view UpdateStatusName(UpdateStatus status) {
  switch (status) {
    case UpdateStatus::kUnavailable:
      return "unavailable";
    case UpdateStatus::kIdle:
      return "idle";
    case UpdateStatus::kChecking:
      return "checking";
    case UpdateStatus::kDownloading:
      return "downloading";
    case UpdateStatus::kReady:
      return "ready";
    case UpdateStatus::kUpToDate:
      return "up-to-date";
    case UpdateStatus::kError:
      return "error";
  }
  return "unavailable";
}

std::string_view UpdateCheckOutcomeName(UpdateCheckOutcome outcome) {
  switch (outcome) {
    case UpdateCheckOutcome::kChecking:
      return "checking";
    case UpdateCheckOutcome::kBusy:
      return "busy";
    case UpdateCheckOutcome::kReady:
      return "ready";
    case UpdateCheckOutcome::kUnavailable:
      return "unavailable";
  }
  return "unavailable";
}

std::optional<std::string> ToastForCheckOutcome(UpdateCheckOutcome outcome) {
  switch (outcome) {
    case UpdateCheckOutcome::kChecking:
      return std::nullopt;
    case UpdateCheckOutcome::kBusy:
      return "Rio is already checking for updates";
    case UpdateCheckOutcome::kReady:
      return "Relaunch Rio to install the update";
    case UpdateCheckOutcome::kUnavailable:
      return "Updates are off in this copy of Rio";
  }
  return std::nullopt;
}

CluUpdateModel::CluUpdateModel(const base::Clock* clock) : clock_(clock) {}

CluUpdateModel::~CluUpdateModel() = default;

void CluUpdateModel::SetAvailable() {
  if (status_ == UpdateStatus::kUnavailable) {
    status_ = UpdateStatus::kIdle;
  }
}

UpdateCheckOutcome CluUpdateModel::BeginCheck(bool user) {
  switch (status_) {
    case UpdateStatus::kUnavailable:
      return UpdateCheckOutcome::kUnavailable;
    case UpdateStatus::kReady:
      return UpdateCheckOutcome::kReady;
    case UpdateStatus::kChecking:
    case UpdateStatus::kDownloading:
      // A user check joins the one that runs, so its result still toasts.
      user_check_ = user_check_ || user;
      return UpdateCheckOutcome::kBusy;
    case UpdateStatus::kIdle:
    case UpdateStatus::kUpToDate:
    case UpdateStatus::kError:
      status_ = UpdateStatus::kChecking;
      user_check_ = user;
      return UpdateCheckOutcome::kChecking;
  }
  return UpdateCheckOutcome::kUnavailable;
}

std::optional<std::string> CluUpdateModel::OnFound(std::string_view version) {
  if (status_ == UpdateStatus::kUnavailable || ready()) {
    return std::nullopt;
  }
  version_ = CleanUpdateVersion(version);
  status_ = UpdateStatus::kDownloading;
  if (!user_check_) {
    return std::nullopt;
  }
  return Downloading(version_);
}

std::optional<std::string> CluUpdateModel::OnNotFound() {
  if (!InCycle()) {
    return std::nullopt;
  }
  const bool user = user_check_;
  version_.reset();
  Finish(UpdateStatus::kUpToDate);
  if (!user) {
    return std::nullopt;
  }
  return "Rio is up to date";
}

std::optional<std::string> CluUpdateModel::OnReady(std::string_view version) {
  if (status_ == UpdateStatus::kUnavailable) {
    return std::nullopt;
  }
  const bool user = user_check_;
  if (std::optional<std::string> cleaned = CleanUpdateVersion(version)) {
    version_ = std::move(cleaned);
  }
  Finish(UpdateStatus::kReady);
  if (!user) {
    return std::nullopt;
  }
  return version_ ? "Rio " + *version_ + " is ready"
                  : std::string("The update is ready");
}

std::optional<std::string> CluUpdateModel::OnError() {
  // Sparkle may report one failure twice (a failed download, then the abort): the first one ends the cycle.
  if (!InCycle()) {
    return std::nullopt;
  }
  const bool user = user_check_;
  // A background check that fails says nothing: Sparkle tries again on its schedule.
  Finish(user ? UpdateStatus::kError : UpdateStatus::kIdle);
  if (!user) {
    return std::nullopt;
  }
  return "Couldn't check for updates";
}

void CluUpdateModel::OnCycleFinished() {
  if (status_ == UpdateStatus::kChecking ||
      status_ == UpdateStatus::kDownloading) {
    Finish(UpdateStatus::kIdle);
  }
  user_check_ = false;
}

bool CluUpdateModel::Dismiss() {
  if (status_ != UpdateStatus::kReady) {
    return false;
  }
  dismissed_version_ = version_.value_or(std::string());
  return true;
}

void CluUpdateModel::LoadDismissedVersion(std::string_view stored) {
  if (dismissed_version_) {
    return;
  }
  dismissed_version_ = CleanUpdateVersion(stored);
}

bool CluUpdateModel::InCycle() const {
  // Idle too: Sparkle may report a check that began before Clu saw it start.
  return status_ == UpdateStatus::kIdle || status_ == UpdateStatus::kChecking ||
         status_ == UpdateStatus::kDownloading;
}

void CluUpdateModel::Finish(UpdateStatus status) {
  status_ = status;
  user_check_ = false;
  checked_at_ = clock_->Now();
}

UpdateSnapshot CluUpdateModel::Snapshot() const {
  UpdateSnapshot snapshot;
  snapshot.status = status_;
  snapshot.version = version_;
  if (checked_at_) {
    snapshot.checked_at = checked_at_->InMillisecondsFSinceUnixEpochIgnoringNull();
  }
  snapshot.dismissed = status_ == UpdateStatus::kReady && dismissed_version_ &&
                       *dismissed_version_ == version_.value_or(std::string());
  return snapshot;
}

}  // namespace clu
