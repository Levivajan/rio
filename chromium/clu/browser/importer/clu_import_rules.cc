// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_rules.h"

#include <algorithm>

#include "base/i18n/char_iterator.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "url/gurl.h"

namespace clu {

namespace {

// [\p{Cc}\p{Cf}\p{Zl}\p{Zp}] in sanitizeTitle.
bool IsJunkChar(UChar32 c) {
  const int8_t type = u_charType(c);
  return type == U_CONTROL_CHAR || type == U_FORMAT_CHAR ||
         type == U_LINE_SEPARATOR || type == U_PARAGRAPH_SEPARATOR;
}

}  // namespace

std::optional<std::string> ImportUrl(std::string_view raw) {
  if (raw.size() > kMaxImportUrl) {
    return std::nullopt;
  }
  const GURL url(raw);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      url.spec().size() > kMaxImportUrl) {
    return std::nullopt;
  }
  return url.spec();
}

std::optional<std::string> ImportUrl(const base::Value* raw) {
  if (!raw || !raw->is_string()) {
    return std::nullopt;
  }
  return ImportUrl(raw->GetString());
}

std::string ImportTitle(std::string_view raw) {
  // Long junk is cut before the scan, as sanitizeTitle slices at four times the cap.
  std::string text(raw.substr(0, kMaxImportTitle * 4 * 3));
  if (!base::IsStringUTF8(text)) {
    text = base::UTF16ToUTF8(base::UTF8ToUTF16(text));
  }
  const std::u16string wide = base::UTF8ToUTF16(text);
  std::u16string out;
  out.reserve(wide.size());
  bool space = false;
  for (base::i18n::UTF16CharIterator it(wide); !it.end(); it.Advance()) {
    const UChar32 c = it.get();
    if (IsJunkChar(c) || u_isUWhiteSpace(c)) {
      space = !out.empty();
      continue;
    }
    if (space) {
      out.push_back(u' ');
      space = false;
    }
    base::WriteUnicodeCharacter(static_cast<base_icu::UChar32>(c), &out);
  }
  if (out.size() > kMaxImportTitle) {
    size_t length = kMaxImportTitle;
    if (CBU16_IS_LEAD(out[length - 1])) {
      --length;
    }
    out.resize(length);
  }
  return base::UTF16ToUTF8(out);
}

std::string ImportTitle(const base::Value* raw) {
  return raw && raw->is_string() ? ImportTitle(raw->GetString())
                                 : std::string();
}

std::string ImportHost(std::string_view url) {
  const GURL parsed(url);
  if (parsed.is_valid() && parsed.has_host()) {
    return std::string(parsed.host());
  }
  return std::string(url);
}

bool IsBrokenImportName(std::string_view name) {
  constexpr std::string_view kImported = "Imported";
  if (!name.starts_with(kImported)) {
    return false;
  }
  name.remove_prefix(kImported.size());
  if (name.empty()) {
    return true;
  }
  if (name.size() < 2 || name.size() > 4 || name.front() != ' ') {
    return false;
  }
  name.remove_prefix(1);
  return std::all_of(name.begin(), name.end(),
                     [](char c) { return base::IsAsciiDigit(c); });
}

}  // namespace clu
