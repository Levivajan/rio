// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_BRIDGE_HOST_H_
#define CLU_BROWSER_UI_WEBUI_CLU_BRIDGE_HOST_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"
#include "clu/common/mojom/bridge.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"

namespace content {
class WebUI;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace clu {

class CluArchiveHandler;
class CluBrowsingDataHandler;
class CluBlockingHandler;
class CluCommandHandler;
class CluCommandsHandler;
class CluDownloadHandler;
class CluPermissionHandler;
class CluExtensionsHandler;
class CluFoldersHandler;
class CluHistoryHandler;
class CluImportHandler;
class CluMediaHandler;
class CluMiniHandler;
class CluNavHandler;
class CluZoomHandler;
class CluOnboardingHandler;
class CluOverlayHandler;
class CluPlatformHandler;
class CluPeekHandler;
class CluProfilesHandler;
class CluSectionsHandler;
class CluSettingsHandler;
class CluShellHandler;
class CluSiteControlsHandler;
class CluSpacesHandler;
class CluSplitHandler;
class CluStateHandler;
class CluSwitcherHandler;
class CluTabsHandler;
class CluThemeHandler;
class CluTweaksHandler;
class CluUiHandler;
class CluWindowHandler;
class CluWindowState;

// One Clu WebUI document's side of the bridge: StateSource and the 42 handler interfaces. The window comes from the
// document's own WebContents, never from a payload: only the frame and overlay views a CluShell created for a window,
// and (M8) Clu's settings page in a tab of a Clu window, are surfaces. Anywhere else (chrome://clu-frame opened in a
// tab) the document gets no state, a null window id and kNotAllowed for every command.
class CluBridgeHost : public mojom::StateSource {
 public:
  // `surface` is kSurfaceUi for the frame, kSurfaceOverlay for an overlay; `layer` is set for overlays only, `slot`
  // for passive overlays only.
  CluBridgeHost(content::WebUI* web_ui,
                uint32_t surface,
                std::optional<mojom::SurfaceLayer> layer,
                std::optional<int32_t> slot = std::nullopt);
  CluBridgeHost(const CluBridgeHost&) = delete;
  CluBridgeHost& operator=(const CluBridgeHost&) = delete;
  ~CluBridgeHost() override;

  void BindStateSource(mojo::PendingReceiver<mojom::StateSource> receiver);

  template <typename Interface>
  void Bind(mojo::PendingReceiver<Interface> receiver) {
    Receivers().Bind(std::move(receiver));
  }

  // mojom::StateSource:
  void Subscribe(mojo::PendingRemote<mojom::StateObserver> observer,
                 SubscribeCallback callback) override;

 private:
  // Finds the document's window, once.
  void Resolve();
  BridgeReceivers& Receivers();

  const raw_ptr<content::WebUI> web_ui_;
  const uint32_t surface_;
  const std::optional<mojom::SurfaceLayer> layer_;
  const std::optional<int32_t> slot_;
  bool resolved_ = false;
  base::WeakPtr<CluWindowState> window_;

  std::unique_ptr<CluStateHandler> state_handler_;
  std::unique_ptr<CluUiHandler> ui_handler_;
  std::unique_ptr<CluOverlayHandler> overlay_handler_;
  std::unique_ptr<CluCommandsHandler> commands_handler_;
  std::unique_ptr<CluTabsHandler> tabs_handler_;
  std::unique_ptr<CluNavHandler> nav_handler_;
  std::unique_ptr<CluZoomHandler> zoom_handler_;
  std::unique_ptr<CluShellHandler> shell_handler_;
  std::unique_ptr<CluCommandHandler> command_handler_;
  std::unique_ptr<CluFoldersHandler> folders_handler_;
  std::unique_ptr<CluSectionsHandler> sections_handler_;
  std::unique_ptr<CluArchiveHandler> archive_handler_;
  std::unique_ptr<CluSwitcherHandler> switcher_handler_;
  std::unique_ptr<CluSettingsHandler> settings_handler_;
  std::unique_ptr<CluSpacesHandler> spaces_handler_;
  std::unique_ptr<CluProfilesHandler> profiles_handler_;
  std::unique_ptr<CluWindowHandler> window_handler_;
  std::unique_ptr<CluPlatformHandler> platform_handler_;
  std::unique_ptr<CluDownloadHandler> download_handler_;
  std::unique_ptr<CluPermissionHandler> permission_handler_;
  // M8.
  std::unique_ptr<CluHistoryHandler> history_handler_;
  std::unique_ptr<CluBrowsingDataHandler> browsing_data_handler_;
  std::unique_ptr<CluImportHandler> import_handler_;
  std::unique_ptr<CluThemeHandler> theme_handler_;
  std::unique_ptr<CluOnboardingHandler> onboarding_handler_;
  // M7.
  std::unique_ptr<CluBlockingHandler> blocking_handler_;
  std::unique_ptr<CluSiteControlsHandler> site_controls_handler_;
  std::unique_ptr<CluExtensionsHandler> extensions_handler_;
  std::unique_ptr<CluTweaksHandler> tweaks_handler_;
  // M6.
  std::unique_ptr<CluSplitHandler> split_handler_;
  std::unique_ptr<CluMediaHandler> media_handler_;
  std::unique_ptr<CluPeekHandler> peek_handler_;
  std::unique_ptr<CluMiniHandler> mini_handler_;
  // Declared after the handlers it points to, so it goes first.
  std::unique_ptr<BridgeReceivers> receivers_;
  mojo::ReceiverSet<mojom::StateSource> state_source_receivers_;
  base::WeakPtrFactory<CluBridgeHost> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_BRIDGE_HOST_H_
