// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_bridge_host.h"

#include <string>

#include "base/functional/bind.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_capabilities_handlers.h"
#include "clu/browser/ui/webui/clu_command_bar.h"
#include "clu/browser/ui/webui/clu_platform_handler.h"
#include "clu/browser/ui/webui/clu_data_handlers.h"
#include "clu/browser/ui/webui/clu_settings_window.h"
#include "clu/browser/ui/webui/clu_site_handlers.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/common/clu_urls.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

namespace clu {

CluBridgeHost::CluBridgeHost(content::WebUI* web_ui,
                             uint32_t surface,
                             std::optional<mojom::SurfaceLayer> layer,
                             std::optional<int32_t> slot)
    : web_ui_(web_ui), surface_(surface), layer_(layer), slot_(slot) {}

CluBridgeHost::~CluBridgeHost() = default;

void CluBridgeHost::BindStateSource(
    mojo::PendingReceiver<mojom::StateSource> receiver) {
  state_source_receivers_.Add(this, std::move(receiver));
}

void CluBridgeHost::Subscribe(
    mojo::PendingRemote<mojom::StateObserver> observer,
    SubscribeCallback callback) {
  Resolve();
  std::optional<std::string> window_id;
  if (window_) {
    window_id = window_->window_id();
    window_->Subscribe(std::move(observer),
                       surface_ == kSurfaceUi || surface_ == kSurfaceMiniUi);
  }
  std::move(callback).Run(mojom::SurfaceInfo::New(
      std::move(window_id), layer_, slot_, mojom::kContractVersion));
}

void CluBridgeHost::Resolve() {
  if (resolved_) {
    return;
  }
  resolved_ = true;
  content::WebContents* contents = web_ui_->GetWebContents();
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  // Rio's settings page lives in the app settings window (docs/chromium/parity-settings.md): its main frame on
  // chrome://clu-settings, bound to the Rio window the settings window belongs to. Loaded anywhere else (a tab),
  // it binds to nothing, so every command answers not-allowed.
  if (surface_ == kSurfaceInternalSettings) {
    BrowserWindowInterface* browser = CluSettingsWindow::BrowserFor(contents);
    if (tab || !browser || !CluShell::From(browser) ||
        web_ui_->GetRenderFrameHost()->GetParentOrOuterDocument() ||
        contents->GetLastCommittedURL().host() != kCluSettingsHost) {
      return;
    }
    if (CluWindowState* state = CluWindowState::GetOrCreate(browser)) {
      window_ = state->GetWeakPtr();
    }
    return;
  }
  // A tab is never a frame or overlay surface, whatever it loaded.
  if (tab) {
    return;
  }
  BrowserWindowInterface* browser = webui::GetBrowserWindowInterface(contents);
  CluShell* shell = CluShell::From(browser);
  if (!shell) {
    return;
  }
  // Only the views the shell made for this window, each for its own surface:
  // the frame (mini-ui in a Mini window), the top overlay, the under-peek
  // overlay of the Peek layer, and the passive overlay of the slot its URL
  // names.
  const bool is_frame =
      surface_ == (shell->is_mini() ? kSurfaceMiniUi : kSurfaceUi) &&
      contents == shell->frame_contents();
  const std::optional<int> slot = shell->OverlaySlotOf(contents);
  bool is_overlay = false;
  if (surface_ == kSurfaceOverlay && slot) {
    if (*slot == -1) {
      is_overlay = layer_ == mojom::SurfaceLayer::kTop;
    } else if (*slot == CluShell::kUnderPeekSlot) {
      is_overlay = layer_ == mojom::SurfaceLayer::kUnderPeek;
    } else {
      is_overlay = layer_ == mojom::SurfaceLayer::kPassive && slot_ == *slot;
    }
  }
  if (!is_frame && !is_overlay) {
    return;
  }
  if (CluWindowState* state = CluWindowState::GetOrCreate(browser)) {
    window_ = state->GetWeakPtr();
  }
}

BridgeReceivers& CluBridgeHost::Receivers() {
  if (receivers_) {
    return *receivers_;
  }
  Resolve();
  BridgeReceivers::Impls impls;
  if (window_) {
    state_handler_ = std::make_unique<CluStateHandler>(window_);
    ui_handler_ = std::make_unique<CluUiHandler>(window_);
    overlay_handler_ = std::make_unique<CluOverlayHandler>(window_);
    commands_handler_ = std::make_unique<CluCommandsHandler>(window_);
    tabs_handler_ = std::make_unique<CluTabsHandler>(window_);
    nav_handler_ = std::make_unique<CluNavHandler>(window_);
    zoom_handler_ = std::make_unique<CluZoomHandler>(window_);
    shell_handler_ = std::make_unique<CluShellHandler>(window_);
    command_handler_ = std::make_unique<CluCommandHandler>(
        window_, Profile::FromWebUI(web_ui_));
    folders_handler_ = std::make_unique<CluFoldersHandler>(window_);
    sections_handler_ = std::make_unique<CluSectionsHandler>(window_);
    archive_handler_ = std::make_unique<CluArchiveHandler>(window_);
    switcher_handler_ = std::make_unique<CluSwitcherHandler>(window_);
    settings_handler_ = std::make_unique<CluSettingsHandler>(window_);
    spaces_handler_ = std::make_unique<CluSpacesHandler>(window_);
    profiles_handler_ = std::make_unique<CluProfilesHandler>(window_);
    window_handler_ = std::make_unique<CluWindowHandler>(window_);
    platform_handler_ = std::make_unique<CluPlatformHandler>(window_);
    download_handler_ = std::make_unique<CluDownloadHandler>(window_);
    permission_handler_ = std::make_unique<CluPermissionHandler>(window_);
    history_handler_ = std::make_unique<CluHistoryHandler>(window_);
    browsing_data_handler_ = std::make_unique<CluBrowsingDataHandler>(window_);
    import_handler_ = std::make_unique<CluImportHandler>(window_);
    theme_handler_ = std::make_unique<CluThemeHandler>(window_);
    onboarding_handler_ = std::make_unique<CluOnboardingHandler>(window_);
    blocking_handler_ = std::make_unique<CluBlockingHandler>(window_);
    site_controls_handler_ = std::make_unique<CluSiteControlsHandler>(window_);
    extensions_handler_ = std::make_unique<CluExtensionsHandler>(window_);
    tweaks_handler_ = std::make_unique<CluTweaksHandler>(window_);
    split_handler_ = std::make_unique<CluSplitHandler>(window_);
    media_handler_ = std::make_unique<CluMediaHandler>(window_);
    peek_handler_ = std::make_unique<CluPeekHandler>(window_);
    mini_handler_ = std::make_unique<CluMiniHandler>(window_);
    impls.state_handler = state_handler_.get();
    impls.ui_handler = ui_handler_.get();
    impls.overlay_handler = overlay_handler_.get();
    impls.commands_handler = commands_handler_.get();
    impls.tabs_handler = tabs_handler_.get();
    impls.nav_handler = nav_handler_.get();
    impls.zoom_handler = zoom_handler_.get();
    impls.shell_handler = shell_handler_.get();
    impls.command_handler = command_handler_.get();
    impls.folders_handler = folders_handler_.get();
    impls.sections_handler = sections_handler_.get();
    impls.archive_handler = archive_handler_.get();
    impls.switcher_handler = switcher_handler_.get();
    impls.settings_handler = settings_handler_.get();
    impls.spaces_handler = spaces_handler_.get();
    impls.profiles_handler = profiles_handler_.get();
    impls.window_handler = window_handler_.get();
    impls.platform_handler = platform_handler_.get();
    impls.download_handler = download_handler_.get();
    impls.permission_handler = permission_handler_.get();
    impls.history_handler = history_handler_.get();
    impls.browsing_data_handler = browsing_data_handler_.get();
    impls.import_handler = import_handler_.get();
    impls.theme_handler = theme_handler_.get();
    impls.onboarding_handler = onboarding_handler_.get();
    impls.blocking_handler = blocking_handler_.get();
    impls.site_controls_handler = site_controls_handler_.get();
    impls.extensions_handler = extensions_handler_.get();
    impls.tweaks_handler = tweaks_handler_.get();
    impls.split_handler = split_handler_.get();
    impls.media_handler = media_handler_.get();
    impls.peek_handler = peek_handler_.get();
    impls.mini_handler = mini_handler_.get();
  }
  // Without a window no surface bit is set, so every command answers kNotAllowed.
  receivers_ =
      std::make_unique<BridgeReceivers>(window_ ? surface_ : 0u, impls);
  return *receivers_;
}

}  // namespace clu
