// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/command/clu_copy_url.h"

#include <vector>

#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace clu {

namespace {

constexpr std::string_view kTrackingKeys[] = {
    "fbclid", "gclid",  "gclsrc", "dclid",  "msclkid", "mc_cid", "mc_eid",
    "igshid", "yclid",  "twclid", "_hsenc", "_hsmi",   "ref_src",
};

bool IsTrackingKey(std::string_view raw_key) {
  // The key as a server reads it: '+' is a space and %XX escapes are decoded, so %75tm_source is caught too.
  const std::string key = base::ToLowerASCII(base::UnescapeURLComponent(
      raw_key, base::UnescapeRule::NORMAL | base::UnescapeRule::SPACES |
                   base::UnescapeRule::PATH_SEPARATORS |
                   base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
                   base::UnescapeRule::REPLACE_PLUS_WITH_SPACE));
  if (key.starts_with("utm_")) {
    return true;
  }
  for (std::string_view tracking : kTrackingKeys) {
    if (key == tracking) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string StripTracking(const GURL& url) {
  if (!url.is_valid() || !url.has_query()) {
    return url.is_valid() ? url.spec() : std::string();
  }
  std::vector<std::string_view> kept;
  for (std::string_view pair : base::SplitStringPiece(
           url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (pair.empty()) {
      continue;
    }
    const size_t eq = pair.find('=');
    if (IsTrackingKey(pair.substr(0, eq))) {
      continue;
    }
    kept.push_back(pair);
  }
  const std::string query = base::JoinString(kept, "&");
  GURL::Replacements replacements;
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements).spec();
}

std::optional<std::string> ShareableUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    return std::nullopt;
  }
  return StripTracking(url);
}

std::string MarkdownLink(std::u16string_view title, std::string_view url) {
  // Whitespace runs collapse to one space, then the ends are trimmed and the title is cut at kMaxLinkTitle units
  // (never inside a surrogate pair).
  std::u16string text;
  bool space = false;
  for (char16_t c : title) {
    if (base::IsUnicodeWhitespace(c)) {
      space = true;
      continue;
    }
    if (space && !text.empty()) {
      text.push_back(u' ');
    }
    space = false;
    text.push_back(c);
  }
  if (text.size() > kMaxLinkTitle) {
    size_t cut = kMaxLinkTitle;
    // A lead surrogate at the cut would be left without its pair.
    if ((text[cut - 1] & 0xFC00) == 0xD800) {
      --cut;
    }
    text.resize(cut);
  }
  const std::string shown = text.empty() ? std::string(url)
                                         : base::UTF16ToUTF8(text);

  std::string link = "[";
  for (char c : shown) {
    if (c == '\\' || c == '[' || c == ']') {
      link.push_back('\\');
    }
    link.push_back(c);
  }
  link += "](";
  for (char c : url) {
    if (c == '(' || c == ')' || base::IsAsciiWhitespace(c)) {
      base::StringAppendF(&link, "%%%02X", static_cast<unsigned char>(c));
    } else {
      link.push_back(c);
    }
  }
  link.push_back(')');
  return link;
}

}  // namespace clu
