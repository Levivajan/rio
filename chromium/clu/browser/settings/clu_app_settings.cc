// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/settings/clu_app_settings.h"

#include <set>
#include <utility>

#include "base/i18n/time_formatting.h"
#include "base/logging.h"
#include "base/time/default_clock.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/common/pref_names.h"
#include "clu/browser/commands/clu_shortcuts.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/updater/clu_update_service.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

#if BUILDFLAG(IS_MAC)
#include "clu/browser/app_icon/clu_app_icon.h"
#endif

namespace clu {

namespace {

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

std::set<std::string> SpaceIds() {
  std::set<std::string> ids;
  if (CluSpacesService* spaces = CluSpacesService::Get()) {
    for (const SpaceEntry& space : spaces->list().spaces()) {
      ids.insert(space.id);
    }
  }
  return ids;
}

ThemeService::BrowserColorScheme SchemeFor(std::string_view mode) {
  if (mode == "light") {
    return ThemeService::BrowserColorScheme::kLight;
  }
  if (mode == "dark") {
    return ThemeService::BrowserColorScheme::kDark;
  }
  return ThemeService::BrowserColorScheme::kSystem;
}

}  // namespace

// static
CluAppSettings* CluAppSettings::Get() {
  if (!LocalState()) {
    return nullptr;
  }
  static base::NoDestructor<CluAppSettings> instance;
  return instance.get();
}

CluAppSettings::CluAppSettings() : clock_(base::DefaultClock::GetInstance()) {
  ApplyShortcuts();
}

CluAppSettings::~CluAppSettings() = default;

void CluAppSettings::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CluAppSettings::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void CluAppSettings::EnsureLoaded() {
  if (loaded_) {
    return;
  }
  PrefService* local_state = LocalState();
  if (!local_state) {
    return;
  }
  loaded_ = true;
  CluSpacesService* spaces = CluSpacesService::Get();
  if (spaces && !spaces_observation_.IsObserving()) {
    spaces_observation_.Observe(spaces);
  }
  const int dropped =
      themes_.Load(local_state->GetDict(kCluSpaceThemesPref), SpaceIds());
  if (dropped > 0) {
    LOG(WARNING) << "clu: " << dropped
                 << " unreadable space themes; those spaces show their color";
    SaveThemes();
  }
}

std::optional<base::Value> CluAppSettings::StoredGlobal(std::string_view key) {
  PrefService* local_state = LocalState();
  if (!local_state) {
    return std::nullopt;
  }
  const base::Value* value = local_state->GetDict(kCluSettingsPref).Find(key);
  return value ? std::optional<base::Value>(value->Clone()) : std::nullopt;
}

base::DictValue CluAppSettings::Global() {
  PrefService* local_state = LocalState();
  if (!local_state) {
    return ResolveSettings(SettingScope::kGlobal, base::DictValue());
  }
  return ResolveSettings(SettingScope::kGlobal,
                         local_state->GetDict(kCluSettingsPref));
}

void CluAppSettings::SetGlobal(std::string_view key, base::Value value) {
  PrefService* local_state = LocalState();
  const SettingDef* def = FindSetting(key);
  if (!local_state || !def || def->scope != SettingScope::kGlobal) {
    return;
  }
  {
    ScopedDictPrefUpdate update(local_state, kCluSettingsPref);
    // Unknown stored keys are kept (a newer build's settings survive an older one).
    update->Set(key, std::move(value));
  }
  if (key == kShortcutsCustomKey) {
    ApplyShortcuts();
  }
  if (key == kAppearanceModeKey) {
    if (ProfileManager* profiles = g_browser_process->profile_manager()) {
      for (Profile* profile : profiles->GetLoadedProfiles()) {
        ApplyAppearance(profile);
      }
    }
  }
  if (key == kAppIconKey) {
    ApplyAppIcon();
  }
  if (key == kWarnBeforeQuitKey) {
    ApplyWarnBeforeQuit();
  }
  if (key == kAutoUpdateKey) {
    ApplyAutoUpdate();
  }
  if (key == kAutoPictureInPictureKey) {
    if (ProfileManager* profiles = g_browser_process->profile_manager()) {
      for (Profile* profile : profiles->GetLoadedProfiles()) {
        ApplyAutoPictureInPicture(profile);
      }
    }
  }
  NotifyChanged();
}

bool CluAppSettings::GetBool(std::string_view key) {
  const SettingDef* def = FindSetting(key);
  if (!def || def->scope != SettingScope::kGlobal) {
    return false;
  }
  const std::optional<base::Value> stored = StoredGlobal(key);
  const base::Value value = ReadSetting(*def, stored ? &*stored : nullptr);
  return value.is_bool() && value.GetBool();
}

void CluAppSettings::ApplyShortcuts() {
  const SettingDef* def = FindSetting(kShortcutsCustomKey);
  const std::optional<base::Value> stored = StoredGlobal(kShortcutsCustomKey);
  base::Value custom = ReadSetting(*def, stored ? &*stored : nullptr);
  SetCustomShortcuts(custom.is_dict() ? std::move(custom).TakeDict()
                                     : base::DictValue());
}

std::string CluAppSettings::AppearanceMode() {
  const SettingDef* def = FindSetting(kAppearanceModeKey);
  const std::optional<base::Value> stored = StoredGlobal(kAppearanceModeKey);
  return ReadSetting(*def, stored ? &*stored : nullptr).GetString();
}

void CluAppSettings::ApplyAppearance(Profile* profile) {
  if (!profile) {
    return;
  }
  // An off-the-record profile shares its original profile's ThemeService; private windows stay dark either way.
  ThemeService* theme =
      ThemeServiceFactory::GetForProfile(profile->GetOriginalProfile());
  const ThemeService::BrowserColorScheme scheme = SchemeFor(AppearanceMode());
  if (theme && theme->GetBrowserColorScheme() != scheme) {
    theme->SetBrowserColorScheme(scheme);
  }
}

std::string CluAppSettings::AppIcon() {
  const SettingDef* def = FindSetting(kAppIconKey);
  const std::optional<base::Value> stored = StoredGlobal(kAppIconKey);
  return ReadSetting(*def, stored ? &*stored : nullptr).GetString();
}

void CluAppSettings::ApplyAppIcon() {
#if BUILDFLAG(IS_MAC)
  clu::ApplyAppIcon(AppIcon());
#endif
}

void CluAppSettings::ApplyWarnBeforeQuit() {
  PrefService* local_state = LocalState();
  if (local_state && local_state->FindPreference(prefs::kConfirmToQuitEnabled)) {
    local_state->SetBoolean(prefs::kConfirmToQuitEnabled,
                            GetBool(kWarnBeforeQuitKey));
  }
}

void CluAppSettings::ApplyAutoUpdate() {
  CluUpdateService::Get().SetAutomatic(GetBool(kAutoUpdateKey));
}

void CluAppSettings::ApplyAutoPictureInPicture(Profile* profile) {
  // A private window keeps Chromium's rule (no auto picture in picture without a site's own permission).
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  HostContentSettingsMap* map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  const ContentSetting setting = GetBool(kAutoPictureInPictureKey)
                                     ? CONTENT_SETTING_ALLOW
                                     : CONTENT_SETTING_BLOCK;
  if (map && map->GetDefaultContentSetting(
                 ContentSettingsType::AUTO_PICTURE_IN_PICTURE) != setting) {
    map->SetDefaultContentSetting(ContentSettingsType::AUTO_PICTURE_IN_PICTURE,
                                  setting);
  }
}

bool CluAppSettings::onboarding_show() {
  if (!onboarding_show_) {
    const std::optional<base::Value> completed =
        StoredGlobal(kOnboardingCompletedAtKey);
    const bool first_run =
        first_run_for_testing_.value_or(first_run::IsChromeFirstRun());
    onboarding_show_ =
        first_run && !IsOnboardingCompleted(completed ? &*completed : nullptr);
  }
  return *onboarding_show_;
}

bool CluAppSettings::CompleteOnboarding(bool skipped) {
  if (!onboarding_show()) {
    return false;
  }
  onboarding_show_ = false;
  // SetGlobal notifies.
  SetGlobal(kOnboardingCompletedAtKey,
            base::Value(base::TimeFormatAsIso8601(clock_->Now())));
  VLOG(1) << "clu: onboarding " << (skipped ? "skipped" : "completed");
  return true;
}

int CluAppSettings::last_sidebar_width() {
  const SettingDef* def = FindSetting(kLastSidebarWidthKey);
  const std::optional<base::Value> stored = StoredGlobal(kLastSidebarWidthKey);
  return static_cast<int>(
      ReadSetting(*def, stored ? &*stored : nullptr).GetDouble());
}

bool CluAppSettings::last_sidebar_hidden() {
  PrefService* local_state = LocalState();
  return local_state && local_state->GetBoolean(kCluSidebarHiddenPref);
}

void CluAppSettings::SetLastSidebarHidden(bool hidden) {
  if (PrefService* local_state = LocalState()) {
    local_state->SetBoolean(kCluSidebarHiddenPref, hidden);
  }
}

bool CluAppSettings::last_toolbar() {
  PrefService* local_state = LocalState();
  return local_state && local_state->GetBoolean(kCluToolbarPref);
}

void CluAppSettings::SetLastToolbar(bool shown) {
  if (PrefService* local_state = LocalState()) {
    local_state->SetBoolean(kCluToolbarPref, shown);
  }
}

std::optional<SpaceTheme> CluAppSettings::ShownTheme(
    std::string_view space_id) {
  EnsureLoaded();
  return themes_.Shown(space_id);
}

bool CluAppSettings::SetSpaceTheme(const std::string& space_id,
                                   const SpaceTheme& theme) {
  EnsureLoaded();
  // A commit also ends a preview, which may change state though the stored value does not.
  const std::optional<SpaceTheme> before = themes_.Shown(space_id);
  if (themes_.Set(space_id, theme)) {
    SaveThemes();
  }
  if (before == theme) {
    return false;
  }
  NotifyChanged();
  return true;
}

bool CluAppSettings::ResetSpaceTheme(std::string_view space_id) {
  EnsureLoaded();
  if (!themes_.Reset(space_id)) {
    return false;
  }
  SaveThemes();
  NotifyChanged();
  return true;
}

bool CluAppSettings::PreviewSpaceTheme(const std::string& space_id,
                                       const SpaceTheme& theme) {
  EnsureLoaded();
  if (!themes_.Preview(space_id, theme)) {
    return false;
  }
  NotifyChanged();
  return true;
}

bool CluAppSettings::RevertPreview(std::string_view space_id) {
  EnsureLoaded();
  if (!themes_.RevertPreview(space_id)) {
    return false;
  }
  NotifyChanged();
  return true;
}

void CluAppSettings::OnSpacesChanged() {
  if (loaded_ && themes_.Retain(SpaceIds())) {
    SaveThemes();
    NotifyChanged();
  }
}

void CluAppSettings::SaveThemes() {
  if (PrefService* local_state = LocalState()) {
    local_state->SetDict(kCluSpaceThemesPref, themes_.Value());
  }
}

void CluAppSettings::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnAppSettingsChanged();
  }
}

void CluAppSettings::SetClockForTesting(const base::Clock* clock) {
  clock_ = clock ? clock : base::DefaultClock::GetInstance();
}

void CluAppSettings::SetFirstRunForTesting(std::optional<bool> first_run) {
  first_run_for_testing_ = first_run;
  onboarding_show_.reset();
  NotifyChanged();
}

void CluAppSettings::ResetForTesting() {
  loaded_ = false;
  onboarding_show_.reset();
  themes_.Load(base::DictValue(), {});
  spaces_observation_.Reset();
}

}  // namespace clu
