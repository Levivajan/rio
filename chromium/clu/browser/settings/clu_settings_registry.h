// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SETTINGS_CLU_SETTINGS_REGISTRY_H_
#define CLU_BROWSER_SETTINGS_CLU_SETTINGS_REGISTRY_H_

#include <string_view>
#include <vector>

#include "base/types/expected.h"
#include "base/values.h"

// Clu's settings registry (docs/PLAN.md 7.3, docs/plan/07-data.md), ported from src/shared/settings.ts: each key
// has a scope, a default and a validator. Reading a key with a missing or invalid stored value gives the default;
// unknown stored keys are ignored and kept. Pure, so clu_unittests covers every key.
//
// Where the values live on Chromium: global keys in Local State (`clu.settings`, CluAppSettings), profile keys in the
// profile (tabs.archiveAfterHours in its clu.db through CluModelService, search.engineId as the profile's
// TemplateURLService default, docs/chromium/m5.md).
namespace clu {

enum class SettingScope { kGlobal, kProfile };

// The keys, by name, so callers never spell them twice.
inline constexpr char kAppearanceModeKey[] = "appearance.mode";
inline constexpr char kOnboardingCompletedAtKey[] = "onboarding.completedAt";
// The app settings window (docs/chromium/parity-settings.md): the AI features, the Links tab. Custom shortcuts
// are kShortcutsCustomKey in clu/browser/commands/clu_shortcuts.h.
inline constexpr char kAiTidyTabsKey[] = "ai.tidyTabs";
inline constexpr char kAiInstantLinksKey[] = "ai.instantLinks";
inline constexpr char kAiHoverPreviewsKey[] = "ai.hoverPreviews";
inline constexpr char kAiTidyTitlesKey[] = "ai.tidyTitles";
inline constexpr char kAiTidyDownloadsKey[] = "ai.tidyDownloads";
inline constexpr char kAiAskRowKey[] = "ai.askRow";
inline constexpr char kAiAskAssistantKey[] = "ai.askAssistant";
inline constexpr char kLinksMiniFromOtherAppsKey[] = "links.miniFromOtherApps";
inline constexpr char kLinksPeekOnShiftClickKey[] = "links.peekOnShiftClick";
inline constexpr char kLinksPeekOtherSitesKey[] = "links.peekOtherSites";
inline constexpr char kLastSidebarWidthKey[] = "shell.lastSidebarWidth";
inline constexpr char kArchiveAfterHoursKey[] = "tabs.archiveAfterHours";
inline constexpr char kSearchEngineIdKey[] = "search.engineId";
// The app icon in the Dock (docs/chromium/icons.md): one of the ids of src/shared/appIcons.ts.
inline constexpr char kAppIconKey[] = "appearance.appIcon";
// General, Warn before quitting: Chromium's own "Hold ⌘Q to quit" (browser.confirm_to_quit) follows it.
inline constexpr char kWarnBeforeQuitKey[] = "general.warnBeforeQuit";
// General, Picture in picture when leaving a tab: a playing video pops out when its tab stops showing.
inline constexpr char kAutoPictureInPictureKey[] = "general.autoPictureInPicture";
// General, Automatically update Rio: Sparkle's scheduled checks and silent downloads follow it.
inline constexpr char kAutoUpdateKey[] = "general.autoUpdate";
// Advanced, Enable Site tweaks on websites you visit: off, no page shows a saved tweak (CluSitePage).
inline constexpr char kSiteTweaksEnabledKey[] = "tweaks.enabled";
// Advanced, Show full URL when Toolbar is enabled: read by the frame's URL pill only; off by default.
inline constexpr char kToolbarFullUrlKey[] = "toolbar.fullUrl";
// Advanced, Haptic feedback when reordering tabs: a sidebar drop that moves tabs ticks the trackpad; on by default.
inline constexpr char kReorderHapticKey[] = "tabs.reorderHaptic";

struct SettingDef {
  std::string_view key;
  SettingScope scope;
  // True when `value` is valid for the key.
  bool (*validate)(const base::Value& value);
  // The default, as JSON text.
  std::string_view default_json;
};

// Every registered key, in src/shared/settings.ts order.
const std::vector<SettingDef>& SettingDefs();
// Null for an unknown key.
const SettingDef* FindSetting(std::string_view key);

// The stored value when it is valid for the key, else the default. `stored` may be null (missing).
base::Value ReadSetting(const SettingDef& def, const base::Value* stored);
// Every key of `scope` resolved from `stored` (a dict of raw stored values), for settings.global and
// settings.byProfile[id] in state.
base::DictValue ResolveSettings(SettingScope scope,
                                const base::DictValue& stored);

enum class SettingError {
  // Not a registered key: the generated guard lets any 64-char key through.
  kUnknownKey,
  // A global key with a profile scope, or the other way round.
  kWrongScope,
  // Not JSON, or not valid for the key.
  kBadValue,
};
std::string_view SettingErrorMessage(SettingError error);

// settings.set: the value parsed from `value_json` when `key` is registered for that scope and the value is valid.
base::expected<base::Value, SettingError> CheckSetting(
    std::string_view key,
    bool global_scope,
    std::string_view value_json);

// onboarding.completedAt: any stored value counts as completed, junk included (never trap a returning user); only
// absent or null does not.
bool IsOnboardingCompleted(const base::Value* completed_at);

}  // namespace clu

#endif  // CLU_BROWSER_SETTINGS_CLU_SETTINGS_REGISTRY_H_
