// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_layout.h"

#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

// The cases of src/shared/shell/layout.test.ts and tween.test.ts, so the frame WebUI and the browser agree on every
// rect.
namespace clu {
namespace {

const gfx::Size kSize(1280, 800);

ChromeState Chrome(int sidebar_width = kSidebarWidth,
                   bool sidebar_hidden = false,
                   bool focus_mode = false,
                   bool fullscreen = false) {
  return ChromeState{.sidebar_width = sidebar_width,
                     .sidebar_hidden = sidebar_hidden,
                     .focus_mode = focus_mode,
                     .fullscreen = fullscreen};
}

TEST(CluLayoutTest, CardSitsRightOfTheSidebar) {
  const ShellLayout l = ComputeShellLayout(kSize, Chrome());
  EXPECT_EQ(l.page_rect, gfx::Rect(214, 10, 1056, 780));
  EXPECT_EQ(l.sidebar_rect, gfx::Rect(0, 0, 214, 800));
  EXPECT_FALSE(l.hot_zone);
  EXPECT_EQ(l.radius, 6.5f);
  EXPECT_TRUE(l.traffic_lights);
  EXPECT_EQ(l.traffic_spacer, 66);
}

TEST(CluLayoutTest, TheLibraryWidensTheDockedSidebar) {
  ChromeState chrome = Chrome();
  chrome.library_open = true;
  ShellLayout l = ComputeShellLayout(kSize, chrome);
  EXPECT_EQ(l.sidebar_rect, gfx::Rect(0, 0, 417, 800));
  EXPECT_EQ(l.page_rect, gfx::Rect(417, 10, 853, 780));
  // A narrow window keeps 360 of page. Hidden, the floating sidebar shows the Library at its width and the card keeps
  // the window.
  l = ComputeShellLayout(gfx::Size(700, 800), chrome);
  EXPECT_EQ(l.effective_width, 340);
  chrome.sidebar_hidden = true;
  l = ComputeShellLayout(kSize, chrome);
  EXPECT_EQ(l.sidebar_rect.width(), 417);
  EXPECT_EQ(l.page_rect, gfx::Rect(10, 10, 1260, 780));
  // Focus mode floats the sidebar too: it shows the Library at the same width, and the page keeps its gutter.
  chrome.sidebar_hidden = false;
  chrome.focus_mode = true;
  l = ComputeShellLayout(kSize, chrome);
  EXPECT_EQ(l.sidebar_rect.width(), 417);
  EXPECT_EQ(l.page_rect, gfx::Rect(kFocusGutter, 0, 1280 - kFocusGutter, 800));
}

// The Spaces view asks for room for every card; the Library never goes under 417 nor past the page's 360.
TEST(CluLayoutTest, TheLibraryTakesTheWidthItsSpacesViewAsksFor) {
  ChromeState chrome = Chrome();
  chrome.library_open = true;
  chrome.library_width = 860;
  EXPECT_EQ(ComputeShellLayout(gfx::Size(1600, 800), chrome).effective_width,
            860);
  EXPECT_EQ(ComputeShellLayout(gfx::Size(1000, 800), chrome).effective_width,
            640);
  chrome.library_width = 100;
  EXPECT_EQ(ComputeShellLayout(gfx::Size(1600, 800), chrome).effective_width,
            417);
  chrome.library_open = false;
  chrome.library_width = 860;
  EXPECT_EQ(ComputeShellLayout(gfx::Size(1600, 800), chrome).effective_width,
            214);
}

TEST(CluLayoutTest, HiddenSidebarInsetsTheCardWithAHotZone) {
  const ShellLayout l =
      ComputeShellLayout(kSize, Chrome(kSidebarWidth, /*sidebar_hidden=*/true));
  EXPECT_EQ(l.page_rect, gfx::Rect(10, 10, 1260, 780));
  // The reveal zone is x 0 to 5, 5 included.
  EXPECT_EQ(l.hot_zone, gfx::Rect(0, 0, 6, 800));
  EXPECT_EQ(l.sidebar_rect, gfx::Rect(0, 0, 214, 800));
  EXPECT_EQ(l.radius, 6.5f);
  EXPECT_FALSE(l.traffic_lights);
}

// A page that goes full screen fills the screen; the window's own full screen keeps the sidebar and the card.
TEST(CluLayoutTest, APageInFullScreenFillsTheWindow) {
  ChromeState chrome = Chrome(kSidebarWidth, /*sidebar_hidden=*/false);
  chrome.fullscreen = true;
  const ShellLayout window_only = ComputeShellLayout(kSize, chrome);
  EXPECT_EQ(window_only.page_rect, gfx::Rect(214, 10, 1056, 780));
  EXPECT_EQ(window_only.radius, 6.5f);

  chrome.content_fullscreen = true;
  for (bool hidden : {false, true}) {
    chrome.sidebar_hidden = hidden;
    const ShellLayout l = ComputeShellLayout(kSize, chrome);
    EXPECT_EQ(l.page_rect, gfx::Rect(0, 0, 1280, 800));
    EXPECT_EQ(l.radius, 0.f);
    EXPECT_FALSE(l.hot_zone);
    EXPECT_FALSE(l.traffic_lights);
  }
  // Pending (the window not full screen yet): the shell stays until the window is.
  chrome.fullscreen = false;
  chrome.sidebar_hidden = false;
  EXPECT_EQ(ComputeShellLayout(kSize, chrome).page_rect,
            gfx::Rect(214, 10, 1056, 780));
}

TEST(CluLayoutTest, FocusModeRunsEdgeToEdge) {
  const ShellLayout l = ComputeShellLayout(
      kSize, Chrome(kSidebarWidth, false, /*focus_mode=*/true));
  EXPECT_EQ(l.page_rect, gfx::Rect(4, 0, 1276, 800));
  EXPECT_EQ(l.radius, 0.f);
  EXPECT_EQ(l.hot_zone, gfx::Rect(0, 0, 4, 800));
  EXPECT_FALSE(l.traffic_lights);
}

TEST(CluLayoutTest, ToolbarModeMovesTheCardUnderTheBand) {
  ChromeState chrome = Chrome();
  chrome.toolbar = true;
  EXPECT_EQ(ComputeShellLayout(kSize, chrome).page_rect,
            gfx::Rect(214, 48, 1056, 742));
  chrome.sidebar_hidden = true;
  EXPECT_EQ(ComputeShellLayout(kSize, chrome).page_rect,
            gfx::Rect(10, 48, 1260, 742));
  chrome.focus_mode = true;
  EXPECT_EQ(ComputeShellLayout(kSize, chrome).page_rect,
            gfx::Rect(4, 0, 1276, 800));
  chrome.focus_mode = false;
  EXPECT_EQ(ComputeShellLayout(gfx::Size(400, 30), chrome).page_rect.height(),
            0);
}

TEST(CluLayoutTest, NarrowWindowClampsTheEffectiveWidth) {
  const ChromeState chrome = Chrome(400);
  const ShellLayout narrow = ComputeShellLayout(gfx::Size(700, 600), chrome);
  EXPECT_EQ(narrow.effective_width, 315);
  EXPECT_EQ(narrow.sidebar_rect.width(), 315);
  EXPECT_EQ(narrow.page_rect, gfx::Rect(315, 10, 375, 580));
  EXPECT_EQ(ComputeShellLayout(kSize, chrome).effective_width, 400);
  EXPECT_EQ(ComputeShellLayout(gfx::Size(kMinWindowWidth, 400), chrome)
                .effective_width,
            200);
}

TEST(CluLayoutTest, JunkStoredWidthFallsBackAndClamps) {
  EXPECT_EQ(StoredSidebarWidth(std::numeric_limits<double>::quiet_NaN()), 214);
  EXPECT_EQ(StoredSidebarWidth(std::numeric_limits<double>::infinity()), 214);
  EXPECT_EQ(StoredSidebarWidth(50), 164);
  EXPECT_EQ(StoredSidebarWidth(9000), 500);
  EXPECT_EQ(StoredSidebarWidth(321.6), 322);
  EXPECT_EQ(StoredSidebarWidth(-1e300), 164);
  EXPECT_EQ(EffectiveSidebarWidth(260, -5), 164);
}

TEST(CluLayoutTest, FullScreenKeepsTheTrafficSpacer) {
  const ShellLayout full =
      ComputeShellLayout(kSize, Chrome(kSidebarWidth, false, false, true));
  EXPECT_EQ(full.traffic_spacer, kTrafficSpacer);
  EXPECT_EQ(full.page_rect, gfx::Rect(214, 10, 1056, 780));
}

TEST(CluLayoutTest, JunkSizesNeverGiveNegativeRects) {
  for (const gfx::Size& size :
       {gfx::Size(-5, -5), gfx::Size(3, 3), gfx::Size(100, 0)}) {
    for (const ChromeState& chrome :
         {Chrome(), Chrome(kSidebarWidth, true), Chrome(kSidebarWidth, false, true)}) {
      const ShellLayout l = ComputeShellLayout(size, chrome);
      for (const gfx::Rect& r : {l.page_rect, l.sidebar_rect,
                                 l.hot_zone.value_or(gfx::Rect())}) {
        EXPECT_GE(r.x(), 0);
        EXPECT_GE(r.y(), 0);
        EXPECT_GE(r.width(), 0);
        EXPECT_GE(r.height(), 0);
      }
    }
  }
}

// The tween math with the 180 ms of tween.test.ts.
constexpr int kTestTweenMs = 180;

TEST(CluLayoutTest, SidebarDocksIn85MsAndHidesIn80) {
  EXPECT_EQ(kSidebarShowMs, 85);
  EXPECT_EQ(kSidebarHideMs, 80);
}

// Progress comes from (now - start) / ms at any moment, so a 120 Hz display draws a new card position every
// 8.3 ms where the old 16 ms ticks drew the same one twice.
TEST(CluLayoutTest, TweenProgressIsContinuousAndEndsOnTheTarget) {
  const gfx::Rect from(260, 10, 1010, 780);
  const gfx::Rect to(10, 10, 1260, 780);
  const SidebarAnim anim{.from = 10, .to = 260, .start_at_ms = 1000, .ms = kTestTweenMs};
  EXPECT_EQ(TweenProgress(anim, 999), 0);
  EXPECT_EQ(TweenProgress(anim, 1000), 0);
  // Two 120 Hz frames inside what was one 16 ms tick land on different progress.
  EXPECT_GT(TweenProgress(anim, 1016 + 8.33), TweenProgress(anim, 1016));
  EXPECT_NEAR(TweenProgress(anim, 1090), 1 - std::pow(0.5, 3), 1e-9);
  // Ease-out: progress rises on every 120 Hz frame. Rounded to whole DIPs, the last frames may land on the same x.
  double last = 0;
  int last_x = from.x();
  for (double now = 1000 + 1000.0 / 120; now < 1180; now += 1000.0 / 120) {
    const double p = TweenProgress(anim, now);
    EXPECT_GT(p, last) << now;
    last = p;
    const gfx::Rect frame = LerpRect(from, to, p);
    EXPECT_LE(frame.x(), last_x);
    last_x = frame.x();
  }
  EXPECT_EQ(TweenProgress(anim, 1180), 1);
  EXPECT_EQ(TweenProgress(anim, 99999), 1);
  EXPECT_EQ(LerpRect(from, to, TweenProgress(anim, 1180)), to);
  // Reduced motion (ms 0) and a junk clock land at once.
  EXPECT_EQ(TweenProgress(SidebarAnim{.start_at_ms = 1000, .ms = 0}, 0), 1);
  EXPECT_EQ(TweenProgress(anim, std::nan("")), 1);
}

// The Library's tween is exponential over kLibraryMs, as easeOutExp in tween.ts: half way by about 30 ms,
// 90% by about 100 ms, exactly on the target on its last tick, and still rising on every tick before it.
TEST(CluLayoutTest, LibraryTweenIsExponentialAndEndsOnTheTarget) {
  const SidebarAnim anim{.from = 214, .to = 417, .start_at_ms = 0, .ms = kLibraryMs, .exponential = true};
  EXPECT_EQ(CurveOf(anim), TweenCurve::kExp);
  EXPECT_EQ(CurveOf(SidebarAnim{.from = 417, .to = 214, .exponential = true}), TweenCurve::kExp);
  EXPECT_NEAR(TweenProgress(anim, 32),
              (1 - std::exp(-32.0 / kLibraryTauMs)) /
                  (1 - std::exp(-static_cast<double>(kLibraryMs) /
                                kLibraryTauMs)),
              1e-9);
  EXPECT_GT(TweenProgress(anim, 32), 0.5);
  EXPECT_GT(TweenProgress(anim, 96), 0.89);
  EXPECT_GT(EaseAt(32.0 / kLibraryMs, kLibraryMs, TweenCurve::kExp), EaseAt(32.0 / kLibraryMs, kLibraryMs));
  double last = 0;
  for (double now = 1000.0 / 120; now < kLibraryMs; now += 1000.0 / 120) {
    EXPECT_GT(TweenProgress(anim, now), last) << now;
    last = TweenProgress(anim, now);
  }
  EXPECT_EQ(TweenProgress(anim, kLibraryMs), 1);
}

// The hide moves the card edge 41% by 25 ms, 61% by 50, 88% by 63, all of it by 80; nearly even, where the dock
// eases out.
TEST(CluLayoutTest, HidingFollowsTheSampledCurve) {
  const SidebarAnim hide{.from = 260, .to = 10, .start_at_ms = 0, .ms = kSidebarHideMs};
  EXPECT_EQ(CurveOf(hide), TweenCurve::kHide);
  // The same samples the 16 ms ticks read, now any moment between them too.
  constexpr std::array<double, 5> expected = {0.2624, 0.466, 0.594, 0.88706, 1};
  for (int i = 1; i <= 5; ++i) {
    EXPECT_NEAR(TweenProgress(hide, i * 16), expected[static_cast<size_t>(i - 1)], 0.001) << i;
  }
  double last = 0;
  for (double now = 1000.0 / 120; now < kSidebarHideMs; now += 1000.0 / 120) {
    EXPECT_GT(TweenProgress(hide, now), last) << now;
    last = TweenProgress(hide, now);
  }
  // At 48 ms the hide has done about 59%, the dock's ease-out about 94%.
  EXPECT_LT(TweenProgress(hide, 48),
            EaseAt(48.0 / kSidebarHideMs, kSidebarHideMs) - 0.3);
  EXPECT_EQ(TweenProgress(hide, 0), 0);
}

// docs/chromium/fixes.md: the downloads bubble hangs from the sidebar's downloads button only while the docked
// sidebar shows it; anything else hangs it from the card.
TEST(CluLayoutTest, TheDownloadsBubbleHangsFromTheButtonOnlyWhileItShows) {
  const ShellLayout docked = ComputeShellLayout(kSize, Chrome());
  const gfx::Rect button(176, 762, 28, 28);
  EXPECT_EQ(DownloadsButtonAnchor(docked, button, /*tweening=*/false), button);
  // No report, an empty rect, a rect outside the sidebar (over the card, junk), a running tween.
  EXPECT_EQ(DownloadsButtonAnchor(docked, std::nullopt, false), std::nullopt);
  EXPECT_EQ(DownloadsButtonAnchor(docked, gfx::Rect(176, 762, 0, 0), false),
            std::nullopt);
  EXPECT_EQ(DownloadsButtonAnchor(docked, gfx::Rect(600, 400, 28, 28), false),
            std::nullopt);
  EXPECT_EQ(DownloadsButtonAnchor(docked, gfx::Rect(200, 762, 28, 28), false),
            std::nullopt);
  EXPECT_EQ(DownloadsButtonAnchor(docked, gfx::Rect(-40, 762, 28, 28), false),
            std::nullopt);
  EXPECT_EQ(DownloadsButtonAnchor(docked, button, /*tweening=*/true),
            std::nullopt);
  // A hidden sidebar and focus mode show no button.
  EXPECT_EQ(DownloadsButtonAnchor(
                ComputeShellLayout(kSize, Chrome(kSidebarWidth, true)), button,
                false),
            std::nullopt);
  EXPECT_EQ(
      DownloadsButtonAnchor(
          ComputeShellLayout(kSize, Chrome(kSidebarWidth, false, true)), button,
          false),
      std::nullopt);
}

}  // namespace
}  // namespace clu
