// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ai/clu_ai_client.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

TEST(CluAiClientTest, CleanModelTitleKeepsAShortName) {
  EXPECT_EQ(CleanModelTitle("Rio de Janeiro", 40), "Rio de Janeiro");
  EXPECT_EQ(CleanModelTitle("\"Weather Alerts.\"\n", 40), "Weather Alerts");
  EXPECT_EQ(CleanModelTitle("**Apple Products**", 40), "Apple Products");
}

TEST(CluAiClientTest, CleanModelTitleCutsAtAWordAndRefusesJunk) {
  EXPECT_EQ(CleanModelTitle("Interior Design Book Notes And More", 20),
            "Interior Design Book");
  EXPECT_FALSE(CleanModelTitle("", 40));
  EXPECT_FALSE(CleanModelTitle("  ...  ", 40));
  EXPECT_FALSE(CleanModelTitle("I'm sorry, I can't help with that.", 40));
  EXPECT_FALSE(CleanModelTitle(std::string(500, 'a'), 40));
  EXPECT_FALSE(CleanModelTitle("Supercalifragilistic", 10));
}

}  // namespace
}  // namespace clu
