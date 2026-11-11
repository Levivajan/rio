// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_instant_link.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "url/third_party/mozilla/url_parse.h"

namespace clu {

namespace {

// Longer addresses are never followed (the bar caps what it sends far below this).
constexpr size_t kMaxUrlLength = 8 * 1024;

// www.google.<tld> or google.<tld> over https, where the country domain is one or two short labels (com, de, co.uk,
// com.br): the hosts the profile's Google engine searches on.
bool IsGoogleHost(const GURL& url) {
  if (!url.SchemeIs("https") || url.spec().size() > kMaxUrlLength) {
    return false;
  }
  std::string_view host = url.host();
  if (base::StartsWith(host, "www.")) {
    host.remove_prefix(4);
  }
  if (!base::StartsWith(host, "google.")) {
    return false;
  }
  host.remove_prefix(7);
  const std::vector<std::string_view> labels = base::SplitStringPiece(
      host, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (labels.empty() || labels.size() > 2) {
    return false;
  }
  for (std::string_view label : labels) {
    if (label.size() < 2 || label.size() > 3 ||
        !std::ranges::all_of(label, base::IsAsciiAlpha<char>)) {
      return false;
    }
  }
  return true;
}

// The first non-empty value of `name` in the URL's query, unescaped.
std::optional<std::string> QueryValue(const GURL& url, std::string_view name) {
  const std::string& spec = url.spec();
  url::Component query = url.parsed_for_possibly_invalid_spec().query;
  url::Component key;
  url::Component value;
  while (url::ExtractQueryKeyValue(spec, &query, &key, &value)) {
    if (std::string_view(spec).substr(key.begin, key.len) == name &&
        value.is_nonempty()) {
      return base::UnescapeURLComponent(
          std::string_view(spec).substr(value.begin, value.len),
          base::UnescapeRule::NORMAL | base::UnescapeRule::SPACES |
              base::UnescapeRule::PATH_SEPARATORS |
              base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
              base::UnescapeRule::REPLACE_PLUS_WITH_SPACE);
    }
  }
  return std::nullopt;
}

}  // namespace

bool IsInstantLinkSearch(const GURL& url) {
  return url.is_valid() && IsGoogleHost(url) && url.path() == "/search" &&
         QueryValue(url, "btnI").has_value();
}

std::optional<GURL> InstantLinkTarget(const GURL& start, const GURL& redirect) {
  if (!IsInstantLinkSearch(start) || !redirect.is_valid() ||
      !IsGoogleHost(redirect) || redirect.path() != "/url") {
    return std::nullopt;
  }
  std::optional<std::string> value = QueryValue(redirect, "q");
  if (!value) {
    value = QueryValue(redirect, "url");
  }
  if (!value || value->size() > kMaxUrlLength) {
    return std::nullopt;
  }
  const GURL target(*value);
  if (!target.is_valid() || !target.SchemeIsHTTPOrHTTPS() ||
      !target.has_host()) {
    return std::nullopt;
  }
  return target;
}

}  // namespace clu
