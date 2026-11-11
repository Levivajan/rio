// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/bridge/validation.h"

#include <cmath>
#include <optional>

#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/third_party/icu/icu_utf.h"
#include "base/values.h"

namespace clu {

namespace {

// Number.MAX_SAFE_INTEGER.
constexpr double kMaxSafeInteger = 9007199254740991.0;
constexpr size_t kMaxJsonArrayItems = 1000;
constexpr size_t kMaxJsonKeyLength = 256;
// json() has no depth cap of its own; this bounds the recursion below.
constexpr size_t kMaxJsonDepth = 64;

bool IsHex(char c) {
  return base::IsHexDigit(c);
}

bool IsJsonValue(const base::Value& value) {
  switch (value.type()) {
    case base::Value::Type::NONE:
    case base::Value::Type::BOOLEAN:
    case base::Value::Type::INTEGER:
      return true;
    case base::Value::Type::DOUBLE:
      return std::isfinite(value.GetDouble());
    case base::Value::Type::STRING:
      return IsUtf16LengthAtMost(value.GetString(), kMaxUrlLength);
    case base::Value::Type::LIST: {
      const base::ListValue& list = value.GetList();
      if (list.size() > kMaxJsonArrayItems) {
        return false;
      }
      for (const base::Value& item : list) {
        if (!IsJsonValue(item)) {
          return false;
        }
      }
      return true;
    }
    case base::Value::Type::DICT:
      for (const auto [key, item] : value.GetDict()) {
        if (!IsUtf16LengthAtMost(key, kMaxJsonKeyLength) ||
            !IsJsonValue(item)) {
          return false;
        }
      }
      return true;
    case base::Value::Type::BINARY:
      return false;
  }
  return false;
}

}  // namespace

bool IsValidUtf8(std::string_view value) {
  return base::IsStringUTF8AllowingNoncharacters(value);
}

bool IsUtf16LengthAtMost(std::string_view value, size_t max) {
  // UTF-16 never needs more code units than UTF-8 needs bytes, and needs at least one unit per three bytes.
  if (value.size() <= max) {
    return IsValidUtf8(value);
  }
  if (value.size() > max * 3 + 3) {
    return false;
  }
  size_t units = 0;
  for (size_t i = 0; i < value.size(); ++i) {
    base_icu::UChar32 code_point;
    if (!base::ReadUnicodeCharacter(value, &i, &code_point)) {
      return false;
    }
    units += code_point > 0xFFFF ? 2 : 1;
    if (units > max) {
      return false;
    }
  }
  return true;
}

bool IsUuidV4(std::string_view value) {
  if (value.size() != 36) {
    return false;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') {
        return false;
      }
    } else if (!IsHex(c)) {
      return false;
    }
  }
  const char variant = base::ToLowerASCII(value[19]);
  return value[14] == '4' &&
         (variant == '8' || variant == '9' || variant == 'a' || variant == 'b');
}

bool IsWholeNumber(double value) {
  return std::isfinite(value) && std::trunc(value) == value &&
         std::fabs(value) <= kMaxSafeInteger;
}

bool IsJsonText(std::string_view text) {
  if (text.size() > kMaxJsonTextBytes) {
    return false;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC, kMaxJsonDepth);
  return value && IsJsonValue(*value);
}

}  // namespace clu
