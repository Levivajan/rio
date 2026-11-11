// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_store.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "clu/browser/model/clu_nav_state.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

// CluStore on a temp file: rows survive a reopen, junk rows repair in one transaction, the archive pages, searches
// literally, prunes at 90 days and 5000 rows, and Cmd+Shift+T's fallback never brings back an auto row.
namespace clu {
namespace {

constexpr int64_t kNow = 1'800'000'000'000;
constexpr int64_t kDay = 24 * 60 * 60 * 1000;

std::string Id() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

ArchiveSnapshot Snapshot(const std::string& url,
                         const std::string& title = "T") {
  ArchiveSnapshot snap;
  snap.archive_id = Id();
  snap.url = url;
  snap.title = title;
  return snap;
}

class CluStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    path_ = dir_.GetPath().AppendASCII("Clu").AppendASCII("clu.db");
  }

  base::ScopedTempDir dir_;
  base::FilePath path_;
};

TEST_F(CluStoreTest, AFreshFileGetsAProfileIdAndTheHomeSpace) {
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  EXPECT_TRUE(load.persistent);
  EXPECT_EQ(load.profile_id.size(), 36u);
  ASSERT_EQ(load.spaces.size(), 1u);
  EXPECT_EQ(load.spaces[0].name, kDefaultSpaceName);
  EXPECT_TRUE(load.tree.nodes.empty());
}

TEST_F(CluStoreTest, TheSpacesMirrorReplacesTheStoredSpaces) {
  const std::string work = base::Uuid::GenerateRandomV4().AsLowercaseString();
  {
    CluStore store(path_);
    StoreLoad load = store.Load(kNow);
    SpaceRow row;
    row.id = work;
    row.name = "Work";
    row.icon_kind = "emoji";
    row.icon_value = "\U0001F4BC";
    row.color = "teal";
    ASSERT_TRUE(store.ReplaceSpaces({row, load.spaces[0]}, kNow));
  }
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  ASSERT_EQ(load.spaces.size(), 2u);
  EXPECT_EQ(load.spaces[0].id, work);
  EXPECT_EQ(load.spaces[0].icon_value, "\U0001F4BC");
  EXPECT_EQ(load.spaces[0].color, "teal");
  EXPECT_EQ(load.spaces[1].name, kDefaultSpaceName);
}

// Symbol icons use the existing free-text columns, so a file from before them keeps every emoji icon as it was.
TEST_F(CluStoreTest, SymbolIconsSurviveAReopenBesideOlderEmojiRows) {
  const std::string work = Id();
  const std::string planet = Id();
  const std::string junk = Id();
  {
    CluStore store(path_);
    store.Load(kNow);
    SpaceRow emoji;
    emoji.id = work;
    emoji.name = "Work";
    emoji.icon_kind = "emoji";
    emoji.icon_value = "\U0001F4BC";
    SpaceRow symbol;
    symbol.id = planet;
    symbol.name = "Planet";
    symbol.icon_kind = "symbol";
    symbol.icon_value = "planet";
    SpaceRow unknown;
    unknown.id = junk;
    unknown.name = "Junk";
    unknown.icon_kind = "symbol";
    unknown.icon_value = "not-a-glyph";
    ASSERT_TRUE(store.ReplaceSpaces({emoji, symbol, unknown}, kNow));
  }
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  ASSERT_EQ(load.spaces.size(), 3u);
  EXPECT_EQ(load.spaces[0].icon_kind, "emoji");
  EXPECT_EQ(load.spaces[0].icon_value, "\U0001F4BC");
  EXPECT_EQ(load.spaces[1].icon_kind, "symbol");
  EXPECT_EQ(load.spaces[1].icon_value, "planet");
  EXPECT_EQ(load.spaces[2].icon_kind, "letter");
  EXPECT_EQ(load.spaces[2].icon_value, "");
}

TEST_F(CluStoreTest, RowsAndSpaceStateSurviveAReopen) {
  std::string profile_id;
  std::string space_id;
  std::string today_id;
  std::string pinned_id;
  {
    CluStore store(path_);
    StoreLoad load = store.Load(kNow);
    profile_id = load.profile_id;
    space_id = load.spaces[0].id;
    CluTabTree tree;
    tree.SetSpaces({space_id});
    NewTab today;
    today.url = "https://a.test/today";
    today.nav_state = R"({"index":0,"entries":[{"url":"https://a.test/today","title":"T"}]})";
    today_id = tree.InsertTab(today, DropTarget::Today(space_id, 0), kNow).value();
    NewTab pinned;
    pinned.url = "https://a.test/pinned";
    pinned_id =
        tree.InsertTab(pinned, DropTarget::Pinned(space_id, "", 0), kNow)
            .value();
    // The pinned tab wandered off its pinned URL before the quit.
    tree.Mutable(pinned_id)->url = "https://a.test/elsewhere";
    tree.MutableSpaceState(space_id).active_tab_id = today_id;
    tree.MutableSpaceState(space_id).pinned_collapsed = true;
    TreeChanges changes = tree.TakeChanges();
    StoreWrite write;
    for (const std::string& id : changes.upserts) {
      write.upserts.push_back(*tree.Get(id));
    }
    write.spaces.emplace_back(space_id, tree.SpaceState(space_id));
    ASSERT_TRUE(store.Write(std::move(write)));
  }
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow + 1);
  EXPECT_EQ(load.profile_id, profile_id);
  EXPECT_TRUE(load.tree.kinds.empty());
  ASSERT_EQ(load.tree.nodes.size(), 2u);
  for (const TabNode& node : load.tree.nodes) {
    if (node.id == pinned_id) {
      // F2: back at its pinned URL after the restart.
      EXPECT_EQ(node.url, "https://a.test/pinned");
    } else {
      EXPECT_EQ(node.id, today_id);
      EXPECT_TRUE(node.nav_state);
    }
  }
  EXPECT_EQ(load.space_states.at(space_id).active_tab_id, today_id);
  EXPECT_TRUE(load.space_states.at(space_id).pinned_collapsed);
}

// A corrupt clu.db is razed and opened again, so the profile keeps saving instead of running from memory on every
// launch (and a dcheck build no longer hits "already open" at the memory fallback).
TEST_F(CluStoreTest, ACorruptFileIsRazedAndSavesAgain) {
  {
    CluStore store(path_);
    store.Load(kNow);
  }
  ASSERT_TRUE(sql::test::CorruptSizeInHeader(path_));
  std::string today_id;
  {
    CluStore store(path_);
    StoreLoad load = store.Load(kNow);
    EXPECT_TRUE(load.persistent);
    ASSERT_EQ(load.spaces.size(), 1u);
    CluTabTree tree;
    tree.SetSpaces({load.spaces[0].id});
    NewTab today;
    today.url = "https://a.test/after";
    today_id = tree.InsertTab(today, DropTarget::Today(load.spaces[0].id, 0),
                              kNow)
                   .value();
    StoreWrite write;
    write.upserts.push_back(*tree.Get(today_id));
    ASSERT_TRUE(store.Write(std::move(write)));
  }
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  EXPECT_TRUE(load.persistent);
  ASSERT_EQ(load.tree.nodes.size(), 1u);
  EXPECT_EQ(load.tree.nodes[0].id, today_id);
}

// A closed tab's tab_nodes delete and its archive row are one transaction: a failed write keeps the row in the tree
// on disk and writes no archive row, and the retried write lands both.
TEST_F(CluStoreTest, ATabsDeleteAndItsArchiveRowLandTogetherOrNotAtAll) {
  CluStore store(path_);
  StoreLoad load = store.Load(kNow);
  const std::string space_id = load.spaces[0].id;
  CluTabTree tree;
  tree.SetSpaces({space_id});
  NewTab today;
  today.url = "https://a.test/closed";
  const std::string id =
      tree.InsertTab(today, DropTarget::Today(space_id, 0), kNow).value();
  {
    StoreWrite write;
    write.upserts.push_back(*tree.Get(id));
    ASSERT_TRUE(store.Write(std::move(write)));
  }
  auto close_write = [&] {
    StoreWrite write;
    write.deletes.push_back(id);
    write.archive.emplace_back(
        std::vector<ArchiveSnapshot>{Snapshot("https://a.test/closed")},
        ArchiveReason::kClose, kNow);
    return write;
  };
  store.SetFailWritesForTesting(true);
  EXPECT_FALSE(store.Write(close_write()));
  store.SetFailWritesForTesting(false);
  EXPECT_TRUE(store.ListArchive("", "", 10).rows.empty());
  {
    sql::Statement s(store.database_for_testing().GetUniqueStatement(
        "SELECT COUNT(*) FROM tab_nodes"));
    ASSERT_TRUE(s.Step());
    EXPECT_EQ(s.ColumnInt(0), 1);
  }
  EXPECT_TRUE(store.Write(close_write()));
  EXPECT_EQ(store.ListArchive("", "", 10).rows.size(), 1u);
  sql::Statement s(store.database_for_testing().GetUniqueStatement(
      "SELECT COUNT(*) FROM tab_nodes"));
  ASSERT_TRUE(s.Step());
  EXPECT_EQ(s.ColumnInt(0), 0);
}

TEST_F(CluStoreTest, JunkRowsRepairInOneTransactionAndStayRepaired) {
  std::string space_id;
  {
    CluStore store(path_);
    space_id = store.Load(kNow).spaces[0].id;
    sql::Database& db = store.database_for_testing();
    ASSERT_TRUE(db.Execute(
        "INSERT INTO tab_nodes (id, section, kind, url, position) VALUES "
        "('junk', 'today', 'tab', 'https://a.test/', 0)"));
    ASSERT_TRUE(db.Execute(
        "INSERT INTO tab_nodes (id, section, kind, url, position) VALUES "
        "('00000000-0000-4000-8000-000000000001', 'sideways', 'tab', "
        "'https://a.test/', 0)"));
    ASSERT_TRUE(db.Execute(
        "INSERT INTO tab_nodes (id, section, kind, url, position, space_id) "
        "VALUES ('00000000-0000-4000-8000-000000000002', 'today', 'tab', "
        "'javascript:x', 'zero', NULL)"));
  }
  {
    CluStore store(path_);
    const StoreLoad load = store.Load(kNow);
    EXPECT_EQ(load.tree.kinds.at("bad-id"), 1);
    EXPECT_EQ(load.tree.kinds.at("bad-section-or-kind"), 1);
    EXPECT_EQ(load.tree.kinds.at("invalid-url"), 1);
    EXPECT_EQ(load.tree.kinds.at("missing-space"), 1);
    ASSERT_EQ(load.tree.nodes.size(), 1u);
    EXPECT_EQ(load.tree.nodes[0].url, "about:blank");
    EXPECT_EQ(load.tree.nodes[0].space_id, space_id);
  }
  // The repair was written: the next load finds nothing to fix.
  CluStore store(path_);
  EXPECT_TRUE(store.Load(kNow).tree.kinds.empty());
}

TEST_F(CluStoreTest, ArchiveListsNewestFirstAndPagesWithoutDuplicates) {
  CluStore store(path_);
  store.Load(kNow);
  for (int i = 0; i < 250; ++i) {
    store.AddArchive({Snapshot("https://a.test/" + base::NumberToString(i))},
                     ArchiveReason::kClose, kNow + i);
  }
  std::set<std::string> seen;
  std::string cursor;
  int pages = 0;
  int64_t last = kNow + 1000;
  do {
    ArchivePage page = store.ListArchive("", cursor, 100);
    ++pages;
    for (const ArchiveRow& row : page.rows) {
      EXPECT_TRUE(seen.insert(row.id).second);
      EXPECT_LE(row.archived_at, last);
      last = row.archived_at;
    }
    cursor = page.next_cursor.value_or("");
  } while (!cursor.empty());
  EXPECT_EQ(seen.size(), 250u);
  EXPECT_EQ(pages, 3);
}

TEST_F(CluStoreTest, ArchiveSearchMatchesPercentAndUnderscoreLiterally) {
  CluStore store(path_);
  store.Load(kNow);
  store.AddArchive({Snapshot("https://a.test/1", "50% off"),
                    Snapshot("https://a.test/2", "500 off"),
                    Snapshot("https://a.test/3", "a_b"),
                    Snapshot("https://a.test/4", "axb")},
                   ArchiveReason::kClose, kNow);
  EXPECT_EQ(store.ListArchive("50%", "", 100).rows.size(), 1u);
  EXPECT_EQ(store.ListArchive("a_b", "", 100).rows.size(), 1u);
  EXPECT_EQ(store.ListArchive("off", "", 100).rows.size(), 2u);
}

TEST_F(CluStoreTest, ArchiveSkipsUrlsThePolicyRefusesAndJunkRows) {
  CluStore store(path_);
  store.Load(kNow);
  EXPECT_TRUE(store.AddArchive({Snapshot("javascript:alert(1)")},
                               ArchiveReason::kClose, kNow)
                  .empty());
  ASSERT_TRUE(store.database_for_testing().Execute(
      "INSERT INTO archived_tabs (id, url, reason, archived_at) VALUES "
      "('a', 'https://a.test/', 'lost', 1), ('b', NULL, 'close', 1), "
      "('c', 'https://a.test/', 'close', 'when'), ('d', 'https://a.test/', "
      "'close', 2)"));
  const ArchivePage page = store.ListArchive("", "", 100);
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].id, "d");
}

TEST_F(CluStoreTest, PruneDropsRowsAtNinetyDaysAndBeyondFiveThousand) {
  CluStore store(path_);
  store.Load(kNow);
  store.AddArchive({Snapshot("https://a.test/old")}, ArchiveReason::kAuto,
                   kNow - 90 * kDay);
  store.AddArchive({Snapshot("https://a.test/kept")}, ArchiveReason::kAuto,
                   kNow - 89 * kDay);
  EXPECT_EQ(store.Prune(kNow), 1);
  std::vector<ArchiveSnapshot> many;
  for (int i = 0; i < kArchiveMaxRows + 4; ++i) {
    many.push_back(Snapshot("https://a.test/m"));
  }
  store.AddArchive(std::move(many), ArchiveReason::kClose, kNow);
  EXPECT_EQ(store.Prune(kNow), 5);
}

TEST_F(CluStoreTest, ReopenFallbackSkipsAutoRowsAndWidensToTheClearBatch) {
  CluStore store(path_);
  store.Load(kNow);
  store.AddArchive({Snapshot("https://a.test/c1"), Snapshot("https://a.test/c2"),
                    Snapshot("https://a.test/c3")},
                   ArchiveReason::kClear, kNow);
  store.AddArchive({Snapshot("https://a.test/auto")}, ArchiveReason::kAuto,
                   kNow + 10);
  const std::vector<ArchiveRow> rows = store.NewestReopenable();
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].url, "https://a.test/c1");
  EXPECT_EQ(rows[2].url, "https://a.test/c3");
}

TEST_F(CluStoreTest, ArchivedSplitMembersShareAGroup) {
  CluStore store(path_);
  store.Load(kNow);
  ArchiveSnapshot a = Snapshot("https://a.test/a");
  ArchiveSnapshot b = Snapshot("https://a.test/b");
  a.split_id = b.split_id = Id();
  const std::vector<std::string> ids =
      store.AddArchive({a, b}, ArchiveReason::kAuto, kNow);
  const std::vector<ArchiveRow> rows = store.GetArchive(ids);
  ASSERT_EQ(rows.size(), 2u);
  ASSERT_TRUE(rows[0].group_id);
  EXPECT_EQ(rows[0].group_id, rows[1].group_id);
}

// Two Today tabs of the first space that carry `split_id`; returns their ids in
// sidebar order.
std::vector<std::string> WriteSplitTabs(
    CluStore& store,
    const std::string& space_id,
    const std::optional<std::string>& split_id,
    SplitRow* split = nullptr) {
  CluTabTree tree;
  tree.SetSpaces({space_id});
  NewTab a;
  a.url = "https://a.test/a";
  NewTab b;
  b.url = "https://a.test/b";
  const std::string b_id =
      tree.InsertTab(b, DropTarget::Today(space_id, 0), kNow).value();
  const std::string a_id =
      tree.InsertTab(a, DropTarget::Today(space_id, 0), kNow).value();
  tree.Mutable(a_id)->split_id = split_id;
  tree.Mutable(b_id)->split_id = split_id;
  StoreWrite write;
  for (const std::string& id : tree.TakeChanges().upserts) {
    write.upserts.push_back(*tree.Get(id));
  }
  if (split) {
    split->tab_ids = {a_id, b_id};
    split->space_id = space_id;
    write.split_upserts.push_back(*split);
  }
  EXPECT_TRUE(store.Write(std::move(write)));
  return {a_id, b_id};
}

TEST_F(CluStoreTest, ASplitRoundTripsWithItsDirectionAndShare) {
  SplitRow split;
  split.id = Id();
  split.dir = SplitDir::kCol;
  split.ratio = 0.3;
  std::vector<std::string> ids;
  {
    CluStore store(path_);
    const std::string space_id = store.Load(kNow).spaces[0].id;
    ids = WriteSplitTabs(store, space_id, split.id, &split);
  }
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  ASSERT_EQ(load.splits.size(), 1u);
  EXPECT_EQ(load.splits[0].id, split.id);
  EXPECT_EQ(load.splits[0].tab_ids, ids);
  EXPECT_EQ(load.splits[0].dir, SplitDir::kCol);
  EXPECT_NEAR(load.splits[0].ratio, 0.3, 1e-9);
}

TEST_F(CluStoreTest, AJunkSplitRowRebuildsFromItsTabs) {
  const std::string split_id = Id();
  std::vector<std::string> ids;
  {
    CluStore store(path_);
    const std::string space_id = store.Load(kNow).spaces[0].id;
    ids = WriteSplitTabs(store, space_id, split_id);
    sql::Statement junk(store.database_for_testing().GetUniqueStatement(
        "INSERT INTO tab_splits (id, space_id, layout) VALUES (?, ?, "
        "'{not json')"));
    junk.BindString(0, split_id);
    junk.BindString(1, space_id);
    ASSERT_TRUE(junk.Run());
  }
  {
    CluStore store(path_);
    const StoreLoad load = store.Load(kNow);
    ASSERT_EQ(load.splits.size(), 1u);
    EXPECT_EQ(load.splits[0].tab_ids, ids);
  }
  // The rebuilt layout was written back: a second load reads it as it is.
  CluStore again(path_);
  EXPECT_EQ(again.Load(kNow).splits.size(), 1u);
}

TEST_F(CluStoreTest, ASplitWithOneValidTabDissolves) {
  SplitRow split;
  split.id = Id();
  {
    CluStore store(path_);
    const std::string space_id = store.Load(kNow).spaces[0].id;
    WriteSplitTabs(store, space_id, split.id, &split);
    // One pane's row goes; its split row still names it.
    sql::Statement drop(store.database_for_testing().GetUniqueStatement(
        "DELETE FROM tab_nodes WHERE id = ?"));
    drop.BindString(0, split.tab_ids[1]);
    ASSERT_TRUE(drop.Run());
  }
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  EXPECT_TRUE(load.splits.empty());
  ASSERT_EQ(load.tree.nodes.size(), 1u);
  EXPECT_FALSE(load.tree.nodes[0].split_id);
  sql::Statement count(store.database_for_testing().GetUniqueStatement(
      "SELECT COUNT(*) FROM tab_splits"));
  ASSERT_TRUE(count.Step());
  EXPECT_EQ(count.ColumnInt(0), 0);
}

TEST_F(CluStoreTest, ArchiveAfterHoursIsReadBack) {
  {
    CluStore store(path_);
    store.Load(kNow);
    ASSERT_TRUE(store.SetSetting("tabs.archiveAfterHours", "168", kNow));
  }
  CluStore store(path_);
  EXPECT_EQ(store.Load(kNow).archive_after_hours, 168);
}

// The favorites whose unread badge is off come back; junk entries are skipped.
TEST_F(CluStoreTest, UnreadBadgeOffIsReadBackWithoutJunk) {
  {
    CluStore store(path_);
    store.Load(kNow);
    ASSERT_TRUE(store.SetSetting(
        kUnreadBadgeOffKey,
        base::StrCat({"[\"fav-1\", 7, null, \"\", \"", std::string(65, 'x'),
                      "\", \"fav-2\"]"}),
        kNow));
  }
  {
    CluStore store(path_);
    EXPECT_EQ(store.Load(kNow).unread_badge_off,
              (std::vector<std::string>{"fav-1", "fav-2"}));
    ASSERT_TRUE(store.SetSetting(kUnreadBadgeOffKey, "{\"not\": \"a list\"}",
                                 kNow));
  }
  CluStore store(path_);
  EXPECT_TRUE(store.Load(kNow).unread_badge_off.empty());
}

TEST_F(CluStoreTest, AnUnopenableFileKeepsRowsInMemory) {
  // A directory where the file should be.
  ASSERT_TRUE(base::CreateDirectory(path_));
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  EXPECT_FALSE(load.persistent);
  EXPECT_EQ(load.spaces.size(), 1u);
}

}  // namespace
}  // namespace clu

namespace clu {
namespace {

// M8: Clear browsing data's "Archived tabs" box clears by range; the import map survives a reopen.
TEST_F(CluStoreTest, ArchiveClearsOnlyRowsInsideTheRange) {
  CluStore store(path_);
  store.Load(kNow);
  store.AddArchive({Snapshot("https://old.test/")}, ArchiveReason::kClose,
                   kNow - 10 * kDay);
  store.AddArchive({Snapshot("https://recent.test/")}, ArchiveReason::kClose,
                   kNow - 2 * 60 * 60 * 1000);
  store.AddArchive({Snapshot("https://now.test/")}, ArchiveReason::kAuto,
                   kNow - 1000);
  // The last hour: only the newest row.
  EXPECT_EQ(store.RemoveArchiveBetween(kNow - 60 * 60 * 1000, kNow + 1), 1);
  // An empty or reversed range removes nothing.
  EXPECT_EQ(store.RemoveArchiveBetween(kNow, kNow), 0);
  EXPECT_EQ(store.RemoveArchiveBetween(kNow, kNow - kDay), 0);
  ArchivePage page = store.ListArchive("", "", 100);
  ASSERT_EQ(page.rows.size(), 2u);
  // All time.
  EXPECT_EQ(store.RemoveArchiveBetween(0, kNow + 1), 2);
  EXPECT_TRUE(store.ListArchive("", "", 100).rows.empty());
}

TEST_F(CluStoreTest, TheImportMapSurvivesAReopen) {
  {
    CluStore store(path_);
    store.Load(kNow);
    EXPECT_TRUE(store.PutImportMap("chrome", {{"Default/guid:a", "id-a"},
                                              {"Default/guid:b", "id-b"}}));
    EXPECT_FALSE(store.PutImportMap("chrome", {}));
    // A key maps to its latest id.
    EXPECT_TRUE(store.PutImportMap("chrome", {{"Default/guid:b", "id-b2"}}));
  }
  CluStore store(path_);
  store.Load(kNow);
  const std::map<std::string, std::string> got = store.GetImportMap(
      "chrome", {"Default/guid:a", "Default/guid:b", "Default/guid:c"});
  EXPECT_EQ(got, (std::map<std::string, std::string>{{"Default/guid:a", "id-a"},
                                                     {"Default/guid:b", "id-b2"}}));
  // Sources are kept apart.
  EXPECT_TRUE(store.GetImportMap("sidebar", {"Default/guid:a"}).empty());
}

}  // namespace
}  // namespace clu

namespace clu {
namespace {

// M8 (the first box of docs/PLAN.md 12.13): a damaged clu.db moves aside and the backup comes back, or
// Clu starts fresh; a good file is backed up at most once a day.
TEST_F(CluStoreTest, ADamagedFileMovesAsideAndStartsFresh) {
  ASSERT_TRUE(base::CreateDirectory(path_.DirName()));
  ASSERT_TRUE(base::WriteFile(path_, "these bytes are not a database at all"));
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow);
  EXPECT_TRUE(load.persistent);
  EXPECT_EQ(load.recovered, "fresh");
  ASSERT_EQ(load.spaces.size(), 1u);
  EXPECT_TRUE(base::PathExists(base::FilePath(
      path_.value() + ".corrupt-" + base::NumberToString(kNow))));
  // The fresh file is the one now in place, and it was backed up.
  EXPECT_TRUE(base::PathExists(base::FilePath(path_.value() + ".bak")));
}

TEST_F(CluStoreTest, ADamagedFileComesBackFromTheBackup) {
  std::string kept_id;
  {
    CluStore store(path_);
    const StoreLoad load = store.Load(kNow);
    EXPECT_EQ(load.recovered, std::nullopt);
    StoreWrite write;
    TabNode node;
    node.id = Id();
    kept_id = node.id;
    node.space_id = load.spaces[0].id;
    node.section = Section::kToday;
    node.url = "https://kept.test/";
    node.title = "Kept";
    node.last_active_at = kNow;
    write.upserts.push_back(node);
    ASSERT_TRUE(store.Write(std::move(write)));
  }
  // A backup of the file with the row (the one Load made held no row yet).
  ASSERT_TRUE(base::CopyFile(path_, base::FilePath(path_.value() + ".bak")));
  ASSERT_TRUE(base::WriteFile(path_, std::string(4096, '\x7f')));
  CluStore store(path_);
  const StoreLoad load = store.Load(kNow + kDay);
  EXPECT_EQ(load.recovered, "backup");
  ASSERT_EQ(load.tree.nodes.size(), 1u);
  EXPECT_EQ(load.tree.nodes[0].id, kept_id);
}

TEST_F(CluStoreTest, AGoodFileIsBackedUpAtMostOnceADay) {
  {
    CluStore store(path_);
    EXPECT_EQ(store.Load(kNow).recovered, std::nullopt);
  }
  const base::FilePath backup(path_.value() + ".bak");
  ASSERT_TRUE(base::PathExists(backup));
  // An hour old: the next launch copies nothing.
  const base::Time recent = base::Time::Now() - base::Hours(1);
  ASSERT_TRUE(base::TouchFile(backup, recent, recent));
  base::File::Info touched;
  ASSERT_TRUE(base::GetFileInfo(backup, &touched));
  {
    CluStore store(path_);
    store.Load(base::Time::Now().InMillisecondsSinceUnixEpoch());
  }
  base::File::Info after;
  ASSERT_TRUE(base::GetFileInfo(backup, &after));
  EXPECT_EQ(after.last_modified, touched.last_modified);
  // Two days old: it is copied again.
  const base::Time old = base::Time::Now() - base::Days(2);
  ASSERT_TRUE(base::TouchFile(backup, old, old));
  {
    CluStore store(path_);
    store.Load(base::Time::Now().InMillisecondsSinceUnixEpoch());
  }
  ASSERT_TRUE(base::GetFileInfo(backup, &after));
  EXPECT_GT(after.last_modified, old + base::Days(1));
}

}  // namespace
}  // namespace clu
