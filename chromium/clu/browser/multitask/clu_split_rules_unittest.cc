// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_split_rules.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

// The stored layout validator, load-time repair of junk rows, drop placement,
// the resize clamp and the payload rules.
namespace clu {
namespace {

constexpr char kA[] = "11111111-1111-4111-8111-111111111111";
constexpr char kB[] = "22222222-2222-4222-8222-222222222222";
constexpr char kC[] = "33333333-3333-4333-8333-333333333333";
constexpr char kD[] = "44444444-4444-4444-8444-444444444444";
constexpr char kS1[] = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr char kS2[] = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
constexpr char kSpace[] = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
constexpr char kOtherSpace[] = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";

std::string Row(const std::string& a,
                const std::string& b,
                double first = 0.5,
                const std::string& dir = "row") {
  return "{\"kind\":\"group\",\"dir\":\"" + dir + "\",\"sizes\":[" +
         std::to_string(first) + "," + std::to_string(1 - first) +
         "],\"children\":[{\"kind\":\"pane\",\"tabId\":\"" + a +
         "\"},{\"kind\":\"pane\",\"tabId\":\"" + b + "\"}]}";
}

SplitMember Member(const std::string& id,
                   std::optional<std::string> split_id,
                   const std::string& space = kSpace,
                   bool favorite = false) {
  return {.id = id,
          .space_id = favorite ? std::string() : space,
          .favorite = favorite,
          .split_id = std::move(split_id)};
}

RawSplitRow Raw(const std::string& id, std::optional<std::string> layout) {
  RawSplitRow row;
  row.id = id;
  row.space_id = kSpace;
  row.layout = std::move(layout);
  row.focused_tab_id = kA;
  return row;
}

TEST(CluSplitRulesTest, AFlatPairParsesWithItsDirectionAndShare) {
  std::optional<ParsedSplitLayout> parsed =
      ParseSplitLayout(Row(kA, kB, 0.3, "col"));
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->tab_ids, (std::vector<std::string>{kA, kB}));
  EXPECT_EQ(parsed->dir, SplitDir::kCol);
  ASSERT_TRUE(parsed->ratio);
  EXPECT_NEAR(*parsed->ratio, 0.3, 1e-9);
}

TEST(CluSplitRulesTest, JunkLayoutsAreRefused) {
  for (const std::string& junk : std::vector<std::string>{
           "", "null", "[]", "{", "{\"kind\":\"pane\",\"tabId\":\"x\"}",
           // A lone pane is no split.
           std::string("{\"kind\":\"pane\",\"tabId\":\"") + kA + "\"}",
           // Sizes summing to 1.2, NaN-like strings, a size of 0.
           "{\"kind\":\"group\",\"dir\":\"row\",\"sizes\":[0.6,0.6],"
           "\"children\":[{\"kind\":\"pane\",\"tabId\":\"" +
               std::string(kA) + "\"},{\"kind\":\"pane\",\"tabId\":\"" + kB +
               "\"}]}",
           "{\"kind\":\"group\",\"dir\":\"row\",\"sizes\":[\"NaN\",0.5],"
           "\"children\":[{\"kind\":\"pane\",\"tabId\":\"" +
               std::string(kA) + "\"},{\"kind\":\"pane\",\"tabId\":\"" + kB +
               "\"}]}",
           // A duplicate tab, an unknown key, a bad direction, a non-UUID id.
           Row(kA, kA), Row(kA, "not-a-uuid"),
           Row(kA, kB).insert(1, "\"extra\":1,"),
           Row(kA, kB, 0.5, "diagonal")}) {
    EXPECT_FALSE(ParseSplitLayout(junk)) << junk;
  }
  EXPECT_FALSE(ParseSplitLayout(std::string(kMaxSplitLayoutBytes + 1, ' ')));
}

TEST(CluSplitRulesTest, ANestedFourPaneLayoutParsesInTreeOrderWithoutAShare) {
  const std::string nested =
      "{\"kind\":\"group\",\"dir\":\"row\",\"sizes\":[0.5,0.5],\"children\":"
      "[" +
      Row(kA, kB, 0.5, "col") + "," + Row(kC, kD, 0.5, "col") + "]}";
  std::optional<ParsedSplitLayout> parsed = ParseSplitLayout(nested);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->tab_ids, (std::vector<std::string>{kA, kB, kC, kD}));
  EXPECT_FALSE(parsed->ratio);
  // Three levels is too deep.
  const std::string deep =
      "{\"kind\":\"group\",\"dir\":\"col\",\"sizes\":[0.5,"
      "0.5],\"children\":[" +
      nested + "," + Row(kC, kD) + "]}";
  EXPECT_FALSE(ParseSplitLayout(deep));
}

TEST(CluSplitRulesTest, TheRootRoundTripsThroughTheStoredJson) {
  SplitRow split;
  split.id = kS1;
  split.tab_ids = {kA, kB};
  split.dir = SplitDir::kCol;
  split.ratio = 0.25;
  std::optional<ParsedSplitLayout> parsed =
      ParseSplitLayout(SplitLayoutJson(split));
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->tab_ids, split.tab_ids);
  EXPECT_EQ(parsed->dir, SplitDir::kCol);
  EXPECT_NEAR(*parsed->ratio, 0.25, 1e-9);
}

TEST(CluSplitRulesTest, AValidRowLoadsAsItIs) {
  SplitRepairResult r = RepairSplits({Raw(kS1, Row(kA, kB, 0.4))},
                                     {Member(kA, kS1), Member(kB, kS1)});
  ASSERT_EQ(r.splits.size(), 1u);
  EXPECT_EQ(r.splits[0].tab_ids, (std::vector<std::string>{kA, kB}));
  EXPECT_NEAR(r.splits[0].ratio, 0.4, 1e-9);
  EXPECT_EQ(r.splits[0].focused_tab_id, kA);
  EXPECT_TRUE(r.member_fixes.empty());
  EXPECT_TRUE(r.dropped.empty());
  EXPECT_TRUE(r.rewritten.empty());
}

TEST(CluSplitRulesTest, JunkJsonRebuildsAFlatRowFromTheTabsInSidebarOrder) {
  SplitRepairResult r =
      RepairSplits({Raw(kS1, "{not json")}, {Member(kB, kS1), Member(kA, kS1)});
  ASSERT_EQ(r.splits.size(), 1u);
  EXPECT_EQ(r.splits[0].tab_ids, (std::vector<std::string>{kB, kA}));
  EXPECT_EQ(r.splits[0].dir, SplitDir::kRow);
  EXPECT_EQ(r.rewritten, (std::vector<std::string>{kS1}));
}

TEST(CluSplitRulesTest, UnknownTabsLeaveAndOneValidTabDissolves) {
  // kC is no tab row: one pane is left, so the split dissolves and kA becomes a
  // normal tab.
  SplitRepairResult r =
      RepairSplits({Raw(kS1, Row(kA, kC))}, {Member(kA, kS1)});
  EXPECT_TRUE(r.splits.empty());
  EXPECT_EQ(r.dropped, (std::vector<std::string>{kS1}));
  ASSERT_EQ(r.member_fixes.size(), 1u);
  EXPECT_EQ(r.member_fixes[0].first, kA);
  EXPECT_FALSE(r.member_fixes[0].second);
}

TEST(CluSplitRulesTest, FavoritesTabsOfAnotherSpaceAndTakenTabsLeave) {
  SplitRepairResult r = RepairSplits(
      {Raw(kS1, Row(kA, kB)), Raw(kS2, Row(kA, kC))},
      {Member(kA, kS1), Member(kB, kS1), Member(kC, kS2, kOtherSpace),
       Member(kD, std::nullopt, kSpace, /*favorite=*/true)});
  // kS2 loses kA (kS1 has it) and kC is alone: it dissolves.
  ASSERT_EQ(r.splits.size(), 1u);
  EXPECT_EQ(r.splits[0].id, kS1);
  EXPECT_EQ(r.dropped, (std::vector<std::string>{kS2}));
  ASSERT_EQ(r.member_fixes.size(), 1u);
  EXPECT_EQ(r.member_fixes[0].first, kC);
}

// A split holds up to 4 panes.
TEST(CluSplitRulesTest, FourPanesLoadAsTheyAre) {
  const std::string nested =
      "{\"kind\":\"group\",\"dir\":\"row\",\"sizes\":[0.5,0.5],\"children\":"
      "[" +
      Row(kA, kB, 0.5, "col") + "," + Row(kC, kD, 0.5, "col") + "]}";
  SplitRepairResult r = RepairSplits(
      {Raw(kS1, nested)},
      {Member(kA, kS1), Member(kB, kS1), Member(kC, kS1), Member(kD, kS1)});
  ASSERT_EQ(r.splits.size(), 1u);
  EXPECT_EQ(r.splits[0].tab_ids, (std::vector<std::string>{kA, kB, kC, kD}));
  EXPECT_TRUE(r.rewritten.empty());
  EXPECT_TRUE(r.member_fixes.empty());
  EXPECT_EQ(kMaxSplitPanes, 4u);
  EXPECT_STREQ(kSplitFullToast, "A split holds up to 4 tabs");
}

TEST(CluSplitRulesTest, AThreePaneRowKeepsItsSharesThroughTheStoredJson) {
  SplitRow split;
  split.id = kS1;
  split.tab_ids = {kA, kB, kC};
  split.sizes = {0.5, 0.25, 0.25};
  std::optional<ParsedSplitLayout> parsed =
      ParseSplitLayout(SplitLayoutJson(split));
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->tab_ids, split.tab_ids);
  EXPECT_FALSE(parsed->ratio);
  ASSERT_EQ(parsed->sizes.size(), 3u);
  EXPECT_NEAR(parsed->sizes[0], 0.5, 1e-9);
  SplitRepairResult r = RepairSplits(
      {Raw(kS1, SplitLayoutJson(split))},
      {Member(kA, kS1), Member(kB, kS1), Member(kC, kS1)});
  ASSERT_EQ(r.splits.size(), 1u);
  EXPECT_EQ(r.splits[0].sizes.size(), 3u);
  // Junk or missing shares are even.
  EXPECT_EQ(PaneShares(std::vector<double>{0.9, 0.9, 0.1}, 3),
            std::vector<double>(3, 1.0 / 3));
  EXPECT_EQ(PaneShares({}, 4), std::vector<double>(4, 0.25));
  const std::vector<double> good = {0.5, 0.25, 0.25};
  EXPECT_EQ(PaneShares(good, 3), good);
  // A third pane that leaves takes the shares with it: two panes are even.
  SplitRepairResult two = RepairSplits({Raw(kS1, SplitLayoutJson(split))},
                                       {Member(kA, kS1), Member(kB, kS1)});
  ASSERT_EQ(two.splits.size(), 1u);
  EXPECT_TRUE(two.splits[0].sizes.empty());
}

TEST(CluSplitRulesTest, BadIdsDuplicatesStaleTabIdsAndBadFocusAreRepaired) {
  RawSplitRow bad_id = Raw("not-a-uuid", Row(kC, kD));
  RawSplitRow focus = Raw(kS1, Row(kA, kB));
  focus.focused_tab_id = kC;
  RawSplitRow duplicate = Raw(kS1, Row(kC, kD));
  SplitRepairResult r =
      RepairSplits({bad_id, focus, duplicate},
                   {Member(kA, kS1), Member(kB, std::nullopt),
                    Member(kC, "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"),
                    Member(kD, std::nullopt)});
  ASSERT_EQ(r.splits.size(), 1u);
  EXPECT_EQ(r.splits[0].focused_tab_id, kA);
  // kB gets the split id it lacked, kC loses one that names no split.
  EXPECT_EQ(r.member_fixes.size(), 2u);
  // The junk id goes; the second row with kS1's id is skipped, so kS1 itself is
  // never deleted.
  EXPECT_EQ(r.dropped, (std::vector<std::string>{"not-a-uuid"}));
}

TEST(CluSplitRulesTest, AnEdgeDropPlacesTheTabBesideItsTarget) {
  EXPECT_EQ(PlaceDrop(SplitEdge::kLeft).dir, SplitDir::kRow);
  EXPECT_TRUE(PlaceDrop(SplitEdge::kLeft).dropped_first);
  EXPECT_FALSE(PlaceDrop(SplitEdge::kRight).dropped_first);
  EXPECT_EQ(PlaceDrop(SplitEdge::kTop).dir, SplitDir::kCol);
  EXPECT_TRUE(PlaceDrop(SplitEdge::kTop).dropped_first);
  EXPECT_EQ(PlaceDrop(SplitEdge::kBottom).dir, SplitDir::kCol);
  EXPECT_FALSE(PlaceDrop(SplitEdge::kBottom).dropped_first);
}

TEST(CluSplitRulesTest, AResizeKeepsTheMinimumPaneAt1280) {
  // 1280 wide: each pane keeps 220 DIP, so the share stays within [0.171875,
  // 0.828125].
  EXPECT_NEAR(ClampSplitRatio(0.05, 1280, kMinPaneWidth, 0.5), 220.0 / 1280,
              1e-9);
  EXPECT_NEAR(ClampSplitRatio(0.99, 1280, kMinPaneWidth, 0.5), 1 - 220.0 / 1280,
              1e-9);
  EXPECT_NEAR(ClampSplitRatio(0.6, 1280, kMinPaneWidth, 0.5), 0.6, 1e-9);
  // A room too small for two minimum panes splits evenly.
  EXPECT_EQ(ClampSplitRatio(0.7, 400, kMinPaneWidth, 0.5), 0.5);
  // Junk keeps the current share.
  EXPECT_EQ(ClampSplitRatio(std::nan(""), 1280, kMinPaneWidth, 0.4), 0.4);
  EXPECT_EQ(ClampSplitRatio(1.2, 1280, kMinPaneWidth, 0.4), 0.4);
}

TEST(CluSplitRulesTest, PayloadRulesZodRefines) {
  const std::vector<std::string> unique = {kA, kB};
  const std::vector<std::string> duplicate = {kA, kA};
  EXPECT_TRUE(UniqueIds(unique));
  EXPECT_FALSE(UniqueIds(duplicate));
  EXPECT_TRUE(SizesSumToOne(std::vector<double>{0.3, 0.7}));
  EXPECT_TRUE(SizesSumToOne(std::vector<double>{0.3, 0.7005}));
  EXPECT_FALSE(SizesSumToOne(std::vector<double>{0.6, 0.6}));
  EXPECT_FALSE(SizesSumToOne(std::vector<double>{std::nan(""), 1}));
}

TEST(CluSplitRulesTest, PanesAreFoundByIndexAndStepWithWrap) {
  const std::vector<std::string> panes = {kA, kB};
  EXPECT_EQ(PaneAt(panes, 1), kA);
  EXPECT_EQ(PaneAt(panes, 2), kB);
  EXPECT_FALSE(PaneAt(panes, 3));
  EXPECT_FALSE(PaneAt(panes, 0));
  EXPECT_EQ(StepPane(panes, kA, 1), kB);
  EXPECT_EQ(StepPane(panes, kB, 1), kA);
  EXPECT_EQ(StepPane(panes, kA, -1), kB);
  EXPECT_EQ(StepPane(panes, kC, 1), kA);
  EXPECT_FALSE(StepPane(panes, kA, 2));
}

// View ▸ Expand Current Split gives the focused pane all the room the
// others' minimum leaves.
TEST(CluSplitRulesTest, ExpandedSharesKeepTheOtherPanesAtTheirMinimum) {
  const std::vector<double> two = ExpandedShares(2, 1, 1000, kMinPaneWidth);
  ASSERT_EQ(two.size(), 2u);
  EXPECT_NEAR(two[0] * 1000, kMinPaneWidth + 1, 1e-6);
  EXPECT_NEAR(two[1], 1 - two[0], 1e-9);
  EXPECT_TRUE(SizesSumToOne(two));
  const std::vector<double> four = ExpandedShares(4, 0, 1400, kMinPaneWidth);
  ASSERT_EQ(four.size(), 4u);
  EXPECT_TRUE(SizesSumToOne(four));
  EXPECT_GT(four[0], four[1]);
  for (size_t i = 1; i < 4; ++i) {
    EXPECT_GE(four[i] * 1400, kMinPaneWidth);
  }
  // PaneShares keeps them as they are.
  EXPECT_EQ(PaneShares(four, 4), four);
  // No room to give, a pane out of range, junk room: nothing.
  EXPECT_TRUE(ExpandedShares(4, 0, 600, kMinPaneWidth).empty());
  EXPECT_TRUE(ExpandedShares(2, 2, 1000, kMinPaneWidth).empty());
  EXPECT_TRUE(ExpandedShares(1, 0, 1000, kMinPaneWidth).empty());
  EXPECT_TRUE(ExpandedShares(2, 0, 0, kMinPaneWidth).empty());
}

}  // namespace
}  // namespace clu
