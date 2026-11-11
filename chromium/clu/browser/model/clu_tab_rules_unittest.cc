// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tab_rules.h"

#include <string>

#include "clu/browser/model/clu_tab_node.h"
#include "testing/gtest/include/gtest/gtest.h"

// The cases of src/shared/tabs.test.ts (isOneEmoji, cleanName, isOffPinned), the archive search escape and the
// Markdown link of src/main/tabs/service.test.ts.
namespace clu {
namespace {

TEST(CluTabRulesTest, OneEmojiOnly) {
  EXPECT_TRUE(IsOneEmoji("\xF0\x9F\x98\x80"));          // 😀
  EXPECT_TRUE(IsOneEmoji("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"));  // 👍🏽, one grapheme
  EXPECT_TRUE(IsOneEmoji("1\xEF\xB8\x8F\xE2\x83\xA3"));  // 1️⃣
  EXPECT_TRUE(IsOneEmoji("\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7"));  // 🇫🇷
  EXPECT_FALSE(IsOneEmoji("ab"));
  EXPECT_FALSE(IsOneEmoji("a"));
  EXPECT_FALSE(IsOneEmoji("\xF0\x9F\x98\x80\xF0\x9F\x98\x80"));  // two
  EXPECT_FALSE(IsOneEmoji(""));
  EXPECT_FALSE(IsOneEmoji(std::string(300, 'x')));
  EXPECT_FALSE(IsOneEmoji("\xFF\xFE"));  // not UTF-8
}

TEST(CluTabRulesTest, CleanNameCollapsesControlsAndCaps) {
  EXPECT_EQ(CleanName("  Work\t\nstuff \x01 here  "), "Work stuff here");
  EXPECT_EQ(CleanName(""), "");
  EXPECT_EQ(CleanName(std::string(500, 'a')).size(), kMaxFolderName);
}

TEST(CluTabRulesTest, OffPinnedIgnoresFragmentAndTrailingSlash) {
  EXPECT_FALSE(IsOffPinned("https://a.test/x/#top",
                           std::string("https://a.test/x")));
  EXPECT_TRUE(IsOffPinned("https://a.test/y", std::string("https://a.test/x")));
  EXPECT_FALSE(IsOffPinned("https://a.test/y", std::nullopt));
}

TEST(CluTabRulesTest, RestorableUrls) {
  EXPECT_TRUE(IsRestorableTabUrl("https://a.test/"));
  EXPECT_TRUE(IsRestorableTabUrl("about:blank"));
  EXPECT_TRUE(IsRestorableTabUrl("chrome://history/"));
  EXPECT_FALSE(IsRestorableTabUrl("javascript:alert(1)"));
  EXPECT_FALSE(IsRestorableTabUrl("chrome://clu-frame/"));
  EXPECT_FALSE(IsRestorableTabUrl(""));
}

TEST(CluTabRulesTest, CleanTitleDropsControlsAndCaps) {
  EXPECT_EQ(CleanTitle("a\x01" "b"), "a b");
  EXPECT_LE(CleanTitle(std::string(1024 * 1024, 'x')).size(), kMaxNodeTitle);
}

TEST(CluTabRulesTest, LikePatternMatchesPercentAndUnderscoreLiterally) {
  EXPECT_EQ(LikePattern("50%_off\\"), "%50\\%\\_off\\\\%");
}

TEST(CluTabRulesTest, MarkdownLinkEscapes) {
  EXPECT_EQ(MarkdownLink("A [b]\n c", "https://a.test/(x)"),
            "[A \\[b\\] c](https://a.test/%28x%29)");
  EXPECT_EQ(MarkdownLink("", "https://a.test/"),
            "[https://a.test/](https://a.test/)");
}

TEST(CluTabRulesTest, StripTrackingDropsOnlyTrackingParams) {
  EXPECT_EQ(StripTracking("https://a.test/p?utm_source=x&id=3&fbclid=y#f"),
            "https://a.test/p?id=3#f");
  EXPECT_EQ(StripTracking("https://a.test/p?gclid=1"), "https://a.test/p");
  EXPECT_EQ(StripTracking("not a url"), "not a url");
}

}  // namespace
}  // namespace clu
