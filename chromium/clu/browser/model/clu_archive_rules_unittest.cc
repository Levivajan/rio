// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_archive_rules.h"

#include <map>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// archive-t1 and archive-t2 at the rule layer (src/main/archive/sweeper.test.ts, reopen.test.ts, mru.test.ts): every
// "Archive tabs after" choice on an injected clock, held tabs, negative idle, junk settings, splits, the closed stack.
namespace clu {
namespace {

constexpr int64_t kNow = 1'800'000'000'000;
constexpr int64_t kHour = 60 * 60 * 1000;

SweepTab Tab(const std::string& id,
             int64_t last_active_at,
             Section section = Section::kToday) {
  SweepTab tab;
  tab.id = id;
  tab.section = section;
  tab.last_active_at = last_active_at;
  return tab;
}

class CluSweepTest : public testing::TestWithParam<int> {};

TEST_P(CluSweepTest, JustUnderStaysAndTheThresholdArchives) {
  const int hours = GetParam();
  const int64_t threshold = ArchiveAfterMs(hours);
  std::map<std::string, int64_t> first_seen;
  const std::vector<SweepTab> tabs = {
      Tab("under", kNow - threshold + 1), Tab("at", kNow - threshold),
      Tab("pinned", kNow - threshold * 2, Section::kPinned),
      Tab("favorite", kNow - threshold * 2, Section::kFavorite)};
  EXPECT_EQ(SelectIdleTabs(tabs, kNow, threshold, first_seen),
            std::vector<std::string>{"at"});
}

INSTANTIATE_TEST_SUITE_P(EveryChoice,
                         CluSweepTest,
                         testing::Values(12, 24, 168, 720));

TEST(CluArchiveRulesTest, HeldTabsWaitAndArchiveOnceReleased) {
  std::map<std::string, int64_t> first_seen;
  SweepTab held = Tab("held", kNow - 13 * kHour);
  held.held = true;
  EXPECT_TRUE(SelectIdleTabs({held}, kNow, 12 * kHour, first_seen).empty());
  held.held = false;
  EXPECT_EQ(SelectIdleTabs({held}, kNow, 12 * kHour, first_seen),
            std::vector<std::string>{"held"});
}

TEST(CluArchiveRulesTest, NegativeIdleWaits) {
  std::map<std::string, int64_t> first_seen;
  EXPECT_TRUE(
      SelectIdleTabs({Tab("future", kNow + kHour)}, kNow, 12 * kHour, first_seen)
          .empty());
}

TEST(CluArchiveRulesTest, NeverActiveTabsIdleFromTheFirstSweepThatSawThem) {
  std::map<std::string, int64_t> first_seen;
  EXPECT_TRUE(
      SelectIdleTabs({Tab("new", 0)}, kNow, 12 * kHour, first_seen).empty());
  EXPECT_TRUE(SelectIdleTabs({Tab("new", 0)}, kNow + 11 * kHour, 12 * kHour,
                             first_seen)
                  .empty());
  EXPECT_EQ(
      SelectIdleTabs({Tab("new", 0)}, kNow + 12 * kHour, 12 * kHour, first_seen),
      std::vector<std::string>{"new"});
}

TEST(CluArchiveRulesTest, ASplitArchivesAsOneUnitByItsNewestMember) {
  std::map<std::string, int64_t> first_seen;
  SweepTab old_pane = Tab("old", kNow - 20 * kHour);
  SweepTab new_pane = Tab("new", kNow - 13 * kHour);
  old_pane.split_id = new_pane.split_id = std::string("s");
  EXPECT_EQ(SelectIdleTabs({old_pane, new_pane}, kNow, 12 * kHour, first_seen),
            (std::vector<std::string>{"old", "new"}));
  new_pane.last_active_at = kNow - kHour;
  EXPECT_TRUE(SelectIdleTabs({old_pane, new_pane}, kNow, 12 * kHour, first_seen)
                  .empty());
}

TEST(CluArchiveRulesTest, AnInvalidSettingReadsAsTwelveHours) {
  EXPECT_EQ(ArchiveAfterHours(std::nullopt), 12);
  EXPECT_EQ(ArchiveAfterHours(13), 12);
  EXPECT_EQ(ArchiveAfterHours(-1), 12);
  EXPECT_EQ(ArchiveAfterHours(24.5), 12);
  EXPECT_EQ(ArchiveAfterHours(168), 168);
  EXPECT_EQ(ArchiveAfterMs(720), 720 * kHour);
}

TEST(CluArchiveRulesTest, ReasonsRoundTrip) {
  for (const char* name : {"close", "clear", "auto", "delete", "mini"}) {
    ASSERT_TRUE(ArchiveReasonFromName(name));
    EXPECT_EQ(ArchiveReasonName(*ArchiveReasonFromName(name)), name);
  }
  EXPECT_FALSE(ArchiveReasonFromName("lost"));
}

TEST(CluClosedStackTest, AutoRowsNeverEnterAndTheStackKeepsFifty) {
  ClosedStack stack;
  stack.PushRows({"a"}, ArchiveReason::kAuto);
  EXPECT_TRUE(stack.empty());
  for (int i = 0; i < 60; ++i) {
    stack.PushRows({"r"}, ArchiveReason::kClose);
  }
  EXPECT_EQ(stack.entries().size(), ClosedStack::kMaxEntries);
  stack.PushAnchored("tab", "https://a.test/", std::nullopt);
  std::optional<ClosedEntry> top = stack.Pop();
  ASSERT_TRUE(top);
  EXPECT_EQ(top->kind, ClosedEntry::Kind::kAnchored);
  EXPECT_EQ(top->tab_id, "tab");
  EXPECT_EQ(stack.Pop()->kind, ClosedEntry::Kind::kRows);
}

TEST(CluTabMruTest, SeedsFromLastActiveAndMovesOnTouch) {
  TabMru mru;
  mru.Touch("now");
  mru.SeedIfEmpty({{"old", 1}, {"never", 0}, {"recent", 5}});
  EXPECT_EQ(mru.list(), (std::vector<std::string>{"now", "recent", "old"}));
  mru.Touch("old");
  EXPECT_EQ(mru.list(), (std::vector<std::string>{"old", "now", "recent"}));
  mru.Remove("now");
  EXPECT_EQ(mru.list(), (std::vector<std::string>{"old", "recent"}));
  // Seeding happens once.
  mru.SeedIfEmpty({{"late", 9}});
  EXPECT_EQ(mru.list(), (std::vector<std::string>{"old", "recent"}));
}

}  // namespace
}  // namespace clu
