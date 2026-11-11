// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tab_repair.h"

#include <string>
#include <vector>

#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

// tabs-t1: a DB seeded with each junk row kind loads without throwing and yields a valid tree, one repair count per
// kind (the cases of src/main/tabs/tabStore.repair.test.ts).
namespace clu {
namespace {

constexpr int64_t kNow = 1'800'000'000'000;

std::string Id() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

class CluTabRepairTest : public testing::Test {
 protected:
  void SetUp() override { spaces_ = {Id(), Id()}; }

  RawTabRow Row(const char* section, const char* kind = "tab") {
    RawTabRow row;
    row.rowid = ++rowid_;
    row.id = Id();
    row.space_id = std::string(section) == "favorite"
                       ? std::nullopt
                       : std::optional<std::string>(spaces_[0]);
    row.section = section;
    row.kind = kind;
    row.position = 0;
    row.url = "https://a.test/";
    if (std::string(section) != "today" && std::string(kind) == "tab") {
      row.pinned_url = "https://a.test/";
    }
    row.title = "A";
    row.last_active_at = kNow - 1000;
    row.created_at = kNow - 2000;
    return row;
  }

  RepairResult Repair(const std::vector<RawTabRow>& rows) {
    RepairResult result = RepairRows(rows, spaces_, kNow);
    EXPECT_EQ(CheckTree(result.nodes, spaces_), std::vector<std::string>());
    return result;
  }

  std::vector<std::string> spaces_;
  int64_t rowid_ = 0;
};

TEST_F(CluTabRepairTest, ACleanTreeNeedsNoRepair) {
  RawTabRow folder = Row("pinned", "folder");
  folder.url.reset();
  RawTabRow child = Row("pinned");
  child.parent_id = folder.id;
  child.position = 0;
  const RepairResult result =
      Repair({Row("today"), Row("favorite"), folder, child});
  EXPECT_TRUE(result.kinds.empty()) << result.kinds.begin()->first;
  EXPECT_TRUE(result.deleted.empty());
  EXPECT_EQ(result.nodes.size(), 4u);
}

TEST_F(CluTabRepairTest, UnknownSectionOrKindAndBadIdsAreDeleted) {
  RawTabRow bad_section = Row("today");
  bad_section.section = "sidebar";
  RawTabRow bad_kind = Row("today");
  bad_kind.kind = "group";
  RawTabRow bad_id = Row("today");
  bad_id.id = "not-a-uuid";
  RawTabRow null_section = Row("today");
  null_section.section.reset();
  const RepairResult result =
      Repair({bad_section, bad_kind, bad_id, null_section, Row("today")});
  EXPECT_EQ(result.nodes.size(), 1u);
  EXPECT_EQ(result.deleted.size(), 4u);
  EXPECT_EQ(result.kinds.at("bad-section-or-kind"), 3);
  EXPECT_EQ(result.kinds.at("bad-id"), 1);
}

TEST_F(CluTabRepairTest, AnOrphanParentMovesToThePinnedRoot) {
  RawTabRow orphan = Row("pinned");
  orphan.parent_id = Id();
  const RepairResult result = Repair({orphan});
  EXPECT_EQ(result.kinds.at("orphan-parent"), 1);
  EXPECT_TRUE(result.nodes[0].parent_id.empty());
}

TEST_F(CluTabRepairTest, ACycleBreaksAtTheNewestMember) {
  RawTabRow a = Row("pinned", "folder");
  RawTabRow b = Row("pinned", "folder");
  a.url.reset();
  b.url.reset();
  a.parent_id = b.id;
  b.parent_id = a.id;
  b.created_at = a.created_at.value() + 10;
  const RepairResult result = Repair({a, b});
  EXPECT_EQ(result.kinds.at("cycle"), 1);
  for (const TabNode& node : result.nodes) {
    if (node.id == *b.id) {
      EXPECT_TRUE(node.parent_id.empty());
    } else {
      EXPECT_EQ(node.parent_id, *b.id);
    }
  }
}

TEST_F(CluTabRepairTest, ATodayRowWithoutASpaceGoesToTheFirstSpace) {
  RawTabRow null_space = Row("today");
  null_space.space_id.reset();
  RawTabRow gone_space = Row("today");
  gone_space.space_id = Id();
  const RepairResult result = Repair({null_space, gone_space});
  EXPECT_EQ(result.kinds.at("missing-space"), 1);
  EXPECT_EQ(result.kinds.at("unknown-space"), 1);
  for (const TabNode& node : result.nodes) {
    EXPECT_EQ(node.space_id, spaces_[0]);
  }
}

TEST_F(CluTabRepairTest, AnInvalidUrlBecomesAboutBlank) {
  RawTabRow script = Row("today");
  script.url = "javascript:alert(1)";
  RawTabRow number = Row("today");
  number.url.reset();
  RawTabRow pinned = Row("pinned");
  pinned.pinned_url = "data:text/html,x";
  const RepairResult result = Repair({script, number, pinned});
  EXPECT_EQ(result.kinds.at("invalid-url"), 3);
  for (const TabNode& node : result.nodes) {
    EXPECT_TRUE(node.url == "about:blank" ||
                node.pinned_url == "https://a.test/");
  }
}

TEST_F(CluTabRepairTest, DuplicatePositionsAreRenumbered) {
  RawTabRow a = Row("today");
  RawTabRow b = Row("today");
  RawTabRow c = Row("today");
  a.position = 0;
  b.position = 0;
  c.position = 7;
  const RepairResult result = Repair({a, b, c});
  EXPECT_EQ(result.kinds.at("positions"), 1);
}

TEST_F(CluTabRepairTest, FavoritesOverTheCapMoveTheExtraToTheFirstSpacesPinned) {
  std::vector<RawTabRow> rows;
  for (int i = 0; i < kMaxFavorites + 1; ++i) {
    RawTabRow row = Row("favorite");
    row.position = i;
    rows.push_back(row);
  }
  const RepairResult result = Repair(rows);
  EXPECT_EQ(result.kinds.at("favorites-over-cap"), 1);
  int favorites = 0;
  for (const TabNode& node : result.nodes) {
    if (node.section == Section::kFavorite) {
      ++favorites;
    } else {
      EXPECT_EQ(node.id, *rows.back().id);
      EXPECT_EQ(node.section, Section::kPinned);
      EXPECT_EQ(node.space_id, spaces_[0]);
    }
  }
  EXPECT_EQ(favorites, kMaxFavorites);
}

TEST_F(CluTabRepairTest, AFolderOutsidePinnedMovesToThePinnedRoot) {
  RawTabRow folder = Row("today", "folder");
  folder.url.reset();
  const RepairResult result = Repair({folder});
  EXPECT_EQ(result.kinds.at("folder-outside-pinned"), 1);
  EXPECT_EQ(result.nodes[0].section, Section::kPinned);
}

TEST_F(CluTabRepairTest, NineLevelsOfFoldersAreCutBackToEight) {
  std::vector<RawTabRow> rows;
  std::string parent;
  for (int i = 0; i < 9; ++i) {
    RawTabRow folder = Row("pinned", "folder");
    folder.url.reset();
    folder.position = 0;
    if (!parent.empty()) {
      folder.parent_id = parent;
    }
    parent = *folder.id;
    rows.push_back(folder);
  }
  const RepairResult result = Repair(rows);
  EXPECT_EQ(result.kinds.at("too-deep"), 1);
}

TEST_F(CluTabRepairTest, JunkFieldsAndCustomIconsAreCleaned) {
  RawTabRow junk = Row("pinned");
  junk.title.reset();
  junk.last_active_at = kNow + 1'000'000;
  junk.custom_icon = "ab";
  RawTabRow two = Row("pinned");
  two.custom_icon = "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";  // two emoji
  RawTabRow long_icon = Row("pinned");
  long_icon.custom_icon = std::string(300, 'x');
  RawTabRow nav = Row("pinned");
  nav.nav_state = R"({"index":0,"entries":[]})";
  const RepairResult result = Repair({junk, two, long_icon, nav});
  EXPECT_EQ(result.kinds.at("custom-icon"), 3);
  EXPECT_EQ(result.kinds.at("fields"), 2);
  for (const TabNode& node : result.nodes) {
    EXPECT_FALSE(node.custom_icon);
    EXPECT_FALSE(node.nav_state);
    EXPECT_LE(node.last_active_at, kNow);
  }
}

TEST_F(CluTabRepairTest, AFavoriteWithASpaceLosesIt) {
  RawTabRow favorite = Row("favorite");
  favorite.space_id = spaces_[1];
  const RepairResult result = Repair({favorite});
  EXPECT_EQ(result.kinds.at("favorite-space"), 1);
  EXPECT_TRUE(result.nodes[0].space_id.empty());
}

TEST_F(CluTabRepairTest, WithoutAnySpaceRowsThatNeedOneAreDeleted) {
  RawTabRow today = Row("today");
  today.space_id = Id();
  const RawTabRow favorite = Row("favorite");
  spaces_.clear();
  const RepairResult result = RepairRows({today, favorite}, {}, kNow);
  EXPECT_EQ(result.nodes.size(), 1u);
  EXPECT_EQ(result.deleted.size(), 1u);
}

}  // namespace
}  // namespace clu
