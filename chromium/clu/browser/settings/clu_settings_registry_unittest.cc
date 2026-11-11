// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/settings/clu_settings_registry.h"

#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

// The settings rules: invalid stored values read as the default, unknown keys and wrong scopes are refused, and
// global and profile keys stay apart.
namespace clu {
namespace {

base::Value Json(std::string_view text) {
  return base::JSONReader::Read(text, base::JSON_PARSE_RFC).value();
}

std::string Text(const base::Value& value) {
  return base::WriteJson(value).value_or("?");
}

TEST(CluSettingsRegistryTest, EveryKeyOfTheSharedRegistryIsHereWithItsScope) {
  ASSERT_EQ(SettingDefs().size(), 23u);
  EXPECT_EQ(FindSetting("appearance.mode")->scope, SettingScope::kGlobal);
  EXPECT_EQ(FindSetting("onboarding.completedAt")->scope, SettingScope::kGlobal);
  EXPECT_EQ(FindSetting("shell.lastSidebarWidth")->scope, SettingScope::kGlobal);
  EXPECT_EQ(FindSetting("tabs.archiveAfterHours")->scope, SettingScope::kProfile);
  EXPECT_EQ(FindSetting("search.engineId")->scope, SettingScope::kProfile);
  EXPECT_EQ(FindSetting("appearance.appIcon")->scope, SettingScope::kGlobal);
  EXPECT_EQ(FindSetting("unknown.key"), nullptr);
}

TEST(CluSettingsRegistryTest, InvalidStoredValuesResolveToDefaults) {
  const base::DictValue stored =
      Json(R"({"appearance.mode":"purple","shell.lastSidebarWidth":9000,
               "onboarding.completedAt":null,"some.other":1})")
          .TakeDict();
  const base::DictValue global = ResolveSettings(SettingScope::kGlobal, stored);
  EXPECT_EQ(Text(*global.Find("appearance.mode")), "\"auto\"");
  EXPECT_EQ(Text(*global.Find("onboarding.completedAt")), "null");
  EXPECT_EQ(Text(*global.Find("shell.lastSidebarWidth")), "214");
  EXPECT_FALSE(global.Find("some.other"));
  EXPECT_EQ(Text(*global.Find("appearance.appIcon")), "\"flatWarm\"");
  const base::DictValue junk_profile =
      Json(R"({"tabs.archiveAfterHours":13,"search.engineId":7})").TakeDict();
  EXPECT_EQ(Text(base::Value(ResolveSettings(SettingScope::kProfile, junk_profile))),
            R"({"search.engineId":"google","tabs.archiveAfterHours":12})");
  // Missing values read as the default too.
  EXPECT_EQ(Text(ReadSetting(*FindSetting("appearance.mode"), nullptr)), "\"auto\"");
}

// docs/chromium/parity-settings.md section 1: the AI features, the Links switches and custom shortcuts.
TEST(CluSettingsRegistryTest, TheAppSettingsWindowKeysAreGlobalWithTheirDefaults) {
  const base::DictValue none;
  const base::DictValue global = ResolveSettings(SettingScope::kGlobal, none);
  for (const char* key :
       {"ai.tidyTabs", "ai.instantLinks", "ai.hoverPreviews", "ai.tidyTitles",
        "ai.tidyDownloads", "ai.askRow", "links.miniFromOtherApps",
        "links.peekOnShiftClick", "links.peekOtherSites",
        "general.warnBeforeQuit", "general.autoPictureInPicture",
        "general.autoUpdate", "tweaks.enabled", "tabs.reorderHaptic"}) {
    ASSERT_TRUE(FindSetting(key)) << key;
    EXPECT_EQ(FindSetting(key)->scope, SettingScope::kGlobal) << key;
    EXPECT_EQ(global.FindBool(key), true) << key;
    EXPECT_TRUE(CheckSetting(key, true, "false").has_value()) << key;
    EXPECT_FALSE(CheckSetting(key, true, "1").has_value()) << key;
    EXPECT_FALSE(CheckSetting(key, false, "true").has_value()) << key;
  }
  EXPECT_EQ(global.FindBool("toolbar.fullUrl"), false);
  EXPECT_EQ(FindSetting("toolbar.fullUrl")->scope, SettingScope::kGlobal);
  EXPECT_TRUE(CheckSetting("toolbar.fullUrl", true, "true").has_value());
  EXPECT_FALSE(CheckSetting("toolbar.fullUrl", true, "\"yes\"").has_value());
  EXPECT_EQ(*global.FindString("ai.askAssistant"), "chatgpt");
  EXPECT_TRUE(CheckSetting("ai.askAssistant", true, "\"grok\"").has_value());
  EXPECT_FALSE(CheckSetting("ai.askAssistant", true, "\"bard\"").has_value());
  EXPECT_EQ(Text(*global.Find("shortcuts.custom")), "{}");
  EXPECT_TRUE(CheckSetting("shortcuts.custom", true,
                           R"({"tabs.close":"Cmd+E","history.open":null})")
                  .has_value());
  EXPECT_FALSE(
      CheckSetting("shortcuts.custom", true, R"({"tabs.close":"Cmd+Q"})")
          .has_value());
  // Junk stored reads as no custom keys.
  const base::DictValue junk =
      Json(R"({"shortcuts.custom":["Cmd+K"],"ai.askRow":"yes"})").TakeDict();
  const base::DictValue read = ResolveSettings(SettingScope::kGlobal, junk);
  EXPECT_EQ(Text(*read.Find("shortcuts.custom")), "{}");
  EXPECT_EQ(read.FindBool("ai.askRow"), true);
}

TEST(CluSettingsRegistryTest, ValidStoredValuesAreKept) {
  const base::DictValue stored =
      Json(R"({"appearance.mode":"dark","shell.lastSidebarWidth":300.0,
               "onboarding.completedAt":"2026-11-08T01:00:00.000Z"})")
          .TakeDict();
  const base::DictValue global = ResolveSettings(SettingScope::kGlobal, stored);
  EXPECT_EQ(*global.FindString("appearance.mode"), "dark");
  EXPECT_EQ(global.FindDouble("shell.lastSidebarWidth"), 300);
  EXPECT_EQ(*global.FindString("onboarding.completedAt"),
            "2026-11-08T01:00:00.000Z");
}

TEST(CluSettingsRegistryTest, SetRefusesUnknownKeysWrongScopesAndBadValues) {
  EXPECT_EQ(CheckSetting("nope", true, "1").error(), SettingError::kUnknownKey);
  // A profile key with the global scope, and a global key with a profile id.
  EXPECT_EQ(CheckSetting("tabs.archiveAfterHours", true, "24").error(),
            SettingError::kWrongScope);
  EXPECT_EQ(CheckSetting("appearance.mode", false, "\"dark\"").error(),
            SettingError::kWrongScope);
  for (const char* junk : {"\"purple\"", "1", "null", "{", "", "[\"dark\"]"}) {
    EXPECT_EQ(CheckSetting("appearance.mode", true, junk).error(),
              SettingError::kBadValue)
        << junk;
  }
  for (const char* junk : {"163", "501", "260.5", "\"260\"", "1e400"}) {
    EXPECT_EQ(CheckSetting("shell.lastSidebarWidth", true, junk).error(),
              SettingError::kBadValue)
        << junk;
  }
  for (const char* junk : {"13", "0", "-12", "\"12\"", "true"}) {
    EXPECT_EQ(CheckSetting("tabs.archiveAfterHours", false, junk).error(),
              SettingError::kBadValue)
        << junk;
  }
  EXPECT_EQ(CheckSetting("search.engineId", false, "\"yahoo\"").error(),
            SettingError::kBadValue);
  // Deeply nested JSON is refused before any validator runs.
  std::string deep;
  for (int i = 0; i < 64; ++i) {
    deep += "[";
  }
  for (int i = 0; i < 64; ++i) {
    deep += "]";
  }
  EXPECT_EQ(CheckSetting("onboarding.completedAt", true, deep).error(),
            SettingError::kBadValue);
}

TEST(CluSettingsRegistryTest, SetAcceptsValidValues) {
  EXPECT_EQ(Text(CheckSetting("appearance.mode", true, "\"light\"").value()),
            "\"light\"");
  EXPECT_EQ(Text(CheckSetting("shell.lastSidebarWidth", true, "420").value()),
            "420");
  EXPECT_EQ(Text(CheckSetting("tabs.archiveAfterHours", false, "168").value()),
            "168");
  EXPECT_TRUE(CheckSetting("search.engineId", false, "\"duckduckgo\"").has_value());
  EXPECT_TRUE(CheckSetting("onboarding.completedAt", true, "{\"any\":[1]}").has_value());
}

// docs/chromium/icons.md: Flat warm (flatWarm) unless a valid alternate is stored; ids are case-sensitive. A Sunset
// stored before Flat warm became the default stays Sunset.
TEST(CluSettingsRegistryTest, AppIconDefaultsToFlatWarmAndTakesTheSevenIds) {
  const SettingDef& def = *FindSetting("appearance.appIcon");
  EXPECT_EQ(Text(ReadSetting(def, nullptr)), "\"flatWarm\"");
  const base::Value sunset = Json("\"airySun\"");
  EXPECT_EQ(Text(ReadSetting(def, &sunset)), "\"airySun\"");
  for (const char* junk : {"\"Ocean\"", "\"neon\"", "\"\"", "7", "null", "[\"dusk\"]", "{\"id\":\"dusk\"}"}) {
    const base::Value stored = Json(junk);
    EXPECT_EQ(Text(ReadSetting(def, &stored)), "\"flatWarm\"") << junk;
    EXPECT_EQ(CheckSetting("appearance.appIcon", true, junk).error(), SettingError::kBadValue) << junk;
  }
  for (const char* id : {"flatWarm", "airySun", "ocean", "flatBlue", "sunset", "warm", "dusk"}) {
    const std::string json = std::string("\"") + id + "\"";
    EXPECT_EQ(Text(CheckSetting("appearance.appIcon", true, json).value()), json);
  }
  EXPECT_EQ(CheckSetting("appearance.appIcon", false, "\"dusk\"").error(), SettingError::kWrongScope);
}

TEST(CluSettingsRegistryTest, OnboardingCountsAnyStoredValueAsCompleted) {
  EXPECT_FALSE(IsOnboardingCompleted(nullptr));
  const base::Value null_value;
  EXPECT_FALSE(IsOnboardingCompleted(&null_value));
  for (const char* stored : {"\"2026-11-08T01:00:00.000Z\"", "\"junk\"", "0", "false", "{}", "[]"}) {
    const base::Value value = Json(stored);
    EXPECT_TRUE(IsOnboardingCompleted(&value)) << stored;
  }
}

}  // namespace
}  // namespace clu
