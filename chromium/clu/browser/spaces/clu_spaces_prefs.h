// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SPACES_CLU_SPACES_PREFS_H_
#define CLU_BROWSER_SPACES_CLU_SPACES_PREFS_H_

class PrefRegistrySimple;

// Clu's Local State prefs (docs/chromium/m4.md): what spans profiles. Its own target with no Clu dependencies, so
// //chrome/browser/prefs:impl can call the registration without a cycle.
namespace clu {

// CluSpaceList::ProfilesValue(): [{id, dir, name, default}].
inline constexpr char kCluProfilesPref[] = "clu.profiles";
// CluSpaceList::SpacesValue(), in switcher order: [{id, name, icon_kind, icon_value, color, profile_id}].
inline constexpr char kCluSpacesPref[] = "clu.spaces";
// The space a window showed last.
inline constexpr char kCluLastSpacePref[] = "clu.last_space";
// Profiles a finished import Replace removes: [id]. One a window kept is removed at the next launch.
inline constexpr char kCluProfilesToRemovePref[] = "clu.profiles_to_remove";

// The hook RegisterLocalState() calls (patch chrome-browser-prefs-browser_prefs.cc.patch).
void RegisterCluLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace clu

#endif  // CLU_BROWSER_SPACES_CLU_SPACES_PREFS_H_
