// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_keep_live_rules.h"

#include "testing/gtest/include/gtest/gtest.h"

// Clu's exemptions from Memory Saver (docs/PLAN.md 3.6, docs/chromium/m9.md). The rest of the 3.6 list (visible,
// audible, capturing, DevTools, fullscreen, recently active) is Chromium's own DiscardEligibilityPolicy.
namespace clu {
namespace {

TEST(CluKeepLiveRulesTest, MiniAndPopupWindowsAndSplitsStayLive) {
  EXPECT_EQ(KeepLiveReason({.normal_window = false}), "window");
  EXPECT_EQ(KeepLiveReason({.normal_window = true, .split = true}), "split");
  // The window rule comes first: a split in a non-normal window reads as the window.
  EXPECT_EQ(KeepLiveReason({.normal_window = false, .split = true}), "window");
}

TEST(CluKeepLiveRulesTest, PinnedAndFavoriteRowsAreNotExempt) {
  // PLAN 3.6: pinned tabs and favorites restore at their saved state, so a plain tab of a normal window is
  // Chromium's to decide.
  EXPECT_EQ(KeepLiveReason({}), std::nullopt);
  EXPECT_EQ(KeepLiveReason({.normal_window = true, .split = false}),
            std::nullopt);
}

}  // namespace
}  // namespace clu
