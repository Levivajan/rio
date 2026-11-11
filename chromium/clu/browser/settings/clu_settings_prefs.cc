// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/settings/clu_settings_prefs.h"

#include <string>

#include "components/prefs/pref_registry_simple.h"

namespace clu {

void RegisterCluSettingsLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kCluSettingsPref);
  registry->RegisterDictionaryPref(kCluSpaceThemesPref);
  registry->RegisterBooleanPref(kCluSidebarHiddenPref, false);
  registry->RegisterBooleanPref(kCluToolbarPref, false);
  registry->RegisterDictionaryPref(kCluImportMapPref);
  registry->RegisterDictionaryPref(kCluLiveFoldersPref);
  registry->RegisterDictionaryPref(kCluSettingsWindowOriginPref);
  registry->RegisterStringPref(kCluUpdateDismissedVersionPref, std::string());
}

}  // namespace clu
