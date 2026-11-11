// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_platform_handler.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/ui/webui/bridge/call_result.h"
#include "clu/browser/ui/webui/clu_default_browser.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/browser/updater/clu_update_service.h"
#include "components/prefs/pref_service.h"

namespace clu {

namespace {

constexpr base::TimeDelta kUpdateToast = base::Seconds(4);

mojom::CallResultPtr Status(std::string_view status) {
  return ValueResult(base::Value(base::DictValue().Set("status", status)));
}

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

}  // namespace

void EnsureUpdatesStarted() {
  CluUpdateService& service = CluUpdateService::Get();
  if (PrefService* prefs = LocalState()) {
    service.LoadDismissedVersion(
        prefs->GetString(kCluUpdateDismissedVersionPref));
  }
  service.EnsureStarted();
}

UpdateCheckOutcome CheckForUpdatesFrom(CluWindowState& window) {
  return CluUpdateService::Get().Check(base::BindRepeating(
      [](base::WeakPtr<CluWindowState> window, const std::string& text) {
        if (window) {
          window->ShowToast(text, std::nullopt, kUpdateToast);
        }
      },
      window.GetWeakPtr()));
}

CluPlatformHandler::CluPlatformHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}

CluPlatformHandler::~CluPlatformHandler() = default;

void CluPlatformHandler::RequestDefaultBrowser(
    RequestDefaultBrowserCallback callback) {
  CluDefaultBrowser::Get().Request(base::BindOnce(
      [](RequestDefaultBrowserCallback callback, std::string_view status) {
        std::move(callback).Run(Status(status));
      },
      std::move(callback)));
}

void CluPlatformHandler::CheckForUpdates(CheckForUpdatesCallback callback) {
  if (!window_) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kRejected, "the window is closing"));
    return;
  }
  std::move(callback).Run(
      Status(UpdateCheckOutcomeName(CheckForUpdatesFrom(*window_))));
}

void CluPlatformHandler::InstallUpdate(InstallUpdateCallback callback) {
  // Answers first: installing quits Clu, and this document goes with it.
  const bool ready = CluUpdateService::Get().Snapshot().status ==
                     UpdateStatus::kReady;
  std::move(callback).Run(Status(ready ? "installing" : "not-ready"));
  if (ready) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([] { CluUpdateService::Get().Install(); }));
  }
}

void CluPlatformHandler::DismissUpdate(DismissUpdateCallback callback) {
  CluUpdateService& service = CluUpdateService::Get();
  if (!service.Dismiss()) {
    std::move(callback).Run(Status("not-ready"));
    return;
  }
  // Stored, so the card stays hidden for this version after a relaunch that did not install it.
  if (PrefService* prefs = LocalState()) {
    prefs->SetString(kCluUpdateDismissedVersionPref,
                     service.dismissed_version().value_or(std::string()));
  }
  std::move(callback).Run(Status("dismissed"));
}

}  // namespace clu
