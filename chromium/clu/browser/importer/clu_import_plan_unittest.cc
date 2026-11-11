// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_plan.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "testing/gtest/include/gtest/gtest.h"

// Imported trees to rows: positions and parents, a re-import that adds nothing, a deleted row that comes back, folders
// that would sit too deep, and favorites with duplicates and the 12-favorite cap.
namespace clu {
namespace {

constexpr int64_t kNow = 1'800'000'000'000;
constexpr char kSpace[] = "11111111-1111-4111-8111-111111111111";

ImportNode Folder(const std::string& key, const std::string& parent, const std::string& title) {
  ImportNode n;
  n.key = key;
  n.parent_key = parent;
  n.folder = true;
  n.title = title;
  return n;
}

ImportNode Tab(const std::string& key, const std::string& parent, const std::string& url) {
  ImportNode n;
  n.key = key;
  n.parent_key = parent;
  n.url = url;
  n.title = key;
  return n;
}

class CluImportPlanTest : public testing::Test {
 protected:
  void SetUp() override { tree_.SetSpaces({kSpace}); }

  NewIdCallback Ids() {
    return base::BindRepeating(
        [](int* next) { return "id-" + base::NumberToString((*next)++); }, &next_);
  }

  // Applies rows to the tree and returns the import map they make.
  std::map<std::string, std::string> Apply(ImportRows rows) {
    std::map<std::string, std::string> map(rows.mapped.begin(), rows.mapped.end());
    tree_.AddNodes(std::move(rows.rows));
    return map;
  }

  CluTabTree tree_;
  int next_ = 0;
};

TEST_F(CluImportPlanTest, RowsKeepTheTreeAndOrderAndAReimportAddsNothing) {
  const std::vector<ImportNode> nodes = {
      Folder("root", "", "Imported from Google Chrome"), Folder("bar", "root", "Bookmarks bar"),
      Tab("a", "bar", "https://a.example/"), Tab("b", "bar", "https://b.example/"),
      Tab("c", "root", "https://c.example/")};
  ImportRows rows = PlanPinnedRows(nodes, kSpace, {}, tree_, Ids(), kNow);
  EXPECT_EQ(rows.tabs, 3);
  ASSERT_EQ(rows.rows.size(), 5u);
  EXPECT_EQ(rows.rows[0].title_override, "Imported from Google Chrome");
  EXPECT_TRUE(rows.rows[0].is_folder());
  EXPECT_EQ(rows.rows[2].pinned_url, "https://a.example/");
  EXPECT_EQ(rows.rows[2].section, Section::kPinned);
  const std::map<std::string, std::string> map = Apply(std::move(rows));
  ASSERT_EQ(tree_.Pinned(kSpace).size(), 1u);
  const std::string root = tree_.Pinned(kSpace)[0];
  ASSERT_EQ(tree_.Children(root).size(), 2u);
  EXPECT_EQ(tree_.Get(tree_.Children(root)[1])->url, "https://c.example/");
  const std::string bar = tree_.Children(root)[0];
  ASSERT_EQ(tree_.Children(bar).size(), 2u);
  EXPECT_EQ(tree_.Get(tree_.Children(bar)[0])->url, "https://a.example/");
  EXPECT_EQ(tree_.Get(tree_.Children(bar)[1])->url, "https://b.example/");

  // Again with the map: nothing new.
  const ImportRows again = PlanPinnedRows(nodes, kSpace, map, tree_, Ids(), kNow);
  EXPECT_TRUE(again.rows.empty());
  EXPECT_EQ(again.tabs, 0);

  // A deleted bookmark comes back into the folder it had; a new one lands after the rows already there.
  ASSERT_TRUE(tree_.RemoveTab(tree_.Children(bar)[0]));
  std::vector<ImportNode> more = nodes;
  more.push_back(Tab("d", "bar", "https://d.example/"));
  ImportRows back = PlanPinnedRows(more, kSpace, map, tree_, Ids(), kNow);
  EXPECT_EQ(back.tabs, 2);
  Apply(std::move(back));
  std::vector<std::string> urls;
  for (const std::string& id : tree_.Children(bar)) {
    urls.push_back(tree_.Get(id)->url);
  }
  EXPECT_EQ(urls, (std::vector<std::string>{"https://b.example/", "https://a.example/",
                                            "https://d.example/"}));
}

TEST_F(CluImportPlanTest, FoldersPastDepthEightFlattenIntoTheirParent) {
  std::vector<ImportNode> nodes;
  std::string parent;
  for (int i = 1; i <= 10; ++i) {
    const std::string key = "f" + base::NumberToString(i);
    nodes.push_back(Folder(key, parent, key));
    nodes.push_back(Tab("t" + base::NumberToString(i), key, "https://x.example/" + key));
    parent = key;
  }
  ImportRows rows = PlanPinnedRows(nodes, kSpace, {}, tree_, Ids(), kNow);
  EXPECT_EQ(rows.tabs, 10);
  Apply(std::move(rows));
  std::string folder = tree_.Pinned(kSpace)[0];
  int depth = 1;
  while (true) {
    std::string next;
    for (const std::string& id : tree_.Children(folder)) {
      if (tree_.Get(id)->is_folder()) {
        next = id;
      }
    }
    if (next.empty()) {
      break;
    }
    folder = next;
    ++depth;
  }
  EXPECT_EQ(depth, kMaxFolderDepth);
  EXPECT_EQ(tree_.Depth(folder), kMaxFolderDepth);
  // Tabs 8, 9 and 10 sit in the level-8 folder.
  EXPECT_EQ(tree_.Children(folder).size(), 3u);
}

TEST_F(CluImportPlanTest, FavoritesSkipDuplicatesAndStopAtTheCap) {
  std::vector<ImportNode> nodes = {Tab("a", "", "https://a.example/"),
                                   Tab("a2", "", "https://a.example/"),
                                   Folder("f", "", "ignored")};
  for (int i = 0; i < kMaxFavorites + 2; ++i) {
    nodes.push_back(Tab("n" + base::NumberToString(i), "",
                        "https://n.example/" + base::NumberToString(i)));
  }
  ImportRows rows = PlanFavoriteRows(nodes, {}, tree_, Ids(), kNow);
  EXPECT_EQ(rows.tabs, kMaxFavorites);
  EXPECT_EQ(rows.duplicates, 1);
  EXPECT_EQ(rows.over_cap, 3);
  EXPECT_TRUE(std::all_of(rows.rows.begin(), rows.rows.end(), [](const TabNode& n) {
    return n.section == Section::kFavorite && n.space_id.empty() && n.is_tab();
  }));
  const std::map<std::string, std::string> map = Apply(std::move(rows));
  EXPECT_EQ(tree_.Favorites().size(), static_cast<size_t>(kMaxFavorites));
  // Again: everything mapped or full.
  const ImportRows again = PlanFavoriteRows(nodes, map, tree_, Ids(), kNow);
  EXPECT_EQ(again.tabs, 0);
}

TEST_F(CluImportPlanTest, TodayTabsLandAfterTheSpacesTodayRowsAndOnlyOnce) {
  NewTab open;
  open.url = "https://open.example/";
  ASSERT_TRUE(tree_.InsertTab(open, DropTarget::Today(kSpace, 0), kNow).has_value());
  const std::vector<ImportNode> nodes = {Tab("t1", "", "https://t1.example/"),
                                         Folder("f", "", "ignored"),
                                         Tab("t2", "", "https://t2.example/")};
  ImportRows rows = PlanTodayRows(nodes, kSpace, {}, tree_, Ids(), kNow);
  EXPECT_EQ(rows.tabs, 2);
  EXPECT_TRUE(std::all_of(rows.rows.begin(), rows.rows.end(), [](const TabNode& n) {
    return n.section == Section::kToday && n.space_id == kSpace && n.is_tab() &&
           !n.pinned_url.has_value();
  }));
  std::map<std::string, std::string> map = Apply(std::move(rows));
  ASSERT_EQ(tree_.Today(kSpace).size(), 3u);
  EXPECT_EQ(tree_.Get(tree_.Today(kSpace)[1])->url, "https://t1.example/");
  EXPECT_EQ(tree_.Get(tree_.Today(kSpace)[2])->url, "https://t2.example/");
  // Again: nothing new. A row the user since closed comes back.
  EXPECT_EQ(PlanTodayRows(nodes, kSpace, map, tree_, Ids(), kNow).tabs, 0);
  ASSERT_TRUE(tree_.RemoveTab(map.at("t1")));
  const ImportRows back = PlanTodayRows(nodes, kSpace, map, tree_, Ids(), kNow);
  ASSERT_EQ(back.tabs, 1);
  EXPECT_EQ(back.rows[0].url, "https://t1.example/");
}

TEST_F(CluImportPlanTest, NewSpacesTakeThePaletteInTurn) {
  EXPECT_EQ(ImportSpaceColor(0), "slate");
  EXPECT_EQ(ImportSpaceColor(1), "red");
  EXPECT_EQ(ImportSpaceColor(3), "amber");
  EXPECT_EQ(ImportSpaceColor(12), "slate");
}

}  // namespace
}  // namespace clu
