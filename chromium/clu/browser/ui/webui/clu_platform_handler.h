// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_PLATFORM_HANDLER_H_
#define CLU_BROWSER_UI_WEBUI_CLU_PLATFORM_HANDLER_H_

#include "base/memory/weak_ptr.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"
#include "clu/browser/updater/clu_update_rules.h"

// platform.checkForUpdates and platform.installUpdate (docs/chromium/m12.md): the frame's Relaunch button and the
// overlay's palette row reach CluUpdateService, the browser's one updater. platform.requestDefaultBrowser
// (onboarding and Settings) reaches CluDefaultBrowser. Every other platform method answers kNotImplemented.
namespace clu {

class CluWindowState;

// A check the user asked for in `window` (the menu item, the palette row, the bridge): its toasts show there.
UpdateCheckOutcome CheckForUpdatesFrom(CluWindowState& window);

// Starts the browser's updater with the version whose update card the user closed, from Local State.
void EnsureUpdatesStarted();

class CluPlatformHandler : public DefaultPlatformHandler {
 public:
  explicit CluPlatformHandler(base::WeakPtr<CluWindowState> window);
  ~CluPlatformHandler() override;

  // mojom::PlatformHandler:
  void RequestDefaultBrowser(RequestDefaultBrowserCallback callback) override;
  void CheckForUpdates(CheckForUpdatesCallback callback) override;
  void InstallUpdate(InstallUpdateCallback callback) override;
  void DismissUpdate(DismissUpdateCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_PLATFORM_HANDLER_H_
