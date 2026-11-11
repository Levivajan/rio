// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/command/clu_suggest_rules.h"

#include <algorithm>

#include "base/strings/string_util.h"

namespace clu {

namespace {

bool IsSpace(char16_t c) {
  return base::IsUnicodeWhitespace(c);
}

// /^[a-z][a-z0-9+.-]*:(\/\/|[^\s\d])/i
bool StartsWithScheme(std::u16string_view q) {
  if (q.empty() || !base::IsAsciiAlpha(q[0])) {
    return false;
  }
  size_t i = 1;
  while (i < q.size() && (base::IsAsciiAlphaNumeric(q[i]) || q[i] == u'+' ||
                          q[i] == u'.' || q[i] == u'-')) {
    ++i;
  }
  if (i >= q.size() || q[i] != u':' || i + 1 >= q.size()) {
    return false;
  }
  const std::u16string_view rest = q.substr(i + 1);
  return rest.starts_with(u"//") ||
         (!IsSpace(rest[0]) && !base::IsAsciiDigit(rest[0]));
}

// /\S:\S*@\S/
bool HasCredentials(std::u16string_view q) {
  for (size_t colon = 1; colon < q.size(); ++colon) {
    if (q[colon] != u':' || IsSpace(q[colon - 1])) {
      continue;
    }
    for (size_t k = colon + 1; k < q.size() && !IsSpace(q[k]); ++k) {
      if (q[k] == u'@' && k + 1 < q.size() && !IsSpace(q[k + 1])) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

bool ShouldSuggest(std::u16string_view query) {
  const std::u16string_view q = base::TrimWhitespace(query, base::TRIM_ALL);
  if (q.empty() || q.size() > kMaxSuggestQuery) {
    return false;
  }
  if (StartsWithScheme(q) || HasCredentials(q)) {
    return false;
  }
  const bool has_at = q.find(u'@') != std::u16string_view::npos;
  return !(has_at && std::ranges::none_of(q, IsSpace));
}

std::vector<std::u16string> CleanSuggestions(
    const std::vector<std::u16string>& raw) {
  std::vector<std::u16string> out;
  for (const std::u16string& item : raw) {
    std::u16string text = item;
    std::ranges::replace_if(
        text, [](char16_t c) { return base::IsUnicodeControl(c); }, u' ');
    const std::u16string trimmed(base::TrimWhitespace(text, base::TRIM_ALL));
    if (trimmed.empty() || trimmed.size() > kMaxSuggestionLength ||
        std::ranges::find(out, trimmed) != out.end()) {
      continue;
    }
    out.push_back(trimmed);
    if (out.size() == kMaxSuggestions) {
      break;
    }
  }
  return out;
}

}  // namespace clu
