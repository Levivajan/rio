// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tab_tree.h"

#include <string>
#include <vector>

#include "base/uuid.h"
#include "clu/browser/model/clu_tab_repair.h"
#include "testing/gtest/include/gtest/gtest.h"

// The F8 move rules, depth and cycle checks, dense positions, folder delete, sidebar order and the F9 next-active rule
// (the cases of src/main/tabs/tabStore.test.ts and nextActive tests in service.test.ts).
namespace clu {
namespace {

constexpr int64_t kNow = 1'800'000'000'000;

std::string Id() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

class CluTabTreeTest : public testing::Test {
 protected:
  void SetUp() override {
    space_ = Id();
    other_ = Id();
    tree_.SetSpaces({space_, other_});
  }

  std::string Today(const std::string& url = "https://a.test/") {
    NewTab tab;
    tab.url = url;
    auto id = tree_.InsertTab(tab, DropTarget::Today(space_, 0), kNow);
    EXPECT_TRUE(id.has_value());
    return id.value_or("");
  }

  std::string Pinned(const std::string& parent = {}) {
    NewTab tab;
    tab.url = "https://p.test/";
    const int end = static_cast<int>(
        (parent.empty() ? tree_.Pinned(space_) : tree_.Children(parent))
            .size());
    auto id = tree_.InsertTab(tab, DropTarget::Pinned(space_, parent, end),
                              kNow);
    EXPECT_TRUE(id.has_value());
    return id.value_or("");
  }

  std::string Folder(const std::string& parent = {}) {
    auto id = tree_.CreateFolder(space_, parent, "", std::nullopt, kNow);
    EXPECT_TRUE(id.has_value());
    return id.value_or("");
  }

  // A chain of `levels` nested folders; returns the innermost.
  std::string Chain(int levels) {
    std::string parent;
    for (int i = 0; i < levels; ++i) {
      parent = Folder(parent);
    }
    return parent;
  }

  void ExpectValid() {
    std::vector<TabNode> nodes;
    for (const TabNode* node : tree_.All()) {
      nodes.push_back(*node);
    }
    EXPECT_EQ(CheckTree(nodes, tree_.spaces()), std::vector<std::string>());
  }

  CluTabTree tree_;
  std::string space_;
  std::string other_;
};

TEST_F(CluTabTreeTest, TodayToPinnedSetsThePinnedUrlAndDropsHistory) {
  NewTab tab;
  tab.url = "https://a.test/page";
  tab.nav_state = R"({"index":0,"entries":[]})";
  const std::string id =
      tree_.InsertTab(tab, DropTarget::Today(space_, 0), kNow).value();
  ASSERT_TRUE(tree_.Move(id, DropTarget::Pinned(space_, "", 0), kNow + 1)
                  .has_value());
  const TabNode* node = tree_.Get(id);
  EXPECT_EQ(node->section, Section::kPinned);
  EXPECT_EQ(node->pinned_url, "https://a.test/page");
  EXPECT_FALSE(node->nav_state);
  ExpectValid();
}

TEST_F(CluTabTreeTest, PinnedToTodayClearsThePinnedUrlAndRestartsTheClock) {
  const std::string id = Pinned();
  ASSERT_TRUE(
      tree_.Move(id, DropTarget::Today(space_, 0), kNow + 5).has_value());
  const TabNode* node = tree_.Get(id);
  EXPECT_EQ(node->section, Section::kToday);
  EXPECT_FALSE(node->pinned_url);
  EXPECT_EQ(node->last_active_at, kNow + 5);
  ExpectValid();
}

TEST_F(CluTabTreeTest, PinnedToFavoriteAndBackKeepsThePinnedUrl) {
  const std::string id = Pinned();
  tree_.Mutable(id)->url = "https://p.test/elsewhere";
  ASSERT_TRUE(tree_.Move(id, DropTarget::Favorite(0), kNow).has_value());
  EXPECT_EQ(tree_.Get(id)->pinned_url, "https://p.test/");
  EXPECT_TRUE(tree_.Get(id)->space_id.empty());
  ASSERT_TRUE(
      tree_.Move(id, DropTarget::Pinned(other_, "", 0), kNow).has_value());
  EXPECT_EQ(tree_.Get(id)->pinned_url, "https://p.test/");
  EXPECT_EQ(tree_.Get(id)->space_id, other_);
  ExpectValid();
}

TEST_F(CluTabTreeTest, FoldersOnlyGoInPinned) {
  const std::string folder = Folder();
  EXPECT_EQ(tree_.Move(folder, DropTarget::Today(space_, 0), kNow).error(),
            TreeError::kFolderNotAllowed);
  EXPECT_EQ(tree_.Move(folder, DropTarget::Favorite(0), kNow).error(),
            TreeError::kFolderNotAllowed);
  EXPECT_EQ(tree_.Get(folder)->section, Section::kPinned);
}

TEST_F(CluTabTreeTest, TheThirteenthFavoriteIsRefused) {
  for (int i = 0; i < kMaxFavorites; ++i) {
    ASSERT_TRUE(tree_.Move(Today(), DropTarget::Favorite(i), kNow).has_value());
  }
  const std::string extra = Today();
  EXPECT_EQ(tree_.Move(extra, DropTarget::Favorite(0), kNow).error(),
            TreeError::kFavoritesFull);
  NewTab tab;
  tab.url = "https://f.test/";
  EXPECT_EQ(tree_.InsertTab(tab, DropTarget::Favorite(0), kNow).error(),
            TreeError::kFavoritesFull);
  // A favorite reordered within Favorites is not a new one.
  EXPECT_TRUE(tree_.Move(tree_.Favorites()[0], DropTarget::Favorite(5), kNow)
                  .has_value());
  EXPECT_EQ(tree_.Favorites().size(), static_cast<size_t>(kMaxFavorites));
}

TEST_F(CluTabTreeTest, DepthNineIsRefusedCountingTheMovedSubtree) {
  const std::string deep = Chain(7);
  // A folder with one subfolder brings two levels: 7 + 2 = 9.
  const std::string moved = Folder();
  Folder(moved);
  EXPECT_EQ(tree_.Move(moved, DropTarget::Pinned(space_, deep, 0), kNow)
                .error(),
            TreeError::kTooDeep);
  // A single folder fits: 7 + 1 = 8.
  const std::string single = Folder();
  EXPECT_TRUE(tree_.Move(single, DropTarget::Pinned(space_, deep, 0), kNow)
                  .has_value());
  EXPECT_EQ(tree_.Depth(single), kMaxFolderDepth);
  EXPECT_EQ(tree_.CreateFolder(space_, single, "", std::nullopt, kNow).error(),
            TreeError::kTooDeep);
  ExpectValid();
}

TEST_F(CluTabTreeTest, AFolderNeverGoesUnderItsOwnChild) {
  const std::string outer = Folder();
  const std::string inner = Folder(outer);
  EXPECT_EQ(
      tree_.Move(outer, DropTarget::Pinned(space_, inner, 0), kNow).error(),
      TreeError::kCycle);
  EXPECT_EQ(
      tree_.Move(outer, DropTarget::Pinned(space_, outer, 0), kNow).error(),
      TreeError::kCycle);
}

TEST_F(CluTabTreeTest, PositionsStayDenseAfterMovesAndDeletes) {
  std::vector<std::string> ids;
  for (int i = 0; i < 5; ++i) {
    ids.push_back(Today());
  }
  ASSERT_TRUE(
      tree_.Move(ids[1], DropTarget::Pinned(space_, "", 0), kNow).has_value());
  ASSERT_TRUE(tree_.RemoveTab(ids[3]));
  ASSERT_TRUE(
      tree_.Move(ids[0], DropTarget::Today(space_, 99), kNow).has_value());
  const std::vector<std::string>& today = tree_.Today(space_);
  for (size_t i = 0; i < today.size(); ++i) {
    EXPECT_EQ(tree_.Get(today[i])->position, static_cast<int>(i));
  }
  EXPECT_EQ(today.back(), ids[0]);
  ExpectValid();
}

TEST_F(CluTabTreeTest, FolderDeleteFlattensItsTabsInOrderToTheTopOfToday) {
  const std::string existing = Today();
  const std::string outer = Folder();
  const std::string a = Pinned(outer);
  const std::string inner = Folder(outer);
  const std::string b = Pinned(inner);
  const std::string c = Pinned(outer);
  auto moved = tree_.DeleteFolder(outer, kNow + 9);
  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(moved.value(), (std::vector<std::string>{a, b, c}));
  EXPECT_EQ(tree_.Today(space_),
            (std::vector<std::string>{a, b, c, existing}));
  EXPECT_FALSE(tree_.Get(outer));
  EXPECT_FALSE(tree_.Get(inner));
  EXPECT_FALSE(tree_.Get(a)->pinned_url);
  EXPECT_EQ(tree_.Get(b)->last_active_at, kNow + 9);
  ExpectValid();
}

TEST_F(CluTabTreeTest, AMovedFolderTakesItsSubtreeToTheOtherSpace) {
  const std::string folder = Folder();
  const std::string tab = Pinned(folder);
  ASSERT_TRUE(
      tree_.Move(folder, DropTarget::Pinned(other_, "", 0), kNow).has_value());
  EXPECT_EQ(tree_.Get(tab)->space_id, other_);
  EXPECT_EQ(tree_.Children(folder), std::vector<std::string>{tab});
  ExpectValid();
}

TEST_F(CluTabTreeTest, MoveManyLandsTogetherInOrderOrNotAtAll) {
  const std::string a = Today();
  const std::string b = Today();
  const std::string c = Today();
  const std::string p = Pinned();
  // Pinned is [p]; a, c land before p, in the given order.
  ASSERT_TRUE(
      tree_.MoveMany({c, a}, DropTarget::Pinned(space_, "", 0), kNow)
          .has_value());
  EXPECT_EQ(tree_.Pinned(space_), (std::vector<std::string>{c, a, p}));
  EXPECT_EQ(tree_.Today(space_), std::vector<std::string>{b});
  // A folder moved into itself refuses the whole batch.
  const std::string folder = Folder();
  EXPECT_FALSE(
      tree_.MoveMany({b, folder}, DropTarget::Pinned(space_, folder, 0), kNow)
          .has_value());
  EXPECT_EQ(tree_.Get(b)->section, Section::kToday);
  ExpectValid();
}

TEST_F(CluTabTreeTest, BadTargetsAreRefused) {
  const std::string tab = Today();
  EXPECT_EQ(tree_.Move(tab, DropTarget::Today(Id(), 0), kNow).error(),
            TreeError::kBadTarget);
  EXPECT_EQ(tree_.Move(tab, DropTarget::Pinned(space_, Id(), 0), kNow).error(),
            TreeError::kBadTarget);
  // A folder of another space is not a parent here.
  const std::string folder =
      tree_.CreateFolder(other_, "", "", std::nullopt, kNow).value();
  EXPECT_EQ(
      tree_.Move(tab, DropTarget::Pinned(space_, folder, 0), kNow).error(),
      TreeError::kBadTarget);
  EXPECT_EQ(tree_.Move(Id(), DropTarget::Today(space_, 0), kNow).error(),
            TreeError::kNotFound);
}

TEST_F(CluTabTreeTest, InsertRefusesUrlsThePolicyRefuses) {
  for (const char* url :
       {"javascript:alert(1)", "data:text/html,hi", "chrome://clu-frame/",
        "not a url", ""}) {
    NewTab tab;
    tab.url = url;
    EXPECT_EQ(tree_.InsertTab(tab, DropTarget::Today(space_, 0), kNow).error(),
              TreeError::kBadUrl)
        << url;
  }
  EXPECT_TRUE(tree_.Today(space_).empty());
}

TEST_F(CluTabTreeTest, SidebarOrderCountsFavoritesPinnedThenToday) {
  const std::string fav = Today();
  ASSERT_TRUE(tree_.Move(fav, DropTarget::Favorite(0), kNow).has_value());
  const std::string p1 = Pinned();
  const std::string folder = Folder();
  const std::string inside = Pinned(folder);
  const std::string t1 = Today();
  EXPECT_EQ(tree_.SidebarOrder(space_, ""),
            (std::vector<std::string>{fav, p1, inside, t1}));
  // A collapsed folder hides its tabs, except the active one shown beneath it.
  tree_.Mutable(folder)->collapsed = true;
  EXPECT_EQ(tree_.SidebarOrder(space_, ""),
            (std::vector<std::string>{fav, p1, t1}));
  EXPECT_EQ(tree_.SidebarOrder(space_, inside),
            (std::vector<std::string>{fav, p1, inside, t1}));
  // A collapsed Pinned section hides every pinned row but the active one.
  tree_.MutableSpaceState(space_).pinned_collapsed = true;
  EXPECT_EQ(tree_.SidebarOrder(space_, p1),
            (std::vector<std::string>{fav, p1, t1}));
  EXPECT_EQ(tree_.SidebarOrder(space_, ""),
            (std::vector<std::string>{fav, t1}));
  // Rows of a pending Clear are left out.
  EXPECT_EQ(tree_.SidebarOrder(space_, "", {t1}),
            (std::vector<std::string>{fav}));
}

TEST_F(CluTabTreeTest, RemovingTheActiveTabClearsItFromItsSpaceState) {
  const std::string tab = Today();
  tree_.MutableSpaceState(space_).active_tab_id = tab;
  tree_.TakeChanges();
  ASSERT_TRUE(tree_.RemoveTab(tab));
  EXPECT_TRUE(tree_.SpaceState(space_).active_tab_id.empty());
  const TreeChanges changes = tree_.TakeChanges();
  EXPECT_TRUE(changes.deletes.contains(tab));
  EXPECT_TRUE(changes.spaces.contains(space_));
}

TEST_F(CluTabTreeTest, ChangesNameExactlyTheRowsTouched) {
  const std::string a = Today();
  const std::string b = Today();
  tree_.TakeChanges();
  tree_.Mutable(a)->title = "renamed";
  const TreeChanges changes = tree_.TakeChanges();
  EXPECT_EQ(changes.upserts, std::set<std::string>{a});
  EXPECT_FALSE(changes.upserts.contains(b));
  EXPECT_TRUE(tree_.TakeChanges().empty());
}

// F9 next active -------------------------------------------------------------------------------------------------

TEST_F(CluTabTreeTest, ASpaceLeavesWithItsRowsAndComesBackElsewhereAsItWas) {
  const std::string folder = Folder();
  const std::string inner = Pinned(folder);
  const std::string pinned = Pinned();
  const std::string today = Today();
  tree_.MutableSpaceState(space_).active_tab_id = today;
  tree_.TakeChanges();

  SpaceTabState state;
  std::vector<TabNode> nodes = tree_.TakeSpace(space_, &state);
  EXPECT_EQ(nodes.size(), 4u);
  EXPECT_EQ(state.active_tab_id, today);
  EXPECT_TRUE(tree_.Pinned(space_).empty());
  EXPECT_TRUE(tree_.Today(space_).empty());
  const TreeChanges changes = tree_.TakeChanges();
  EXPECT_EQ(changes.deletes.size(), 4u);

  // Another profile's tree takes them as they were.
  CluTabTree other;
  other.SetSpaces({space_});
  const std::vector<std::string> added = other.AddNodes(std::move(nodes));
  EXPECT_EQ(added.size(), 4u);
  EXPECT_EQ(other.Pinned(space_), (std::vector<std::string>{folder, pinned}));
  EXPECT_EQ(other.Children(folder), std::vector<std::string>{inner});
  EXPECT_EQ(other.Today(space_), std::vector<std::string>{today});
  EXPECT_EQ(other.TakeChanges().upserts.size(), 4u);
}

// A folder's children keep their order when the folder sits below another pinned row: a child at position 0 waits
// one pass for its folder and must not land after its siblings.
TEST_F(CluTabTreeTest, AddNodesKeepsFolderChildrenInOrder) {
  const std::string top = Pinned();
  const std::string folder = Folder();
  const std::string a = Pinned(folder);
  const std::string b = Pinned(folder);
  const std::string c = Pinned(folder);
  ASSERT_EQ(tree_.Pinned(space_), (std::vector<std::string>{top, folder}));
  SpaceTabState state;
  std::vector<TabNode> nodes = tree_.TakeSpace(space_, &state);

  CluTabTree other;
  other.SetSpaces({space_});
  EXPECT_EQ(other.AddNodes(std::move(nodes)).size(), 5u);
  EXPECT_EQ(other.Pinned(space_), (std::vector<std::string>{top, folder}));
  EXPECT_EQ(other.Children(folder), (std::vector<std::string>{a, b, c}));
}

TEST_F(CluTabTreeTest, AddNodesSkipsKnownIdsUnknownSpacesAndMissingFolders) {
  const std::string kept = Today();
  TabNode same = *tree_.Get(kept);
  TabNode elsewhere = same;
  elsewhere.id = Id();
  elsewhere.space_id = Id();
  TabNode orphan = same;
  orphan.id = Id();
  orphan.section = Section::kPinned;
  orphan.parent_id = Id();
  EXPECT_TRUE(tree_.AddNodes({same, elsewhere, orphan}).empty());
}

TEST_F(CluTabTreeTest, ASubtreeLeavesWithEverythingUnderIt) {
  const std::string folder = Folder();
  const std::string sub = Folder(folder);
  const std::string inner = Pinned(sub);
  const std::string other = Pinned();
  std::vector<TabNode> nodes = tree_.TakeSubtree(folder);
  ASSERT_EQ(nodes.size(), 3u);
  // A folder before what it holds.
  EXPECT_EQ(nodes[0].id, folder);
  EXPECT_EQ(nodes[1].id, sub);
  EXPECT_EQ(nodes[2].id, inner);
  EXPECT_EQ(tree_.Pinned(space_), std::vector<std::string>{other});
  EXPECT_EQ(tree_.Get(other)->position, 0);
  EXPECT_TRUE(tree_.TakeSubtree("unknown").empty());
}

TEST(CluNextActiveTest, TheRowThatTakesThePlaceThenTheOneAboveThenRecentThenNone) {
  const std::vector<std::string> list = {"a", "b", "c"};
  auto all = [](std::string_view) { return true; };
  EXPECT_EQ(NextActive(list, "b", {}, {}, all), "c");
  EXPECT_EQ(NextActive(list, "c", {}, {}, all), "b");
  // The rows around it leave in the same batch: the window's most recent tab in its space.
  EXPECT_EQ(NextActive(list, "b", {"a", "c"}, {"z", "b"}, all), "z");
  EXPECT_EQ(NextActive({"b"}, "b", {}, {}, all), std::nullopt);
  // A row another window shows is skipped.
  auto not_c = [](std::string_view id) { return id != "c"; };
  EXPECT_EQ(NextActive(list, "b", {}, {}, not_c), "a");
}

}  // namespace
}  // namespace clu
