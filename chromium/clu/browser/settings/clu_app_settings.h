// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SETTINGS_CLU_APP_SETTINGS_H_
#define CLU_BROWSER_SETTINGS_CLU_APP_SETTINGS_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/theme/clu_space_themes.h"
#include "clu/browser/theme/clu_theme_math.h"

class Profile;

namespace base {
class Clock;
}  // namespace base

// Clu's app-wide settings (docs/chromium/m8.md), kept in Local State because they span profiles: the global keys of
// the settings registry (appearance.mode, onboarding.completedAt, shell.lastSidebarWidth), the sidebar's last hidden
// state, and every space's theme. It applies the appearance mode to Chromium (each profile's ThemeService browser
// color scheme, which drives Chromium's own UI and the pages' prefers-color-scheme) and decides whether first-run
// onboarding shows. One instance for the browser process, on the UI thread.
namespace clu {

class CluAppSettings : public CluSpacesService::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // A global setting, a space theme or onboarding changed: windows republish their state.
    virtual void OnAppSettingsChanged() = 0;
  };

  // Null without Local State (clu_unittests, early startup, shutdown).
  static CluAppSettings* Get();

  CluAppSettings(const CluAppSettings&) = delete;
  CluAppSettings& operator=(const CluAppSettings&) = delete;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // settings.global in state: every global key resolved, junk read as its default.
  base::DictValue Global();
  // settings.set of a global key with a value CheckSetting() accepted. appearance.mode applies to every loaded
  // profile at once, appearance.appIcon to the Dock at once.
  void SetGlobal(std::string_view key, base::Value value);

  // A global boolean key resolved (junk and missing read as its default); false for a key that is not one. The AI
  // features and Links switches of the app settings window (docs/chromium/parity-settings.md section 1).
  bool GetBool(std::string_view key);

  // ---- appearance (docs/plan/01-shell.md F10) --------------------------------------------------------------------

  // "auto", "light" or "dark".
  std::string AppearanceMode();
  // The profile's browser color scheme follows the mode (auto is Chromium's system scheme). Windows call it for their
  // profile as they open, so a profile loaded later follows too.
  void ApplyAppearance(Profile* profile);

  // ---- the app icon (docs/chromium/icons.md) ---------------------------------------------------------------------

  // appearance.appIcon: "flatWarm" (Flat warm, the bundle's icon) or one of the six alternates.
  std::string AppIcon();
  // Shows AppIcon() in the Dock (clu_app_icon.h). Windows call it as they open, which covers every launch; it does
  // nothing when that icon already shows.
  void ApplyAppIcon();

  // ---- quitting ---------------------------------------------------------------------------------------

  // general.warnBeforeQuit (on by default) sets Chromium's browser.confirm_to_quit, so ⌘Q asks to be held. Windows
  // call it as they open, which covers every launch.
  void ApplyWarnBeforeQuit();

  // ---- updates (docs/chromium/m12.md) ----------------------------------------------------------------------------

  // general.autoUpdate (on by default) turns the updater's scheduled checks and silent downloads on or off
  // (CluUpdateService::SetAutomatic); "Check for Updates…" works either way. Windows call it as they open.
  void ApplyAutoUpdate();

  // ---- picture in picture (docs/chromium/m6.md) -------------------------------------------------------------------

  // general.autoPictureInPicture (on by default) sets the profile's AUTO_PICTURE_IN_PICTURE default to allow, so
  // Chromium's AutoPictureInPictureTabHelper pops a playing video out with no prompt; off sets it to block. Windows
  // call it for their profile as they open, so a profile loaded later follows too.
  void ApplyAutoPictureInPicture(Profile* profile);

  // ---- onboarding (F12) ------------------------------------------------------------------------------------------

  // Shows on a fresh install only: onboarding.completedAt is absent (any stored value counts as completed, so a
  // returning user is never trapped) and this is Chromium's first run of the user data directory. Decided once per
  // run; a completion hides it for the rest of the run.
  bool onboarding_show();
  // onboarding.complete: writes completedAt (ISO 8601) and hides it. False when it was not showing.
  bool CompleteOnboarding(bool skipped);

  // ---- the sidebar's last state (F5, F13) ------------------------------------------------------------------------

  // shell.lastSidebarWidth, 164..500, default 214.
  int last_sidebar_width();
  bool last_sidebar_hidden();
  void SetLastSidebarHidden(bool hidden);
  bool last_toolbar();
  void SetLastToolbar(bool shown);

  // ---- space themes (F9) -----------------------------------------------------------------------------------------

  // What the space shows: a preview, else its theme, else nullopt (its named color).
  std::optional<SpaceTheme> ShownTheme(std::string_view space_id);
  // The caller checked that the space exists. Each answers whether state changed.
  bool SetSpaceTheme(const std::string& space_id, const SpaceTheme& theme);
  bool ResetSpaceTheme(std::string_view space_id);
  bool PreviewSpaceTheme(const std::string& space_id, const SpaceTheme& theme);
  bool RevertPreview(std::string_view space_id);

  // CluSpacesService::Observer: a deleted space's theme goes.
  void OnSpacesChanged() override;

  // Tests.
  void SetClockForTesting(const base::Clock* clock);
  // Overrides first_run::IsChromeFirstRun() for the onboarding decision, which is made again on the next read.
  void SetFirstRunForTesting(std::optional<bool> first_run);
  // Forget everything read so far, so the next use reads Local State again.
  void ResetForTesting();

 private:
  friend base::NoDestructor<CluAppSettings>;

  CluAppSettings();
  ~CluAppSettings() override;

  void EnsureLoaded();
  void SaveThemes();
  void NotifyChanged();
  // shortcuts.custom, resolved, to the key table (clu_shortcuts.h); at startup and on every write of the key.
  void ApplyShortcuts();
  std::optional<base::Value> StoredGlobal(std::string_view key);

  bool loaded_ = false;
  std::optional<bool> onboarding_show_;
  std::optional<bool> first_run_for_testing_;
  raw_ptr<const base::Clock> clock_;
  CluSpaceThemes themes_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<CluSpacesService, CluSpacesService::Observer>
      spaces_observation_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_SETTINGS_CLU_APP_SETTINGS_H_
