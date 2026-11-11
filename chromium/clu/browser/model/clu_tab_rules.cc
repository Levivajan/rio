// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tab_rules.h"

#include <array>
#include <string>
#include <vector>

#include "base/i18n/break_iterator.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "url/gurl.h"

namespace clu {

namespace {

constexpr size_t kMaxIconUnits = 32;
constexpr char16_t kVariationSelector16 = 0xFE0F;
constexpr char16_t kCombiningKeycap = 0x20E3;

// The union of the shell and command lists (PLAN 3.9). utm_* is matched by prefix.
constexpr std::array<std::string_view, 13> kTrackingParams = {
    "fbclid", "gclid",  "gclsrc", "dclid",  "msclkid", "mc_cid",  "mc_eid",
    "igshid", "yclid",  "twclid", "_hsenc", "_hsmi",   "ref_src"};

bool IsControl(char32_t c) {
  return c <= 0x1F || (c >= 0x7F && c <= 0x9F) || c == 0x2028 || c == 0x2029;
}

// The code points of `text`; a lone surrogate reads as itself, the way JS sees it.
std::vector<char32_t> CodePoints(std::u16string_view text) {
  std::vector<char32_t> out;
  size_t i = 0;
  while (i < text.size()) {
    char32_t c = text[i++];
    if (CBU16_IS_LEAD(c) && i < text.size() && CBU16_IS_TRAIL(text[i])) {
      c = CBU16_GET_SUPPLEMENTARY(c, text[i]);
      ++i;
    }
    out.push_back(c);
  }
  return out;
}

bool IsKeycap(const std::vector<char32_t>& cps) {
  if (cps.empty()) {
    return false;
  }
  const char32_t first = cps[0];
  const bool base = (first >= '0' && first <= '9') || first == '#' || first == '*';
  if (!base) {
    return false;
  }
  if (cps.size() == 2) {
    return cps[1] == kCombiningKeycap;
  }
  return cps.size() == 3 && cps[1] == kVariationSelector16 &&
         cps[2] == kCombiningKeycap;
}

// `value` capped at `max` UTF-16 units without splitting a surrogate pair.
std::u16string CapUnits(std::u16string value, size_t max) {
  if (value.size() <= max) {
    return value;
  }
  size_t length = max;
  if (length > 0 && CBU16_IS_LEAD(value[length - 1])) {
    --length;
  }
  value.resize(length);
  return value;
}

GURL Comparable(const GURL& url) {
  GURL::Replacements replacements;
  replacements.ClearRef();
  GURL out = url.ReplaceComponents(replacements);
  const std::string path(out.path());
  if (path.size() > 1 && path.back() == '/') {
    GURL::Replacements trim;
    const std::string trimmed = path.substr(0, path.size() - 1);
    trim.SetPathStr(trimmed);
    out = out.ReplaceComponents(trim);
  }
  return out;
}

bool IsTrackingParam(std::string_view key) {
  if (key.starts_with("utm_")) {
    return true;
  }
  for (std::string_view param : kTrackingParams) {
    if (key == param) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool IsOneEmoji(std::string_view value) {
  if (value.empty() || !base::IsStringUTF8(value)) {
    return false;
  }
  const std::u16string text = base::UTF8ToUTF16(value);
  if (text.empty() || text.size() > kMaxIconUnits) {
    return false;
  }
  base::i18n::BreakIterator graphemes(text,
                                      base::i18n::BreakIterator::BREAK_CHARACTER);
  if (!graphemes.Init()) {
    return false;
  }
  int count = 0;
  while (graphemes.Advance()) {
    ++count;
  }
  if (count != 1) {
    return false;
  }
  const std::vector<char32_t> cps = CodePoints(text);
  if (IsKeycap(cps)) {
    return true;
  }
  for (char32_t c : cps) {
    if (u_hasBinaryProperty(static_cast<UChar32>(c),
                            UCHAR_EXTENDED_PICTOGRAPHIC) ||
        u_hasBinaryProperty(static_cast<UChar32>(c),
                            UCHAR_REGIONAL_INDICATOR)) {
      return true;
    }
  }
  return false;
}

std::string CleanName(std::string_view raw) {
  const std::u16string text = base::UTF8ToUTF16(raw);
  std::u16string out;
  out.reserve(text.size());
  bool space = false;
  for (char16_t c : text) {
    const bool blank = IsControl(c) || base::IsUnicodeWhitespace(c);
    if (blank) {
      space = true;
      continue;
    }
    if (space && !out.empty()) {
      out.push_back(u' ');
    }
    space = false;
    out.push_back(c);
  }
  return base::UTF16ToUTF8(CapUnits(std::move(out), kMaxFolderName));
}

bool IsOffPinned(std::string_view url,
                 const std::optional<std::string>& pinned_url) {
  if (!pinned_url) {
    return false;
  }
  const GURL current{std::string(url)};
  const GURL pinned(*pinned_url);
  if (!current.is_valid() || !pinned.is_valid()) {
    return url != *pinned_url;
  }
  return Comparable(current) != Comparable(pinned);
}

bool IsRestorableTabUrl(std::string_view url) {
  return !url.empty() && IsTypedUrlAllowed(GURL(std::string(url)));
}

std::string CleanTitle(std::string_view title) {
  std::string valid(title);
  std::erase(valid, '\0');
  if (!base::IsStringUTF8(valid)) {
    valid = base::UTF16ToUTF8(base::UTF8ToUTF16(valid));
  }
  std::u16string text = base::UTF8ToUTF16(valid);
  std::u16string out;
  out.reserve(text.size());
  for (char16_t c : text) {
    out.push_back(IsControl(c) ? u' ' : c);
  }
  std::string result;
  base::TruncateUTF8ToByteSize(
      base::UTF16ToUTF8(base::TrimWhitespace(out, base::TRIM_ALL)),
      kMaxNodeTitle, &result);
  return result;
}

std::string LikePattern(std::string_view query) {
  std::string out = "%";
  for (char c : query) {
    if (c == '\\' || c == '%' || c == '_') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('%');
  return out;
}

std::string MarkdownLink(std::string_view title, std::string_view url) {
  std::string text;
  bool space = false;
  for (char c : title) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F || base::IsAsciiWhitespace(c)) {
      space = true;
      continue;
    }
    if (space && !text.empty()) {
      text.push_back(' ');
    }
    space = false;
    if (c == '\\' || c == '[' || c == ']') {
      text.push_back('\\');
    }
    text.push_back(c);
  }
  std::string href;
  for (char c : url) {
    if (c == '(') {
      href += "%28";
    } else if (c == ')') {
      href += "%29";
    } else {
      href.push_back(c);
    }
  }
  return base::StrCat({"[", text.empty() ? std::string(url) : text, "](",
                       href, ")"});
}

std::string StripTracking(std::string_view url) {
  const GURL gurl{std::string(url)};
  if (!gurl.is_valid() || !gurl.has_query()) {
    return std::string(url);
  }
  std::vector<std::string_view> kept;
  for (std::string_view part : base::SplitStringPiece(
           gurl.query(), "&", base::KEEP_WHITESPACE,
           base::SPLIT_WANT_NONEMPTY)) {
    const size_t eq = part.find('=');
    const std::string_view key =
        eq == std::string_view::npos ? part : part.substr(0, eq);
    if (!IsTrackingParam(key)) {
      kept.push_back(part);
    }
  }
  GURL::Replacements replacements;
  const std::string query = base::JoinString(kept, "&");
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return gurl.ReplaceComponents(replacements).spec();
}

std::string DisplayHost(std::string_view url) {
  const GURL gurl{std::string(url)};
  if (!gurl.is_valid() || !gurl.has_host()) {
    return std::string(url);
  }
  std::string host(gurl.host());
  if (host.starts_with("www.")) {
    host = host.substr(4);
  }
  return host;
}

}  // namespace clu
