// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SPACES_CLU_SPACE_RULES_H_
#define CLU_BROWSER_SPACES_CLU_SPACE_RULES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/function_ref.h"

// The small rules of the spaces module, ported from src/shared/spaces.ts and src/main/spaces with the same test cases.
// Pure, so clu_unittests covers them without a browser.
namespace clu {

// MAX_SPACES, MAX_PROFILES, MAX_NAME, MAX_NAME_UNITS and MAX_EMOJI_UNITS in src/shared/spaces.ts.
inline constexpr size_t kMaxSpaces = 50;
inline constexpr size_t kMaxProfiles = 20;
inline constexpr size_t kMaxNameGraphemes = 60;
inline constexpr size_t kMaxNameUnits = 240;
inline constexpr size_t kMaxEmojiUnits = 16;

inline constexpr char kHomeSpaceName[] = "Home";
inline constexpr char kHomeSpaceColor[] = "blue";
// A new space's color when none is chosen: indigo, a lavender.
inline constexpr char kNewSpaceColor[] = "indigo";
inline constexpr char kDefaultProfileName[] = "Default";
inline constexpr char kPrivateSpaceName[] = "Private";
inline constexpr char kPrivateSpaceColor[] = "slate";

// normalizeName(): white space collapses to single spaces, control characters and bidi overrides go, the result is
// trimmed and cut to 60 graphemes and at most 240 UTF-16 units at a grapheme boundary. Nullopt when nothing is left
// (the caller falls back to "Space N" or "Profile N"). Invalid UTF-8 counts as nothing.
std::optional<std::string> NormalizeName(std::string_view raw);

// parseEmoji(): exactly one emoji grapheme (a ZWJ sequence, a flag pair, a skin-tone or tag sequence) of at most 16
// UTF-16 units, trimmed; else nullopt. "ab", "<script>", two emoji and an empty string are refused.
std::optional<std::string> ParseEmoji(std::string_view raw);

// isSpaceColor(): one of SPACE_COLORS.
bool IsSpaceColor(std::string_view color);
// SPACE_SYMBOLS in src/shared/types.ts, in that order (the mojom SpaceIconSymbolValue numbers them the same way).
inline constexpr size_t kSpaceSymbolCount = 40;
// One of SPACE_SYMBOLS.
bool IsSpaceSymbol(std::string_view symbol);
// The symbol a mojom SpaceIconSymbolValue stands for, or nullopt past the list.
std::optional<std::string> SpaceSymbolAt(size_t index);
// The bundled glyph closest to a symbol name another browser stored (an SF-Symbols-style dotted name such as
// "globe.americas.fill" or "cup.and.saucer"): the first dotted part that names or aliases one of SPACE_SYMBOLS, else
// "star". Nullopt for an empty or non-ASCII name, or one longer than 64 characters.
std::optional<std::string> ClosestSpaceSymbol(std::string_view name);
// SPACE_COLOR_HEX: the fill of each named color; "#64748b" (slate) for anything else.
std::string_view SpaceColorHex(std::string_view color);

// nextDefaultName(): the first "<prefix> N" from `start` that no name in `taken` uses.
std::string NextDefaultName(std::string_view prefix,
                            int start,
                            const std::vector<std::string>& taken);

// deleteSpaceText(): the delete confirmation's title and body.
struct DeleteSpaceText {
  std::string title;
  std::string body;
};
DeleteSpaceText MakeDeleteSpaceText(std::string_view name,
                                    int pinned,
                                    int today);

// The tab a window shows when it switches to a space (docs/plan/03-spaces.md "Space switching"): the space's last
// active tab, else the first Today tab, else the first pinned tab in sidebar order, else none. `usable` refuses rows
// another window shows or a pending Clear holds.
std::optional<std::string> PickSpaceTarget(
    std::string_view last_active,
    const std::vector<std::string>& today,
    const std::vector<std::string>& pinned_tabs,
    base::FunctionRef<bool(std::string_view)> usable);

// spaces.switch: the index in `order` a switch lands on, or nullopt for a no-op (an index past the end, a step past
// either end: no wrap, or the space already shown). `current` is the index shown now, or -1.
struct SwitchTarget {
  std::optional<std::string> space_id;
  std::optional<int> index;
  std::optional<int> step;
};
std::optional<std::string> ResolveSwitch(const std::vector<std::string>& order,
                                         std::string_view current,
                                         const SwitchTarget& target);

// spaces.reorder: the space order after moving `space_id` to `to_index`, clamped to 0..n-1. Nullopt when nothing
// changes or the id is unknown.
std::optional<std::vector<std::string>> ReorderSpaces(
    const std::vector<std::string>& order,
    std::string_view space_id,
    int to_index);

// The space a window shows after `space_id` is deleted: the previous one in `order`, else the next one.
std::optional<std::string> FallbackSpace(const std::vector<std::string>& order,
                                         std::string_view space_id);

// windows[id].swipe (src/main/spaces/swipe.ts): the drag as a fraction of the sidebar width, clamped to -1..1, and
// the neighbor it heads for (null at 0 or past either end).
struct SwipeState {
  double fraction = 0;
  std::optional<std::string> target_space_id;
};
SwipeState ComputeSwipe(double dx,
                        int sidebar_width,
                        const std::vector<std::string>& order,
                        std::string_view current);

}  // namespace clu

#endif  // CLU_BROWSER_SPACES_CLU_SPACE_RULES_H_
