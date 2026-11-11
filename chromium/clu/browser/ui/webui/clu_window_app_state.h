// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WINDOW_APP_STATE_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WINDOW_APP_STATE_H_

#include <optional>
#include <string>

#include "base/memory/raw_ref.h"
#include "base/scoped_observation.h"
#include "clu/browser/importer/clu_import_service.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

class Profile;

namespace clu {

class CluWindowState;
struct WindowSnapshot;

// The app-wide part of one window's state (docs/chromium/m8.md): app.appearance (mode, resolved, Reduce
// transparency), app.onboarding, app.import, settings.global and every space's theme, from CluAppSettings,
// CluImportService and the system's NativeTheme. Any change republishes the window. After each publish the window's
// native accent (Chromium's bubbles, menus and dialogs) follows the space it shows (12.4 second box), and the sidebar's
// hidden state is remembered for the next window.
class CluWindowAppState : public CluAppSettings::Observer,
                          public CluImportService::Observer,
                          public ui::NativeThemeObserver {
 public:
  CluWindowAppState(CluWindowState& window, Profile* profile);
  CluWindowAppState(const CluWindowAppState&) = delete;
  CluWindowAppState& operator=(const CluWindowAppState&) = delete;
  ~CluWindowAppState() override;

  // Fills the app fields and the themes of `s.spaces`.
  void Fill(WindowSnapshot& s);
  // After a publish: the accent, the sidebar's last state, and onboarding ending once a row shows (a tab opened while
  // it showed: Cmd+T, a link from another app).
  void Apply(const WindowSnapshot& s);
  // The accent the window's widget has now (tests), or nullopt for none.
  std::optional<SkColor> accent_for_testing() const { return accent_; }

  // CluAppSettings::Observer:
  void OnAppSettingsChanged() override;
  // CluImportService::Observer:
  void OnImportProgress() override;
  // ui::NativeThemeObserver: the system's dark mode or Reduce transparency changed.
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  const raw_ref<CluWindowState> window_;
  std::optional<SkColor> accent_;
  std::optional<bool> last_hidden_;
  std::optional<bool> last_toolbar_;
  // The row the window showed when onboarding came up (set while it shows): another row ends it.
  std::optional<std::optional<std::string>> onboarding_tab_;
  base::ScopedObservation<CluAppSettings, CluAppSettings::Observer>
      settings_observation_{this};
  base::ScopedObservation<CluImportService, CluImportService::Observer>
      import_observation_{this};
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WINDOW_APP_STATE_H_
