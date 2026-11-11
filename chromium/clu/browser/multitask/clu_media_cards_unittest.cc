// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_media_cards.h"

#include <string>
#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"

// The media card rules for one window.
namespace clu {
namespace {

constexpr char kA[] = "a";
constexpr char kB[] = "b";

bool NotMuted(std::string_view) {
  return false;
}

TEST(CluMediaCardsTest, AnAudibleTabOutOfSightGetsTheCard) {
  CluMediaCards cards;
  cards.SetShown({kA});
  cards.Audible(kA, true);
  cards.Started(kA);
  // On screen: no card.
  EXPECT_FALSE(cards.Card(NotMuted));
  cards.SetShown({kB});
  EXPECT_EQ(cards.Card(NotMuted), kA);
  EXPECT_TRUE(cards.IsPlaying(kA));
}

TEST(CluMediaCardsTest, SilentPlaybackGetsNoCardUnlessTheTabIsMuted) {
  CluMediaCards cards;
  cards.SetShown({kB});
  cards.Started(kA);
  EXPECT_FALSE(cards.Card(NotMuted));
  EXPECT_EQ(cards.Card([](std::string_view id) { return id == "a"; }), kA);
  // Sound later still counts while it plays.
  cards.Audible(kA, true);
  EXPECT_EQ(cards.Card(NotMuted), kA);
}

TEST(CluMediaCardsTest, TheMostRecentlyStartedTabWins) {
  CluMediaCards cards;
  cards.SetShown({});
  cards.Audible(kA, true);
  cards.Started(kA);
  cards.Audible(kB, true);
  cards.Started(kB);
  EXPECT_EQ(cards.Card(NotMuted), kB);
  cards.Reset(kB);
  EXPECT_EQ(cards.Card(NotMuted), kA);
}

TEST(CluMediaCardsTest, PausingFromTheCardHoldsItUntilTheTabIsSeen) {
  CluMediaCards cards;
  cards.SetShown({kB});
  cards.Audible(kA, true);
  cards.Started(kA);
  EXPECT_EQ(cards.Toggle(kA), CluMediaCards::ToggleAction::kPause);
  cards.Paused(kA);
  cards.Audible(kA, false);
  EXPECT_EQ(cards.Card(NotMuted), kA);
  EXPECT_FALSE(cards.IsPlaying(kA));
  EXPECT_EQ(cards.Toggle(kA), CluMediaCards::ToggleAction::kPlay);
  cards.Started(kA);
  EXPECT_EQ(cards.Card(NotMuted), kA);
  EXPECT_TRUE(cards.IsPlaying(kA));
  // Paused by the page itself: the card goes.
  cards.Paused(kA);
  EXPECT_FALSE(cards.Card(NotMuted));
  // Held and then seen: the card lets go.
  cards.Started(kA);
  cards.Audible(kA, true);
  ASSERT_EQ(cards.Toggle(kA), CluMediaCards::ToggleAction::kPause);
  cards.Paused(kA);
  cards.SetShown({kA});
  cards.SetShown({kB});
  EXPECT_FALSE(cards.Card(NotMuted));
}

TEST(CluMediaCardsTest, NothingToToggleForAnUnknownOrStoppedTab) {
  CluMediaCards cards;
  EXPECT_EQ(cards.Toggle("unknown"), CluMediaCards::ToggleAction::kNone);
  cards.Started(kA);
  cards.Paused(kA);
  EXPECT_EQ(cards.Toggle(kA), CluMediaCards::ToggleAction::kNone);
}

}  // namespace
}  // namespace clu
