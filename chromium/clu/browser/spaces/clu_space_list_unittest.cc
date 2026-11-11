// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/spaces/clu_space_list.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "testing/gtest/include/gtest/gtest.h"

// The list of profiles and spaces at its own layer (docs/plan/03-spaces.md T1 and its load-time repair): caps, default
// names, the reorder clamp, the last space and the default profile, and every corrupt-row case of repair.test.ts in
// the Local State shape.
namespace clu {
namespace {

constexpr char kP1[] = "11111111-1111-4111-8111-111111111111";
constexpr char kP2[] = "22222222-2222-4222-8222-222222222222";
constexpr char kS1[] = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa1";
constexpr char kS2[] = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa2";
constexpr char kS3[] = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa3";

class CluSpaceListTest : public testing::Test {
 protected:
  CluSpaceListTest()
      : list_(base::BindRepeating(&CluSpaceListTest::NextId,
                                  base::Unretained(this))) {}

  std::string NextId() {
    return base::StringPrintf("cccccccc-cccc-4ccc-8ccc-%012d", ++ids_);
  }

  static base::DictValue Profile(const std::string& id,
                                 const std::string& dir,
                                 bool is_default = false,
                                 const std::string& name = "P") {
    return base::DictValue()
        .Set("id", id)
        .Set("dir", dir)
        .Set("name", name)
        .Set("default", is_default);
  }

  static base::DictValue Space(const std::string& id,
                               const std::string& profile_id,
                               const std::string& name = "S",
                               const std::string& color = "blue",
                               const std::string& icon_kind = "letter",
                               const std::string& icon_value = "") {
    return base::DictValue()
        .Set("id", id)
        .Set("name", name)
        .Set("icon_kind", icon_kind)
        .Set("icon_value", icon_value)
        .Set("color", color)
        .Set("profile_id", profile_id);
  }

  // One default profile P1 in "Default" and one space S1 on it.
  void LoadHealthy() {
    base::ListValue profiles;
    profiles.Append(Profile(kP1, "Default", true, "Default"));
    base::ListValue spaces;
    spaces.Append(Space(kS1, kP1, "Home"));
    EXPECT_TRUE(list_.Load(&profiles, &spaces, kS1).empty());
  }

  int ids_ = 0;
  CluSpaceList list_;
};

TEST_F(CluSpaceListTest, AHealthyListLoadsWithoutFixesAndRoundTrips) {
  LoadHealthy();
  const base::ListValue profiles = list_.ProfilesValue();
  const base::ListValue spaces = list_.SpacesValue();
  CluSpaceList again(base::BindRepeating([] { return std::string(); }));
  EXPECT_TRUE(again.Load(&profiles, &spaces, kS1).empty());
  EXPECT_EQ(again.Order(), std::vector<std::string>{kS1});
  EXPECT_EQ(again.last_space(), kS1);
  EXPECT_EQ(again.DefaultProfile()->dir, "Default");
}

TEST_F(CluSpaceListTest, NullAndJunkListsReadAsEmpty) {
  EXPECT_TRUE(list_.Load(nullptr, nullptr, "").empty());
  EXPECT_TRUE(list_.profiles().empty());
  base::ListValue junk;
  junk.Append(42);
  junk.Append("text");
  junk.Append(base::DictValue().Set("id", 7));
  const std::set<SpacesFix> fixes = list_.Load(&junk, &junk, "nope");
  EXPECT_TRUE(fixes.contains(SpacesFix::kProfileJunk));
  EXPECT_TRUE(fixes.contains(SpacesFix::kSpaceJunk));
  EXPECT_TRUE(fixes.contains(SpacesFix::kLastSpace));
  EXPECT_TRUE(list_.spaces().empty());
}

TEST_F(CluSpaceListTest, RepairDropsProfilesWithBadIdsOrUnsafeDirectories) {
  base::ListValue profiles;
  profiles.Append(Profile("not-a-uuid", "Default", true));
  profiles.Append(Profile(kP1, "../escape", true));
  profiles.Append(Profile(kP2, "Profile 1"));
  profiles.Append(Profile(kP2, "Profile 2"));  // a duplicate id
  const std::set<SpacesFix> fixes = list_.Load(&profiles, nullptr, "");
  EXPECT_TRUE(fixes.contains(SpacesFix::kProfileJunk));
  ASSERT_EQ(list_.profiles().size(), 1u);
  EXPECT_EQ(list_.profiles()[0].id, kP2);
  // No profile claimed the default: the first one left becomes it.
  EXPECT_TRUE(fixes.contains(SpacesFix::kDefaultPromoted));
  EXPECT_TRUE(list_.profiles()[0].is_default);
}

TEST_F(CluSpaceListTest, RepairKeepsOneDefault) {
  base::ListValue profiles;
  profiles.Append(Profile(kP1, "Default", true));
  profiles.Append(Profile(kP2, "Profile 1", true));
  EXPECT_TRUE(list_.Load(&profiles, nullptr, "")
                  .contains(SpacesFix::kDefaultDuplicate));
  EXPECT_TRUE(list_.FindProfile(kP1)->is_default);
  EXPECT_FALSE(list_.FindProfile(kP2)->is_default);
}

TEST_F(CluSpaceListTest, RepairFixesEveryCorruptSpaceField) {
  base::ListValue profiles;
  profiles.Append(Profile(kP1, "Default", true, "  "));
  base::ListValue spaces;
  spaces.Append(Space(kS1, "33333333-3333-4333-8333-333333333333", "Work"));
  spaces.Append(Space(kS2, kP1, std::string(500, 'w'), "chartreuse"));
  spaces.Append(Space(kS3, kP1, "\t", "teal", "emoji", "ab"));
  spaces.Append(Space(kS3, kP1));  // a duplicate id
  const std::set<SpacesFix> fixes = list_.Load(&profiles, &spaces, "");
  for (SpacesFix fix :
       {SpacesFix::kProfileName, SpacesFix::kSpaceProfile, SpacesFix::kSpaceColor,
        SpacesFix::kSpaceName, SpacesFix::kSpaceIcon, SpacesFix::kSpaceJunk}) {
    EXPECT_TRUE(fixes.contains(fix)) << static_cast<int>(fix);
  }
  EXPECT_EQ(list_.FindProfile(kP1)->name, "Profile 1");
  ASSERT_EQ(list_.spaces().size(), 3u);
  // A space whose profile is unknown moves to the default profile.
  EXPECT_EQ(list_.FindSpace(kS1)->profile_id, kP1);
  EXPECT_EQ(list_.FindSpace(kS2)->color, "slate");
  EXPECT_EQ(list_.FindSpace(kS2)->name, std::string(60, 'w'));
  EXPECT_EQ(list_.FindSpace(kS3)->icon_kind, "letter");
  EXPECT_EQ(list_.FindSpace(kS3)->icon_value, "");
  EXPECT_EQ(list_.FindSpace(kS3)->name, "Space 3");
}

TEST_F(CluSpaceListTest, SpacesWithoutAnyProfileJoinTheFirstAdoptedProfile) {
  base::ListValue spaces;
  spaces.Append(Space(kS1, kP1, "Home"));
  list_.Load(nullptr, &spaces, "");
  EXPECT_EQ(list_.FindSpace(kS1)->profile_id, "");
  const std::string id = list_.AdoptProfile(kP2, "Default", {});
  EXPECT_EQ(id, kP2);
  EXPECT_TRUE(list_.FindProfile(kP2)->is_default);
  EXPECT_EQ(list_.FindProfile(kP2)->name, kDefaultProfileName);
  EXPECT_EQ(list_.FindSpace(kS1)->profile_id, kP2);
}

TEST_F(CluSpaceListTest, AdoptionKeepsTheMirroredSpacesAndSkipsKnownOnes) {
  LoadHealthy();
  SpaceEntry known;
  known.id = kS1;
  known.name = "Dup";
  SpaceEntry work;
  work.id = kS2;
  work.name = "Work";
  work.color = "chartreuse";
  SpaceEntry junk;
  junk.id = "junk";
  const std::string id =
      list_.AdoptProfile("bad id", "Profile 1", {known, work, junk});
  // A bad id gets a fresh one; the second profile is no default.
  EXPECT_EQ(id, "cccccccc-cccc-4ccc-8ccc-000000000001");
  EXPECT_FALSE(list_.FindProfile(id)->is_default);
  EXPECT_EQ(list_.Order(), (std::vector<std::string>{kS1, kS2}));
  EXPECT_EQ(list_.FindSpace(kS1)->name, "Home");
  EXPECT_EQ(list_.FindSpace(kS2)->profile_id, id);
  EXPECT_EQ(list_.FindSpace(kS2)->color, "slate");
  // The same directory again is the same profile.
  EXPECT_EQ(list_.AdoptProfile(kP2, "Profile 1", {}), id);
}

// A junk profile row (its directory missing) leaves its space on the default profile for now, but the space keeps
// the profile's id through a save: once that profile's clu.db is adopted under the same id, the space goes back to it
// and to its rows.
TEST_F(CluSpaceListTest, ASpaceOfAJunkProfileRowGoesBackWhenThatProfileIsAdopted) {
  base::ListValue profiles;
  profiles.Append(Profile(kP1, "Default", true));
  profiles.Append(Profile(kP2, ""));
  base::ListValue spaces;
  spaces.Append(Space(kS1, kP1, "Home"));
  spaces.Append(Space(kS2, kP2, "Work"));
  EXPECT_TRUE(list_.Load(&profiles, &spaces, "").contains(
      SpacesFix::kSpaceProfile));
  EXPECT_EQ(list_.FindSpace(kS2)->profile_id, kP1);

  // Saved and loaded again, the lost id stays.
  const base::ListValue saved_profiles = list_.ProfilesValue();
  const base::ListValue saved_spaces = list_.SpacesValue();
  CluSpaceList again(base::BindRepeating([] { return std::string(); }));
  again.Load(&saved_profiles, &saved_spaces, "");
  EXPECT_EQ(again.FindSpace(kS2)->profile_id, kP1);

  EXPECT_EQ(again.AdoptProfile(kP2, "Profile 1", {}), kP2);
  EXPECT_EQ(again.FindSpace(kS2)->profile_id, kP2);
  EXPECT_EQ(again.FindSpace(kS1)->profile_id, kP1);
  EXPECT_FALSE(again.SpacesValue()[1].GetDict().contains("lost_profile_id"));
}

TEST_F(CluSpaceListTest, CreateUsesDefaultsAndRefusesTheFiftyFirst) {
  LoadHealthy();
  base::expected<std::string, SpacesError> id =
      list_.CreateSpace(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(id.has_value());
  const SpaceEntry* space = list_.FindSpace(id.value());
  EXPECT_EQ(space->name, "Space 2");
  EXPECT_EQ(space->icon_kind, "letter");
  // A new space takes the lavender default (indigo), the home space keeps blue.
  EXPECT_EQ(space->color, kNewSpaceColor);
  EXPECT_EQ(space->profile_id, kP1);
  while (list_.spaces().size() < kMaxSpaces) {
    ASSERT_TRUE(list_
                    .CreateSpace(std::nullopt, std::nullopt, std::nullopt,
                                 std::nullopt)
                    .has_value());
  }
  EXPECT_EQ(list_.CreateSpace("More", std::nullopt, std::nullopt, std::nullopt)
                .error(),
            SpacesError::kMaxSpaces);
}

TEST_F(CluSpaceListTest, CreateAndUpdateRefuseBadIconsColorsAndProfiles) {
  LoadHealthy();
  EXPECT_EQ(list_
                .CreateSpace("W", SpaceIconInput{.emoji = true, .value = "abc"},
                             std::nullopt, std::nullopt)
                .error(),
            SpacesError::kBadPayload);
  EXPECT_EQ(list_.CreateSpace("W", std::nullopt, "chartreuse", std::nullopt)
                .error(),
            SpacesError::kBadPayload);
  EXPECT_EQ(list_.CreateSpace("W", std::nullopt, std::nullopt, kP2).error(),
            SpacesError::kNotFound);
  EXPECT_EQ(list_.UpdateSpace(kS1, "   ", std::nullopt, std::nullopt).error(),
            SpacesError::kBadPayload);
  EXPECT_EQ(list_.UpdateSpace(kS3, "X", std::nullopt, std::nullopt).error(),
            SpacesError::kNotFound);
  // Nothing changed after the refusals.
  EXPECT_EQ(list_.spaces().size(), 1u);
  EXPECT_EQ(list_.FindSpace(kS1)->name, "Home");
}

TEST_F(CluSpaceListTest, UpdateRenamesRecolorsAndReIcons) {
  LoadHealthy();
  EXPECT_EQ(list_.UpdateSpace(kS1, " Work ",
                              SpaceIconInput{.emoji = true,
                                             .value = "\U0001F4BC"},
                              "teal"),
            true);
  const SpaceEntry* space = list_.FindSpace(kS1);
  EXPECT_EQ(space->name, "Work");
  EXPECT_EQ(space->icon_kind, "emoji");
  EXPECT_EQ(space->icon_value, "\U0001F4BC");
  EXPECT_EQ(space->color, "teal");
  EXPECT_EQ(list_.UpdateSpace(kS1, "Work", std::nullopt, std::nullopt), false);
  EXPECT_EQ(list_.UpdateSpace(kS1, std::nullopt, SpaceIconInput{}, std::nullopt),
            true);
  EXPECT_EQ(list_.FindSpace(kS1)->icon_kind, "letter");
}

TEST_F(CluSpaceListTest, SymbolIconsAreKeptAndUnknownOnesRefused) {
  LoadHealthy();
  EXPECT_EQ(list_.UpdateSpace(kS1, std::nullopt,
                              SpaceIconInput{.value = "planet", .symbol = true},
                              std::nullopt),
            true);
  EXPECT_EQ(list_.FindSpace(kS1)->icon_kind, "symbol");
  EXPECT_EQ(list_.FindSpace(kS1)->icon_value, "planet");
  EXPECT_EQ(list_
                .UpdateSpace(kS1, std::nullopt,
                             SpaceIconInput{.value = "sparkly", .symbol = true},
                             std::nullopt)
                .error(),
            SpacesError::kBadPayload);
  EXPECT_EQ(list_
                .CreateSpace("W", SpaceIconInput{.value = "", .symbol = true},
                             std::nullopt, std::nullopt)
                .error(),
            SpacesError::kBadPayload);
  base::expected<std::string, SpacesError> created = list_.CreateSpace(
      "W", SpaceIconInput{.value = "rocket", .symbol = true}, std::nullopt,
      std::nullopt);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(list_.FindSpace(*created)->icon_value, "rocket");
}

TEST_F(CluSpaceListTest, AStoredUnknownSymbolLoadsAsTheLetter) {
  base::ListValue profiles;
  profiles.Append(Profile(kP1, "Default", true));
  base::ListValue spaces;
  spaces.Append(Space(kS1, kP1, "A", "teal", "symbol", "moon"));
  spaces.Append(Space(kS2, kP1, "B", "teal", "symbol", "sparkly"));
  spaces.Append(Space(kS3, kP1, "C", "teal", "emoji", "\U0001F4BC"));
  const std::set<SpacesFix> fixes = list_.Load(&profiles, &spaces, "");
  EXPECT_TRUE(fixes.contains(SpacesFix::kSpaceIcon));
  EXPECT_EQ(list_.FindSpace(kS1)->icon_kind, "symbol");
  EXPECT_EQ(list_.FindSpace(kS1)->icon_value, "moon");
  EXPECT_EQ(list_.FindSpace(kS2)->icon_kind, "letter");
  EXPECT_EQ(list_.FindSpace(kS2)->icon_value, "");
  EXPECT_EQ(list_.FindSpace(kS3)->icon_kind, "emoji");
}

TEST_F(CluSpaceListTest, ReorderClampsAndKeepsPositionsDense) {
  LoadHealthy();
  const std::string b = list_
                            .CreateSpace("B", std::nullopt, std::nullopt,
                                         std::nullopt)
                            .value();
  const std::string c = list_
                            .CreateSpace("C", std::nullopt, std::nullopt,
                                         std::nullopt)
                            .value();
  EXPECT_EQ(list_.ReorderSpace(c, -5), true);
  EXPECT_EQ(list_.Order(), (std::vector<std::string>{c, kS1, b}));
  EXPECT_EQ(list_.ReorderSpace(c, 999), true);
  EXPECT_EQ(list_.Order(), (std::vector<std::string>{kS1, b, c}));
  EXPECT_EQ(list_.ReorderSpace(c, 2), false);
  EXPECT_EQ(list_.ReorderSpace(kS3, 0).error(), SpacesError::kNotFound);
}

TEST_F(CluSpaceListTest, DeletingTheLastSpaceIsRefusedAndOthersFallBack) {
  LoadHealthy();
  EXPECT_EQ(list_.DeleteSpace(kS1).error(), SpacesError::kLastSpace);
  const std::string b = list_
                            .CreateSpace("B", std::nullopt, std::nullopt,
                                         std::nullopt)
                            .value();
  list_.SetLastSpace(b);
  // The previous space, else the next.
  EXPECT_EQ(list_.DeleteSpace(b).value(), kS1);
  EXPECT_EQ(list_.last_space(), kS1);
  EXPECT_EQ(list_.Order(), std::vector<std::string>{kS1});
}

TEST_F(CluSpaceListTest, ProfilesCapRenameAndDelete) {
  LoadHealthy();
  base::expected<std::string, SpacesError> work =
      list_.AddProfile(std::nullopt, "Profile 1");
  ASSERT_TRUE(work.has_value());
  EXPECT_EQ(list_.FindProfile(work.value())->name, "Profile 2");
  EXPECT_FALSE(list_.FindProfile(work.value())->is_default);
  EXPECT_EQ(list_.AddProfile("X", "Profile 1").error(),
            SpacesError::kBadPayload);
  EXPECT_EQ(list_.AddProfile("X", "../up").error(), SpacesError::kBadPayload);
  for (int i = 2; list_.profiles().size() < kMaxProfiles; ++i) {
    ASSERT_TRUE(list_.AddProfile(std::nullopt,
                                 base::StringPrintf("Profile %d", i))
                    .has_value());
  }
  EXPECT_EQ(list_.AddProfile("21st", "Profile 99").error(),
            SpacesError::kMaxProfiles);

  EXPECT_EQ(list_.RenameProfile(work.value(), " Client "), true);
  EXPECT_EQ(list_.FindProfile(work.value())->name, "Client");
  EXPECT_EQ(list_.RenameProfile(work.value(), "\n").error(),
            SpacesError::kBadPayload);

  EXPECT_EQ(list_.RemoveProfile(kP1).error(), SpacesError::kDefaultProfile);
  ASSERT_TRUE(list_.SetSpaceProfile(kS1, work.value()).value());
  EXPECT_EQ(list_.RemoveProfile(work.value()).error(), SpacesError::kInUse);
  ASSERT_TRUE(list_.SetSpaceProfile(kS1, kP1).value());
  EXPECT_TRUE(list_.RemoveProfile(work.value()).has_value());
  EXPECT_EQ(list_.FindProfile(work.value()), nullptr);
  EXPECT_EQ(list_.SetSpaceProfile(kS1, work.value()).error(),
            SpacesError::kNotFound);
}

TEST_F(CluSpaceListTest, AForgottenProfileGivesItsSpacesToTheDefault) {
  LoadHealthy();
  const std::string work = list_.AddProfile("Work", "Profile 1").value();
  ASSERT_TRUE(list_.SetSpaceProfile(kS1, work).value());
  EXPECT_FALSE(list_.ForgetProfile(kP1));
  EXPECT_TRUE(list_.ForgetProfile(work));
  EXPECT_EQ(list_.FindSpace(kS1)->profile_id, kP1);
  EXPECT_EQ(list_.FindProfile(work), nullptr);
}

TEST_F(CluSpaceListTest, SafeDirectoryNames) {
  EXPECT_TRUE(CluSpaceList::IsSafeDir("Default"));
  EXPECT_TRUE(CluSpaceList::IsSafeDir("Profile 12"));
  for (const char* bad : {"", ".", "..", "../x", "a/b", "a\\b", ".hidden",
                          " lead", "trail ", "nul\x01"}) {
    EXPECT_FALSE(CluSpaceList::IsSafeDir(bad)) << bad;
  }
  EXPECT_FALSE(CluSpaceList::IsSafeDir(std::string(300, 'a')));
}

// Launch removes a leftover import profile with its spaces, never the default, one an import owns, an
// unknown or junk id, or the profile holding the last space.
TEST_F(CluSpaceListTest, LaunchRemovesOnlyMarkedProfilesThatCanGo) {
  base::ListValue profiles;
  profiles.Append(Profile(kP1, "Default", true, "Default"));
  profiles.Append(Profile(kP2, "Profile 4", false, "Imported 4"));
  base::ListValue spaces;
  spaces.Append(Space(kS1, kP1, "Home"));
  spaces.Append(Space(kS2, kP2, "Old"));
  spaces.Append(Space(kS3, kP2, "Old 2"));
  ASSERT_TRUE(list_.Load(&profiles, &spaces, kS1).empty());

  EXPECT_EQ(list_.RemovableAtLaunch({kP2}, {}), std::vector<std::string>{kP2});
  EXPECT_EQ(list_.RemovableAtLaunch({kP1, kP2, "", "junk", std::string(5000, 'x')}, {}),
            std::vector<std::string>{kP2});
  EXPECT_TRUE(list_.RemovableAtLaunch({kP2}, {kP2}).empty());
  EXPECT_TRUE(list_.RemovableAtLaunch({}, {}).empty());

  // Every space on the marked profile: it stays, so the list keeps a space.
  ASSERT_TRUE(list_.SetSpaceProfile(kS1, kP2).value());
  EXPECT_TRUE(list_.RemovableAtLaunch({kP2}, {}).empty());
}

}  // namespace
}  // namespace clu
