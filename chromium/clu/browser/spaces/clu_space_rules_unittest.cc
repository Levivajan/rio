// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/spaces/clu_space_rules.h"

#include <string>
#include <vector>

#include "base/strings/string_util.h"
#include "testing/gtest/include/gtest/gtest.h"

// The cases of src/shared/spaces.test.ts and the switch, reorder and swipe rules of src/main/spaces.
namespace clu {
namespace {

std::string Repeat(const std::string& part, int times) {
  std::string out;
  for (int i = 0; i < times; ++i) {
    out += part;
  }
  return out;
}

TEST(CluSpaceRulesTest, NormalizeNameStripsControlsAndCollapsesWhiteSpace) {
  EXPECT_EQ(NormalizeName(std::string("Wo\0rk\x07", 6)), "Work");
  EXPECT_EQ(NormalizeName("  Side\t\n  project \r\n"), "Side project");
  EXPECT_EQ(NormalizeName(std::string("a \0 b", 5)), "a b");
  EXPECT_EQ(NormalizeName("‮" "evilName"), "evilName");
  EXPECT_EQ(NormalizeName("C1\xC2\x85\xC2\x9F" "chars"), "C1chars");
}

TEST(CluSpaceRulesTest, NormalizeNameRefusesEmptyAndJunk) {
  EXPECT_EQ(NormalizeName(""), std::nullopt);
  EXPECT_EQ(NormalizeName("   \t\n "), std::nullopt);
  EXPECT_EQ(NormalizeName(std::string("\0\x01\x1f", 3)), std::nullopt);
  // Invalid UTF-8 reads as nothing.
  EXPECT_EQ(NormalizeName("\xff\xfe"), std::nullopt);
}

TEST(CluSpaceRulesTest, NormalizeNameCutsAt60Graphemes) {
  EXPECT_EQ(NormalizeName(std::string(10000, 'x')), std::string(60, 'x'));
  EXPECT_EQ(NormalizeName(std::string(59, 'a') + " b"), std::string(59, 'a'));
}

TEST(CluSpaceRulesTest, NormalizeNameNeverSplitsAnEmoji) {
  const std::string family = "\U0001F469‍\U0001F469‍\U0001F467‍\U0001F466";
  // 11 UTF-16 units each: the 240-unit cap stops at 21 whole families, below the 60-grapheme cap.
  EXPECT_EQ(NormalizeName(Repeat(family, 100)), Repeat(family, 21));
  const std::string flag = "\U0001F1E6\U0001F1F2";
  EXPECT_EQ(NormalizeName(Repeat(flag, 70)), Repeat(flag, 60));
  EXPECT_EQ(NormalizeName("Work \U0001F4BC"), "Work \U0001F4BC");
}

TEST(CluSpaceRulesTest, ParseEmojiAcceptsOneEmoji) {
  EXPECT_EQ(ParseEmoji("\U0001F4BC"), "\U0001F4BC");
  EXPECT_EQ(ParseEmoji("\U0001F469‍\U0001F4BB"), "\U0001F469‍\U0001F4BB");
  EXPECT_EQ(ParseEmoji("\U0001F1E6\U0001F1F2"), "\U0001F1E6\U0001F1F2");
  EXPECT_EQ(ParseEmoji("\U0001F44D\U0001F3FD"), "\U0001F44D\U0001F3FD");
  EXPECT_EQ(ParseEmoji("❤️"), "❤️");
  EXPECT_EQ(ParseEmoji(" \U0001F680 "), "\U0001F680");
}

TEST(CluSpaceRulesTest, ParseEmojiRefusesTextMarkupAndSeveralEmoji) {
  for (const char* junk :
       {"ab", "a", "<script>", "", "   ", "\U0001F4BC\U0001F680",
        "\U0001F1E6\U0001F1F2\U0001F1E6\U0001F1F2", "\U0001F1E6",
        "\U0001F4BC" "a", "1", "\xff"}) {
    EXPECT_EQ(ParseEmoji(junk), std::nullopt) << junk;
  }
  EXPECT_EQ(ParseEmoji(Repeat("\U0001F469‍\U0001F4BB", 3)), std::nullopt);
}

TEST(CluSpaceRulesTest, ColorsAndDefaultNames) {
  EXPECT_TRUE(IsSpaceColor("teal"));
  EXPECT_FALSE(IsSpaceColor("chartreuse"));
  EXPECT_FALSE(IsSpaceColor(""));
  EXPECT_EQ(SpaceColorHex("blue"), "#3b82f6");
  EXPECT_EQ(SpaceColorHex("junk"), "#64748b");
  EXPECT_EQ(NextDefaultName("Space", 2, {"Home"}), "Space 2");
  EXPECT_EQ(NextDefaultName("Space", 2, {"Home", "Space 2", "Space 3"}),
            "Space 4");
  EXPECT_EQ(NextDefaultName("Profile", -3, {}), "Profile 1");
}

TEST(CluSpaceRulesTest, SymbolsMatchTheContractList) {
  EXPECT_TRUE(IsSpaceSymbol("planet"));
  EXPECT_TRUE(IsSpaceSymbol("cup"));
  EXPECT_FALSE(IsSpaceSymbol("Planet"));
  EXPECT_FALSE(IsSpaceSymbol("planet.fill"));
  EXPECT_FALSE(IsSpaceSymbol(""));
  EXPECT_EQ(SpaceSymbolAt(0), "planet");
  EXPECT_EQ(SpaceSymbolAt(kSpaceSymbolCount - 1), "cup");
  EXPECT_EQ(SpaceSymbolAt(kSpaceSymbolCount), std::nullopt);
}

TEST(CluSpaceRulesTest, ClosestSpaceSymbolReadsDottedNames) {
  EXPECT_EQ(ClosestSpaceSymbol("planet"), "planet");
  EXPECT_EQ(ClosestSpaceSymbol("globe.americas.fill"), "globe");
  EXPECT_EQ(ClosestSpaceSymbol("cup.and.saucer.fill"), "cup");
  EXPECT_EQ(ClosestSpaceSymbol("square.and.pencil"), "pencil");
  EXPECT_EQ(ClosestSpaceSymbol("testtube.2"), "flask");
  EXPECT_EQ(ClosestSpaceSymbol("gamecontroller.fill"), "gamepad");
  EXPECT_EQ(ClosestSpaceSymbol("Briefcase.Fill"), "briefcase");
  // A name Rio has no glyph for still gets one.
  EXPECT_EQ(ClosestSpaceSymbol("person.crop.circle"), "star");
  EXPECT_EQ(ClosestSpaceSymbol(""), std::nullopt);
  EXPECT_EQ(ClosestSpaceSymbol("\xF0\x9F\xAA\x90"), std::nullopt);
  EXPECT_EQ(ClosestSpaceSymbol(std::string(65, 'a')), std::nullopt);
}

TEST(CluSpaceRulesTest, DeleteSpaceTextCountsWithPlurals) {
  const DeleteSpaceText text = MakeDeleteSpaceText("Work", 2, 3);
  EXPECT_EQ(text.title, "Delete your “Work” Space?");
  EXPECT_EQ(text.body,
            "Its 2 pinned tabs close and its 3 Today tabs move to Archive.");
  EXPECT_EQ(MakeDeleteSpaceText("Work", 1, 0).body, "Its 1 pinned tab closes.");
  EXPECT_EQ(MakeDeleteSpaceText("Work", 0, 1).body,
            "Its 1 Today tab moves to Archive.");
  EXPECT_EQ(MakeDeleteSpaceText("Work", 0, 0).body, "This space has no tabs.");
  EXPECT_EQ(MakeDeleteSpaceText("Work", -1, -4).body,
            "This space has no tabs.");
}

TEST(CluSpaceRulesTest, PickSpaceTargetOrder) {
  auto all = [](std::string_view) { return true; };
  // T3 unit: last active, then the first Today tab, then the first pinned tab, then none.
  EXPECT_EQ(PickSpaceTarget("t2", {"t1", "t2"}, {"p1"}, all), "t2");
  EXPECT_EQ(PickSpaceTarget("", {"t1", "t2"}, {"p1"}, all), "t1");
  EXPECT_EQ(PickSpaceTarget("", {}, {"p1", "p2"}, all), "p1");
  EXPECT_EQ(PickSpaceTarget("", {}, {}, all), std::nullopt);
  // A row another window shows is skipped.
  EXPECT_EQ(PickSpaceTarget("t2", {"t1", "t2"}, {"p1"},
                            [](std::string_view id) { return id != "t2"; }),
            "t1");
}

TEST(CluSpaceRulesTest, ResolveSwitchHasNoWrapAndIgnoresPastTheEnd) {
  const std::vector<std::string> order = {"a", "b", "c"};
  EXPECT_EQ(ResolveSwitch(order, "a", {.index = 2}), "c");
  // Ctrl+9 with 3 spaces does nothing.
  EXPECT_EQ(ResolveSwitch(order, "a", {.index = 8}), std::nullopt);
  // Cmd+Option+Left from space 1 does nothing; Right goes to space 2.
  EXPECT_EQ(ResolveSwitch(order, "a", {.step = -1}), std::nullopt);
  EXPECT_EQ(ResolveSwitch(order, "a", {.step = 1}), "b");
  EXPECT_EQ(ResolveSwitch(order, "c", {.step = 1}), std::nullopt);
  EXPECT_EQ(ResolveSwitch(order, "b", {.space_id = std::string("b")}),
            std::nullopt);
  EXPECT_EQ(ResolveSwitch(order, "b", {.space_id = std::string("zzz")}),
            std::nullopt);
  // A window on a space that is gone steps to the first.
  EXPECT_EQ(ResolveSwitch(order, "gone", {.step = 1}), "a");
}

TEST(CluSpaceRulesTest, ReorderClampsAndKeepsEveryId) {
  const std::vector<std::string> order = {"a", "b", "c"};
  EXPECT_EQ(ReorderSpaces(order, "c", -5),
            (std::vector<std::string>{"c", "a", "b"}));
  EXPECT_EQ(ReorderSpaces(order, "a", 999),
            (std::vector<std::string>{"b", "c", "a"}));
  EXPECT_EQ(ReorderSpaces(order, "b", 1), std::nullopt);
  EXPECT_EQ(ReorderSpaces(order, "x", 0), std::nullopt);
}

TEST(CluSpaceRulesTest, FallbackIsThePreviousSpaceElseTheNext) {
  const std::vector<std::string> order = {"a", "b", "c"};
  EXPECT_EQ(FallbackSpace(order, "b"), "a");
  EXPECT_EQ(FallbackSpace(order, "a"), "b");
  EXPECT_EQ(FallbackSpace({"a"}, "a"), std::nullopt);
}

TEST(CluSpaceRulesTest, SwipeFractionAndTarget) {
  const std::vector<std::string> order = {"a", "b", "c"};
  SwipeState s = ComputeSwipe(120, 240, order, "b");
  EXPECT_DOUBLE_EQ(s.fraction, 0.5);
  EXPECT_EQ(s.target_space_id, "c");
  s = ComputeSwipe(-480, 240, order, "b");
  EXPECT_DOUBLE_EQ(s.fraction, -1);
  EXPECT_EQ(s.target_space_id, "a");
  // Past the last space there is no target; a zero-width sidebar never divides by zero.
  EXPECT_EQ(ComputeSwipe(50, 240, order, "c").target_space_id, std::nullopt);
  EXPECT_DOUBLE_EQ(ComputeSwipe(5, 0, order, "a").fraction, 1);
  EXPECT_EQ(ComputeSwipe(0, 240, order, "a").target_space_id, std::nullopt);
}

}  // namespace
}  // namespace clu
