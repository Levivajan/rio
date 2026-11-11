// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tidy.h"

#include <algorithm>
#include <array>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace clu {

namespace {

// Separators between a page's parts, as sites write them. Replaced by a unit separator, then split on it.
constexpr std::array<std::string_view, 9> kSeparators = {
    " - ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", " | ", " \xC2\xB7 ",
    " \xE2\x80\xA2 ", " :: ", " \xC2\xBB ", " ~ "};
constexpr char kUnit = '\x1f';

// Host labels that never name the site.
constexpr std::array<std::string_view, 9> kHostNoise = {
    "www", "m", "app", "web", "com", "org", "net", "co", "io"};

// Names a download gets from a site that says nothing about the file.
constexpr std::array<std::string_view, 12> kJunkNames = {
    "download", "file",   "document", "untitled", "image",  "attachment",
    "export",   "report", "invoice",  "doc",      "blob",   "unknown"};

std::string Lower(std::string_view s) {
  return base::ToLowerASCII(s);
}

// Letters and digits only, lowercased (ASCII), for comparing a part with the host.
std::string Squash(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (base::IsAsciiAlphaNumeric(c)) {
      out.push_back(base::ToLowerASCII(c));
    }
  }
  return out;
}

// The site's name tokens: host labels without www, the top-level domain and other noise ("docs", "google").
std::vector<std::string> SiteTokens(const GURL& url) {
  std::vector<std::string> tokens;
  if (!url.is_valid() || !url.has_host()) {
    return tokens;
  }
  std::vector<std::string> labels =
      base::SplitString(Lower(url.HostNoBracketsPiece()), ".",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (labels.size() > 1) {
    labels.pop_back();  // the top-level domain
  }
  for (std::string& label : labels) {
    std::string squashed = Squash(label);
    if (squashed.size() >= 3 &&
        std::find(kHostNoise.begin(), kHostNoise.end(), squashed) ==
            kHostNoise.end()) {
      tokens.push_back(std::move(squashed));
    }
  }
  return tokens;
}

// A part that names the site: "Google Docs" on docs.google.com, "GitHub" on github.com, "Gmail" on mail.google.com.
bool IsSitePart(std::string_view part,
                const std::vector<std::string>& tokens) {
  const std::string squashed = Squash(part);
  if (squashed.empty()) {
    return true;
  }
  for (const std::string& token : tokens) {
    if (squashed == token ||
        (squashed.size() <= 24 && squashed.find(token) != std::string::npos) ||
        (squashed.size() >= 3 && token.find(squashed) != std::string::npos)) {
      return true;
    }
  }
  return false;
}

// "(3) Inbox" and "Inbox (1,234)": an unread count in parentheses at either end goes.
std::string DropCounts(std::string s) {
  auto is_count = [](std::string_view inner) {
    if (inner.empty()) {
      return false;
    }
    for (char c : inner) {
      if (!base::IsAsciiDigit(c) && c != ',' && c != '.' && c != '+') {
        return false;
      }
    }
    return true;
  };
  if (s.size() > 2 && s.front() == '(') {
    const size_t close = s.find(')');
    if (close != std::string::npos &&
        is_count(std::string_view(s).substr(1, close - 1))) {
      s = std::string(base::TrimWhitespaceASCII(
          std::string_view(s).substr(close + 1), base::TRIM_ALL));
    }
  }
  if (s.size() > 2 && s.back() == ')') {
    const size_t open = s.rfind('(');
    if (open != std::string::npos && open > 0 &&
        is_count(std::string_view(s).substr(open + 1, s.size() - open - 2))) {
      s = std::string(base::TrimWhitespaceASCII(
          std::string_view(s).substr(0, open), base::TRIM_ALL));
    }
  }
  return s;
}

// At most `max` UTF-16 units, cut at the last space when one falls in the second half; never inside a pair.
std::string CutAtWord(std::string_view s, size_t max) {
  std::u16string text = base::UTF8ToUTF16(s);
  if (text.size() <= max) {
    return std::string(s);
  }
  size_t end = max;
  if (end > 0 && text[end - 1] >= 0xD800 && text[end - 1] <= 0xDBFF) {
    --end;
  }
  const size_t space = text.rfind(u' ', end);
  if (space != std::u16string::npos && space >= max / 2) {
    end = space;
  }
  text.resize(end);
  const std::string out = base::UTF16ToUTF8(text);
  return std::string(base::TrimWhitespaceASCII(out, base::TRIM_ALL));
}

std::string Clean(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c);
  }
  return base::CollapseWhitespaceASCII(out, /*trim_sequences_with_line_breaks=*/true);
}

}  // namespace

std::optional<std::string> TidyPinnedTitle(std::string_view title,
                                           const GURL& url) {
  if (!base::IsStringUTF8(title)) {
    return std::nullopt;
  }
  const std::string original = Clean(title);
  std::string text = DropCounts(original);
  if (text.empty()) {
    return std::nullopt;
  }
  // An address as the title (a page without one) stays: there is nothing tidier to say.
  if (url.is_valid() &&
      (text == url.spec() || Lower(text) == Lower(url.HostNoBracketsPiece()))) {
    return std::nullopt;
  }
  for (std::string_view sep : kSeparators) {
    base::ReplaceSubstringsAfterOffset(&text, 0, sep, std::string(1, kUnit));
  }
  const std::vector<std::string> parts = base::SplitString(
      text, std::string(1, kUnit), base::TRIM_WHITESPACE,
      base::SPLIT_WANT_NONEMPTY);
  const std::vector<std::string> tokens = SiteTokens(url);
  std::string chosen;
  for (const std::string& part : parts) {
    if (!IsSitePart(part, tokens)) {
      chosen = part;
      break;
    }
  }
  if (chosen.empty()) {
    // Only the site's name: nothing tidier to say.
    return std::nullopt;
  }
  chosen = DropCounts(std::move(chosen));
  if (base::EndsWith(chosen, "...")) {
    chosen.resize(chosen.size() - 3);
  } else if (base::EndsWith(chosen, "\xE2\x80\xA6")) {
    chosen.resize(chosen.size() - 3);
  }
  chosen = CutAtWord(
      base::TrimWhitespaceASCII(chosen, base::TRIM_ALL), kMaxTidyTitle);
  if (chosen.empty() || chosen == original) {
    return std::nullopt;
  }
  return chosen;
}

std::optional<std::string> TidyDownloadName(std::string_view file_name,
                                            std::string_view page_title,
                                            const GURL& page_url) {
  if (!base::IsStringUTF8(file_name) || file_name.empty()) {
    return std::nullopt;
  }
  // The extension: up to 8 letters or digits after the last dot.
  std::string_view stem = file_name;
  std::string_view ext;
  const size_t dot = file_name.rfind('.');
  if (dot != std::string_view::npos && dot > 0 &&
      file_name.size() - dot - 1 <= 8 && file_name.size() - dot - 1 > 0) {
    bool ok = true;
    for (char c : file_name.substr(dot + 1)) {
      ok = ok && base::IsAsciiAlphaNumeric(c);
    }
    if (ok) {
      stem = file_name.substr(0, dot);
      ext = file_name.substr(dot);
    }
  }
  // A copy number goes before the junk test: "file (2)", "download-3", "export_12".
  std::string base_stem = Lower(base::TrimWhitespaceASCII(stem, base::TRIM_ALL));
  if (base_stem.size() > 2 && base_stem.back() == ')') {
    const size_t open = base_stem.rfind('(');
    if (open != std::string::npos) {
      base_stem = std::string(base::TrimWhitespaceASCII(
          std::string_view(base_stem).substr(0, open), base::TRIM_ALL));
    }
  }
  const std::string word = [&] {
    std::string w = base_stem;
    while (!w.empty() && (base::IsAsciiDigit(w.back()) || w.back() == '-' ||
                          w.back() == '_' || w.back() == ' ')) {
      w.pop_back();
    }
    return w;
  }();
  bool junk = std::find(kJunkNames.begin(), kJunkNames.end(), word) !=
              kJunkNames.end();
  if (!junk && base_stem.size() >= 6) {
    // Digits, hex and id punctuation only, with at least one digit.
    bool id_like = true;
    bool digit = false;
    for (char c : base_stem) {
      digit = digit || base::IsAsciiDigit(c);
      id_like = id_like && (base::IsHexDigit(c) || c == '-' || c == '_');
    }
    junk = id_like && digit;
  }
  if (!junk) {
    return std::nullopt;
  }
  std::string name = TidyPinnedTitle(page_title, page_url)
                         .value_or(DropCounts(Clean(page_title)));
  // Characters a file name may not hold on macOS or in Chromium's download path checks.
  for (char& c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|') {
      c = ' ';
    }
  }
  name = base::CollapseWhitespaceASCII(name, true);
  const std::string trimmed(base::TrimString(name, " .", base::TRIM_ALL));
  name = trimmed;
  name = CutAtWord(name, kMaxTidyDownloadStem);
  if (name.empty() || !base::IsStringUTF8(name)) {
    return std::nullopt;
  }
  std::string out = name + std::string(ext);
  if (out == file_name) {
    return std::nullopt;
  }
  return out;
}

std::optional<std::vector<int>> TidyOrderFromModel(std::string_view answer,
                                                   int count) {
  if (count <= 0 || count > 500 || answer.size() > 8000) {
    return std::nullopt;
  }
  std::vector<std::string> topics;
  std::vector<std::vector<int>> groups;
  std::vector<bool> seen(count, false);
  for (const std::string_view item : base::SplitStringPiece(
           answer, "|", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const size_t colon = item.find(':');
    int n = 0;
    if (colon == std::string_view::npos ||
        !base::StringToInt(
            base::TrimWhitespaceASCII(item.substr(0, colon), base::TRIM_ALL),
            &n) ||
        n < 1 || n > count || seen[n - 1]) {
      return std::nullopt;
    }
    // The model sometimes ends its line with a period.
    const std::string topic = base::ToLowerASCII(base::TrimString(
        base::TrimWhitespaceASCII(item.substr(colon + 1), base::TRIM_ALL), ".",
        base::TRIM_TRAILING));
    if (topic.empty()) {
      return std::nullopt;
    }
    seen[n - 1] = true;
    const auto at = std::ranges::find(topics, topic);
    if (at == topics.end()) {
      topics.push_back(topic);
      groups.push_back({n - 1});
    } else {
      groups[at - topics.begin()].push_back(n - 1);
    }
  }
  // One topic, or a topic per tab (the model copied the titles): nothing grouped, so the rules tidy instead.
  if (groups.size() < 2 ||
      std::ranges::none_of(groups, [](const auto& g) { return g.size() > 1; })) {
    return std::nullopt;
  }
  for (std::vector<int>& group : groups) {
    std::ranges::sort(group);
  }
  std::ranges::stable_sort(groups, [](const auto& a, const auto& b) {
    return a.size() > b.size();
  });
  std::vector<int> order;
  for (const std::vector<int>& group : groups) {
    order.insert(order.end(), group.begin(), group.end());
  }
  for (int i = 0; i < count; ++i) {
    if (!seen[i]) {
      order.push_back(i);
    }
  }
  return order;
}

}  // namespace clu
