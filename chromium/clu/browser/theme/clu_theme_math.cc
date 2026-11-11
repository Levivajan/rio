// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/theme/clu_theme_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "clu/browser/spaces/clu_space_rules.h"

namespace clu {

namespace {

// THEME_LIGHTNESS, THEME_SATURATION, DEFAULT_THEME, VIBRANCY_BASE and INK in
// src/shared/theme/space.ts.
constexpr double kLightnessLight = 0.9;
constexpr double kLightnessDark = 0.17;
constexpr double kSaturationDark = 0.3;
// INTENSITY_LIGHTNESS: how far lightness moves at intensity 1 (none at or below
// the default 0.5).
constexpr double kIntensityLightnessLight = -0.12;
constexpr double kIntensityLightnessDark = 0.05;
// INTENSITY_SOFTEN: below the default the default's color turns into a lighter
// pastel in OKLab, linearly down to the minimum, where lightness has risen by
// the lift and chroma keeps the keep share (in light mode, pink reaches #f2b6b8
// at the minimum).
constexpr double kSoftenLiftLight = 0.054;
constexpr double kSoftenKeepLight = 0.92;
constexpr double kSoftenLiftDark = 0.073;
constexpr double kSoftenKeepDark = 0.38;
constexpr double kDefaultMaxSaturation = 0.5;
constexpr ThemeColor kDefaultWarm = {0, 0.3};
constexpr double kDefaultGrain = 0.25;
// ACCENT_LIGHTNESS, ACCENT_MAX_SATURATION: the main color as a soft tint, never
// neon.
constexpr double kAccentLightness = 0.68;
constexpr double kAccentMaxSaturation = 0.58;
constexpr Rgb kVibrancyLight = {0xec, 0xec, 0xec};
constexpr Rgb kVibrancyDark = {0x1e, 0x1e, 0x1e};
constexpr char kInkDark[] = "#1d1d1f";
constexpr char kInkLight[] = "#f2f2f7";
constexpr double kMinContrast = 4.5;

double Clamp01(double v) {
  return std::min(1.0, std::max(0.0, v));
}

// Math.round for the non-negative values here: halves go up.
double Round(double v) {
  return std::floor(v + 0.5);
}

double Lightness(ResolvedMode mode) {
  return mode == ResolvedMode::kDark ? kLightnessDark : kLightnessLight;
}

// A finite number in [min, max] (max_open: below max).
std::optional<double> NumberIn(const base::Value* value,
                               double min,
                               double max,
                               bool max_open) {
  if (!value || !(value->is_double() || value->is_int())) {
    return std::nullopt;
  }
  const double v = value->GetDouble();
  if (!std::isfinite(v) || v < min || (max_open ? v >= max : v > max)) {
    return std::nullopt;
  }
  return v;
}

// rgbToOklab, oklabToRgb: sRGB channels in [0, 255] to OKLab and back (clipped
// to the gamut and rounded).
double ToLinear(double v) {
  return v <= 0.04045 * 255 ? v / 255 / 12.92
                            : std::pow((v / 255 + 0.055) / 1.055, 2.4);
}

double FromLinear(double v) {
  const double c = Clamp01(v);
  return 255 * (c <= 0.0031308 ? 12.92 * c
                               : 1.055 * std::pow(c, 1 / 2.4) - 0.055);
}

std::array<double, 3> RgbToOklab(const Rgb& rgb) {
  const double r = ToLinear(rgb[0]), g = ToLinear(rgb[1]), b = ToLinear(rgb[2]);
  const double l =
      std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const double m =
      std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const double s =
      std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return {0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
          1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
          0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s};
}

Rgb OklabToRgb(double lightness, double a, double b) {
  const double l = std::pow(lightness + 0.3963377774 * a + 0.2158037573 * b, 3);
  const double m = std::pow(lightness - 0.1055613458 * a - 0.0638541728 * b, 3);
  const double s = std::pow(lightness - 0.0894841775 * a - 1.291485548 * b, 3);
  return {
      Round(FromLinear(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s)),
      Round(FromLinear(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s)),
      Round(FromLinear(-0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s))};
}

// softenBelowDefault: the default's color at an intensity below the default,
// lighter and nearly as colorful, never a gray.
Rgb SoftenBelowDefault(const Rgb& rgb, ResolvedMode mode, double intensity) {
  const double t = Clamp01(1 - 2 * intensity);
  if (t == 0) {
    return rgb;
  }
  const bool dark = mode == ResolvedMode::kDark;
  const double lift = dark ? kSoftenLiftDark : kSoftenLiftLight;
  const double keep = dark ? kSoftenKeepDark : kSoftenKeepLight;
  const std::array<double, 3> lab = RgbToOklab(rgb);
  const double k = 1 - (1 - keep) * t;
  return OklabToRgb(std::min(1.0, lab[0] + lift * t), lab[1] * k, lab[2] * k);
}

// legacyColorRgb: the default 0.5 is the mode's pastel (or muted dark), lower
// softens it into a lighter pastel, 1 doubles the saturation and
// deepens the tint.
Rgb ColorRgb(const ThemeColor& c, ResolvedMode mode, double intensity) {
  const double i =
      std::isfinite(intensity) ? Clamp01(intensity) : kDefaultThemeIntensity;
  const double k = std::max(1.0, 2 * i);
  const bool dark = mode == ResolvedMode::kDark;
  const double s = Clamp01(c.s * (dark ? kSaturationDark : 1.0) * k);
  const double l =
      Lightness(mode) +
      (dark ? kIntensityLightnessDark : kIntensityLightnessLight) * Clamp01(k - 1);
  return SoftenBelowDefault(HslToRgb(c.h, s, Round(l * 1000) / 1000), mode, i);
}

ThemePreset Preset(const char* name,
                   std::initializer_list<std::pair<double, double>> colors,
                   double grain = 0) {
  ThemePreset preset;
  preset.name = name;
  preset.id = base::ToLowerASCII(name);
  for (const auto& [h, s] : colors) {
    preset.theme.colors.push_back({h, s});
  }
  preset.theme.intensity = kDefaultThemeIntensity;
  preset.theme.grain = grain;
  return preset;
}

}  // namespace

SpaceTheme::SpaceTheme() = default;
SpaceTheme::SpaceTheme(const SpaceTheme&) = default;
SpaceTheme& SpaceTheme::operator=(const SpaceTheme&) = default;
SpaceTheme::~SpaceTheme() = default;

std::optional<SpaceTheme> ParseSpaceTheme(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict || dict->size() != 3) {
    return std::nullopt;
  }
  const base::ListValue* colors = dict->FindList("colors");
  const std::optional<double> intensity =
      NumberIn(dict->Find("intensity"), 0, 1, false);
  const std::optional<double> grain =
      NumberIn(dict->Find("grain"), 0, 1, false);
  if (!colors || !intensity || !grain || colors->empty() ||
      colors->size() > kMaxThemeColors) {
    return std::nullopt;
  }
  SpaceTheme theme;
  for (const base::Value& item : *colors) {
    const base::DictValue* color = item.GetIfDict();
    if (!color || color->size() != 2) {
      return std::nullopt;
    }
    const std::optional<double> h = NumberIn(color->Find("h"), 0, 360, true);
    const std::optional<double> s = NumberIn(color->Find("s"), 0, 1, false);
    if (!h || !s) {
      return std::nullopt;
    }
    theme.colors.push_back({*h, *s});
  }
  theme.intensity = *intensity;
  theme.grain = *grain;
  return theme;
}

base::DictValue SpaceThemeValue(const SpaceTheme& theme) {
  base::ListValue colors;
  for (const ThemeColor& c : theme.colors) {
    colors.Append(base::DictValue().Set("h", c.h).Set("s", c.s));
  }
  return base::DictValue()
      .Set("colors", std::move(colors))
      .Set("intensity", theme.intensity)
      .Set("grain", theme.grain);
}

Rgb HslToRgb(double h, double s, double l) {
  const double hue = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
  const double sat = Clamp01(s);
  const double lig = Clamp01(l);
  const double c = (1 - std::abs(2 * lig - 1)) * sat;
  const double x = c * (1 - std::abs(std::fmod(hue / 60, 2.0) - 1));
  const double m = lig - c / 2;
  double r = 0, g = 0, b = 0;
  if (hue < 60) {
    r = c, g = x;
  } else if (hue < 120) {
    r = x, g = c;
  } else if (hue < 180) {
    g = c, b = x;
  } else if (hue < 240) {
    g = x, b = c;
  } else if (hue < 300) {
    r = x, b = c;
  } else {
    r = c, b = x;
  }
  return {Round((r + m) * 255), Round((g + m) * 255), Round((b + m) * 255)};
}

std::array<double, 3> RgbToHsl(const Rgb& rgb) {
  const double r = rgb[0] / 255, g = rgb[1] / 255, b = rgb[2] / 255;
  const double max = std::max({r, g, b});
  const double min = std::min({r, g, b});
  const double l = (max + min) / 2;
  const double d = max - min;
  if (d == 0) {
    return {0, 0, l};
  }
  const double s = d / (1 - std::abs(2 * l - 1));
  // JS % keeps the dividend's sign; std::fmod does too.
  const double h = max == r   ? std::fmod((g - b) / d, 6.0)
                   : max == g ? (b - r) / d + 2
                              : (r - g) / d + 4;
  return {std::fmod(std::fmod(h * 60, 360.0) + 360.0, 360.0), Clamp01(s), l};
}

std::optional<Rgb> HexToRgb(std::string_view hex) {
  if (hex.size() != 7 || hex[0] != '#') {
    return std::nullopt;
  }
  uint32_t n = 0;
  if (!base::HexStringToUInt(hex.substr(1), &n)) {
    return std::nullopt;
  }
  return Rgb{static_cast<double>((n >> 16) & 255),
             static_cast<double>((n >> 8) & 255), static_cast<double>(n & 255)};
}

std::string RgbToHex(const Rgb& rgb) {
  auto channel = [](double v) {
    return static_cast<int>(Round(std::min(255.0, std::max(0.0, v))));
  };
  return base::StringPrintf("#%02x%02x%02x", channel(rgb[0]), channel(rgb[1]),
                            channel(rgb[2]));
}

Rgb MixRgb(const Rgb& a, const Rgb& b, double t) {
  const double k = Clamp01(t);
  return {a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k,
          a[2] + (b[2] - a[2]) * k};
}

double Luminance(const Rgb& rgb) {
  auto lin = [](double v) {
    const double c = v / 255;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(rgb[0]) + 0.7152 * lin(rgb[1]) + 0.0722 * lin(rgb[2]);
}

double Contrast(const Rgb& a, const Rgb& b) {
  const double la = Luminance(a);
  const double lb = Luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

std::array<Rgb, 3> ThemeStops(const SpaceTheme& theme, ResolvedMode mode) {
  std::vector<Rgb> rgb;
  for (const ThemeColor& c : theme.colors) {
    rgb.push_back(ColorRgb(c, mode, theme.intensity));
  }
  if (rgb.empty()) {
    rgb.push_back(HslToRgb(0, 0, Lightness(mode)));
  }
  if (rgb.size() == 1) {
    return {rgb[0], rgb[0], rgb[0]};
  }
  if (rgb.size() == 2) {
    return {rgb[0], MixRgb(rgb[0], rgb[1], 0.5), rgb[1]};
  }
  return {rgb[0], rgb[1], rgb[2]};
}

Rgb EffectiveRgb(const Rgb& stop, ResolvedMode mode, double alpha) {
  return MixRgb(mode == ResolvedMode::kDark ? kVibrancyDark : kVibrancyLight,
                stop, alpha);
}

double TranslucencyAlpha(ResolvedMode mode,
                         double intensity,
                         bool reduce_transparency,
                         bool focused) {
  if (reduce_transparency || !focused) {
    return 1;
  }
  const double i = std::isfinite(intensity) ? Clamp01(intensity)
                                            : kDefaultThemeIntensity;
  const double alpha =
      mode == ResolvedMode::kDark ? 0.4 + 0.5 * i : 0.3 + 0.55 * i;
  return Round(alpha * 1000) / 1000;
}

std::string InkFor(const std::vector<Rgb>& backgrounds) {
  auto worst = [&](const char* ink) {
    const Rgb rgb = *HexToRgb(ink);
    double min = std::numeric_limits<double>::infinity();
    for (const Rgb& bg : backgrounds) {
      min = std::min(min, Contrast(rgb, bg));
    }
    return min;
  };
  return worst(kInkDark) >= worst(kInkLight) ? kInkDark : kInkLight;
}

ThemeInk ThemeInkFor(const SpaceTheme& theme, ResolvedMode mode) {
  const std::array<Rgb, 3> stops = ThemeStops(theme, mode);
  const double alpha = TranslucencyAlpha(mode, theme.intensity,
                                         /*reduce_transparency=*/false,
                                         /*focused=*/true);
  // Both the translucent layer and the opaque one (inactive window, Reduce transparency) count.
  std::vector<Rgb> shown;
  for (const double a : {alpha, 1.0}) {
    for (const Rgb& stop : stops) {
      shown.push_back(EffectiveRgb(stop, mode, a));
    }
  }
  ThemeInk out;
  out.ink = InkFor(shown);
  out.ink_muted = out.ink;
  const Rgb ink = *HexToRgb(out.ink);
  Rgb avg = {0, 0, 0};
  for (const Rgb& c : shown) {
    for (int i = 0; i < 3; ++i) {
      avg[i] += c[i] / shown.size();
    }
  }
  // mutedInk: from 0.4 down in steps of 0.02, the first mix that keeps 4.5 on every stop.
  for (int step = 20; step > 0; --step) {
    const Rgb c = MixRgb(ink, avg, step * 0.02);
    const bool ok = std::all_of(shown.begin(), shown.end(), [&](const Rgb& bg) {
      return Contrast(c, bg) >= kMinContrast;
    });
    if (ok) {
      out.ink_muted = RgbToHex(c);
      break;
    }
  }
  return out;
}

std::string AccentHex(const ThemeColor& color) {
  return RgbToHex(HslToRgb(color.h, std::min(color.s, kAccentMaxSaturation),
                          kAccentLightness));
}

std::string SpaceAccentHex(const std::optional<SpaceTheme>& theme,
                           std::string_view space_color) {
  if (theme && !theme->colors.empty()) {
    return AccentHex(theme->colors.front());
  }
  return std::string(SpaceColorHex(space_color));
}

SkColor SpaceAccentColor(const std::optional<SpaceTheme>& theme,
                         std::string_view space_color) {
  const std::optional<Rgb> rgb = HexToRgb(SpaceAccentHex(theme, space_color));
  if (!rgb) {
    return SK_ColorBLUE;
  }
  return SkColorSetRGB(static_cast<U8CPU>((*rgb)[0]),
                       static_cast<U8CPU>((*rgb)[1]),
                       static_cast<U8CPU>((*rgb)[2]));
}

SpaceTheme DefaultThemeFor(std::string_view space_color) {
  const Rgb rgb = HexToRgb(SpaceColorHex(space_color))
                      .value_or(Rgb{0x3b, 0x82, 0xf6});
  const std::array<double, 3> hsl = RgbToHsl(rgb);
  SpaceTheme theme;
  theme.colors.push_back(
      {std::fmod(Round(hsl[0] * 10) / 10, 360.0),
       Round(std::min(hsl[1], kDefaultMaxSaturation) * 1000) / 1000});
  theme.colors.push_back(kDefaultWarm);
  theme.intensity = kDefaultThemeIntensity;
  theme.grain = kDefaultGrain;
  return theme;
}

const std::vector<ThemePreset>& ThemePresets() {
  static const base::NoDestructor<std::vector<ThemePreset>> presets(
      std::vector<ThemePreset>{
          Preset("Moss", {{96, 0.3}, {140, 0.32}}),
          Preset("Ember", {{12, 0.82}, {36, 0.88}}),
          Preset("Tide", {{190, 0.62}, {222, 0.58}}),
          Preset("Dusk", {{262, 0.45}, {316, 0.48}, {22, 0.6}}),
          Preset("Sand", {{38, 0.45}}, 0.25),
          Preset("Plum", {{290, 0.4}, {330, 0.44}}),
          Preset("Slate", {{215, 0.16}}),
          Preset("Coral", {{4, 0.74}, {26, 0.8}}),
          Preset("Fern", {{150, 0.45}}, 0.15),
          Preset("Ink", {{228, 0.36}, {250, 0.3}}),
          Preset("Honey", {{44, 0.86}, {30, 0.8}}),
          Preset("Frost", {{200, 0.36}, {180, 0.26}, {220, 0.32}}),
      });
  return *presets;
}

}  // namespace clu
