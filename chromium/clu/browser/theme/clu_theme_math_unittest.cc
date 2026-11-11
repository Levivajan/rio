// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/theme/clu_theme_math.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

// The cases of src/shared/theme/space.test.ts and appearance.test.ts.
namespace clu {
namespace {

base::Value Json(const char* text) {
  std::optional<base::Value> value =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  CHECK(value) << text;
  return std::move(*value);
}

SpaceTheme Theme(std::vector<ThemeColor> colors,
                 double intensity = 0.5,
                 double grain = 0) {
  SpaceTheme theme;
  theme.colors = std::move(colors);
  theme.intensity = intensity;
  theme.grain = grain;
  return theme;
}

TEST(CluThemeMathTest, HslRoundTripsThroughRgb) {
  EXPECT_EQ(HslToRgb(0, 1, 0.5), (Rgb{255, 0, 0}));
  EXPECT_EQ(HslToRgb(120, 1, 0.5), (Rgb{0, 255, 0}));
  EXPECT_EQ(HslToRgb(240, 1, 0.5), (Rgb{0, 0, 255}));
  EXPECT_EQ(HslToRgb(0, 0, 1), (Rgb{255, 255, 255}));
  // Hues wrap and saturation clamps.
  EXPECT_EQ(HslToRgb(360, 1, 0.5), HslToRgb(0, 1, 0.5));
  EXPECT_EQ(HslToRgb(-120, 2, 0.5), HslToRgb(240, 1, 0.5));
  const std::array<double, 3> hsl = RgbToHsl({255, 0, 0});
  EXPECT_DOUBLE_EQ(hsl[0], 0);
  EXPECT_DOUBLE_EQ(hsl[1], 1);
  EXPECT_DOUBLE_EQ(hsl[2], 0.5);
  EXPECT_DOUBLE_EQ(RgbToHsl({128, 128, 128})[1], 0);
}

TEST(CluThemeMathTest, HexParsesAndPrints) {
  EXPECT_EQ(HexToRgb("#3b82f6"), (Rgb{0x3b, 0x82, 0xf6}));
  EXPECT_EQ(HexToRgb("#3B82F6"), (Rgb{0x3b, 0x82, 0xf6}));
  EXPECT_EQ(HexToRgb("3b82f6"), std::nullopt);
  EXPECT_EQ(HexToRgb("#3b82f"), std::nullopt);
  EXPECT_EQ(HexToRgb("#zzzzzz"), std::nullopt);
  EXPECT_EQ(RgbToHex({59, 130, 246}), "#3b82f6");
  EXPECT_EQ(RgbToHex({300, -4, 0.4}), "#ff0000");
}

TEST(CluThemeMathTest, ExpandsEveryThemeToThreeStops) {
  const std::array<Rgb, 3> one = ThemeStops(Theme({{10, 0.5}}), ResolvedMode::kLight);
  EXPECT_EQ(one[0], HslToRgb(10, 0.5, 0.9));
  EXPECT_EQ(one[1], one[0]);
  EXPECT_EQ(one[2], one[0]);
  const std::array<Rgb, 3> two =
      ThemeStops(Theme({{0, 1}, {240, 1}}), ResolvedMode::kLight);
  EXPECT_EQ(two[1], MixRgb(two[0], two[2], 0.5));
  const std::array<Rgb, 3> dark = ThemeStops(Theme({{10, 0.5}}), ResolvedMode::kDark);
  EXPECT_EQ(dark[0], HslToRgb(10, 0.15, 0.17));
}

// The theme editor's intensity slider (docs/chromium/parity-theme.md): 0 is a
// lighter pastel that keeps its color, 0.5 the mode's tint, 1
// doubles the saturation and deepens the tint. The values below 0.5 are
// legacyColorRgb's in src/shared/theme/space.ts.
TEST(CluThemeMathTest, IntensityScalesTheColorsStrength) {
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 0), ResolvedMode::kLight)[0],
            (Rgb{255, 239, 236}));
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 0.25), ResolvedMode::kLight)[0],
            (Rgb{250, 230, 226}));
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 0), ResolvedMode::kDark)[0],
            (Rgb{64, 59, 58}));
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 0.25), ResolvedMode::kDark)[0],
            (Rgb{57, 49, 48}));
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 0.5), ResolvedMode::kLight)[0],
            HslToRgb(10, 0.5, 0.9));
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 1), ResolvedMode::kLight)[0],
            HslToRgb(10, 1, 0.78));
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 0.75), ResolvedMode::kLight)[0],
            HslToRgb(10, 0.75, 0.84));
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}}, 1), ResolvedMode::kDark)[0],
            HslToRgb(10, 0.3, 0.22));
  // Junk intensity reads as the default.
  EXPECT_EQ(ThemeStops(Theme({{10, 0.5}},
                             std::numeric_limits<double>::quiet_NaN()),
                       ResolvedMode::kLight)[0],
            HslToRgb(10, 0.5, 0.9));
  // Ink keeps its contrast on the strongest tint in both modes.
  for (const ResolvedMode mode : {ResolvedMode::kLight, ResolvedMode::kDark}) {
    for (double h = 0; h < 360; h += 30) {
      const SpaceTheme strong = Theme({{h, 1}}, 1);
      const ThemeInk ink = ThemeInkFor(strong, mode);
      for (const Rgb& stop : ThemeStops(strong, mode)) {
        EXPECT_GE(Contrast(*HexToRgb(ink.ink), EffectiveRgb(stop, mode, 1)), 4.5)
            << h;
      }
    }
  }
}

TEST(CluThemeMathTest, TranslucencyFollowsModeIntensityAndFocus) {
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kLight, 0.5, false, true), 0.575);
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kDark, 0.5, false, true), 0.65);
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kLight, 0, false, true), 0.3);
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kDark, 1, false, true), 0.9);
  // Reduce transparency and an inactive window flatten the layer.
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kLight, 0.5, true, true), 1);
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kDark, 0.5, false, false), 1);
  // Junk intensity reads as the default.
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kLight, std::numeric_limits<double>::quiet_NaN(), false, true), 0.575);
  EXPECT_DOUBLE_EQ(TranslucencyAlpha(ResolvedMode::kLight, 7, false, true), 0.85);
}

TEST(CluThemeMathTest, InkKeepsContrastOnEveryPresetInBothModes) {
  const std::vector<ThemePreset>& presets = ThemePresets();
  std::vector<std::string> names;
  for (const ThemePreset& p : presets) {
    names.push_back(p.name);
  }
  EXPECT_EQ(names, (std::vector<std::string>{"Moss", "Ember", "Tide", "Dusk", "Sand",
                                             "Plum", "Slate", "Coral", "Fern", "Ink",
                                             "Honey", "Frost"}));
  for (const ThemePreset& p : presets) {
    EXPECT_EQ(ParseSpaceTheme(base::Value(SpaceThemeValue(p.theme))), p.theme)
        << p.name;
    for (const ResolvedMode mode : {ResolvedMode::kLight, ResolvedMode::kDark}) {
      const ThemeInk ink = ThemeInkFor(p.theme, mode);
      EXPECT_EQ(ink.ink, mode == ResolvedMode::kLight ? "#1d1d1f" : "#f2f2f7")
          << p.name;
      const double alpha = TranslucencyAlpha(mode, p.theme.intensity, false, true);
      for (const double a : {alpha, 1.0}) {
        for (const Rgb& stop : ThemeStops(p.theme, mode)) {
          const Rgb bg = EffectiveRgb(stop, mode, a);
          EXPECT_GE(Contrast(*HexToRgb(ink.ink), bg), 4.5) << p.name;
          EXPECT_GE(Contrast(*HexToRgb(ink.ink_muted), bg), 4.5) << p.name;
        }
      }
    }
  }
}

TEST(CluThemeMathTest, InkForPicksTheTextColorWithMoreContrast) {
  EXPECT_EQ(InkFor({{250, 250, 250}}), "#1d1d1f");
  EXPECT_EQ(InkFor({{10, 10, 30}}), "#f2f2f7");
  EXPECT_EQ(InkFor({{250, 250, 250}, {240, 240, 240}}), "#1d1d1f");
}

TEST(CluThemeMathTest, ParsesOneToThreeColorsInRange) {
  EXPECT_EQ(ParseSpaceTheme(Json(R"({"colors":[{"h":0,"s":0},{"h":359.9,"s":1}],"intensity":1,"grain":0})")),
            Theme({{0, 0}, {359.9, 1}}, 1, 0));
  EXPECT_TRUE(ParseSpaceTheme(Json(R"({"colors":[{"h":1,"s":0.1},{"h":2,"s":0.2},{"h":3,"s":0.3}],"intensity":0,"grain":1})")));
}

TEST(CluThemeMathTest, RejectsEveryJunkTheme) {
  const char* junk[] = {
      R"({"colors":[],"intensity":0.5,"grain":0})",
      R"({"colors":[{"h":1,"s":0.1},{"h":2,"s":0.2},{"h":3,"s":0.3},{"h":4,"s":0.4}],"intensity":0.5,"grain":0})",
      R"({"colors":[{"h":360,"s":0.5}],"intensity":0.5,"grain":0})",
      R"({"colors":[{"h":-1,"s":0.5}],"intensity":0.5,"grain":0})",
      R"({"colors":[{"h":10,"s":1.01}],"intensity":0.5,"grain":0})",
      R"({"colors":[{"h":10,"s":-0.1}],"intensity":0.5,"grain":0})",
      R"({"colors":[{"h":10,"s":0.5}],"intensity":1.5,"grain":0})",
      R"({"colors":[{"h":10,"s":0.5}],"intensity":-0.1,"grain":0})",
      R"({"colors":[{"h":10,"s":0.5}],"intensity":0.5,"grain":2})",
      R"({"colors":[{"h":10,"s":0.5}],"intensity":0.5,"grain":-1})",
      R"({"colors":[{"h":10,"s":0.5}],"intensity":0.5,"grain":0,"extra":1})",
      R"({"colors":[{"h":1,"s":0.5,"l":0.5}],"intensity":0.5,"grain":0})",
      R"({"colors":[{"h":"10","s":0.5}],"intensity":0.5,"grain":0})",
      R"({"colors":"red","intensity":0.5,"grain":0})",
      R"({"colors":[{"h":10,"s":0.5}],"intensity":0.5})",
      R"(null)",
      R"([1,2])",
  };
  for (const char* text : junk) {
    EXPECT_EQ(ParseSpaceTheme(Json(text)), std::nullopt) << text;
  }
  // NaN and Infinity cannot reach a base::Value (it stores 0 instead); the Mojo payload guard refuses them.
}

TEST(CluThemeMathTest, DefaultsComeFromTheNamedColor) {
  for (const char* color : {"slate", "red", "orange", "amber", "lime", "green", "teal",
                            "cyan", "blue", "indigo", "violet", "pink"}) {
    const SpaceTheme t = DefaultThemeFor(color);
    EXPECT_EQ(ParseSpaceTheme(base::Value(SpaceThemeValue(t))), t) << color;
    EXPECT_DOUBLE_EQ(t.intensity, 0.5);
    EXPECT_DOUBLE_EQ(t.grain, 0.25);
    ASSERT_EQ(t.colors.size(), 2u);
    EXPECT_DOUBLE_EQ(t.colors[1].h, 0);
    EXPECT_DOUBLE_EQ(t.colors[1].s, 0.3);
  }
  const std::array<double, 3> blue = RgbToHsl(*HexToRgb("#3b82f6"));
  EXPECT_NEAR(DefaultThemeFor("blue").colors[0].h, blue[0], 0.5);
  EXPECT_NEAR(DefaultThemeFor("blue").colors[0].s, 0.5, 0.005);
}

TEST(CluThemeMathTest, TheAccentIsTheFirstThemeColorElseTheNamedColor) {
  const SpaceTheme t = Theme({{150, 0.45}, {20, 0.9}});
  EXPECT_EQ(SpaceAccentHex(t, "red"), RgbToHex(HslToRgb(150, 0.45, 0.68)));
  // A full-saturation mint pick is the sidebar's mint, never neon #00ff52.
  EXPECT_EQ(SpaceAccentHex(Theme({{139.3, 1}, {20, 0.9}}), "red"), RgbToHex(HslToRgb(139.3, 0.58, 0.68)));
  EXPECT_NE(SpaceAccentHex(Theme({{139.3, 1}, {20, 0.9}}), "red"), "#00ff52");
  EXPECT_EQ(SpaceAccentHex(std::nullopt, "red"), "#ef4444");
  EXPECT_EQ(SpaceAccentHex(std::nullopt, "blue"), "#3b82f6");
  EXPECT_EQ(SpaceAccentColor(std::nullopt, "blue"), SkColorSetRGB(0x3b, 0x82, 0xf6));
  const Rgb accent = HslToRgb(150, 0.45, 0.68);
  EXPECT_EQ(SpaceAccentColor(t, "red"),
            SkColorSetRGB(static_cast<U8CPU>(accent[0]), static_cast<U8CPU>(accent[1]),
                          static_cast<U8CPU>(accent[2])));
}

}  // namespace
}  // namespace clu
