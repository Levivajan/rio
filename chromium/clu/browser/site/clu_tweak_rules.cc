// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_tweak_rules.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"

namespace clu {

namespace {

struct Range {
  double min;
  double max;
};
constexpr Range kHue{-180, 180};
constexpr Range kContrast{0.5, 2};
constexpr Range kBrightness{0.5, 2};
constexpr Range kSaturation{0, 2};
constexpr Range kFontScale{0.75, 1.5};

// Media keeps its colors under invert: inverting it once more cancels the page's inversion.
constexpr char kMedia[] = "img, video, picture, canvas";
// Icon fonts and code keep their own families.
constexpr char kFontTargets[] =
    "body, body *:not(i, code, pre, kbd, samp, [class*='icon' i], "
    "[class*='symbol' i])";
constexpr char kJsErrorPrefix[] = "[Rio site tweak]";

double Clamp(const base::Value* v, Range r, double fallback) {
  if (!v || !(v->is_double() || v->is_int())) {
    return fallback;
  }
  const double d = v->GetDouble();
  if (!std::isfinite(d)) {
    return fallback;
  }
  return std::clamp(d, r.min, r.max);
}

// String(Math.round(n * 1000) / 1000): three decimals at most, no "-0".
std::string Num(double n) {
  double rounded = std::round(n * 1000) / 1000;
  if (rounded == 0) {
    rounded = 0;
  }
  return base::NumberToString(rounded);
}

// Generic families stay bare; named ones are quoted, with the system font behind them.
std::string Family(const std::string& f) {
  const bool generic =
      !f.empty() && std::all_of(f.begin(), f.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || c == '-';
      });
  return generic ? f : base::StrCat({"\"", f, "\", system-ui"});
}

bool IsBlank(std::string_view s) {
  return base::TrimWhitespaceASCII(s, base::TRIM_ALL).empty();
}

}  // namespace

SiteTweak::SiteTweak() = default;
SiteTweak::SiteTweak(const SiteTweak&) = default;
SiteTweak& SiteTweak::operator=(const SiteTweak&) = default;
SiteTweak::SiteTweak(SiteTweak&&) = default;
SiteTweak& SiteTweak::operator=(SiteTweak&&) = default;
SiteTweak::~SiteTweak() = default;

bool IsTweakFont(std::string_view font) {
  return std::find(std::begin(kTweakFonts), std::end(kTweakFonts), font) !=
         std::end(kTweakFonts);
}

TweakControls ClampControls(const base::Value& raw) {
  TweakControls c;
  const base::DictValue* d = raw.GetIfDict();
  if (!d) {
    return c;
  }
  c.hue = Clamp(d->Find("hue"), kHue, 0);
  const base::Value* invert = d->Find("invert");
  c.invert = invert && invert->is_bool() && invert->GetBool();
  c.contrast = Clamp(d->Find("contrast"), kContrast, 1);
  c.brightness = Clamp(d->Find("brightness"), kBrightness, 1);
  c.saturation = Clamp(d->Find("saturation"), kSaturation, 1);
  const std::string* font = d->FindString("fontFamily");
  if (font && IsTweakFont(*font)) {
    c.font_family = *font;
  }
  c.font_scale = Clamp(d->Find("fontScale"), kFontScale, 1);
  return c;
}

bool IsDefaultControls(const TweakControls& controls) {
  return controls == TweakControls();
}

base::DictValue ControlsToValue(const TweakControls& c) {
  return base::DictValue()
      .Set("hue", c.hue)
      .Set("invert", c.invert)
      .Set("contrast", c.contrast)
      .Set("brightness", c.brightness)
      .Set("saturation", c.saturation)
      .Set("fontFamily",
           c.font_family ? base::Value(*c.font_family) : base::Value())
      .Set("fontScale", c.font_scale);
}

std::string ControlsToCss(const std::optional<TweakControls>& input) {
  if (!input) {
    return std::string();
  }
  // Clamp again: the controls may come from a payload the guard only range-checked.
  const TweakControls c = ClampControls(base::Value(ControlsToValue(*input)));
  std::vector<std::string> fns;
  if (c.hue != 0) {
    fns.push_back(base::StrCat({"hue-rotate(", Num(c.hue), "deg)"}));
  }
  if (c.invert) {
    fns.push_back("invert(1)");
  }
  if (c.contrast != 1) {
    fns.push_back(base::StrCat({"contrast(", Num(c.contrast), ")"}));
  }
  if (c.brightness != 1) {
    fns.push_back(base::StrCat({"brightness(", Num(c.brightness), ")"}));
  }
  if (c.saturation != 1) {
    fns.push_back(base::StrCat({"saturate(", Num(c.saturation), ")"}));
  }
  std::vector<std::string> rules;
  if (!fns.empty()) {
    rules.push_back(base::StrCat(
        {"html { filter: ", base::JoinString(fns, " "), " !important; }"}));
  }
  if (c.invert) {
    rules.push_back(
        base::StrCat({kMedia, " { filter: invert(1) !important; }"}));
    // A page without a background shows the view's white, which no filter reaches. Not !important, so a page
    // background still wins.
    rules.push_back("html { background-color: #fff; }");
  }
  if (c.font_family || c.font_scale != 1) {
    std::vector<std::string> parts;
    if (c.font_scale != 1) {
      parts.push_back(base::StrCat(
          {"html { font-size: ", Num(c.font_scale * 100), "% !important; }"}));
    }
    if (c.font_family) {
      parts.push_back(base::StrCat({kFontTargets, " { font-family: ",
                                    Family(*c.font_family),
                                    " !important; }"}));
    }
    rules.push_back(base::JoinString(parts, " "));
  }
  return base::JoinString(rules, "\n");
}

bool IsSafeSelector(std::string_view s) {
  if (IsBlank(s) || s.size() > kMaxZapSelector) {
    return false;
  }
  return s.find_first_of("{};@<") == std::string_view::npos &&
         s.find("/*") == std::string_view::npos;
}

std::string CleanLabel(std::string_view label) {
  // Control characters (DEL) become spaces, runs of white space one space.
  std::u16string text = base::UTF8ToUTF16(label);
  for (char16_t& c : text) {
    if (c < 0x20 || (c >= 0x7f && c <= 0x9f)) {
      c = u' ';
    }
  }
  std::u16string collapsed = base::CollapseWhitespace(text, false);
  if (collapsed.size() > kMaxZapLabel) {
    size_t cut = kMaxZapLabel;
    // Never split a surrogate pair.
    if (cut > 0 && (collapsed[cut - 1] & 0xFC00) == 0xD800) {
      --cut;
    }
    collapsed.resize(cut);
  }
  return base::UTF16ToUTF8(collapsed);
}

AppendZapResult AppendZap(std::vector<Zap>& zaps, Zap zap) {
  if (!IsSafeSelector(zap.selector)) {
    return AppendZapResult::kUnsafe;
  }
  if (zaps.size() >= kMaxZaps) {
    return AppendZapResult::kFull;
  }
  zap.label = CleanLabel(zap.label);
  zaps.push_back(std::move(zap));
  return AppendZapResult::kOk;
}

std::string ZapCss(const std::vector<Zap>& zaps) {
  std::vector<std::string> rules;
  for (const Zap& zap : zaps) {
    if (IsSafeSelector(zap.selector)) {
      rules.push_back(
          base::StrCat({zap.selector, " { display: none !important; }"}));
    }
  }
  return base::JoinString(rules, "\n");
}

std::vector<Zap> ParseZaps(std::string_view json, bool* junk) {
  std::vector<Zap> zaps;
  if (json.empty()) {
    return zaps;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    if (junk) {
      *junk = true;
    }
    return zaps;
  }
  for (const base::Value& entry : parsed->GetList()) {
    Zap zap;
    if (entry.is_string()) {
      zap.selector = entry.GetString();
    } else if (const base::DictValue* d = entry.GetIfDict()) {
      const std::string* selector = d->FindString("selector");
      if (!selector) {
        continue;
      }
      zap.selector = *selector;
      const std::string* label = d->FindString("label");
      zap.label = label ? CleanLabel(*label) : std::string();
      zap.related = d->FindBool("related").value_or(false);
      const std::optional<double> at = d->FindDouble("createdAt");
      zap.created_at = at && std::isfinite(*at) ? *at : 0;
    } else {
      continue;
    }
    if (!IsSafeSelector(zap.selector)) {
      continue;
    }
    zaps.push_back(std::move(zap));
    if (zaps.size() >= kMaxZaps) {
      break;
    }
  }
  return zaps;
}

std::string ZapsToJson(const std::vector<Zap>& zaps) {
  base::ListValue list;
  for (const Zap& zap : zaps) {
    list.Append(base::DictValue()
                    .Set("selector", zap.selector)
                    .Set("label", zap.label)
                    .Set("related", zap.related)
                    .Set("createdAt", zap.created_at));
  }
  return base::WriteJson(list).value_or("[]");
}

std::optional<TweakControls> ParseControls(std::string_view json) {
  if (json.empty()) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  TweakControls c = ClampControls(*parsed);
  if (IsDefaultControls(c)) {
    return std::nullopt;
  }
  return c;
}

std::string ControlsToJson(const TweakControls& controls) {
  return base::WriteJson(ControlsToValue(controls)).value_or("null");
}

bool IsTweakCodeSizeOk(std::string_view css, std::string_view js) {
  return css.size() <= kMaxTweakCodeBytes && js.size() <= kMaxTweakCodeBytes;
}

bool IsEmptyTweak(const SiteTweak& t) {
  return IsBlank(t.css) && IsBlank(t.js) && !t.controls && t.zaps.empty();
}

base::DictValue SiteTweakToValue(const SiteTweak& t) {
  base::ListValue zaps;
  for (const Zap& zap : t.zaps) {
    zaps.Append(base::DictValue()
                    .Set("selector", zap.selector)
                    .Set("label", zap.label)
                    .Set("related", zap.related)
                    .Set("createdAt", zap.created_at));
  }
  return base::DictValue()
      .Set("site", t.site)
      .Set("enabled", t.enabled)
      .Set("css", t.css)
      .Set("js", t.js)
      .Set("controls", t.controls ? base::Value(ControlsToValue(*t.controls))
                                  : base::Value())
      .Set("zaps", std::move(zaps))
      .Set("updatedAt", t.updated_at);
}

base::Value TweakSummaryValue(const SiteTweak* t) {
  if (!t) {
    return base::Value();
  }
  return base::Value(base::DictValue()
                         .Set("enabled", t->enabled)
                         .Set("hasCss", !IsBlank(t->css))
                         .Set("hasJs", !IsBlank(t->js))
                         .Set("hasControls", t->controls.has_value())
                         .Set("zapCount", static_cast<int>(t->zaps.size())));
}

base::DictValue SiteTweakRowValue(const SiteTweak& t) {
  return base::DictValue()
      .Set("site", t.site)
      .Set("enabled", t.enabled)
      .Set("hasCss", !IsBlank(t.css))
      .Set("hasJs", !IsBlank(t.js))
      .Set("hasControls", t.controls.has_value())
      .Set("zapCount", static_cast<int>(t.zaps.size()))
      .Set("updatedAt", t.updated_at);
}

std::string TweakCss(const SiteTweak* tweak,
                     const std::optional<std::string>& draft_css,
                     const std::optional<TweakControls>& draft_controls) {
  if (tweak && !tweak->enabled) {
    return std::string();
  }
  std::optional<TweakControls> controls = draft_controls;
  if (!controls && tweak) {
    controls = tweak->controls;
  }
  const std::string parts[] = {
      draft_css ? *draft_css : (tweak ? tweak->css : std::string()),
      ControlsToCss(controls),
      tweak ? ZapCss(tweak->zaps) : std::string()};
  std::vector<std::string> kept;
  for (const std::string& part : parts) {
    if (!IsBlank(part)) {
      kept.push_back(part);
    }
  }
  return base::JoinString(kept, "\n");
}

std::string WrapTweakJs(std::string_view js) {
  // The trailing `void 0` keeps the completion value serializable whatever the tweak's last expression is.
  return base::StrCat({"try {\n", js, "\n} catch (e) { console.error(\"",
                       kJsErrorPrefix, "\", e) }\nvoid 0"});
}

}  // namespace clu
