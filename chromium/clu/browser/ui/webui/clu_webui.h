// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WEBUI_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WEBUI_H_

#include <cstdint>
#include <optional>
#include <utility>

#include "clu/browser/ui/webui/clu_bridge_host.h"
#include "clu/common/mojom/bridge.mojom.h"
#include "content/public/browser/webui_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class GURL;

namespace content {
class RenderFrameHost;
class WebUI;
class WebUIConfigMap;
}  // namespace content

namespace mojo {
template <typename>
class BinderMapWithContext;
}  // namespace mojo

namespace clu {

// chrome://clu-frame/: the sidebar and window frame (src/renderer/ui), hosted by CluShell under the page card.
class CluFrameUI : public ui::MojoWebUIController {
 public:
  // ?mode=mini is a Mini window's frame (surface mini-ui, M6).
  CluFrameUI(content::WebUI* web_ui, const GURL& url);
  CluFrameUI(const CluFrameUI&) = delete;
  CluFrameUI& operator=(const CluFrameUI&) = delete;
  ~CluFrameUI() override;

  void BindInterface(mojo::PendingReceiver<mojom::StateSource> receiver);

  // Every handler interface in bridge_handlers.h.
  template <typename Interface>
  void BindInterface(mojo::PendingReceiver<Interface> receiver) {
    bridge_.Bind(std::move(receiver));
  }

 private:
  CluBridgeHost bridge_;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

// chrome://clu-overlay/?layer=top|under-peek|passive: the overlay surfaces (src/renderer/overlay). M2 hosts the top
// layer in CluShell's overlay widget.
class CluOverlayUI : public ui::MojoWebUIController {
 public:
  CluOverlayUI(content::WebUI* web_ui, const GURL& url);
  CluOverlayUI(const CluOverlayUI&) = delete;
  CluOverlayUI& operator=(const CluOverlayUI&) = delete;
  ~CluOverlayUI() override;

  void BindInterface(mojo::PendingReceiver<mojom::StateSource> receiver);

  template <typename Interface>
  void BindInterface(mojo::PendingReceiver<Interface> receiver) {
    bridge_.Bind(std::move(receiver));
  }

 private:
  CluBridgeHost bridge_;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

// chrome://clu-settings/: Rio's app settings (src/renderer/internal/settings) in the settings window
// (CluSettingsWindow, docs/chromium/parity-settings.md), the internal:settings surface. Chromium's own settings are
// chrome://settings, shown as rio://settings.
class CluSettingsUI : public ui::MojoWebUIController {
 public:
  explicit CluSettingsUI(content::WebUI* web_ui);
  CluSettingsUI(const CluSettingsUI&) = delete;
  CluSettingsUI& operator=(const CluSettingsUI&) = delete;
  ~CluSettingsUI() override;

  void BindInterface(mojo::PendingReceiver<mojom::StateSource> receiver);

  template <typename Interface>
  void BindInterface(mojo::PendingReceiver<Interface> receiver) {
    bridge_.Bind(std::move(receiver));
  }

 private:
  CluBridgeHost bridge_;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

class CluFrameUIConfig : public content::DefaultWebUIConfig<CluFrameUI> {
 public:
  CluFrameUIConfig();
};

class CluOverlayUIConfig : public content::DefaultWebUIConfig<CluOverlayUI> {
 public:
  CluOverlayUIConfig();
};

class CluSettingsUIConfig : public content::DefaultWebUIConfig<CluSettingsUI> {
 public:
  CluSettingsUIConfig();
};

// A frame URL of a Mini window (?mode=mini).
bool FrameModeIsMini(const GURL& url);
// The overlay layer an overlay URL names (?layer=), top when it names none or junk.
mojom::SurfaceLayer OverlayLayerFromUrl(const GURL& url);
// The passive slot an overlay URL names (?slot=0..3), for the passive layer only.
std::optional<int32_t> OverlaySlotFromUrl(const GURL& url);

// Called from RegisterChromeWebUIConfigs() (patch chrome-browser-ui-webui-chrome_web_ui_configs.cc.patch).
void RegisterCluWebUIConfigs(content::WebUIConfigMap& map);

// Called from PopulateChromeWebUIFrameBindersPartsDesktop() (patch
// chrome-browser-chrome_browser_interface_binders_webui_parts_desktop.cc.patch): StateSource and every handler
// interface bind only in a CluFrameUI, CluOverlayUI or CluSettingsUI document.
void PopulateCluWebUIFrameBinders(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WEBUI_H_
