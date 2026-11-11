// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_DATA_HANDLERS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_DATA_HANDLERS_H_

#include "base/memory/weak_ptr.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"

// The M8 commands (docs/chromium/m8.md): history.open, browsingData.*, import.*, theme.* and onboarding.complete.
// settings.* stays CluSettingsHandler (clu_window_handlers.h). The guard has checked the surface and the payload caps;
// these check what the caps cannot (spaces and profiles that exist, a running import) and act on the window the WebUI
// belongs to. history.query and the history page's deletes answer kNotImplemented: the history page is Chromium's
// own (chrome://history, shown as rio://history), over the same HistoryService.
namespace clu {

class CluWindowState;

class CluHistoryHandler : public DefaultHistoryHandler {
 public:
  explicit CluHistoryHandler(base::WeakPtr<CluWindowState> window);
  ~CluHistoryHandler() override;

  // mojom::HistoryHandler:
  void Open(OpenCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluBrowsingDataHandler : public DefaultBrowsingDataHandler {
 public:
  explicit CluBrowsingDataHandler(base::WeakPtr<CluWindowState> window);
  ~CluBrowsingDataHandler() override;

  // mojom::BrowsingDataHandler:
  void Open(mojom::BrowsingDataOpenInputPtr input,
            OpenCallback callback) override;
  void Clear(mojom::BrowsingDataClearInputPtr input,
             ClearCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
  base::WeakPtrFactory<CluBrowsingDataHandler> weak_ptr_factory_{this};
};

class CluImportHandler : public DefaultImportHandler {
 public:
  explicit CluImportHandler(base::WeakPtr<CluWindowState> window);
  ~CluImportHandler() override;

  // mojom::ImportHandler:
  void Detect(DetectCallback callback) override;
  void Run(mojom::ImportRunInputPtr input, RunCallback callback) override;
  void OpenPasswords(OpenPasswordsCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluThemeHandler : public DefaultThemeHandler {
 public:
  explicit CluThemeHandler(base::WeakPtr<CluWindowState> window);
  ~CluThemeHandler() override;

  // mojom::ThemeHandler:
  void SetMode(mojom::ThemeSetModeInputPtr input,
               SetModeCallback callback) override;
  void PreviewSpaceTheme(mojom::ThemePreviewSpaceThemeInputPtr input,
                         PreviewSpaceThemeCallback callback) override;
  void SetSpaceTheme(mojom::ThemeSetSpaceThemeInputPtr input,
                     SetSpaceThemeCallback callback) override;
  void ResetSpaceTheme(mojom::ThemeResetSpaceThemeInputPtr input,
                       ResetSpaceThemeCallback callback) override;
  void OpenPicker(mojom::ThemeOpenPickerInputPtr input,
                  OpenPickerCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluOnboardingHandler : public DefaultOnboardingHandler {
 public:
  explicit CluOnboardingHandler(base::WeakPtr<CluWindowState> window);
  ~CluOnboardingHandler() override;

  // mojom::OnboardingHandler:
  void Complete(mojom::OnboardingCompleteInputPtr input,
                CompleteCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_DATA_HANDLERS_H_
