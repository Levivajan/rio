// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_detect.h"

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

// Import detection on the fixture home directory. The import root rule itself (none under --test-type unless a test
// sets one) lives in CluImportService and is covered by the browser tests.
namespace clu {
namespace {

class CluImportDetectTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    ASSERT_TRUE(CopyImportFixture(home_.GetPath(), 1'794'052'800'123));
  }
  base::ScopedTempDir home_;
};

TEST_F(CluImportDetectTest, ListsDefaultAndNumberedProfilesWithNamesAndFiles) {
  EXPECT_EQ(DetectChrome(home_.GetPath()),
            (std::vector<ChromeProfileInfo>{{"Default", "Personal", true, false},
                                            {"Profile 1", "Work", true, true}}));
  EXPECT_TRUE(HasSidebarFile(home_.GetPath()));
}

TEST_F(CluImportDetectTest, ReportsNothingWithoutARootOrAChromeFolder) {
  EXPECT_TRUE(DetectChrome(base::FilePath()).empty());
  base::ScopedTempDir empty;
  ASSERT_TRUE(empty.CreateUniqueTempDir());
  EXPECT_TRUE(DetectChrome(empty.GetPath()).empty());
  EXPECT_FALSE(HasSidebarFile(empty.GetPath()));
  EXPECT_FALSE(HasSidebarFile(base::FilePath()));
}

TEST_F(CluImportDetectTest, FallsBackToDirNamesAndSkipsProfilesWithoutFiles) {
  base::ScopedTempDir root;
  ASSERT_TRUE(root.CreateUniqueTempDir());
  const base::FilePath dir = ChromeDataDir(root.GetPath());
  for (const char* p : {"Profile 3", "Profile 12", "Profile 4"}) {
    ASSERT_TRUE(base::CreateDirectory(dir.Append(p)));
  }
  ASSERT_TRUE(base::WriteFile(dir.Append("Profile 3").Append("History"), ""));
  ASSERT_TRUE(base::WriteFile(dir.Append("Profile 12").Append("Bookmarks"), "{}"));
  ASSERT_TRUE(base::WriteFile(dir.Append("Local State"),
                              R"({"profile": {"info_cache": {"Profile 3": {"name": 5})"));
  std::vector<std::pair<std::string, std::string>> got;
  for (const ChromeProfileInfo& p : DetectChrome(root.GetPath())) {
    got.emplace_back(p.dir, p.name);
  }
  EXPECT_EQ(got, (std::vector<std::pair<std::string, std::string>>{
                     {"Profile 3", "Profile 3"}, {"Profile 12", "Profile 12"}}));
  ASSERT_TRUE(base::DeleteFile(dir.Append("Local State")));
  EXPECT_EQ(DetectChrome(root.GetPath()).size(), 2u);
}

TEST_F(CluImportDetectTest, ResolvesOnlyValidProfileDirsThatExist) {
  EXPECT_EQ(ChromeProfilePath(home_.GetPath(), "Profile 1"),
            ChromeDataDir(home_.GetPath()).Append("Profile 1"));
  for (const char* bad : {"../Chrome", "Profile 1/../..", "System Profile", "Profile 9", "",
                          "Profile 1234", "Profile -1", "default"}) {
    EXPECT_EQ(ChromeProfilePath(home_.GetPath(), bad), std::nullopt) << bad;
  }
  EXPECT_EQ(ChromeProfilePath(base::FilePath(), "Default"), std::nullopt);
  EXPECT_TRUE(IsChromeProfileDir("Profile 999"));
  EXPECT_FALSE(IsChromeProfileDir("Profile "));
}

TEST_F(CluImportDetectTest, FindsTheSidebarProfilesHistoryOnlyForValidProfileKeys) {
  const base::FilePath arc = SidebarUserDataDir(home_.GetPath());
  EXPECT_EQ(SidebarHistoryPath(home_.GetPath(), "default"),
            arc.Append("Default").Append("History"));
  EXPECT_EQ(SidebarHistoryPath(home_.GetPath(), "custom:Profile 1"),
            arc.Append("Profile 1").Append("History"));
  // A directory name that is no Chromium profile directory never becomes a path, nor does a profile with no file.
  for (const char* bad :
       {"custom:../..", "custom:Profile 1/../Default", "custom:", "custom:System Profile", "Default",
        "custom:Profile 2", "custom:default", "junk", ""}) {
    EXPECT_EQ(SidebarHistoryPath(home_.GetPath(), bad), std::nullopt) << bad;
  }
  EXPECT_EQ(SidebarHistoryPath(base::FilePath(), "default"), std::nullopt);
  // A directory where the file should be is not a History file.
  ASSERT_TRUE(base::DeleteFile(arc.Append("Default").Append("History")));
  ASSERT_TRUE(base::CreateDirectory(arc.Append("Default").Append("History")));
  EXPECT_EQ(SidebarHistoryPath(home_.GetPath(), "default"), std::nullopt);
}

}  // namespace
}  // namespace clu
