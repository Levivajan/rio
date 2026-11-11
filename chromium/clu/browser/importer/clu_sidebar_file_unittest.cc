// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_sidebar_file.h"

#include <algorithm>
#include <array>
#include <string>
#include <tuple>
#include <vector>

#include "base/check_op.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

// The parser cases on the import fixture (2 source profiles with 2 spaces each, nested lists, a split view, a renamed
// tab, Today containers, a collapsed folder, a theme per space, an unknown item kind, a childrenIds cycle and a
// missing child id). The writing half is CluImportBrowserTest.
namespace clu {
namespace {

std::string FixtureText() {
  std::string text;
  CHECK(base::ReadFileToString(SidebarFilePath(ImportFixtureRoot()), &text));
  return text;
}

std::string File(const std::string& main) {
  return R"({"sidebar":{"containers":[{"global":{}},)" + main + R"(]},"version":1})";
}

using Row = std::tuple<bool, std::string, std::optional<std::string>, bool>;

std::vector<Row> Children(const std::vector<ImportNode>& nodes, const std::string& parent) {
  std::vector<Row> out;
  for (const ImportNode& n : nodes) {
    if (n.parent_key == parent) {
      out.emplace_back(n.folder, n.title, n.title_override, n.collapsed);
    }
  }
  return out;
}

TEST(CluSidebarFileTest, ReadsTheFixtureIntoProfilesSpacesAndSkips) {
  const SidebarPlan plan = ParseSidebarFile(FixtureText());
  ASSERT_FALSE(plan.error);
  ASSERT_EQ(plan.profiles.size(), 2u);
  EXPECT_EQ(plan.profiles[0].key, "default");
  EXPECT_EQ(plan.profiles[1].key, "custom:Profile 1");
  // Before favorites are written: the repeated URL is a duplicate only then.
  EXPECT_EQ(plan.skipped.unsupported, 2);
  EXPECT_EQ(plan.skipped.missing, 1);
  EXPECT_EQ(plan.skipped.cycles, 1);
  EXPECT_EQ(plan.skipped.bad_urls, 1);
  EXPECT_EQ(plan.skipped.over_cap, 0);

  ASSERT_EQ(plan.spaces.size(), 4u);
  EXPECT_EQ(plan.spaces[0].name, "Work");
  EXPECT_EQ(plan.spaces[0].emoji, "\U0001F4BC");
  EXPECT_EQ(plan.spaces[0].profile_key, "default");
  EXPECT_EQ(plan.spaces[1].name, "Reading");
  EXPECT_EQ(plan.spaces[1].emoji, std::nullopt);
  EXPECT_EQ(plan.spaces[2].name, "Side project");
  EXPECT_EQ(plan.spaces[2].profile_key, "custom:Profile 1");
  EXPECT_EQ(plan.spaces[3].name, "Recipes");
  EXPECT_EQ(plan.spaces[3].theme, std::nullopt);

  // Themes: background colors first, factors kept.
  const std::optional<SpaceTheme>& work_theme = plan.spaces[0].theme;
  ASSERT_TRUE(work_theme);
  ASSERT_EQ(work_theme->colors.size(), 2u);
  EXPECT_NEAR(work_theme->colors[0].h, 20, 0.5);
  EXPECT_NEAR(work_theme->colors[0].s, 0.882, 0.005);
  EXPECT_NEAR(work_theme->colors[1].h, 248.571, 0.005);
  EXPECT_DOUBLE_EQ(work_theme->intensity, 0.6);
  EXPECT_DOUBLE_EQ(work_theme->grain, 0.25);
  ASSERT_TRUE(plan.spaces[1].theme);
  EXPECT_DOUBLE_EQ(plan.spaces[1].theme->intensity, 0.3);
  EXPECT_DOUBLE_EQ(plan.spaces[1].theme->grain, 0);

  // Work: the renamed tab, then the collapsed folder with the split view's tabs in its place and a nested folder.
  const std::vector<ImportNode>& work = plan.spaces[0].pinned;
  EXPECT_EQ(Children(work, ""),
            (std::vector<Row>{{false, "Mail", "Inbox", false}, {true, "Projects", std::nullopt, true}}));
  EXPECT_EQ(Children(work, "item:l-projects"),
            (std::vector<Row>{{false, "Roadmap", std::nullopt, false},
                              {false, "Left pane", std::nullopt, false},
                              {false, "right.example", std::nullopt, false},
                              {true, "Deep", std::nullopt, false}}));
  EXPECT_EQ(Children(work, "item:l-deep"),
            (std::vector<Row>{{false, "Deep page", std::nullopt, false}}));
  // The split view's two tabs are also a split, side by side.
  ASSERT_EQ(plan.spaces[0].splits.size(), 1u);
  EXPECT_EQ(plan.spaces[0].splits[0].keys,
            (std::vector<std::string>{"item:t-left", "item:t-right"}));
  EXPECT_FALSE(plan.spaces[0].splits[0].stacked);
  EXPECT_TRUE(plan.spaces[1].splits.empty());
  // Reading: the cycle is cut after one visit.
  EXPECT_EQ(Children(plan.spaces[1].pinned, ""),
            (std::vector<Row>{{false, "Paper", std::nullopt, false}, {true, "Loop", std::nullopt, false}}));
  EXPECT_EQ(Children(plan.spaces[1].pinned, "item:l-loop"),
            (std::vector<Row>{{false, "Essay", std::nullopt, false}}));
  EXPECT_EQ(plan.spaces[2].pinned.size(), 2u);
  EXPECT_EQ(plan.spaces[3].pinned.size(), 1u);

  // Today tabs per space, top to bottom; the welcome item beside them is the second unsupported item.
  auto today = [&](size_t space) {
    std::vector<std::string> titles;
    for (const ImportNode& n : plan.spaces[space].today) {
      EXPECT_FALSE(n.folder);
      EXPECT_TRUE(n.parent_key.empty());
      titles.push_back(n.title);
    }
    return titles;
  };
  EXPECT_EQ(today(0), (std::vector<std::string>{"Today 1", "Today 2"}));
  EXPECT_TRUE(today(1).empty());
  EXPECT_TRUE(today(2).empty());
  ASSERT_EQ(today(3).size(), 1u);
  EXPECT_EQ(plan.spaces[3].today[0].url, "https://today.example/3");

  // Favorites per profile, in order (the repeated URL is dropped when writing).
  std::vector<std::string> favorites;
  for (const ImportNode& n : plan.profiles[0].favorites) {
    favorites.push_back(n.title);
  }
  EXPECT_EQ(favorites, (std::vector<std::string>{"Calendar", "Calendar again", "Music"}));
  ASSERT_EQ(plan.profiles[1].favorites.size(), 1u);
  EXPECT_EQ(plan.profiles[1].favorites[0].title, "Board");
  for (const SidebarSpacePlan& s : plan.spaces) {
    for (const ImportNode& n : s.pinned) {
      EXPECT_FALSE(n.url.starts_with("javascript:"));
    }
  }
}

TEST(CluSidebarFileTest, AFileWithoutSpacesIsUnsupported) {
  for (const std::string& text :
       {File(R"({"items":[]})"), std::string(R"({"sidebar":{"containers":[]}})"),
        std::string(R"({"sidebar": {)"), std::string("[]"), std::string()}) {
    const SidebarPlan plan = ParseSidebarFile(text);
    EXPECT_EQ(plan.error, kSidebarUnsupported) << text;
    EXPECT_TRUE(plan.spaces.empty());
    EXPECT_TRUE(plan.profiles.empty());
  }
}

TEST(CluSidebarFileTest, ReadsPairsIgnoresOddTailsAndCapsAt10000Nodes) {
  std::string ids, items;
  for (size_t i = 0; i < kMaxImportItems + 5; ++i) {
    const std::string id = "t" + base::NumberToString(i);
    ids += (i ? ",\"" : "\"") + id + "\"";
    items += ",\"" + id + R"(",{"id":")" + id +
             R"(","childrenIds":[],"title":null,"data":{"tab":{"savedURL":"https://x.example/)" + id +
             R"(","savedTitle":")" + id + R"("}}})";
  }
  const std::string main =
      R"({"spaces":["s",{"title":"S","containerIDs":["pinned","c","unpinned"]},"odd"],"items":["c",{"id":"c","childrenIds":[)" +
      ids + R"(],"data":{"itemContainer":{}}})" + items +
      R"(,"tail"],"topAppsContainerIDs":[{"default":{}}]})";
  const SidebarPlan plan = ParseSidebarFile(File(main));
  ASSERT_FALSE(plan.error);
  ASSERT_EQ(plan.spaces.size(), 1u);
  EXPECT_EQ(plan.spaces[0].pinned.size(), kMaxImportItems);
  EXPECT_EQ(plan.skipped.over_cap, 5);
  EXPECT_EQ(plan.skipped.missing, 0);
}

TEST(CluSidebarFileTest, KeepsOnlyRealEmojiAndThemesWithColors) {
  auto space = [](const std::string& icon_type, const std::string& theme) {
    const SidebarPlan plan = ParseSidebarFile(File(
        R"({"spaces":["s",{"title":"S","customInfo":{"iconType":)" + icon_type +
        R"(,"windowTheme":)" + theme + R"(},"containerIDs":[]}],"items":[]})"));
    CHECK(!plan.error);
    CHECK_EQ(plan.spaces.size(), 1u);
    return plan.spaces[0];
  };
  EXPECT_EQ(space(R"({"emoji_v2":"🧪"})", "null").emoji, "🧪");
  EXPECT_EQ(space(R"({"icon":"star"})", "null").emoji, std::nullopt);
  EXPECT_EQ(space(R"({"icon":"star"})", "null").symbol, "star");
  EXPECT_EQ(space(R"({"icon":"planet"})", "null").symbol, "planet");
  EXPECT_EQ(space(R"({"icon":"globe.americas.fill"})", "null").symbol, "globe");
  EXPECT_EQ(space(R"({"emoji_v2":"🧪"})", "null").symbol, std::nullopt);
  EXPECT_EQ(space(R"({"icon":7})", "null").symbol, std::nullopt);
  EXPECT_EQ(space(R"({"emoji_v2":"🧪🧪🧪🧪🧪"})", "null").emoji, std::nullopt);
  EXPECT_EQ(space("null", R"({"background":{"foo":1}})").theme, std::nullopt);
  SpaceTheme red;
  red.colors.push_back({0, 1});
  EXPECT_EQ(space("null", R"({"background":[{"red":255,"green":0,"blue":0}]})").theme, red);
  EXPECT_EQ(space("null", R"("junk")").theme, std::nullopt);
  // A transparent color does not count.
  EXPECT_EQ(space("null", R"({"background":[{"red":1,"green":0,"blue":0,"alpha":0}]})").theme,
            std::nullopt);
}

TEST(CluSidebarFileTest, JunkSpacesProfileKeysAndAMissingContainer) {
  const SidebarPlan plan = ParseSidebarFile(File(
      R"({"spaces":[7,{},"s",{"containerIDs":["pinned","gone"],"profile":{"custom":{"_0":{"directoryBasename":"../x"}}}}],"items":[]})"));
  ASSERT_FALSE(plan.error);
  ASSERT_EQ(plan.spaces.size(), 1u);
  EXPECT_EQ(plan.spaces[0].key, "space:s");
  EXPECT_EQ(plan.spaces[0].profile_key, "default");
  EXPECT_EQ(plan.spaces[0].name, "");
  EXPECT_TRUE(plan.spaces[0].pinned.empty());
  EXPECT_EQ(plan.skipped.unsupported, 1);
  EXPECT_EQ(plan.skipped.missing, 1);
}

// The anonymized copy of a real sidebar file (clu/test/data/import-arc: every title "Tab N", every URL
// https://example.com/N, the structure unchanged). It has 10 profile folders, 4 spaces on 4 of them and 15 favorites
// containers from 2 machines; only the spaces' profiles come over, each with this machine's favorites.
TEST(CluSidebarFileTest, ArcCopyBringsTheSpacesProfilesWithThisMachinesFavorites) {
  const base::FilePath root =
      ImportFixtureRoot().DirName().AppendASCII("import-arc");
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(SidebarFilePath(root), &text));
  const SidebarPlan plan = ParseSidebarFile(text);
  ASSERT_FALSE(plan.error);
  ASSERT_EQ(plan.profiles.size(), 4u);
  ASSERT_EQ(plan.spaces.size(), 4u);
  struct Want {
    const char* space;
    const char* profile;
    size_t tabs;
    size_t folders;
    size_t favorites;
  };
  const std::array<Want, 4> want = {{{"Main", "custom:Profile 6", 7, 1, 6},
                                      {"work.example", "custom:Profile 3", 31, 2, 12},
                                      {"side.example", "custom:Profile 7", 0, 0, 2},
                                      {"client.example", "custom:Profile 4", 0, 0, 9}}};
  size_t tabs = 0;
  size_t folders = 0;
  size_t favorites = 0;
  for (size_t i = 0; i < want.size(); ++i) {
    const SidebarSpacePlan& s = plan.spaces[i];
    EXPECT_EQ(s.name, want[i].space);
    EXPECT_EQ(s.profile_key, want[i].profile);
    EXPECT_EQ(plan.profiles[i].key, want[i].profile);
    size_t space_tabs = 0;
    size_t space_folders = 0;
    for (const ImportNode& n : s.pinned) {
      ++(n.folder ? space_folders : space_tabs);
    }
    EXPECT_EQ(space_tabs, want[i].tabs) << s.name;
    EXPECT_EQ(space_folders, want[i].folders) << s.name;
    EXPECT_TRUE(s.today.empty()) << s.name;
    EXPECT_EQ(plan.profiles[i].favorites.size(), want[i].favorites)
        << want[i].profile;
    tabs += space_tabs;
    folders += space_folders;
    favorites += plan.profiles[i].favorites.size();
  }
  EXPECT_EQ(tabs, 38u);
  EXPECT_EQ(folders, 3u);
  EXPECT_EQ(favorites, 29u);
  EXPECT_EQ(plan.spaces[1].emoji, "\U0001F9FC");
  EXPECT_TRUE(plan.spaces[0].theme);
  // Main's Folder 1 holds 4 tabs and a split view's 2 tabs; the welcome item is skipped.
  const std::vector<ImportNode>& main_pinned = plan.spaces[0].pinned;
  const auto folder = std::find_if(main_pinned.begin(), main_pinned.end(),
                                   [](const ImportNode& n) { return n.folder; });
  ASSERT_NE(folder, main_pinned.end());
  EXPECT_EQ(std::count_if(main_pinned.begin(), main_pinned.end(),
                          [&](const ImportNode& n) {
                            return n.parent_key == folder->key;
                          }),
            6);
  // The split view of 2 tabs (nesting depth 3: space, Folder 1, split) comes over as a split of those 2 tabs, in pane
  // order, both in Folder 1; no other space has one.
  ASSERT_EQ(plan.spaces[0].splits.size(), 1u);
  const SidebarSplitPlan& split = plan.spaces[0].splits[0];
  EXPECT_EQ(split.keys, (std::vector<std::string>{"item:browserCompanySubstack",
                                                  "item:arcTwitter"}));
  EXPECT_FALSE(split.stacked);
  for (const std::string& key : split.keys) {
    const auto pane = std::find_if(main_pinned.begin(), main_pinned.end(),
                                   [&](const ImportNode& n) { return n.key == key; });
    ASSERT_NE(pane, main_pinned.end()) << key;
    EXPECT_FALSE(pane->folder);
    EXPECT_EQ(pane->parent_key, folder->key);
  }
  for (size_t i = 1; i < plan.spaces.size(); ++i) {
    EXPECT_TRUE(plan.spaces[i].splits.empty()) << plan.spaces[i].name;
  }
  EXPECT_GE(plan.skipped.unsupported, 1);
  EXPECT_EQ(plan.skipped.missing, 0);
  EXPECT_EQ(plan.skipped.cycles, 0);
}

}  // namespace
}  // namespace clu
