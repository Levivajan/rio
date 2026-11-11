// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/theme/clu_space_themes.h"

#include <set>
#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

// The repository and preview rules of the space themes: a corrupt row reads as the default theme, previews never
// persist and revert on close, a reset or a deleted space drops the theme.
namespace clu {
namespace {

constexpr char kA[] = "11111111-1111-4111-8111-111111111111";
constexpr char kB[] = "22222222-2222-4222-8222-222222222222";
constexpr char kGone[] = "33333333-3333-4333-8333-333333333333";

SpaceTheme Theme(double h, double s = 0.5) {
  SpaceTheme theme;
  theme.colors.push_back({h, s});
  return theme;
}

base::DictValue Stored(const char* json) {
  return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC).value();
}

TEST(CluSpaceThemesTest, JunkRowsAndUnknownSpacesReadAsTheDefault) {
  CluSpaceThemes themes;
  const std::string json = std::string("{\"") + kA +
                           R"(":{"colors":[{"h":10,"s":0.5}],"intensity":0.5,"grain":0},")" +
                           kB + R"(":{"colors":"junk","intensity":0.5,"grain":0},")" + kGone +
                           R"(":{"colors":[{"h":20,"s":0.5}],"intensity":0.5,"grain":0},"x":7})";
  EXPECT_EQ(themes.Load(Stored(json.c_str()), {kA, kB}), 3);
  EXPECT_EQ(themes.Shown(kA), Theme(10));
  EXPECT_EQ(themes.Shown(kB), std::nullopt);
  EXPECT_EQ(themes.Shown(kGone), std::nullopt);
  // The repaired value holds only the good row.
  EXPECT_EQ(themes.Value().size(), 1u);
  EXPECT_TRUE(themes.Value().contains(kA));
}

TEST(CluSpaceThemesTest, SetPersistsAndResetDrops) {
  CluSpaceThemes themes;
  EXPECT_TRUE(themes.Set(kA, Theme(100)));
  EXPECT_FALSE(themes.Set(kA, Theme(100)));
  EXPECT_EQ(themes.Committed(kA), Theme(100));
  EXPECT_EQ(ParseSpaceTheme(*themes.Value().Find(kA)), Theme(100));
  EXPECT_TRUE(themes.Reset(kA));
  EXPECT_FALSE(themes.Reset(kA));
  EXPECT_EQ(themes.Shown(kA), std::nullopt);
  EXPECT_TRUE(themes.Value().empty());
}

TEST(CluSpaceThemesTest, PreviewsShowWithoutPersistingAndRevert) {
  CluSpaceThemes themes;
  themes.Set(kA, Theme(100));
  EXPECT_TRUE(themes.Preview(kA, Theme(200)));
  EXPECT_FALSE(themes.Preview(kA, Theme(200)));
  EXPECT_EQ(themes.Shown(kA), Theme(200));
  EXPECT_EQ(themes.Committed(kA), Theme(100));
  EXPECT_EQ(ParseSpaceTheme(*themes.Value().Find(kA)), Theme(100));
  EXPECT_TRUE(themes.RevertPreview(kA));
  EXPECT_EQ(themes.Shown(kA), Theme(100));
  EXPECT_FALSE(themes.RevertPreview(kA));
  // A preview of a space with no theme reverts to none.
  themes.Preview(kB, Theme(50));
  EXPECT_TRUE(themes.RevertPreview(kB));
  EXPECT_EQ(themes.Shown(kB), std::nullopt);
}

TEST(CluSpaceThemesTest, ACommitEndsThePreview) {
  CluSpaceThemes themes;
  themes.Preview(kA, Theme(200));
  EXPECT_TRUE(themes.Set(kA, Theme(210)));
  EXPECT_FALSE(themes.RevertPreview(kA));
  EXPECT_EQ(themes.Shown(kA), Theme(210));
}

TEST(CluSpaceThemesTest, DeletedSpacesTakeTheirThemesAlong) {
  CluSpaceThemes themes;
  themes.Set(kA, Theme(1));
  themes.Set(kB, Theme(2));
  themes.Preview(kGone, Theme(3));
  EXPECT_TRUE(themes.Retain({kA}));
  EXPECT_FALSE(themes.Retain({kA}));
  EXPECT_EQ(themes.Shown(kB), std::nullopt);
  EXPECT_EQ(themes.Shown(kGone), std::nullopt);
  EXPECT_EQ(themes.Shown(kA), Theme(1));
}

}  // namespace
}  // namespace clu
