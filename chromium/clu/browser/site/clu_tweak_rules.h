// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_TWEAK_RULES_H_
#define CLU_BROWSER_SITE_CLU_TWEAK_RULES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// Site tweaks as values (docs/plan/08-extensions.md F9 to F11): the limits, the zap rules, the no-code controls as CSS
// and the defensive reads of stored rows, with the rules of src/shared/tweaks.ts. clu_tweak_rules_unittest.cc covers
// them.
namespace clu {

// CSS and JS are capped at 256 KiB each, in UTF-8 bytes.
inline constexpr size_t kMaxTweakCodeBytes = 256 * 1024;
inline constexpr size_t kMaxZaps = 200;
inline constexpr size_t kMaxZapSelector = 1024;
inline constexpr size_t kMaxZapLabel = 80;

// The fonts the Style tab offers (TWEAK_FONTS).
inline constexpr std::string_view kTweakFonts[] = {
    "system-ui", "serif", "sans-serif", "monospace",
    "Georgia",   "Helvetica Neue", "Menlo"};

struct TweakControls {
  double hue = 0;
  bool invert = false;
  double contrast = 1;
  double brightness = 1;
  double saturation = 1;
  std::optional<std::string> font_family;
  double font_scale = 1;

  bool operator==(const TweakControls&) const = default;
};

struct Zap {
  std::string selector;
  std::string label;
  bool related = false;
  // Epoch ms.
  double created_at = 0;

  bool operator==(const Zap&) const = default;
};

struct SiteTweak {
  SiteTweak();
  SiteTweak(const SiteTweak&);
  SiteTweak& operator=(const SiteTweak&);
  SiteTweak(SiteTweak&&);
  SiteTweak& operator=(SiteTweak&&);
  ~SiteTweak();

  std::string site;
  bool enabled = true;
  std::string css;
  std::string js;
  // Null: the page as the site made it.
  std::optional<TweakControls> controls;
  std::vector<Zap> zaps;
  // Epoch ms.
  double updated_at = 0;
};

bool IsTweakFont(std::string_view font);

// Repairs anything into controls (clampControls): numbers clamp into range, NaN and junk take the default, unknown
// fonts read as none.
TweakControls ClampControls(const base::Value& raw);
bool IsDefaultControls(const TweakControls& controls);
base::DictValue ControlsToValue(const TweakControls& controls);

// The controls as user-origin CSS: one filter on html (media inverted back under invert), one font block.
std::string ControlsToCss(const std::optional<TweakControls>& controls);

// A zap selector may not leave its CSS rule: not empty, at most 1 KiB, no braces, semicolons, at-rules, comments or
// markup.
bool IsSafeSelector(std::string_view selector);
// Printable, single line, at most 80 characters; anything else reads as ''.
std::string CleanLabel(std::string_view label);

enum class AppendZapResult { kOk, kUnsafe, kFull };
// Appends one zap when its selector is safe and the site has fewer than 200 (the label cleaned).
AppendZapResult AppendZap(std::vector<Zap>& zaps, Zap zap);
// One `display: none` rule per safe selector.
std::string ZapCss(const std::vector<Zap>& zaps);

// Stored zaps: a JSON array of {selector, label, related, createdAt}; a bare selector string reads as an unlabeled
// zap. Unparseable JSON reads as none and sets `junk`; entries that fail the selector rule are dropped; at most 200.
std::vector<Zap> ParseZaps(std::string_view json, bool* junk);
std::string ZapsToJson(const std::vector<Zap>& zaps);
// Stored controls: junk reads as null, a stored object is repaired field by field, defaults store as none.
std::optional<TweakControls> ParseControls(std::string_view json);
std::string ControlsToJson(const TweakControls& controls);

// False when CSS or JS is over 256 KiB.
bool IsTweakCodeSizeOk(std::string_view css, std::string_view js);

// Nothing in it: no CSS, no JS (both trimmed), no controls, no zaps.
bool IsEmptyTweak(const SiteTweak& tweak);

// tweaks.get's SiteTweak.
base::DictValue SiteTweakToValue(const SiteTweak& tweak);
// TabState.tweak (src/shared/state/tweaks.ts): null without a tweak.
base::Value TweakSummaryValue(const SiteTweak* tweak);
// One row of tweaks.list.
base::DictValue SiteTweakRowValue(const SiteTweak& tweak);

// The user-origin CSS a page of the site shows: the draft CSS or the saved CSS, the draft or saved controls, then the
// zaps, blank parts left out. Empty when the site's tweak is off, drafts included; a site with no tweak yet shows its
// drafts.
std::string TweakCss(const SiteTweak* tweak,
                     const std::optional<std::string>& draft_css,
                     const std::optional<TweakControls>& draft_controls);

// The tweak's JS as it runs: errors go to the page's console with the "[Rio site tweak]" prefix, and the completion
// value is always undefined.
std::string WrapTweakJs(std::string_view js);

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_TWEAK_RULES_H_
