// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_mini_rules.h"

#include <string>

#include "base/test/simple_test_tick_clock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The Mini window's routing and window rules, plus the cap (decision 27) and
// the idle close on a test clock (decision 54).
namespace clu {
namespace {

TEST(CluMiniRulesTest, TheRouterAcceptsWebUrlsAndAboutBlankOnly) {
  EXPECT_TRUE(AcceptMiniUrl(GURL("https://example.com/a")));
  EXPECT_TRUE(AcceptMiniUrl(GURL("http://127.0.0.1:8000/")));
  EXPECT_TRUE(AcceptMiniUrl(GURL("about:blank")));
  for (const char* junk :
       {"file:///etc/hosts", "javascript:alert(1)", "data:text/html,hi",
        "chrome://settings", "clu-file:///tmp/a", "not a url", ""}) {
    EXPECT_FALSE(AcceptMiniUrl(GURL(junk))) << junk;
  }
  // 40 KB is over the 32 KiB cap.
  EXPECT_FALSE(AcceptMiniUrl(
      GURL("https://example.com/?q=" + std::string(40 * 1024, 'a'))));
}

TEST(CluMiniRulesTest, TheSeventhLinkOpensAsATab) {
  for (size_t open = 0; open < kMaxMiniWindows; ++open) {
    EXPECT_EQ(RouteMiniLink(open), MiniRoute::kNewMiniWindow);
  }
  EXPECT_EQ(RouteMiniLink(6), MiniRoute::kTodayTab);
  EXPECT_EQ(MiniCapToast(6), "Opened in a tab: 6 Mini windows are open");
  EXPECT_EQ(MiniConfirmText("Work"),
            "Work uses a different profile. Logins and form data won't carry "
            "over.");
}

TEST(CluMiniRulesTest, WindowsOpenCenteredAndCascade) {
  const gfx::Rect work(0, 25, 1440, 875);
  const gfx::Rect first = MiniWindowBounds(work, 0);
  EXPECT_EQ(first.size(), gfx::Size(880, 620));
  EXPECT_EQ(first.x(), (1440 - 880) / 2);
  EXPECT_EQ(first.y(), 25 + (875 - 620) / 2);
  const gfx::Rect second = MiniWindowBounds(work, 1);
  EXPECT_EQ(second.x(), first.x() + kMiniCascade);
  EXPECT_EQ(second.y(), first.y() + kMiniCascade);
  // A small screen keeps the window inside it, at the minimum size at most.
  const gfx::Rect small = MiniWindowBounds(gfx::Rect(0, 0, 500, 400), 5);
  EXPECT_TRUE(gfx::Rect(0, 0, 500, 400).Contains(small));
}

// The empty prompt is a 490 by 329 window 20 in from the
// screen's top-right (x 1218, y 53 on a 1728 wide screen whose work area starts
// at 33), and grows to 880 by 620 from that corner once a page loads.
TEST(CluMiniRulesTest, ThePromptOpensAtTheTopRightAndGrowsFromIt) {
  const gfx::Rect work(0, 33, 1728, 1084);
  const gfx::Rect prompt = MiniPromptBounds(work, 0);
  EXPECT_EQ(prompt, gfx::Rect(1218, 53, 490, 329));
  EXPECT_EQ(MiniPromptBounds(work, 1), gfx::Rect(1196, 75, 490, 329));
  EXPECT_GE(kMiniPromptSize.height(), kMiniMinSize.height());
  EXPECT_EQ(MiniGrownBounds(prompt, work), gfx::Rect(828, 53, 880, 620));
  // Small screens keep both inside.
  const gfx::Rect small(0, 0, 500, 400);
  EXPECT_TRUE(small.Contains(MiniPromptBounds(small, 5)));
  EXPECT_TRUE(small.Contains(MiniGrownBounds(MiniPromptBounds(small, 0), small)));
}

TEST(CluMiniRulesTest, ThePageSitsUnderTheToolbarAndTheConfirmStrip) {
  EXPECT_EQ(MiniPageRect(gfx::Size(880, 620), false),
            gfx::Rect(8, 36, 864, 576));
  EXPECT_EQ(MiniPageRect(gfx::Size(880, 620), true),
            gfx::Rect(8, 76, 864, 536));
  EXPECT_EQ(MiniPageRect(gfx::Size(10, 10), false).height(), 0);
}

TEST(CluMiniRulesTest, SixIdleHoursCloseAWindow) {
  base::SimpleTestTickClock clock;
  clock.Advance(base::Seconds(1));
  const base::TimeTicks used = clock.NowTicks();
  clock.Advance(base::Hours(6) - base::Seconds(1));
  EXPECT_FALSE(MiniIdleExpired(used, clock.NowTicks()));
  clock.Advance(base::Seconds(1));
  EXPECT_TRUE(MiniIdleExpired(used, clock.NowTicks()));
  EXPECT_FALSE(MiniIdleExpired(base::TimeTicks(), clock.NowTicks()));
}

}  // namespace
}  // namespace clu
