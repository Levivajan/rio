// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_url_policy.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "clu/browser/ui/webui/bridge/validation.h"
#include "clu/common/clu_urls.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace clu {

namespace {

// Longer page URLs get no favicon: the favicon2 URL would carry the whole page URL again.
constexpr size_t kMaxFaviconPageUrlLength = 2048;

bool IsLoadableInnerUrl(const GURL& url) {
  if (!url.is_valid()) {
    return false;
  }
  if (url.SchemeIs(content::kChromeUIScheme)) {
    // Rio's settings page shows only in the app settings window (docs/chromium/parity-settings.md).
    return url.host() != kCluFrameHost && url.host() != kCluOverlayHost &&
           url.host() != kCluSettingsHost;
  }
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile() ||
         url.SchemeIs(url::kAboutScheme);
}

struct Alias {
  const char* clu_host;
  const char* chrome_host;
  // A page the alias opens (rio://about is the About page of Chromium's settings, chrome://settings/help).
  const char* path;
};
// rio://settings is Chromium's own settings (docs/chromium/parity-settings.md section 5); Rio's app settings are a
// window of their own with no address.
constexpr Alias kAliases[] = {
    {"settings", "settings", ""},
    {"history", "history", ""},
    {"extensions", "extensions", ""},
    {"about", "settings", "help"},
};
constexpr char kRioScheme[] = "rio://";
constexpr char kChromeScheme[] = "chrome://";

// The host of a "<scheme>://host/rest" spec and the rest from the first '/', '?' or '#' on.
std::pair<std::string_view, std::string_view> SplitHost(std::string_view rest) {
  const size_t end = rest.find_first_of("/?#");
  if (end == std::string_view::npos) {
    return {rest, std::string_view()};
  }
  return {rest.substr(0, end), rest.substr(end)};
}

}  // namespace

std::string DisplayUrl(std::string_view spec) {
  if (!spec.starts_with(kChromeScheme)) {
    return std::string(spec);
  }
  const auto [host, rest] = SplitHost(spec.substr(sizeof(kChromeScheme) - 1));
  for (const Alias& alias : kAliases) {
    // The page aliases are only ever typed; the page itself shows as rio://settings/help.
    if (host == alias.chrome_host && !*alias.path) {
      return base::StrCat({kRioScheme, alias.clu_host, rest.empty() ? "/" : rest});
    }
  }
  return std::string(spec);
}

GURL ResolveCluUrl(const GURL& url) {
  const std::string& spec = url.possibly_invalid_spec();
  if (!url.SchemeIs("rio") || !spec.starts_with(kRioScheme)) {
    return url;
  }
  const auto [host, rest] = SplitHost(std::string_view(spec).substr(sizeof(kRioScheme) - 1));
  for (const Alias& alias : kAliases) {
    if (base::EqualsCaseInsensitiveASCII(host, alias.clu_host)) {
      if (*alias.path) {
        return GURL(base::StrCat({kChromeScheme, alias.chrome_host, "/", alias.path}));
      }
      return GURL(base::StrCat({kChromeScheme, alias.chrome_host, rest.empty() ? "/" : rest}));
    }
  }
  return url;
}

bool IsTypedUrlAllowed(const GURL& url) {
  if (!url.is_valid() || url.spec().size() > kMaxUrlLength) {
    return false;
  }
  if (url.SchemeIs(content::kViewSourceScheme)) {
    const GURL inner(url.GetContent());
    return !inner.SchemeIs(content::kViewSourceScheme) &&
           IsLoadableInnerUrl(inner);
  }
  return IsLoadableInnerUrl(url);
}

bool IsWebUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() &&
         url.spec().size() <= kMaxUrlLength;
}

namespace {

constexpr size_t kMaxIconHtmlBytes = 512 * 1024;
constexpr size_t kMaxDeclaredIcons = 16;
constexpr int kMaxSuitableIconSize = 256;

bool IsTagSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '/';
}

// The attributes of one tag's body (the text between "<link" and ">"); names lowercased, values as written.
std::vector<std::pair<std::string, std::string>> TagAttributes(
    std::string_view tag) {
  std::vector<std::pair<std::string, std::string>> attributes;
  size_t i = 0;
  while (i < tag.size() && attributes.size() < 32) {
    while (i < tag.size() && IsTagSpace(tag[i])) {
      ++i;
    }
    const size_t name_start = i;
    while (i < tag.size() && !IsTagSpace(tag[i]) && tag[i] != '=') {
      ++i;
    }
    if (i == name_start) {
      ++i;
      continue;
    }
    std::string name =
        base::ToLowerASCII(tag.substr(name_start, i - name_start));
    while (i < tag.size() && tag[i] == ' ') {
      ++i;
    }
    std::string value;
    if (i < tag.size() && tag[i] == '=') {
      ++i;
      while (i < tag.size() && tag[i] == ' ') {
        ++i;
      }
      if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
        const char quote = tag[i++];
        const size_t end = tag.find(quote, i);
        const size_t stop = end == std::string_view::npos ? tag.size() : end;
        value = std::string(tag.substr(i, stop - i));
        i = stop + 1;
      } else {
        const size_t value_start = i;
        // An unquoted value ends at whitespace only: "href=/a.png" keeps its slash.
        while (i < tag.size() && (tag[i] == '/' || !IsTagSpace(tag[i]))) {
          ++i;
        }
        value = std::string(tag.substr(value_start, i - value_start));
      }
    }
    attributes.emplace_back(std::move(name), std::move(value));
  }
  return attributes;
}

// The largest width in a sizes attribute ("16x16 32x32"), 0 for "any", none or junk.
int LargestDeclaredSize(std::string_view sizes) {
  int largest = 0;
  for (std::string_view size : base::SplitStringPiece(
           sizes, " \t\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const size_t x = size.find_first_of("xX");
    int width = 0;
    if (x != std::string_view::npos &&
        base::StringToInt(size.substr(0, x), &width) && width > 0 &&
        width <= 4096) {
      largest = std::max(largest, width);
    }
  }
  return largest;
}

}  // namespace

std::vector<GURL> DeclaredIconUrls(std::string_view html,
                                   const GURL& base_url) {
  struct Icon {
    GURL url;
    int rank;
  };
  std::vector<Icon> icons;
  if (!base_url.is_valid() || !base_url.SchemeIsHTTPOrHTTPS()) {
    return {};
  }
  const std::string lower =
      base::ToLowerASCII(html.substr(0, kMaxIconHtmlBytes));
  const size_t head_end = std::min(lower.find("</head"), lower.size());
  size_t pos = 0;
  while (icons.size() < kMaxDeclaredIcons) {
    pos = lower.find("<link", pos);
    if (pos == std::string::npos || pos >= head_end) {
      break;
    }
    const size_t end = lower.find('>', pos);
    if (end == std::string::npos) {
      break;
    }
    const std::string_view tag = html.substr(pos + 5, end - pos - 5);
    pos = end;
    std::string rel, href, sizes, type;
    for (auto& [name, value] : TagAttributes(tag)) {
      if (name == "rel") {
        rel = base::ToLowerASCII(value);
      } else if (name == "href") {
        href = std::move(value);
      } else if (name == "sizes") {
        sizes = std::move(value);
      } else if (name == "type") {
        type = base::ToLowerASCII(value);
      }
    }
    bool icon = false;
    bool touch = false;
    for (std::string_view token : base::SplitStringPiece(
             rel, " \t\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      icon |= token == "icon";
      touch |= token == "apple-touch-icon" ||
               token == "apple-touch-icon-precomposed";
    }
    const std::string_view trimmed =
        base::TrimWhitespaceASCII(href, base::TRIM_ALL);
    if ((!icon && !touch) || trimmed.empty() ||
        type.find("svg") != std::string::npos) {
      continue;
    }
    const GURL url = base_url.Resolve(trimmed);
    if (!IsWebUrl(url) ||
        base::EndsWith(url.path(), ".svg",
                       base::CompareCase::INSENSITIVE_ASCII)) {
      continue;
    }
    // A touch icon without sizes is 180 px; a plain icon without one is a 16 px favicon.
    int size = LargestDeclaredSize(sizes);
    if (size == 0) {
      size = touch ? 180 : 16;
    }
    const int rank = size <= kMaxSuitableIconSize ? size : 1;
    if (std::none_of(icons.begin(), icons.end(),
                     [&](const Icon& other) { return other.url == url; })) {
      icons.push_back({url, rank});
    }
  }
  std::stable_sort(icons.begin(), icons.end(),
                   [](const Icon& a, const Icon& b) { return a.rank > b.rank; });
  std::vector<GURL> urls;
  for (Icon& icon : icons) {
    urls.push_back(std::move(icon.url));
  }
  return urls;
}

std::optional<std::string> FaviconUrlFor(const GURL& page_url,
                                         std::string_view icon_version) {
  if (!page_url.is_valid() || page_url.IsAboutBlank() ||
      page_url.spec().size() > kMaxFaviconPageUrlLength) {
    return std::nullopt;
  }
  std::string url = base::StrCat(
      {"chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=",
       base::EscapeQueryParamValue(page_url.spec(), /*use_plus=*/false),
       "&allowGoogleServerFallback=0"});
  if (!icon_version.empty()) {
    base::StrAppend(&url, {"&cluIcon=", base::EscapeQueryParamValue(
                                            icon_version, /*use_plus=*/false)});
  }
  return url;
}

std::string StateUrl(const GURL& url) {
  if (url.spec().size() <= kMaxUrlLength) {
    return DisplayUrl(url.spec());
  }
  return base::StrCat({url.scheme(), ":"});
}

}  // namespace clu
