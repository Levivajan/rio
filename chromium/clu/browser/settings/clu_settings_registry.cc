// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/settings/clu_settings_registry.h"

#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "clu/browser/commands/clu_shortcuts.h"

namespace clu {

namespace {

// The JSON text a settings.set value may hold: the generated guard already capped the string at the payload cap.
constexpr size_t kMaxValueDepth = 16;

bool IsModeValue(const base::Value& v) {
  return v.is_string() &&
         (v.GetString() == "auto" || v.GetString() == "light" ||
          v.GetString() == "dark");
}

// Junk counts as completed (PLAN 7.3), so any stored JSON value is kept as it is.
bool IsAnyValue(const base::Value&) {
  return true;
}

// An integer in [min, max]: 300 and 300.0 both count, 300.5 does not.
bool IsIntIn(const base::Value& v, double min, double max) {
  if (!v.is_int() && !v.is_double()) {
    return false;
  }
  const double d = v.GetDouble();
  return std::isfinite(d) && d == std::trunc(d) && d >= min && d <= max;
}

bool IsSidebarWidth(const base::Value& v) {
  return IsIntIn(v, 164, 500);
}

bool IsBool(const base::Value& v) {
  return v.is_bool();
}

bool IsAssistant(const base::Value& v) {
  return v.is_string() && (v.GetString() == "chatgpt" ||
                           v.GetString() == "claude" || v.GetString() == "grok");
}

bool IsArchiveAfter(const base::Value& v) {
  if (!IsIntIn(v, 12, 720)) {
    return false;
  }
  const double d = v.GetDouble();
  return d == 12 || d == 24 || d == 168 || d == 720;
}

bool IsEngineId(const base::Value& v) {
  if (!v.is_string()) {
    return false;
  }
  const std::string& s = v.GetString();
  return s == "google" || s == "duckduckgo" || s == "bing" || s == "brave" ||
         s == "ecosia";
}

// src/shared/appIcons.ts: the default first, then the six alternates.
bool IsAppIcon(const base::Value& v) {
  if (!v.is_string()) {
    return false;
  }
  const std::string& s = v.GetString();
  return s == "flatWarm" || s == "airySun" || s == "ocean" ||
         s == "flatBlue" || s == "sunset" || s == "warm" || s == "dusk";
}

base::Value ParseDefault(std::string_view json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  CHECK(value) << json;
  return std::move(*value);
}

}  // namespace

const std::vector<SettingDef>& SettingDefs() {
  static const base::NoDestructor<std::vector<SettingDef>> defs(
      std::vector<SettingDef>{
          {kAppearanceModeKey, SettingScope::kGlobal, &IsModeValue, "\"auto\""},
          {kOnboardingCompletedAtKey, SettingScope::kGlobal, &IsAnyValue,
           "null"},
          {kAiTidyTabsKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAiInstantLinksKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAiHoverPreviewsKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAiTidyTitlesKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAiTidyDownloadsKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAiAskRowKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAiAskAssistantKey, SettingScope::kGlobal, &IsAssistant,
           "\"chatgpt\""},
          {kLinksMiniFromOtherAppsKey, SettingScope::kGlobal, &IsBool, "true"},
          {kLinksPeekOnShiftClickKey, SettingScope::kGlobal, &IsBool, "true"},
          {kLinksPeekOtherSitesKey, SettingScope::kGlobal, &IsBool, "true"},
          {kShortcutsCustomKey, SettingScope::kGlobal, &IsValidCustomShortcuts,
           "{}"},
          {kLastSidebarWidthKey, SettingScope::kGlobal, &IsSidebarWidth,
           "214"},
          {kArchiveAfterHoursKey, SettingScope::kProfile, &IsArchiveAfter,
           "12"},
          {kSearchEngineIdKey, SettingScope::kProfile, &IsEngineId,
           "\"google\""},
          {kAppIconKey, SettingScope::kGlobal, &IsAppIcon, "\"flatWarm\""},
          {kWarnBeforeQuitKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAutoPictureInPictureKey, SettingScope::kGlobal, &IsBool, "true"},
          {kAutoUpdateKey, SettingScope::kGlobal, &IsBool, "true"},
          {kSiteTweaksEnabledKey, SettingScope::kGlobal, &IsBool, "true"},
          {kToolbarFullUrlKey, SettingScope::kGlobal, &IsBool, "false"},
          {kReorderHapticKey, SettingScope::kGlobal, &IsBool, "true"},
      });
  return *defs;
}

const SettingDef* FindSetting(std::string_view key) {
  for (const SettingDef& def : SettingDefs()) {
    if (def.key == key) {
      return &def;
    }
  }
  return nullptr;
}

base::Value ReadSetting(const SettingDef& def, const base::Value* stored) {
  if (stored && def.validate(*stored)) {
    return stored->Clone();
  }
  return ParseDefault(def.default_json);
}

base::DictValue ResolveSettings(SettingScope scope,
                                const base::DictValue& stored) {
  base::DictValue out;
  for (const SettingDef& def : SettingDefs()) {
    if (def.scope == scope) {
      out.Set(def.key, ReadSetting(def, stored.Find(def.key)));
    }
  }
  return out;
}

std::string_view SettingErrorMessage(SettingError error) {
  switch (error) {
    case SettingError::kUnknownKey:
      return "unknown setting";
    case SettingError::kWrongScope:
      return "wrong scope for this setting";
    case SettingError::kBadValue:
      return "invalid value for this setting";
  }
}

base::expected<base::Value, SettingError> CheckSetting(
    std::string_view key,
    bool global_scope,
    std::string_view value_json) {
  const SettingDef* def = FindSetting(key);
  if (!def) {
    return base::unexpected(SettingError::kUnknownKey);
  }
  if ((def->scope == SettingScope::kGlobal) != global_scope) {
    return base::unexpected(SettingError::kWrongScope);
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(value_json, base::JSON_PARSE_RFC, kMaxValueDepth);
  if (!value || !def->validate(*value)) {
    return base::unexpected(SettingError::kBadValue);
  }
  return std::move(*value);
}

bool IsOnboardingCompleted(const base::Value* completed_at) {
  return completed_at && !completed_at->is_none();
}

}  // namespace clu
