// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_site_state.h"

#include <string>

#include "base/values.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

// The M7 fields of CluState (src/shared/state/extensions.ts, tweaks.ts): every tab carries its site fields, the window
// its action row, the window's own profile its extensions; extension text is capped, oversized icons drop.
namespace clu {
namespace {

constexpr char kWindow[] = "00000000-0000-4000-8000-000100000007";
constexpr char kProfile[] = "00000000-0000-4000-8000-000200000007";
constexpr char kSpace[] = "00000000-0000-4000-8000-000300000007";
constexpr char kTab[] = "00000000-0000-4000-8000-000500000007";

WindowSnapshot Window() {
  WindowSnapshot s;
  s.version = "158.0.8127.18";
  s.window_id = kWindow;
  s.profile_id = kProfile;
  s.profile_name = "Personal";
  s.space_id = kSpace;
  TabSnapshot tab;
  tab.id = kTab;
  tab.url = "https://a.test/";
  s.tabs = {tab};
  s.layout = ComputeShellLayout(gfx::Size(1280, 800), s.chrome);
  return s;
}

TEST(CluSiteStateTest, ATabWithoutASiteHasTheDefaults) {
  const base::DictValue tab = SiteTabValue(SiteTabSnapshot());
  EXPECT_TRUE(tab.Find("site")->is_none());
  EXPECT_EQ(tab.FindInt("blockedCount"), 0);
  EXPECT_EQ(tab.FindBool("blockingEnabled"), true);
  EXPECT_TRUE(tab.Find("tweak")->is_none());
  EXPECT_TRUE(tab.Find("zapMode")->is_none());
}

TEST(CluSiteStateTest, SiteCountTweakAndZapModeReachTheTab) {
  WindowSnapshot s = Window();
  s.tabs[0].site.site = "a.test";
  s.tabs[0].site.blocked_count = 3;
  s.tabs[0].site.blocking_enabled = false;
  s.tabs[0].site.tweak =
      TweakSummarySnapshot{.enabled = false, .has_css = true, .zap_count = 2};
  s.tabs[0].site.zap_active = true;
  s.tabs[0].site.zap_last_result = "Hid 3 related elements";
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* tab = state.FindDictByDottedPath("tabs");
  ASSERT_TRUE(tab);
  tab = tab->FindDict(kTab);
  ASSERT_TRUE(tab);
  EXPECT_EQ(*tab->FindString("site"), "a.test");
  EXPECT_EQ(tab->FindInt("blockedCount"), 3);
  EXPECT_EQ(tab->FindBool("blockingEnabled"), false);
  EXPECT_EQ(tab->FindBoolByDottedPath("tweak.enabled"), false);
  EXPECT_EQ(tab->FindBoolByDottedPath("tweak.hasCss"), true);
  EXPECT_EQ(tab->FindBoolByDottedPath("tweak.hasJs"), false);
  EXPECT_EQ(tab->FindIntByDottedPath("tweak.zapCount"), 2);
  EXPECT_EQ(tab->FindBoolByDottedPath("zapMode.active"), true);
  EXPECT_EQ(*tab->FindStringByDottedPath("zapMode.lastResult"),
            "Hid 3 related elements");
}

// A Mini window's page is no tab row: its site fields ride on windows[id].miniPage, null in a main window.
TEST(CluSiteStateTest, TheMiniWindowsPageCarriesItsSiteFields) {
  WindowSnapshot s = Window();
  const std::string path = std::string("windows.") + kWindow;
  EXPECT_TRUE(
      BuildCluState(s).FindDictByDottedPath(path)->Find("miniPage")->is_none());
  MiniSnapshot mini;
  mini.tab_id = kTab;
  mini.url = "https://a.test/";
  mini.site.site = "a.test";
  mini.site.blocked_count = 4;
  mini.site.blocking_enabled = false;
  s.kind = "mini";
  s.mini = mini;
  s.tabs.clear();
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* page =
      state.FindDictByDottedPath(path)->FindDict("miniPage");
  ASSERT_TRUE(page);
  EXPECT_EQ(*page->FindString("site"), "a.test");
  EXPECT_EQ(page->FindInt("blockedCount"), 4);
  EXPECT_EQ(page->FindBool("blockingEnabled"), false);
  // Developer Mode rides along, off unless the site turned it on.
  EXPECT_EQ(page->FindBool("devMode"), false);
  EXPECT_EQ(page->size(), 4u);
}

TEST(CluSiteStateTest, TheActionRowAndTheProfilesExtensions) {
  WindowSnapshot s = Window();
  EXPECT_TRUE(BuildCluState(s)
                  .FindDictByDottedPath(std::string("windows.") + kWindow)
                  ->Find("actionRow")
                  ->is_none());
  ActionRowSnapshot row;
  row.profile_id = kProfile;
  ActionButtonSnapshot a;
  a.id = std::string(32, 'a');
  a.name = "Notes";
  a.title = "Notes";
  a.icon = "data:image/png;base64,AA==";
  a.badge = "7";
  a.pinned = true;
  row.actions = {a};
  s.action_row = row;
  ExtensionInfoSnapshot info;
  info.id = a.id;
  info.name = "Notes";
  info.version = "1.0";
  info.permissions = {"Read your browsing history"};
  s.extensions = {info};
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* window =
      state.FindDictByDottedPath(std::string("windows.") + kWindow);
  const base::DictValue* action_row = window->FindDict("actionRow");
  ASSERT_TRUE(action_row);
  EXPECT_EQ(*action_row->FindString("profileId"), kProfile);
  const base::DictValue& button = (*action_row->FindList("actions"))[0].GetDict();
  EXPECT_EQ(*button.FindString("badge"), "7");
  EXPECT_EQ(button.FindBool("pinned"), true);
  const base::ListValue* extensions =
      state.FindDictByDottedPath(std::string("profiles.") + kProfile)
          ->FindList("extensions");
  ASSERT_TRUE(extensions);
  ASSERT_EQ(extensions->size(), 1u);
  EXPECT_EQ(*(*extensions)[0].GetDict().FindString("name"), "Notes");
  EXPECT_EQ((*extensions)[0].GetDict().FindBool("pinned"), false);
}

TEST(CluSiteStateTest, ExtensionTextIsCappedAndHugeIconsDrop) {
  ActionRowSnapshot row;
  ActionButtonSnapshot a;
  a.id = std::string(32, 'b');
  a.name = std::string(1000, 'n');
  a.title = "\xF0\x9F\x98\x80" + std::string(300, 't');
  a.badge = std::string(40, '9');
  a.icon = std::string(kMaxIconDataUrl + 1, 'i');
  row.actions = {a};
  const base::Value value = ActionRowValue(row);
  const base::DictValue& button =
      (*value.GetDict().FindList("actions"))[0].GetDict();
  EXPECT_EQ(button.FindString("name")->size(), kMaxActionText);
  EXPECT_LE(button.FindString("title")->size(), kMaxActionText);
  EXPECT_EQ(button.FindString("badge")->size(), kMaxBadgeText);
  EXPECT_TRUE(button.Find("icon")->is_none());
  ExtensionInfoSnapshot info;
  info.permissions.assign(100, "p");
  EXPECT_EQ(ExtensionInfoValue(info).FindList("permissions")->size(),
            kMaxPermissionLines);
  // A cut never splits a UTF-8 sequence.
  EXPECT_EQ(CapUtf8("\xF0\x9F\x98\x80", 3), "");
}

}  // namespace
}  // namespace clu
