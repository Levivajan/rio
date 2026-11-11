// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_SITE_HANDLERS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_SITE_HANDLERS_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"

// The M7 commands (docs/chromium/m7.md): blocking.*, siteControls.*, extensions.* and tweaks.*. The generated guard
// has checked the surface and the caps; these check what the caps cannot: rows that exist in the calling window, sites
// a URL really has, extensions the profile really holds, the profile a payload names being the window's own. Site and
// profile always come from the window and its rows, never from the payload. A private window reads the profile's
// blocker and tweaks but writes nothing ("private-window").
namespace clu {

class CluWindowState;

class CluBlockingHandler : public DefaultBlockingHandler {
 public:
  explicit CluBlockingHandler(base::WeakPtr<CluWindowState> window);
  ~CluBlockingHandler() override;

  // mojom::BlockingHandler:
  void SetSiteEnabled(mojom::BlockingSetSiteEnabledInputPtr input,
                      SetSiteEnabledCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluSiteControlsHandler : public DefaultSiteControlsHandler {
 public:
  explicit CluSiteControlsHandler(base::WeakPtr<CluWindowState> window);
  ~CluSiteControlsHandler() override;

  // mojom::SiteControlsHandler:
  void Open(mojom::SiteControlsOpenInputPtr input,
            OpenCallback callback) override;
  void Share(mojom::SiteControlsShareInputPtr input,
             ShareCallback callback) override;
  void Screenshot(ScreenshotCallback callback) override;
  void SetDevMode(mojom::SiteControlsSetDevModeInputPtr input,
                  SetDevModeCallback callback) override;
  void DevTool(mojom::SiteControlsDevToolInputPtr input,
               DevToolCallback callback) override;
  void CaptureText(CaptureTextCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

// Install prompts are Chromium's own dialog, and Chromium's chrome://extensions is the extensions page, so
// respondInstall, pageProfile and openStore answer not-implemented. installFromStore (the import summary's Reinstall)
// runs Chromium's WebstoreInstallWithPrompt, the same prompt "Add to Chrome" shows, over the window. Turning an extension on goes through Chromium's
// ExtensionEnableFlow, so an extension that asked for new permissions shows the re-enable prompt (the only thing that
// grants them) and one that Chromium disabled for another reason (not allowlisted, blocklisted, corrupted) stays off.
class CluExtensionsHandler : public DefaultExtensionsHandler {
 public:
  explicit CluExtensionsHandler(base::WeakPtr<CluWindowState> window);
  ~CluExtensionsHandler() override;

  // mojom::ExtensionsHandler:
  void List(mojom::ExtensionsListInputPtr input,
            ListCallback callback) override;
  void Remove(mojom::ExtensionsRemoveInputPtr input,
              RemoveCallback callback) override;
  void OpenOptions(mojom::ExtensionsOpenOptionsInputPtr input,
                   OpenOptionsCallback callback) override;
  void SetEnabled(mojom::ExtensionsSetEnabledInputPtr input,
                  SetEnabledCallback callback) override;
  void RunAction(mojom::ExtensionsRunActionInputPtr input,
                 RunActionCallback callback) override;
  void SetPinned(mojom::ExtensionsSetPinnedInputPtr input,
                 SetPinnedCallback callback) override;
  void OpenMenu(mojom::ExtensionsOpenMenuInputPtr input,
                OpenMenuCallback callback) override;
  void InstallFromStore(mojom::ExtensionsInstallFromStoreInputPtr input,
                        InstallFromStoreCallback callback) override;

 private:
  class EnableRequest;

  void EnableDone(const std::string& id);

  base::WeakPtr<CluWindowState> window_;
  // One running enable flow per extension; dropped (with its prompt) when the handler goes.
  std::map<std::string, std::unique_ptr<EnableRequest>> enabling_;
  base::WeakPtrFactory<CluExtensionsHandler> weak_factory_{this};
};

class CluTweaksHandler : public DefaultTweaksHandler {
 public:
  explicit CluTweaksHandler(base::WeakPtr<CluWindowState> window);
  ~CluTweaksHandler() override;

  // mojom::TweaksHandler:
  void Get(mojom::TweaksGetInputPtr input, GetCallback callback) override;
  void Save(mojom::TweaksSaveInputPtr input, SaveCallback callback) override;
  void PreviewCss(mojom::TweaksPreviewCssInputPtr input,
                  PreviewCssCallback callback) override;
  void PreviewControls(mojom::TweaksPreviewControlsInputPtr input,
                       PreviewControlsCallback callback) override;
  void SetEnabled(mojom::TweaksSetEnabledInputPtr input,
                  SetEnabledCallback callback) override;
  void OpenEditor(mojom::TweaksOpenEditorInputPtr input,
                  OpenEditorCallback callback) override;
  void StartZap(mojom::TweaksStartZapInputPtr input,
                StartZapCallback callback) override;
  void StopZap(mojom::TweaksStopZapInputPtr input,
               StopZapCallback callback) override;
  void UndoZap(mojom::TweaksUndoZapInputPtr input,
               UndoZapCallback callback) override;
  void RemoveZap(mojom::TweaksRemoveZapInputPtr input,
                 RemoveZapCallback callback) override;
  void ClearZaps(mojom::TweaksClearZapsInputPtr input,
                 ClearZapsCallback callback) override;
  void List(mojom::TweaksListInputPtr input, ListCallback callback) override;
  void SetEnabledForSite(mojom::TweaksSetEnabledForSiteInputPtr input,
                         SetEnabledForSiteCallback callback) override;
  void DeleteForSite(mojom::TweaksDeleteForSiteInputPtr input,
                     DeleteForSiteCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_SITE_HANDLERS_H_
