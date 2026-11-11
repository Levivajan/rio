// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SETTINGS_CLU_SETTINGS_PREFS_H_
#define CLU_BROWSER_SETTINGS_CLU_SETTINGS_PREFS_H_

class PrefRegistrySimple;

// Clu's app-wide settings in Local State (docs/chromium/m8.md). Its own target with no Clu dependencies, so the
// Local State registration can call it without a cycle.
namespace clu {

// The stored global settings of the registry (clu_settings_registry.h), raw: {key: value}. Readers resolve them, so a
// junk value reads as the default and is never thrown at the UI.
inline constexpr char kCluSettingsPref[] = "clu.settings";
// Committed space themes: {space id: {colors, intensity, grain}} (clu_space_themes.h).
inline constexpr char kCluSpaceThemesPref[] = "clu.space_themes";
// Whether the last window that changed it had its sidebar hidden: new windows and the next launch start that way.
inline constexpr char kCluSidebarHiddenPref[] = "clu.shell.sidebar_hidden";
// Whether the last window that changed it was in toolbar mode (View > Show Toolbar): new windows start that way.
inline constexpr char kCluToolbarPref[] = "clu.shell.toolbar";
// What the Chrome and sidebar-file imports made, so a re-import adds nothing: {source key: Clu id} for profiles and
// spaces (rows are mapped in each profile's clu.db).
inline constexpr char kCluImportMapPref[] = "clu.import_map";
// Live Folders (clu_live_folders.h): {folder id: {kind, url, profile}}.
inline constexpr char kCluLiveFoldersPref[] = "clu.live_folders";

// Where the app settings window last stood, {x, y} in screen points: it opens there again, and centered on the asking
// window when nothing is saved or the spot is on no display now.
inline constexpr char kCluSettingsWindowOriginPref[] = "clu.settings_window.origin";
// The update card's close button: the ready version the user closed it for. The card stays hidden for that
// version (Sparkle still installs it on quit) and comes back for a newer one.
inline constexpr char kCluUpdateDismissedVersionPref[] = "clu.update_dismissed_version";

// Called from RegisterCluLocalStatePrefs() (clu/browser/spaces/clu_spaces_prefs.cc).
void RegisterCluSettingsLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace clu

#endif  // CLU_BROWSER_SETTINGS_CLU_SETTINGS_PREFS_H_
