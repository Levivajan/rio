// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_rules.h"

#include <string>

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

TEST(CluImportRulesTest, KeepsOnlyHttpAndHttpsUrlsUpTo8192Characters) {
  EXPECT_EQ(ImportUrl("https://harbor.example"), "https://harbor.example/");
  EXPECT_EQ(ImportUrl("http://meadow.example/a?b#c"), "http://meadow.example/a?b#c");
  for (const char* junk : {"javascript:alert(1)", "chrome://settings/", "file:///etc/passwd",
                           "data:text/html,x", "not a url", ""}) {
    EXPECT_EQ(ImportUrl(junk), std::nullopt) << junk;
  }
  const std::string longest = "https://a.example/" + std::string(8192 - 18, 'x');
  EXPECT_TRUE(ImportUrl(longest));
  EXPECT_EQ(ImportUrl(longest + "x"), std::nullopt);
  const base::Value number(42);
  EXPECT_EQ(ImportUrl(&number), std::nullopt);
  EXPECT_EQ(ImportUrl(static_cast<const base::Value*>(nullptr)), std::nullopt);
}

TEST(CluImportRulesTest, TitlesLoseControlsCollapseSpacesAndCapAt1024) {
  EXPECT_EQ(ImportTitle("  Harbor \t\n lights  "), "Harbor lights");
  EXPECT_EQ(ImportTitle(std::string("a\0b\x07" "c", 5)), "a b c");
  // Bidi and other format characters go too.
  EXPECT_EQ(ImportTitle("‮evil"), "evil");
  EXPECT_EQ(ImportTitle(std::string(5000, 'x')), std::string(1024, 'x'));
  EXPECT_EQ(ImportTitle("   "), "");
  EXPECT_EQ(ImportTitle("\xff\xfe"), "��");
  const base::Value number(7);
  EXPECT_EQ(ImportTitle(&number), "");
  // A cut never splits a surrogate pair.
  const std::string emoji = std::string(1023, 'x') + "\U0001F600";
  EXPECT_EQ(ImportTitle(emoji), std::string(1023, 'x'));
}

TEST(CluImportRulesTest, HostOfAUrlOrTheTextItself) {
  EXPECT_EQ(ImportHost("https://kettle.example/notes"), "kettle.example");
  EXPECT_EQ(ImportHost("junk"), "junk");
}

}  // namespace
// The names a broken earlier run gave its profiles, and nothing a user would type.
TEST(CluImportRulesTest, BrokenImportNamesAreImportedAndImportedN) {
  EXPECT_TRUE(IsBrokenImportName("Imported"));
  EXPECT_TRUE(IsBrokenImportName("Imported 2"));
  EXPECT_TRUE(IsBrokenImportName("Imported 10"));
  EXPECT_FALSE(IsBrokenImportName("Imported photos"));
  EXPECT_FALSE(IsBrokenImportName("Imported  2"));
  EXPECT_FALSE(IsBrokenImportName("Imported 2 "));
  EXPECT_FALSE(IsBrokenImportName("imported"));
  EXPECT_FALSE(IsBrokenImportName("Personal"));
  EXPECT_FALSE(IsBrokenImportName(""));
}

}  // namespace clu
