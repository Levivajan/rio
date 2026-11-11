// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_COMMAND_CLU_COPY_URL_H_
#define CLU_BROWSER_COMMAND_CLU_COPY_URL_H_

#include <optional>
#include <string>
#include <string_view>

class GURL;

// Copy URL and Copy URL as Markdown (04-command F9, PLAN decision 58), with the tracking rules of stripTracking()
// in src/shared/url.ts.
namespace clu {

// The longest title a Markdown link keeps, in UTF-16 units.
inline constexpr size_t kMaxLinkTitle = 300;

// `url` without tracking parameters (utm_*, fbclid, gclid, ...): every other parameter keeps its order and raw
// encoding, and the fragment stays. A URL without a query comes back as its spec.
std::string StripTracking(const GURL& url);

// What Copy URL puts on the clipboard for a page at `url`: the stripped URL of an http or https page, nullopt for
// anything else (about:blank, Clu's and Chromium's own pages, files, a page with no URL yet).
std::optional<std::string> ShareableUrl(const GURL& url);

// `[title](url)`: backslashes and brackets in the title are escaped and whitespace runs collapse to one space;
// parentheses and whitespace in the URL are percent-encoded so the link target ends at the closing parenthesis. An
// empty title falls back to the URL.
std::string MarkdownLink(std::u16string_view title, std::string_view url);

}  // namespace clu

#endif  // CLU_BROWSER_COMMAND_CLU_COPY_URL_H_
