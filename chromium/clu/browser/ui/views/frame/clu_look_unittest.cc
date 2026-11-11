// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_look.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_look_tokens.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"

// The native half of the look (M11) at the service layer: the generated tokens and the color mixer. The hooks need a
// browser window and are covered by CluLookBrowserTest.CluLookHooksLeaveOtherWindowsAlone.
namespace clu {
namespace {

using ColorMode = ui::ColorProviderKey::ColorMode;

TEST(CluLookTest, GeneratedTokensMatchTheDesign) {
  EXPECT_EQ(look::kCardInset, 10);
  EXPECT_FLOAT_EQ(look::kCardRadius, 6.5f);
  EXPECT_FLOAT_EQ(look::kTrafficLightFirstCenterX, 19.75f);
  EXPECT_FLOAT_EQ(look::kTrafficLightCenterY, 23.75f);
  EXPECT_FLOAT_EQ(look::kTrafficLightPitch, 23.f);
  EXPECT_FLOAT_EQ(look::kWindowCornerRadius, 16.5f);
  EXPECT_EQ(look::kSidebarShowMs, 85);
  EXPECT_EQ(look::kSidebarHideMs, 80);
  // The layout reads the same numbers.
  EXPECT_EQ(kCardInset, look::kCardInset);
  EXPECT_FLOAT_EQ(kCardRadius, look::kCardRadius);
  // ARGB: the popover background is opaque, the separator is black at 15%.
  EXPECT_EQ(look::kLight.popover_bg, SkColorSetRGB(0xfa, 0xfb, 0xff));
  EXPECT_EQ(SkColorGetA(look::kLight.separator), 0x26u);
  EXPECT_EQ(look::kDark.page_bg, SkColorSetRGB(0x21, 0x22, 0x22));
}

TEST(CluLookTest, EveryTargetHasAStateAProbeAndATolerance) {
  int native = 0;
  for (const look::Target& t : look::kTargets) {
    EXPECT_FALSE(t.key.empty());
    EXPECT_FALSE(t.state.empty()) << t.key;
    EXPECT_FALSE(t.probe_id.empty()) << t.key;
    EXPECT_GE(t.tolerance, 0) << t.probe_id;
    native += !t.native.empty();
  }
  EXPECT_GE(native, 12);
  EXPECT_FALSE(look::kProbesJson.empty());
}

TEST(CluLookTest, MixerPaintsSurfacesInTheTokensForBothModes) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    ui::ColorProviderKey key;
    key.color_mode = mode;
    ui::ColorProvider provider;
    CluColorSupplier::Get()->AddColorMixers(&provider, key);
    provider.GenerateColorMapForTesting();
    const CluNativeColors c = NativeColorsFor(mode);
    EXPECT_EQ(provider.GetColor(ui::kColorBubbleBackground), c.surface);
    EXPECT_EQ(provider.GetColor(ui::kColorDialogBackground), c.surface);
    EXPECT_EQ(provider.GetColor(ui::kColorMenuBackground), c.surface);
    EXPECT_EQ(provider.GetColor(kColorFindBarBackground), c.surface);
    EXPECT_EQ(provider.GetColor(kColorWebContentsBackground), c.page);
    EXPECT_EQ(provider.GetColor(kColorSidePanelBackground), c.page);
    EXPECT_EQ(provider.GetColor(ui::kColorFrameActive), c.frame);
    EXPECT_EQ(provider.GetColor(ui::kColorFocusableBorderFocused),
              c.focus_ring);
  }
  // The page is never white in dark mode.
  EXPECT_NE(NativeColorsFor(ColorMode::kDark).page, SK_ColorWHITE);
  EXPECT_NE(NativeColorsFor(ColorMode::kLight).frame,
            NativeColorsFor(ColorMode::kDark).frame);
}

// Split view: the focused pane's outline is the space accent, or the default strong accent without one; the
// other panes have no outline.
TEST(CluLookTest, MixerOutlinesOnlyTheFocusedSplitPaneInTheAccent) {
  for (std::optional<SkColor> accent :
       {std::optional<SkColor>(), std::optional<SkColor>(SkColorSetRGB(0x21, 0x2a, 0xfb))}) {
    ui::ColorProviderKey key;
    key.color_mode = ColorMode::kLight;
    key.user_color = accent;
    ui::ColorProvider provider;
    CluColorSupplier::Get()->AddColorMixers(&provider, key);
    provider.GenerateColorMapForTesting();
    EXPECT_EQ(provider.GetColor(kColorMultiContentsViewActiveContentOutline),
              accent.value_or(SkColorSetRGB(0x20, 0x28, 0xf2)));
    EXPECT_EQ(provider.GetColor(kColorMultiContentsViewInactiveContentOutline),
              SK_ColorTRANSPARENT);
  }
}

// Chromium's own dialogs and WebUI pages: gray secondary buttons, a blue default button, and a
// neutral #1f2123 page in dark, whatever the theme's seed color.
TEST(CluLookTest, MixerKeepsChromiumDialogsAndPagesNeutral) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    ui::ColorProviderKey key;
    key.color_mode = mode;
    ui::ColorProvider provider;
    CluColorSupplier::Get()->AddColorMixers(&provider, key);
    provider.GenerateColorMapForTesting();
    const SkColor tonal = provider.GetColor(ui::kColorButtonBackgroundTonal);
    EXPECT_EQ(SkColorGetR(tonal), SkColorGetB(tonal) - (mode == ColorMode::kDark ? 4 : 5));
    EXPECT_EQ(provider.GetColor(ui::kColorButtonForegroundProminent),
              SK_ColorWHITE);
  }
  ui::ColorProviderKey key;
  key.color_mode = ColorMode::kDark;
  ui::ColorProvider provider;
  CluColorSupplier::Get()->AddColorMixers(&provider, key);
  provider.GenerateColorMapForTesting();
  EXPECT_EQ(provider.GetColor(ui::kColorButtonBackgroundTonal),
            SkColorSetRGB(0x3a, 0x3b, 0x3e));
  EXPECT_EQ(provider.GetColor(kColorWebuiPageBackground),
            SkColorSetRGB(0x1f, 0x21, 0x23));
  EXPECT_EQ(provider.GetColor(ui::kColorSysSurface),
            SkColorSetRGB(0x1f, 0x21, 0x23));
}

}  // namespace
}  // namespace clu
