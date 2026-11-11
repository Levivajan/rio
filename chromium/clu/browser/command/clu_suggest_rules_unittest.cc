// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/command/clu_suggest_rules.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

TEST(CluSuggestRulesTest, OnlyPlainQueriesLeaveTheMachine) {
  EXPECT_TRUE(ShouldSuggest(u"fixture query"));
  EXPECT_TRUE(ShouldSuggest(u"localhost:8080"));
  EXPECT_TRUE(ShouldSuggest(u"c++: x"));
  EXPECT_TRUE(ShouldSuggest(u"mail me @ noon"));
  EXPECT_TRUE(ShouldSuggest(u"r\u00e9sum\u00e9 tips"));
  EXPECT_FALSE(ShouldSuggest(u""));
  EXPECT_FALSE(ShouldSuggest(u"   "));
  EXPECT_FALSE(ShouldSuggest(std::u16string(257, u'a')));
  EXPECT_TRUE(ShouldSuggest(std::u16string(256, u'a')));
  EXPECT_FALSE(ShouldSuggest(u"https://example.com"));
  EXPECT_FALSE(ShouldSuggest(u"file:///etc/passwd"));
  EXPECT_FALSE(ShouldSuggest(u"javascript:alert(1)"));
  EXPECT_FALSE(ShouldSuggest(u"user:pw@host.test"));
  EXPECT_FALSE(ShouldSuggest(u"go to user:pw@host.test now"));
  EXPECT_FALSE(ShouldSuggest(u"someone@example.com"));
}

TEST(CluSuggestRulesTest, SuggestionsAreCleanedDedupedAndCapped) {
  EXPECT_EQ(CleanSuggestions({u" one ", u"one", u"", u"tw\u0001o",
                              std::u16string(300, u'x'), u"three", u"four",
                              u"five", u"six", u"seven", u"eight"}),
            (std::vector<std::u16string>{u"one", u"tw o", u"three", u"four",
                                         u"five", u"six"}));
  EXPECT_TRUE(CleanSuggestions({}).empty());
  EXPECT_TRUE(CleanSuggestions({u"\n\t", u"   "}).empty());
}

}  // namespace
}  // namespace clu
