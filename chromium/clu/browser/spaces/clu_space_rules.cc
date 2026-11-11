// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/spaces/clu_space_rules.h"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

#include "base/i18n/break_iterator.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"
#include "third_party/icu/source/common/unicode/uchar.h"

namespace clu {

namespace {

// Only the first 60 graphemes survive, so a long input is cut early (4 KiB covers 60 graphemes of any real text).
constexpr size_t kNameScanUnits = 4096;

constexpr std::array<std::pair<std::string_view, std::string_view>, 12>
    kSpaceColors = {{{"slate", "#64748b"},
                     {"red", "#ef4444"},
                     {"orange", "#f97316"},
                     {"amber", "#f59e0b"},
                     {"lime", "#84cc16"},
                     {"green", "#22c55e"},
                     {"teal", "#14b8a6"},
                     {"cyan", "#06b6d4"},
                     {"blue", "#3b82f6"},
                     {"indigo", "#6366f1"},
                     {"violet", "#8b5cf6"},
                     {"pink", "#ec4899"}}};

// JS `\s` and String.prototype.trim(): white space and line terminators.
bool IsJsSpace(char16_t c) {
  return c == u'\t' || c == u'\n' || c == 0x0B || c == 0x0C || c == u'\r' ||
         c == u' ' || c == 0x00A0 || c == 0x1680 ||
         (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
         c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

// \p{Cc}, the bidi overrides U+202A..U+202E and the isolates U+2066..U+2069: they could flip how a name renders.
bool IsNameControl(char16_t c) {
  return c <= 0x1F || (c >= 0x7F && c <= 0x9F) ||
         (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

std::u16string_view TrimJsSpace(std::u16string_view text) {
  while (!text.empty() && IsJsSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && IsJsSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::u16string> Graphemes(std::u16string_view text) {
  std::vector<std::u16string> out;
  base::i18n::BreakIterator iter(text,
                                 base::i18n::BreakIterator::BREAK_CHARACTER);
  if (!iter.Init()) {
    return out;
  }
  while (iter.Advance()) {
    out.emplace_back(iter.GetString());
  }
  return out;
}

std::vector<UChar32> CodePoints(std::u16string_view text) {
  std::vector<UChar32> out;
  size_t i = 0;
  while (i < text.size()) {
    UChar32 c = text[i++];
    if (CBU16_IS_LEAD(c) && i < text.size() && CBU16_IS_TRAIL(text[i])) {
      c = CBU16_GET_SUPPLEMENTARY(c, text[i]);
      ++i;
    }
    out.push_back(c);
  }
  return out;
}

bool IsRegionalIndicator(UChar32 c) {
  return u_hasBinaryProperty(c, UCHAR_REGIONAL_INDICATOR);
}

// EMOJI_PARTS: inside an emoji cluster only pictographs, joiners, variation selectors, skin tones, keycap and tag
// characters belong.
bool IsEmojiPart(UChar32 c) {
  return u_hasBinaryProperty(c, UCHAR_EXTENDED_PICTOGRAPHIC) ||
         u_hasBinaryProperty(c, UCHAR_EMOJI_MODIFIER) ||
         u_hasBinaryProperty(c, UCHAR_EMOJI_COMPONENT) || c == 0x200D ||
         c == 0xFE0F || c == 0xFE0E || c == 0x20E3 ||
         (c >= 0xE0020 && c <= 0xE007F);
}

// SPACE_SYMBOLS in src/shared/types.ts, in that order.
constexpr auto kSpaceSymbols = std::to_array<std::string_view>(
    {"planet", "globe",    "house",    "briefcase", "laptop",    "code",
     "terminal", "book",   "bookmark", "folder",    "box",       "pencil",
     "palette", "camera",  "film",     "music",     "gamepad",   "cart",
     "dollar",  "chart",   "calendar", "chat",      "bell",      "megaphone",
     "bulb",    "flask",   "gear",     "wrench",    "rocket",    "plane",
     "star",    "heart",   "flame",    "bolt",      "sun",       "moon",
     "cloud",   "leaf",    "paw",      "cup"});
static_assert(kSpaceSymbols.size() == kSpaceSymbolCount);

// One dotted part of an imported symbol name, and the bundled glyph it reads as.
constexpr auto kSymbolAliases = std::to_array<std::pair<std::string_view, std::string_view>>({
    {"building", "house"},        {"laptopcomputer", "laptop"},
    {"desktopcomputer", "laptop"}, {"display", "laptop"},
    {"curlybraces", "code"},      {"forwardslash", "code"},
    {"books", "book"},            {"magazine", "book"},
    {"tray", "folder"},           {"archivebox", "folder"},
    {"shippingbox", "box"},       {"cube", "box"},
    {"highlighter", "pencil"},    {"scribble", "pencil"},
    {"paintpalette", "palette"},  {"paintbrush", "palette"},
    {"photo", "camera"},          {"tv", "film"},
    {"play", "film"},             {"headphones", "music"},
    {"guitars", "music"},         {"gamecontroller", "gamepad"},
    {"bag", "cart"},              {"basket", "cart"},
    {"dollarsign", "dollar"},     {"banknote", "dollar"},
    {"creditcard", "dollar"},     {"message", "chat"},
    {"bubble", "chat"},           {"envelope", "chat"},
    {"speaker", "megaphone"},     {"lightbulb", "bulb"},
    {"testtube", "flask"},        {"atom", "flask"},
    {"gearshape", "gear"},        {"hammer", "wrench"},
    {"airplane", "plane"},        {"paperplane", "plane"},
    {"sparkles", "star"},         {"sparkle", "star"},
    {"suit", "heart"},
    {"sunrise", "sun"},           {"moonphase", "moon"},
    {"tree", "leaf"},
    {"pawprint", "paw"},          {"dog", "paw"},
    {"cat", "paw"},               {"bird", "paw"},
    {"hare", "paw"},              {"fish", "paw"},
    {"mug", "cup"},               {"fork", "cup"},
    {"network", "globe"},         {"map", "globe"},
});

}  // namespace

std::optional<std::string> NormalizeName(std::string_view raw) {
  if (!base::IsStringUTF8(raw)) {
    return std::nullopt;
  }
  std::u16string text = base::UTF8ToUTF16(raw);
  if (text.size() > kNameScanUnits) {
    text.resize(kNameScanUnits);
    if (CBU16_IS_LEAD(text.back())) {
      text.pop_back();
    }
  }
  // White space collapses to one space, control characters go, then space runs the removal left collapse again.
  std::u16string clean;
  clean.reserve(text.size());
  for (char16_t c : text) {
    if (IsJsSpace(c)) {
      if (clean.empty() || clean.back() != u' ') {
        clean.push_back(u' ');
      }
    } else if (!IsNameControl(c)) {
      clean.push_back(c);
    }
  }
  const std::u16string_view trimmed = TrimJsSpace(clean);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::u16string out;
  size_t count = 0;
  for (const std::u16string& g : Graphemes(trimmed)) {
    if (count >= kMaxNameGraphemes || out.size() + g.size() > kMaxNameUnits) {
      break;
    }
    out += g;
    ++count;
  }
  const std::u16string_view result = TrimJsSpace(out);
  if (result.empty()) {
    return std::nullopt;
  }
  return base::UTF16ToUTF8(result);
}

std::optional<std::string> ParseEmoji(std::string_view raw) {
  if (!base::IsStringUTF8(raw)) {
    return std::nullopt;
  }
  const std::u16string text = base::UTF8ToUTF16(raw);
  const std::u16string_view value = TrimJsSpace(text);
  if (value.empty() || value.size() > kMaxEmojiUnits) {
    return std::nullopt;
  }
  if (Graphemes(value).size() != 1) {
    return std::nullopt;
  }
  const std::vector<UChar32> cps = CodePoints(value);
  if (cps.size() == 2 && IsRegionalIndicator(cps[0]) &&
      IsRegionalIndicator(cps[1])) {
    return base::UTF16ToUTF8(value);
  }
  const bool pictographic = std::ranges::any_of(cps, [](UChar32 c) {
    return u_hasBinaryProperty(c, UCHAR_EXTENDED_PICTOGRAPHIC);
  });
  if (!pictographic || !std::ranges::all_of(cps, IsEmojiPart)) {
    return std::nullopt;
  }
  return base::UTF16ToUTF8(value);
}

bool IsSpaceColor(std::string_view color) {
  return std::ranges::any_of(
      kSpaceColors, [&](const auto& entry) { return entry.first == color; });
}

bool IsSpaceSymbol(std::string_view symbol) {
  return std::ranges::find(kSpaceSymbols, symbol) != kSpaceSymbols.end();
}

std::optional<std::string> SpaceSymbolAt(size_t index) {
  if (index >= kSpaceSymbols.size()) {
    return std::nullopt;
  }
  return std::string(kSpaceSymbols[index]);
}

std::optional<std::string> ClosestSpaceSymbol(std::string_view name) {
  if (name.empty() || name.size() > 64 || !base::IsStringASCII(name)) {
    return std::nullopt;
  }
  const std::string lower = base::ToLowerASCII(name);
  for (std::string_view part : base::SplitStringPiece(
           lower, ".", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    // "testtube.2" and "person.2" carry a count; the digits are not part of the name.
    part = base::TrimString(part, "0123456789", base::TRIM_TRAILING);
    if (IsSpaceSymbol(part)) {
      return std::string(part);
    }
    for (const auto& [alias, symbol] : kSymbolAliases) {
      if (alias == part) {
        return std::string(symbol);
      }
    }
  }
  return std::string("star");
}

std::string_view SpaceColorHex(std::string_view color) {
  for (const auto& [name, hex] : kSpaceColors) {
    if (name == color) {
      return hex;
    }
  }
  return kSpaceColors[0].second;
}

std::string NextDefaultName(std::string_view prefix,
                            int start,
                            const std::vector<std::string>& taken) {
  const std::set<std::string> used(taken.begin(), taken.end());
  int n = std::max(1, start);
  std::string name;
  for (;; ++n) {
    name = base::StrCat({prefix, " ", base::NumberToString(n)});
    if (!used.contains(name)) {
      return name;
    }
  }
}

DeleteSpaceText MakeDeleteSpaceText(std::string_view name,
                                    int pinned,
                                    int today) {
  const int p = std::max(0, pinned);
  const int t = std::max(0, today);
  DeleteSpaceText text;
  text.title = base::StrCat({"Delete your “", name, "” Space?"});
  if (p == 0 && t == 0) {
    text.body = "This space has no tabs.";
    return text;
  }
  auto tabs = [](int n, std::string_view kind) {
    return base::StrCat({base::NumberToString(n), " ", kind, " ",
                         n == 1 ? "tab" : "tabs"});
  };
  std::string body;
  if (p > 0) {
    body = base::StrCat(
        {"its ", tabs(p, "pinned"), " ", p == 1 ? "closes" : "close"});
  }
  if (t > 0) {
    base::StrAppend(&body, {body.empty() ? "" : " and ", "its ",
                            tabs(t, "Today"), " ", t == 1 ? "moves" : "move",
                            " to Archive"});
  }
  body[0] = base::ToUpperASCII(body[0]);
  text.body = base::StrCat({body, "."});
  return text;
}

std::optional<std::string> PickSpaceTarget(
    std::string_view last_active,
    const std::vector<std::string>& today,
    const std::vector<std::string>& pinned_tabs,
    base::FunctionRef<bool(std::string_view)> usable) {
  if (!last_active.empty() && usable(last_active)) {
    return std::string(last_active);
  }
  for (const std::vector<std::string>* list : {&today, &pinned_tabs}) {
    for (const std::string& id : *list) {
      if (usable(id)) {
        return id;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> ResolveSwitch(const std::vector<std::string>& order,
                                         std::string_view current,
                                         const SwitchTarget& target) {
  std::optional<std::string> next;
  if (target.space_id) {
    if (std::ranges::find(order, *target.space_id) != order.end()) {
      next = *target.space_id;
    }
  } else if (target.index) {
    if (*target.index >= 0 &&
        static_cast<size_t>(*target.index) < order.size()) {
      next = order[*target.index];
    }
  } else if (target.step) {
    const auto it = std::ranges::find(order, current);
    if (it == order.end()) {
      if (!order.empty()) {
        next = order.front();
      }
    } else {
      const long at = (it - order.begin()) + *target.step;
      if (at >= 0 && at < static_cast<long>(order.size())) {
        next = order[at];
      }
    }
  }
  if (!next || *next == current) {
    return std::nullopt;
  }
  return next;
}

std::optional<std::vector<std::string>> ReorderSpaces(
    const std::vector<std::string>& order,
    std::string_view space_id,
    int to_index) {
  const auto it = std::ranges::find(order, space_id);
  if (it == order.end()) {
    return std::nullopt;
  }
  const int from = static_cast<int>(it - order.begin());
  const int to =
      std::clamp(to_index, 0, std::max(0, static_cast<int>(order.size()) - 1));
  if (from == to) {
    return std::nullopt;
  }
  std::vector<std::string> next = order;
  next.erase(next.begin() + from);
  next.insert(next.begin() + to, std::string(space_id));
  return next;
}

std::optional<std::string> FallbackSpace(const std::vector<std::string>& order,
                                         std::string_view space_id) {
  const auto it = std::ranges::find(order, space_id);
  if (it == order.end()) {
    return std::nullopt;
  }
  if (it != order.begin()) {
    return *(it - 1);
  }
  if (it + 1 != order.end()) {
    return *(it + 1);
  }
  return std::nullopt;
}

SwipeState ComputeSwipe(double dx,
                        int sidebar_width,
                        const std::vector<std::string>& order,
                        std::string_view current) {
  SwipeState state;
  state.fraction =
      std::clamp(dx / static_cast<double>(std::max(1, sidebar_width)), -1.0,
                 1.0);
  const auto it = std::ranges::find(order, current);
  if (state.fraction == 0 || it == order.end()) {
    return state;
  }
  const long at = (it - order.begin()) + (state.fraction > 0 ? 1 : -1);
  if (at >= 0 && at < static_cast<long>(order.size())) {
    state.target_space_id = order[at];
  }
  return state;
}

}  // namespace clu
