// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_THEME_CLU_THEME_MATH_H_
#define CLU_BROWSER_THEME_CLU_THEME_MATH_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "third_party/skia/include/core/SkColor.h"

// Space themes (docs/plan/01-shell.md F9, PLAN decision 20), ported from src/shared/theme/space.ts, schema.ts and
// appearance.ts: the model, its validator, HSL math, the accent, ink contrast and Clu's 12 presets. The frame WebUI
// paints the theme from state; the browser needs the same numbers for the native accent (bubbles, menus, Settings)
// and to check what a payload or a stored row holds. Pure, so clu_unittests runs the cases of space.test.ts.
namespace clu {

inline constexpr size_t kMaxThemeColors = 3;
inline constexpr double kDefaultThemeIntensity = 0.5;

struct ThemeColor {
  // [0, 360).
  double h = 0;
  // [0, 1].
  double s = 0;

  bool operator==(const ThemeColor&) const = default;
};

struct SpaceTheme {
  SpaceTheme();
  SpaceTheme(const SpaceTheme&);
  SpaceTheme& operator=(const SpaceTheme&);
  ~SpaceTheme();

  bool operator==(const SpaceTheme&) const = default;

  // 1 to 3.
  std::vector<ThemeColor> colors;
  // [0, 1].
  double intensity = kDefaultThemeIntensity;
  // [0, 1].
  double grain = 0;
};

enum class ResolvedMode { kLight, kDark };

using Rgb = std::array<double, 3>;

// parseSpaceTheme: exactly {colors, intensity, grain} with 1 to 3 colors of exactly {h, s}; h in [0, 360), s,
// intensity and grain in [0, 1]; anything else (NaN, Infinity, strings, extra keys, null) is nullopt.
std::optional<SpaceTheme> ParseSpaceTheme(const base::Value& value);
// The theme as state and Local State carry it: {colors: [{h, s}], intensity, grain}.
base::DictValue SpaceThemeValue(const SpaceTheme& theme);

// HSL (h in degrees, s and l in [0, 1]) to sRGB channels in [0, 255], rounded.
Rgb HslToRgb(double h, double s, double l);
// sRGB to HSL; h in [0, 360), 0 for grays.
std::array<double, 3> RgbToHsl(const Rgb& rgb);
std::optional<Rgb> HexToRgb(std::string_view hex);
// "#rrggbb", lowercase.
std::string RgbToHex(const Rgb& rgb);
// Linear sRGB mix: t = 0 gives a, t = 1 gives b.
Rgb MixRgb(const Rgb& a, const Rgb& b, double t);

// WCAG relative luminance and contrast ratio.
double Luminance(const Rgb& rgb);
double Contrast(const Rgb& a, const Rgb& b);

// The theme's gradient as three stops in `mode` (lightness 0.9 light, 0.17 dark; above intensity 0.5 the saturation
// grows up to double and lightness moves up to -0.12 light, +0.05 dark): one color repeats, two colors put their sRGB
// midpoint in the middle.
std::array<Rgb, 3> ThemeStops(const SpaceTheme& theme, ResolvedMode mode);
// A stop as shown: the theme layer at `alpha` over the vibrancy base (#ececec light, #1e1e1e dark).
Rgb EffectiveRgb(const Rgb& stop, ResolvedMode mode, double alpha);
// The theme layer's alpha: 0.30 + 0.55 x intensity in light, 0.40 + 0.50 x intensity in dark, rounded to 0.001;
// 1 with Reduce transparency or in an inactive window.
double TranslucencyAlpha(ResolvedMode mode,
                         double intensity,
                         bool reduce_transparency,
                         bool focused);
// The text color ("#1d1d1f" or "#f2f2f7") with more contrast on every background given.
std::string InkFor(const std::vector<Rgb>& backgrounds);

struct ThemeInk {
  std::string ink;
  std::string ink_muted;
};
// themeToCss's ink and muted ink: chosen over the translucent and the opaque layer, muted ink faded toward the
// background only as far as contrast 4.5 holds on every stop.
ThemeInk ThemeInkFor(const SpaceTheme& theme, ResolvedMode mode);

// accentOf: the color at lightness 0.5.
std::string AccentHex(const ThemeColor& color);
// spaces[id].accent: the theme's first color when a theme is set, the named space color's hex otherwise.
std::string SpaceAccentHex(const std::optional<SpaceTheme>& theme,
                           std::string_view space_color);
// The same as an SkColor, for the window's native accent.
SkColor SpaceAccentColor(const std::optional<SpaceTheme>& theme,
                         std::string_view space_color);

// defaultThemeFor: the default gradient, a pastel of the named color's
// hue (saturation at most 0.5) to a warm corner, intensity 0.5, grain 0.25.
SpaceTheme DefaultThemeFor(std::string_view space_color);

struct ThemePreset {
  std::string id;
  std::string name;
  SpaceTheme theme;
};
// Clu's 12 presets (THEME_PRESETS), in order.
const std::vector<ThemePreset>& ThemePresets();

}  // namespace clu

#endif  // CLU_BROWSER_THEME_CLU_THEME_MATH_H_
