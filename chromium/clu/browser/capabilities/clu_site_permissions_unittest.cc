// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_site_permissions.h"

#include <set>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

class CluSitePermissionsTest : public testing::Test {
 protected:
  void SetUp() override {
    HostContentSettingsMap::RegisterProfilePrefs(prefs_.registry());
    map_ = base::MakeRefCounted<HostContentSettingsMap>(
        &prefs_, /*is_off_the_record=*/false, /*store_last_modified=*/true,
        /*restore_session=*/false, /*should_record_metrics=*/false);
  }
  void TearDown() override { map_->ShutdownOnUIThread(); }

  HostContentSettingsMap& map() { return *map_; }

  static GURL Origin(const std::string& url) {
    return SitePermissionOrigin(url).value_or(GURL());
  }

  static const SitePermissionKind& Kind(const std::string& name) {
    return *FindSitePermission(name);
  }

  base::test::TaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  scoped_refptr<HostContentSettingsMap> map_;
};

TEST_F(CluSitePermissionsTest, KindsStartWithTheContractsSixAndAreUnique) {
  const auto kinds = SitePermissionKinds();
  ASSERT_GE(kinds.size(), 6u);
  const char* const kContract[] = {"camera",        "microphone", "location",
                                   "notifications", "clipboard",  "popups"};
  size_t i = 0;
  for (const char* name : kContract) {
    EXPECT_EQ(kinds[i++].name, name);
  }
  std::set<std::string_view> names;
  std::set<ContentSettingsType> types;
  for (const SitePermissionKind& kind : kinds) {
    EXPECT_TRUE(names.insert(kind.name).second) << kind.name;
    EXPECT_TRUE(types.insert(kind.type).second) << kind.name;
    EXPECT_LE(kind.name.size(), 64u) << kind.name;
  }
  EXPECT_EQ(Kind("camera").type, ContentSettingsType::MEDIASTREAM_CAMERA);
  EXPECT_EQ(Kind("microphone").type, ContentSettingsType::MEDIASTREAM_MIC);
  EXPECT_EQ(Kind("location").type, ContentSettingsType::GEOLOCATION);
  EXPECT_EQ(Kind("notifications").type, ContentSettingsType::NOTIFICATIONS);
  EXPECT_EQ(FindSitePermission("junk"), nullptr);
  EXPECT_EQ(FindSitePermission(""), nullptr);
  EXPECT_EQ(FindSitePermission("Camera"), nullptr);
}

TEST_F(CluSitePermissionsTest, OriginsAreWebOriginsOnly) {
  EXPECT_EQ(SitePermissionOrigin("https://www.example.com/a/b?c#d"),
            GURL("https://www.example.com/"));
  EXPECT_EQ(SitePermissionOrigin("http://example.com:8080/x"),
            GURL("http://example.com:8080/"));
  for (const char* junk :
       {"", "not a url", "https://", "chrome://settings/content",
        "chrome://clu-frame/", "file:///tmp/x.html", "javascript:alert(1)",
        "data:text/html,hi", "about:blank", "blob:https://example.com/x",
        "ftp://example.com/"}) {
    EXPECT_EQ(SitePermissionOrigin(junk), std::nullopt) << junk;
  }
}

TEST_F(CluSitePermissionsTest, SetAllowAndBlockReachChromiumsSettings) {
  const GURL site = Origin("https://example.com/page");
  ASSERT_TRUE(SetSitePermission(map(), site, Kind("camera"),
                                SitePermissionDecision::kAllow));
  ASSERT_TRUE(SetSitePermission(map(), site, Kind("notifications"),
                                SitePermissionDecision::kBlock));
  // What Chromium's prompts, permission checks and chrome://settings/content read.
  EXPECT_EQ(map().GetContentSetting(site, site,
                                    ContentSettingsType::MEDIASTREAM_CAMERA),
            CONTENT_SETTING_ALLOW);
  EXPECT_EQ(
      map().GetContentSetting(site, site, ContentSettingsType::NOTIFICATIONS),
      CONTENT_SETTING_BLOCK);
  // In the profile's prefs, so it sticks across launches.
  EXPECT_FALSE(prefs_.GetDict("profile.content_settings.exceptions.media_stream_camera")
                   .empty());

  const std::vector<SitePermissionRow> rows = ListSitePermissions(map(), site);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].origin, "https://example.com");
  EXPECT_EQ(rows[0].permission, "camera");
  EXPECT_TRUE(rows[0].allow);
  EXPECT_GT(rows[0].updated_at_ms, 0);
  EXPECT_EQ(rows[1].permission, "notifications");
  EXPECT_FALSE(rows[1].allow);
  // Another site has none, and the whole list has the site's two.
  EXPECT_TRUE(ListSitePermissions(map(), Origin("https://other.com")).empty());
  EXPECT_EQ(ListSitePermissions(map(), std::nullopt), rows);
}

TEST_F(CluSitePermissionsTest, AskRemovesTheDecisionAndChromiumsDefaultComesBack) {
  const GURL site = Origin("https://example.com");
  ASSERT_TRUE(SetSitePermission(map(), site, Kind("location"),
                                SitePermissionDecision::kBlock));
  ASSERT_EQ(ListSitePermissions(map(), site).size(), 1u);
  EXPECT_TRUE(SetSitePermission(map(), site, Kind("location"),
                                SitePermissionDecision::kAsk));
  EXPECT_TRUE(ListSitePermissions(map(), site).empty());
  EXPECT_EQ(map().GetContentSetting(site, site, ContentSettingsType::GEOLOCATION),
            CONTENT_SETTING_ASK);
  // Ask on a site with no decision is fine.
  EXPECT_TRUE(SetSitePermission(map(), site, Kind("camera"),
                                SitePermissionDecision::kAsk));
}

TEST_F(CluSitePermissionsTest, ChromiumsOwnDecisionsAndWiderRulesAreListed) {
  // A prompt's answer (the default scope), and a chrome://settings rule for a whole domain.
  const GURL prompted = Origin("https://maps.example.org");
  map().SetContentSettingDefaultScope(prompted, prompted,
                                      ContentSettingsType::GEOLOCATION,
                                      CONTENT_SETTING_ALLOW);
  map().SetContentSettingCustomScope(
      ContentSettingsPattern::FromString("[*.]example.org"),
      ContentSettingsPattern::Wildcard(), ContentSettingsType::POPUPS,
      CONTENT_SETTING_ALLOW);

  const std::vector<SitePermissionRow> mine =
      ListSitePermissions(map(), prompted);
  ASSERT_EQ(mine.size(), 2u);
  EXPECT_EQ(mine[0].permission, "location");
  EXPECT_EQ(mine[1].permission, "popups");
  EXPECT_TRUE(mine[1].allow);
  // The settings list shows the domain rule under its own origin.
  std::set<std::string> listed;
  for (const SitePermissionRow& row : ListSitePermissions(map(), std::nullopt)) {
    listed.insert(row.origin + " " + row.permission);
  }
  EXPECT_EQ(listed, (std::set<std::string>{"https://maps.example.org location",
                                           "https://example.org popups"}));

  // Clearing the site removes the rules that apply to it, the domain rule too.
  ClearSitePermissions(map(), prompted, nullptr);
  EXPECT_TRUE(ListSitePermissions(map(), prompted).empty());
  EXPECT_TRUE(ListSitePermissions(map(), std::nullopt).empty());
}

TEST_F(CluSitePermissionsTest, DefaultsAreNoRowsAndSurviveAClear) {
  map().SetDefaultContentSetting(ContentSettingsType::NOTIFICATIONS,
                                 CONTENT_SETTING_BLOCK);
  const GURL site = Origin("https://example.com");
  EXPECT_TRUE(ListSitePermissions(map(), site).empty());
  EXPECT_TRUE(ListSitePermissions(map(), std::nullopt).empty());

  ASSERT_TRUE(SetSitePermission(map(), site, Kind("microphone"),
                                SitePermissionDecision::kAllow));
  ASSERT_TRUE(SetSitePermission(map(), Origin("https://b.com"), Kind("sound"),
                                SitePermissionDecision::kBlock));
  ASSERT_EQ(ListSitePermissions(map(), std::nullopt).size(), 2u);
  // Reset one kind on one site, then everything.
  ClearSitePermissions(map(), site, &Kind("camera"));
  EXPECT_EQ(ListSitePermissions(map(), std::nullopt).size(), 2u);
  ClearSitePermissions(map(), std::nullopt, nullptr);
  EXPECT_TRUE(ListSitePermissions(map(), std::nullopt).empty());
  EXPECT_EQ(map().GetDefaultContentSetting(ContentSettingsType::NOTIFICATIONS),
            CONTENT_SETTING_BLOCK);
}

}  // namespace
}  // namespace clu
