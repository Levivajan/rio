// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_nav_state.h"

#include <string>

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

// The cases of src/shared/navState.test.ts: 50 entries around the current one, 256 KiB, policy-refused URLs dropped,
// junk JSON reads as none.
namespace clu {
namespace {

NavState Pages(int count, int index) {
  NavState state;
  state.index = index;
  for (int i = 0; i < count; ++i) {
    NavEntry entry;
    entry.url = "https://a.test/" + base::NumberToString(i);
    entry.title = "Page";
    state.entries.push_back(entry);
  }
  return state;
}

TEST(CluNavStateTest, KeepsAWindowOfFiftyAroundTheCurrentEntry) {
  std::optional<NavState> capped = CapNavState(Pages(120, 100));
  ASSERT_TRUE(capped);
  EXPECT_EQ(capped->entries.size(), kNavMaxEntries);
  EXPECT_EQ(capped->entries[capped->index].url, "https://a.test/100");
}

TEST(CluNavStateTest, DropsEntriesThePolicyRefusesAndKeepsTheCurrentPage) {
  NavState state = Pages(3, 2);
  state.entries[0].url = "javascript:alert(1)";
  state.entries[1].url = "chrome://clu-overlay/";
  std::optional<NavState> capped = CapNavState(state);
  ASSERT_TRUE(capped);
  ASSERT_EQ(capped->entries.size(), 1u);
  EXPECT_EQ(capped->index, 0);
  EXPECT_EQ(capped->entries[0].url, "https://a.test/2");
}

TEST(CluNavStateTest, FitsTheByteCapByDroppingPageStateThenFarEntries) {
  NavState state = Pages(40, 5);
  for (NavEntry& entry : state.entries) {
    entry.page_state = std::string(20 * 1024, 'p');
  }
  std::optional<NavState> capped = CapNavState(state);
  ASSERT_TRUE(capped);
  EXPECT_LE(SerializeNavState(*capped).size(), kNavMaxBytes);
  for (const NavEntry& entry : capped->entries) {
    EXPECT_FALSE(entry.page_state);
  }
  EXPECT_EQ(capped->entries[capped->index].url, "https://a.test/5");
}

TEST(CluNavStateTest, JunkJsonReadsAsNone) {
  EXPECT_FALSE(ParseNavState(""));
  EXPECT_FALSE(ParseNavState("{"));
  EXPECT_FALSE(ParseNavState("[]"));
  EXPECT_FALSE(ParseNavState(R"({"entries":"x"})"));
  EXPECT_FALSE(ParseNavState(R"({"index":0,"entries":[{"url":"data:,x"}]})"));
}

TEST(CluNavStateTest, RoundTrips) {
  const std::string json = SerializeNavState(Pages(3, 1));
  std::optional<NavState> parsed = ParseNavState(json);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->index, 1);
  EXPECT_EQ(parsed->entries.size(), 3u);
  EXPECT_EQ(SerializeNavState(*parsed), json);
}

TEST(CluNavStateTest, PushNavEntryLoadsOnTopOfTheCurrentEntry) {
  std::optional<std::string> pushed =
      PushNavEntry(SerializeNavState(Pages(3, 1)), "https://b.test/new");
  ASSERT_TRUE(pushed);
  std::optional<NavState> parsed = ParseNavState(*pushed);
  ASSERT_TRUE(parsed);
  // Back reaches the old entries; the forward one is gone, as after a link.
  ASSERT_EQ(parsed->entries.size(), 3u);
  EXPECT_EQ(parsed->index, 2);
  EXPECT_EQ(parsed->entries[0].url, "https://a.test/0");
  EXPECT_EQ(parsed->entries[1].url, "https://a.test/1");
  EXPECT_EQ(parsed->entries[2].url, "https://b.test/new");
  EXPECT_FALSE(PushNavEntry("{", "https://b.test/new"));
}

}  // namespace
}  // namespace clu
