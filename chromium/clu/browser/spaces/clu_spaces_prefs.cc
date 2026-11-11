// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/spaces/clu_spaces_prefs.h"

#include <string>

#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/site/clu_site_key.h"
#include "components/prefs/pref_registry_simple.h"

namespace clu {

void RegisterCluLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kCluProfilesPref);
  registry->RegisterListPref(kCluSpacesPref);
  registry->RegisterStringPref(kCluLastSpacePref, std::string());
  registry->RegisterListPref(kCluProfilesToRemovePref);
  // M8: global settings, space themes, the sidebar's last state and the import map.
  RegisterCluSettingsLocalStatePrefs(registry);
  // Developer Mode per site.
  registry->RegisterDictionaryPref(kCluDevModeSitesPref);
}

}  // namespace clu
