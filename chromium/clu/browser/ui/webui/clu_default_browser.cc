// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_default_browser.h"

#include <utility>

#include "base/functional/bind.h"

namespace clu {

std::string_view DefaultBrowserStateName(
    shell_integration::DefaultWebClientState state) {
  switch (state) {
    case shell_integration::IS_DEFAULT:
      return "default";
    case shell_integration::NOT_DEFAULT:
    case shell_integration::OTHER_MODE_IS_DEFAULT:
      return "not-default";
    default:
      return "unavailable";
  }
}

std::string_view DefaultBrowserRequestStatus(
    shell_integration::DefaultWebClientState before,
    bool can_set) {
  if (before == shell_integration::IS_DEFAULT) {
    return "already-default";
  }
  if (before == shell_integration::UNKNOWN_DEFAULT || !can_set) {
    return "not-packaged";
  }
  return {};
}

// static
CluDefaultBrowser& CluDefaultBrowser::Get() {
  static base::NoDestructor<CluDefaultBrowser> instance;
  return *instance;
}

CluDefaultBrowser::CluDefaultBrowser() = default;

CluDefaultBrowser::~CluDefaultBrowser() = default;

void CluDefaultBrowser::Refresh() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The worker holds itself until its answer comes back; Get() is never destroyed, so Unretained is safe.
  MakeWorker()->StartCheckIsDefault(base::BindOnce(
      &CluDefaultBrowser::OnChecked, base::Unretained(this), generation_));
}

void CluDefaultBrowser::Request(RequestCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MakeWorker()->StartCheckIsDefault(
      base::BindOnce(&CluDefaultBrowser::OnCheckedForRequest,
                     base::Unretained(this), generation_, std::move(callback)));
}

base::CallbackListSubscription CluDefaultBrowser::AddObserver(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return observers_.Add(std::move(callback));
}

void CluDefaultBrowser::SetWorkerFactoryForTesting(WorkerFactory factory) {
  factory_ = std::move(factory);
}

void CluDefaultBrowser::ResetForTesting() {
  factory_.Reset();
  state_ = shell_integration::UNKNOWN_DEFAULT;
  ++generation_;
}

scoped_refptr<shell_integration::DefaultWebClientWorker>
CluDefaultBrowser::MakeWorker() {
  if (factory_) {
    return factory_.Run();
  }
  return base::MakeRefCounted<shell_integration::DefaultBrowserWorker>();
}

void CluDefaultBrowser::OnChecked(
    int generation,
    shell_integration::DefaultWebClientState state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != generation_) {
    return;
  }
  const bool changed =
      DefaultBrowserStateName(state) != DefaultBrowserStateName(state_);
  state_ = state;
  if (changed) {
    observers_.Notify();
  }
}

void CluDefaultBrowser::OnCheckedForRequest(
    int generation,
    RequestCallback callback,
    shell_integration::DefaultWebClientState state) {
  OnChecked(generation, state);
  const std::string_view status = DefaultBrowserRequestStatus(
      state, shell_integration::GetDefaultBrowserSetPermission() !=
                 shell_integration::SET_DEFAULT_NOT_ALLOWED);
  if (!status.empty()) {
    std::move(callback).Run(status);
    return;
  }
  // macOS shows its own "change your default web browser?" prompt and answers later, so the check that follows the
  // set usually still reads "not-default"; the window's Refresh() on coming back to Rio reads the user's answer.
  MakeWorker()->StartSetAsDefault(base::BindOnce(
      &CluDefaultBrowser::OnChecked, base::Unretained(this), generation_));
  std::move(callback).Run("requested");
}

}  // namespace clu
