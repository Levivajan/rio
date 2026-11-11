// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/updater/clu_update_service.h"

#include <utility>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/time/default_clock.h"
#include "build/build_config.h"

namespace clu {

#if !BUILDFLAG(IS_MAC)
// Clu ships on Mac only; elsewhere (Linux test builds) nothing updates.
std::unique_ptr<CluUpdateDriver> CreatePlatformUpdateDriver() {
  return nullptr;
}
#endif

// static
CluUpdateService& CluUpdateService::Get() {
  static base::NoDestructor<CluUpdateService> service;
  return *service;
}

CluUpdateService::CluUpdateService()
    : clock_(base::DefaultClock::GetInstance()) {
  model_.emplace(clock_);
}

CluUpdateService::~CluUpdateService() = default;

void CluUpdateService::EnsureStarted() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (started_) {
    return;
  }
  Start(CreatePlatformUpdateDriver());
}

void CluUpdateService::Start(std::unique_ptr<CluUpdateDriver> driver) {
  started_ = true;
  driver_ = std::move(driver);
  if (!driver_ || !driver_->Start(this)) {
    // A dev build, a package made without a feed, or a feed the rules refuse: Clu runs without updates.
    driver_.reset();
    return;
  }
  driver_->SetAutomatic(automatic_);
  model_->SetAvailable();
  Changed(std::nullopt);
}

UpdateCheckOutcome CluUpdateService::Check(ToastCallback toast) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const UpdateCheckOutcome outcome = model_->BeginCheck(/*user=*/true);
  if (std::optional<std::string> now = ToastForCheckOutcome(outcome);
      now && toast) {
    toast.Run(*now);
  }
  if (outcome == UpdateCheckOutcome::kChecking ||
      outcome == UpdateCheckOutcome::kBusy) {
    // The running check's result toasts in this window.
    toast_ = std::move(toast);
  }
  if (outcome == UpdateCheckOutcome::kChecking) {
    Changed(std::nullopt);
    RunCheck();
  }
  return outcome;
}

void CluUpdateService::RunCheck() {
  if (!automatic_ && !downloads_for_check_) {
    downloads_for_check_ = true;
    driver_->SetDownloads(true);
  }
  driver_->CheckNow();
}

void CluUpdateService::SetAutomatic(bool automatic) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  automatic_ = automatic;
  if (driver_) {
    driver_->SetAutomatic(automatic_);
  }
}

bool CluUpdateService::Install() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return driver_ && model_->ready() && driver_->InstallNow();
}

bool CluUpdateService::Dismiss() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!model_->Dismiss()) {
    return false;
  }
  Changed(std::nullopt);
  return true;
}

void CluUpdateService::LoadDismissedVersion(std::string_view stored) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool before = model_->Snapshot().dismissed;
  model_->LoadDismissedVersion(stored);
  if (model_->Snapshot().dismissed != before) {
    Changed(std::nullopt);
  }
}

bool CluUpdateService::CheckFromAbout(AboutCallback report) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!driver_ || !model_->available()) {
    return false;
  }
  about_.push_back(std::move(report));
  const UpdateStatus before = model_->Snapshot().status;
  // With automatic updates off, opening About never checks: a check here would download and install on quit, which
  // only "Check for Updates…" may do. About shows what is known already.
  if (automatic_ && before != UpdateStatus::kChecking &&
      before != UpdateStatus::kDownloading && before != UpdateStatus::kReady) {
    // A background check: its outcome never toasts.
    model_->BeginCheck(/*user=*/false);
    Changed(std::nullopt);
    RunCheck();
  } else {
    ReportToAbout();
  }
  return true;
}

void CluUpdateService::ReportToAbout() {
  if (about_.empty()) {
    return;
  }
  const UpdateSnapshot now = model_->Snapshot();
  std::string state = "disabled";
  bool settled = true;
  switch (now.status) {
    case UpdateStatus::kChecking:
      state = "checking";
      settled = false;
      break;
    case UpdateStatus::kDownloading:
      state = "updating";
      settled = false;
      break;
    case UpdateStatus::kReady:
      state = "nearly-updated";
      break;
    case UpdateStatus::kUpToDate:
      state = "updated";
      break;
    case UpdateStatus::kIdle:
    case UpdateStatus::kUnavailable:
    case UpdateStatus::kError:
      // A quiet check that failed ends idle: the feed is not reachable (not published yet, or offline). About shows
      // nothing rather than an error.
      break;
  }
  // A report may open another check; run a copy.
  const std::vector<AboutCallback> reports = about_;
  if (settled) {
    about_.clear();
  }
  for (const AboutCallback& report : reports) {
    report.Run(state, now.version.value_or(std::string()));
  }
}

UpdateSnapshot CluUpdateService::Snapshot() const {
  return model_->Snapshot();
}

base::CallbackListSubscription CluUpdateService::AddObserver(
    base::RepeatingClosure callback) {
  return observers_.Add(std::move(callback));
}

void CluUpdateService::SetDriverForTesting(
    std::unique_ptr<CluUpdateDriver> driver) {
  ResetForTesting();
  Start(std::move(driver));
}

void CluUpdateService::SetClockForTesting(const base::Clock* clock) {
  clock_ = clock ? clock : base::DefaultClock::GetInstance();
  model_->set_clock(clock_);
}

void CluUpdateService::ResetForTesting() {
  // Each unit test brings its own task environment, so its own sequence.
  DETACH_FROM_SEQUENCE(sequence_checker_);
  driver_.reset();
  toast_.Reset();
  about_.clear();
  started_ = false;
  downloads_for_check_ = false;
  model_.emplace(clock_);
  observers_.Notify();
}

void CluUpdateService::OnCheckStarted() {
  model_->BeginCheck(/*user=*/false);
  Changed(std::nullopt);
}

void CluUpdateService::OnUpdateFound(std::string_view version) {
  Changed(model_->OnFound(version));
}

void CluUpdateService::OnUpdateNotFound() {
  Changed(model_->OnNotFound());
}

void CluUpdateService::OnUpdateReady(std::string_view version) {
  Changed(model_->OnReady(version));
}

void CluUpdateService::OnUpdateError(std::string_view message) {
  LOG(WARNING) << "Clu update: " << message;
  Changed(model_->OnError());
}

void CluUpdateService::OnCycleFinished() {
  if (downloads_for_check_) {
    downloads_for_check_ = false;
    if (driver_) {
      driver_->SetDownloads(automatic_);
    }
  }
  model_->OnCycleFinished();
  Changed(std::nullopt);
}

void CluUpdateService::Changed(std::optional<std::string> toast) {
  if (toast && toast_) {
    toast_.Run(*toast);
  }
  observers_.Notify();
  ReportToAbout();
}

bool CheckForUpdatesFromAbout(
    base::RepeatingCallback<void(const std::string&, const std::string&)> report) {
  return CluUpdateService::Get().CheckFromAbout(std::move(report));
}

}  // namespace clu
