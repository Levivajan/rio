// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_COMMAND_CLU_SUGGEST_RULES_H_
#define CLU_BROWSER_COMMAND_CLU_SUGGEST_RULES_H_

#include <string>
#include <string_view>
#include <vector>

// Search suggestion rules (04-command F7). Chromium's AutocompleteController fetches the suggestions; these say what
// may leave the machine and what the bar lists.
namespace clu {

// Suggestions the bar gets at most, and the longest one kept, in UTF-16 units.
inline constexpr size_t kMaxSuggestions = 6;
inline constexpr size_t kMaxSuggestionLength = 256;
// The longest input sent to the engine (MAX_SUGGEST_QUERY in src/shared/command/limits.ts).
inline constexpr size_t kMaxSuggestQuery = 256;

// False for input that must not go to the engine: empty, too long, an explicit scheme (https://, file:,
// javascript:; `localhost:8080` and `c++: x` are not schemes), credentials before a host (user:pw@host), or a single
// word with an @ in it (an email address).
bool ShouldSuggest(std::u16string_view query);

// The engine's strings as the bar lists them: control characters read as spaces, trimmed, non-empty, at most
// kMaxSuggestionLength units, no duplicates, at most kMaxSuggestions.
std::vector<std::u16string> CleanSuggestions(
    const std::vector<std::u16string>& raw);

}  // namespace clu

#endif  // CLU_BROWSER_COMMAND_CLU_SUGGEST_RULES_H_
