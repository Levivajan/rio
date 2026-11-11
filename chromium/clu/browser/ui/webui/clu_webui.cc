// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_webui.h"

#include <memory>
#include <optional>
#include <string>

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/common/clu_urls.h"
#include "clu/grit/clu_frame_resources.h"
#include "clu/grit/clu_frame_resources_map.h"
#include "clu/grit/clu_mojom_resources_map.h"
#include "clu/grit/clu_overlay_resources.h"
#include "clu/grit/clu_overlay_resources_map.h"
#include "clu/grit/clu_settings_resources.h"
#include "clu/grit/clu_settings_resources_map.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller_interface_binder.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "net/base/url_util.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "clu/browser/ui/cocoa/clu_main_menu.h"
#endif

namespace clu {

namespace {

#if BUILDFLAG(IS_MAC)
// The Spaces menu's titles come from the window in front (its space names).
std::optional<std::u16string> MenuTitle(std::string_view command_id) {
  BrowserWindowInterface* browser =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  CluWindowState* window =
      browser ? CluWindowState::GetOrCreate(browser) : nullptr;
  return window ? window->CommandTitle(command_id) : std::nullopt;
}

// View ▸ Appearance's checks come from the app settings, through the window in front.
std::optional<bool> MenuChecked(std::string_view command_id) {
  BrowserWindowInterface* browser =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  CluWindowState* window =
      browser ? CluWindowState::GetOrCreate(browser) : nullptr;
  return window ? window->CommandChecked(command_id) : std::nullopt;
}
#endif

struct CspDirective {
  network::mojom::CSPDirectiveName name;
  const char* value;
};

// The policy in scripts/chromium/webui-csp.mjs, directive by directive. The ui:fake harness serves the bundles
// under the same policy, so a bundle that passes there passes here. chrome://resources serves Mojo's bindings.js,
// chrome://favicon2 the tab favicons.
constexpr CspDirective kCsp[] = {
    {network::mojom::CSPDirectiveName::DefaultSrc, "default-src 'none';"},
    {network::mojom::CSPDirectiveName::ScriptSrc,
     "script-src 'self' chrome://resources;"},
    {network::mojom::CSPDirectiveName::StyleSrc, "style-src 'self';"},
    {network::mojom::CSPDirectiveName::ImgSrc,
     "img-src 'self' data: blob: chrome://favicon2 chrome://image;"},
    {network::mojom::CSPDirectiveName::FontSrc, "font-src 'self';"},
    {network::mojom::CSPDirectiveName::ConnectSrc, "connect-src 'none';"},
    {network::mojom::CSPDirectiveName::MediaSrc, "media-src 'none';"},
    {network::mojom::CSPDirectiveName::ObjectSrc, "object-src 'none';"},
    {network::mojom::CSPDirectiveName::FrameSrc, "frame-src 'none';"},
    {network::mojom::CSPDirectiveName::WorkerSrc, "worker-src 'none';"},
    {network::mojom::CSPDirectiveName::ManifestSrc, "manifest-src 'none';"},
    {network::mojom::CSPDirectiveName::BaseURI, "base-uri 'none';"},
    {network::mojom::CSPDirectiveName::FormAction, "form-action 'none';"},
    {network::mojom::CSPDirectiveName::RequireTrustedTypesFor,
     "require-trusted-types-for 'script';"},
    {network::mojom::CSPDirectiveName::TrustedTypes, "trusted-types 'none';"},
};

// One bundle's data source: the Vite build from its .grd, the generated Mojo bindings at /mojom/, the strict CSP,
// and chrome://favicon2 for the tab rows.
void AddCluDataSource(content::WebUI* web_ui,
                      const char* host,
                      base::span<const webui::ResourcePath> resources,
                      int default_resource) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, host);
  webui::SetupWebUIDataSource(source, resources, default_resource);
  source->AddResourcePaths(kCluMojomResources);
  for (const CspDirective& directive : kCsp) {
    source->OverrideContentSecurityPolicy(directive.name, directive.value);
  }
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
}

}  // namespace

// CluFrameUI -------------------------------------------------------------------

CluFrameUI::CluFrameUI(content::WebUI* web_ui, const GURL& url)
    : ui::MojoWebUIController(web_ui),
      bridge_(web_ui,
              FrameModeIsMini(url) ? kSurfaceMiniUi : kSurfaceUi,
              /*layer=*/std::nullopt) {
  AddCluDataSource(web_ui, kCluFrameHost, kCluFrameResources,
                   IDR_CLU_FRAME_INDEX_HTML);
}

CluFrameUI::~CluFrameUI() = default;

void CluFrameUI::BindInterface(
    mojo::PendingReceiver<mojom::StateSource> receiver) {
  bridge_.BindStateSource(std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(CluFrameUI)

// CluOverlayUI -----------------------------------------------------------------

CluOverlayUI::CluOverlayUI(content::WebUI* web_ui, const GURL& url)
    : ui::MojoWebUIController(web_ui),
      bridge_(web_ui,
              kSurfaceOverlay,
              OverlayLayerFromUrl(url),
              OverlaySlotFromUrl(url)) {
  AddCluDataSource(web_ui, kCluOverlayHost, kCluOverlayResources,
                   IDR_CLU_OVERLAY_INDEX_HTML);
}

CluOverlayUI::~CluOverlayUI() = default;

void CluOverlayUI::BindInterface(
    mojo::PendingReceiver<mojom::StateSource> receiver) {
  bridge_.BindStateSource(std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(CluOverlayUI)

// CluSettingsUI ----------------------------------------------------------------

CluSettingsUI::CluSettingsUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui),
      bridge_(web_ui, kSurfaceInternalSettings, /*layer=*/std::nullopt) {
  AddCluDataSource(web_ui, kCluSettingsHost, kCluSettingsResources,
                   IDR_CLU_SETTINGS_INDEX_HTML);
}

CluSettingsUI::~CluSettingsUI() = default;

void CluSettingsUI::BindInterface(
    mojo::PendingReceiver<mojom::StateSource> receiver) {
  bridge_.BindStateSource(std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(CluSettingsUI)

// Configs and registration -----------------------------------------------------

CluFrameUIConfig::CluFrameUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kCluFrameHost) {}

CluOverlayUIConfig::CluOverlayUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kCluOverlayHost) {}

CluSettingsUIConfig::CluSettingsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kCluSettingsHost) {}

bool FrameModeIsMini(const GURL& url) {
  std::string mode;
  return net::GetValueForKeyInQuery(url, "mode", &mode) && mode == "mini";
}

mojom::SurfaceLayer OverlayLayerFromUrl(const GURL& url) {
  std::string layer;
  if (net::GetValueForKeyInQuery(url, "layer", &layer)) {
    if (layer == "under-peek") {
      return mojom::SurfaceLayer::kUnderPeek;
    }
    if (layer == "passive") {
      return mojom::SurfaceLayer::kPassive;
    }
  }
  return mojom::SurfaceLayer::kTop;
}

std::optional<int32_t> OverlaySlotFromUrl(const GURL& url) {
  if (OverlayLayerFromUrl(url) != mojom::SurfaceLayer::kPassive) {
    return std::nullopt;
  }
  std::string value;
  int slot = 0;
  if (!net::GetValueForKeyInQuery(url, "slot", &value) ||
      !base::StringToInt(value, &slot) || slot < 0 ||
      slot >= CluShell::kPassiveSlots) {
    return std::nullopt;
  }
  return slot;
}

void RegisterCluWebUIConfigs(content::WebUIConfigMap& map) {
  map.AddWebUIConfig(std::make_unique<CluFrameUIConfig>());
  map.AddWebUIConfig(std::make_unique<CluOverlayUIConfig>());
  map.AddWebUIConfig(std::make_unique<CluSettingsUIConfig>());
  CluShell::SetClientFactory([](BrowserWindowInterface* browser) {
    CluWindowState::GetOrCreate(browser);
  });
#if BUILDFLAG(IS_MAC)
  SetMenuTitleProvider(&MenuTitle);
  SetMenuCheckProvider(&MenuChecked);
#endif
}

void PopulateCluWebUIFrameBinders(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
  content::RegisterWebUIControllerInterfaceBinder<
      mojom::StateSource, CluFrameUI, CluOverlayUI, CluSettingsUI>(map);
  RegisterBridgeBinders<CluFrameUI, CluOverlayUI, CluSettingsUI>(map);
}

}  // namespace clu
