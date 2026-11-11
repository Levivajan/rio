// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/bridge/validation.h"

#include <limits>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

TEST(CluValidationTest, UuidV4MatchesTheZodRegexInEitherCase) {
  EXPECT_TRUE(IsUuidV4("00000000-0000-4000-8000-000500000015"));
  EXPECT_TRUE(IsUuidV4("0A1B2C3D-4E5F-4A6B-9C7D-8E9FA0B1C2D3"));
  EXPECT_FALSE(IsUuidV4(""));
  EXPECT_FALSE(IsUuidV4("00000000-0000-1000-8000-000000000000"));  // v1
  EXPECT_FALSE(IsUuidV4("00000000-0000-4000-c000-000000000000"));  // variant
  EXPECT_FALSE(IsUuidV4("00000000-0000-4000-8000-00000000000"));   // short
  EXPECT_FALSE(IsUuidV4("00000000-0000-4000-8000-0000000000000"));
  EXPECT_FALSE(IsUuidV4("00000000_0000-4000-8000-000000000000"));
  EXPECT_FALSE(IsUuidV4("0000000g-0000-4000-8000-000000000000"));
}

TEST(CluValidationTest, LengthsCountUtf16CodeUnitsLikeJs) {
  EXPECT_TRUE(IsUtf16LengthAtMost("", 0));
  EXPECT_TRUE(IsUtf16LengthAtMost("abc", 3));
  EXPECT_FALSE(IsUtf16LengthAtMost("abcd", 3));
  // U+00E9 is 2 UTF-8 bytes and 1 UTF-16 unit.
  EXPECT_TRUE(IsUtf16LengthAtMost("\xC3\xA9\xC3\xA9", 2));
  // U+1F600 is 4 UTF-8 bytes and 2 UTF-16 units.
  EXPECT_TRUE(IsUtf16LengthAtMost("\xF0\x9F\x98\x80", 2));
  EXPECT_FALSE(IsUtf16LengthAtMost("\xF0\x9F\x98\x80", 1));
  // U+FFFF is a noncharacter, legal in a JS string.
  EXPECT_TRUE(IsUtf16LengthAtMost("\xEF\xBF\xBF", 1));
  // Broken UTF-8 never passes, short or long.
  EXPECT_FALSE(IsUtf16LengthAtMost("\xC3", 5));
  EXPECT_FALSE(IsUtf16LengthAtMost(std::string(10, 'a') + "\xFF", 100));
  EXPECT_FALSE(IsUtf16LengthAtMost(std::string(1000, 'a'), 10));
}

TEST(CluValidationTest, WholeNumbersStayInsideSafeIntegers) {
  EXPECT_TRUE(IsWholeNumber(0));
  EXPECT_TRUE(IsWholeNumber(9007199254740991.0));
  EXPECT_FALSE(IsWholeNumber(9007199254740992.0));
  EXPECT_FALSE(IsWholeNumber(1.5));
  EXPECT_FALSE(IsWholeNumber(std::numeric_limits<double>::quiet_NaN()));
  EXPECT_FALSE(IsWholeNumber(std::numeric_limits<double>::infinity()));
}

TEST(CluValidationTest, JsonTextFollowsTheJsonSchemaCaps) {
  EXPECT_TRUE(IsJsonText("null"));
  EXPECT_TRUE(IsJsonText(R"({"a":[1,2,"x"],"b":{"c":true}})"));
  EXPECT_FALSE(IsJsonText(""));
  EXPECT_FALSE(IsJsonText("{"));
  EXPECT_FALSE(IsJsonText("[1,]"));
  std::string long_array = "[";
  for (int i = 0; i < 1001; ++i) {
    long_array += i ? ",0" : "0";
  }
  EXPECT_FALSE(IsJsonText(long_array + "]"));
  EXPECT_FALSE(IsJsonText("{\"" + std::string(257, 'k') + "\":1}"));
  EXPECT_FALSE(IsJsonText("\"" + std::string(kMaxUrlLength + 1, 's') + "\""));
  EXPECT_FALSE(IsJsonText(std::string(kMaxJsonTextBytes + 1, ' ') + "1"));
  EXPECT_FALSE(IsJsonText(std::string(100, '[') + std::string(100, ']')));
}

}  // namespace
}  // namespace clu
