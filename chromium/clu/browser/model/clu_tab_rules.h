// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_TAB_RULES_H_
#define CLU_BROWSER_MODEL_CLU_TAB_RULES_H_

#include <optional>
#include <string>
#include <string_view>

// Small rules of the tabs module, ported from src/shared/tabs.ts, src/shared/url.ts and src/main/tabs/service.ts with
// the same test cases. Pure, so clu_unittests covers them without a browser.
namespace clu {

// isOneEmoji(): exactly one emoji grapheme of at most 32 UTF-16 units (custom icons, tabs 0201). A keycap (1️⃣)
// counts; "ab", two emoji and plain text do not.
bool IsOneEmoji(std::string_view value);

// cleanName() (F3 rename): control characters become spaces, runs of white space collapse, then trimmed and capped
// at 200 UTF-16 units without splitting a surrogate pair.
std::string CleanName(std::string_view raw);

// isOffPinned() (F2): the URL differs from the pinned URL after dropping the fragment and a trailing slash.
bool IsOffPinned(std::string_view url,
                 const std::optional<std::string>& pinned_url);

// A URL a tab row may hold and Clu may load into a tab again: the typed-URL rule of clu_url_policy.h (web pages, files, Chromium's internal pages, about:), never script or data URLs nor Clu's own WebUIs.
bool IsRestorableTabUrl(std::string_view url);

// A stored page title: control characters dropped, capped at kMaxNodeTitle bytes on a UTF-8 boundary.
std::string CleanTitle(std::string_view title);

// LIKE pattern matching `query` literally, for `LIKE ? ESCAPE '\'` (archive search: % and _ match themselves).
std::string LikePattern(std::string_view query);

// One Markdown link line (Copy URLs as Markdown): brackets and backslashes in the title are escaped, parentheses in
// the URL encoded. An empty title shows the URL.
std::string MarkdownLink(std::string_view title, std::string_view url);

// stripTracking() (PLAN 3.9): the URL without utm_*, fbclid, gclid and the other tracking parameters. A URL that does
// not parse comes back as it was.
std::string StripTracking(std::string_view url);

// displayHost(): the host without "www.", or the URL itself when it has none.
std::string DisplayHost(std::string_view url);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_TAB_RULES_H_
