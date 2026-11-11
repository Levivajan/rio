// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_chrome_history.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "sql/database.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

// The History reader: only non-hidden http(s) rows of the last 90 days with converted times, read from a temp
// copy, and a damaged or missing file is an error result.
namespace clu {
namespace {

constexpr int64_t kHour = 60 * 60 * 1000;
constexpr int64_t kDay = 24 * kHour;
// 2026-11-07T12:00:00.123Z.
constexpr int64_t kNow = 1'794'052'800'123;

std::vector<std::string> Names(const base::FilePath& dir) {
  std::vector<std::string> out;
  base::FileEnumerator e(dir, false,
                         base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath p = e.Next(); !p.empty(); p = e.Next()) {
    out.push_back(p.BaseName().AsUTF8Unsafe());
  }
  std::sort(out.begin(), out.end());
  return out;
}

class CluChromeHistoryTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    ASSERT_TRUE(tmp_.CreateUniqueTempDir());
    ASSERT_TRUE(CopyImportFixture(home_.GetPath(), kNow));
    profile_ = ChromeDataDir(home_.GetPath()).Append("Profile 1");
  }

  base::ScopedTempDir home_;
  base::ScopedTempDir tmp_;
  base::FilePath profile_;
};

TEST_F(CluChromeHistoryTest, ImportsRecentVisibleWebRowsNewestFirst) {
  const HistoryReadResult r =
      ReadChromeHistory(profile_.Append("History"), kNow, tmp_.GetPath());
  ASSERT_FALSE(r.error);
  EXPECT_EQ(r.rows, (std::vector<ImportedVisit>{
                        {"https://harbor.example/", "Harbor", 5, 2, kNow - kHour},
                        {"https://kettle.example/notes", "Kettle notes", 1, 0, kNow - 3 * kDay},
                        {"http://meadow.example/", "Meadow", 2, 0, kNow - 30 * kDay},
                        {"https://docs.example/guide", "", 1, 0, kNow - 89 * kDay},
                    }));
  EXPECT_EQ(r.skipped, 0);
}

TEST_F(CluChromeHistoryTest, ReadsATempCopyAndLeavesTheOriginalAlone) {
  const base::FilePath file = profile_.Append("History");
  ASSERT_TRUE(base::WriteFile(base::FilePath(file.value() + "-journal"), ""));
  std::string before;
  ASSERT_TRUE(base::ReadFileToString(file, &before));
  base::File::Info info;
  ASSERT_TRUE(base::GetFileInfo(file, &info));
  const std::vector<std::string> files = Names(profile_);

  EXPECT_FALSE(ReadChromeHistory(file, kNow, tmp_.GetPath()).error);
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(file, &after));
  EXPECT_EQ(after, before);
  base::File::Info info_after;
  ASSERT_TRUE(base::GetFileInfo(file, &info_after));
  EXPECT_EQ(info_after.last_modified, info.last_modified);
  EXPECT_EQ(Names(profile_), files);
  // The copy is gone.
  EXPECT_TRUE(Names(tmp_.GetPath()).empty());
}

// A running Chrome commits history while Rio copies it: a copy during which the file changed is taken again, and
// one that never holds still is an unreadable file, not rows from two commits.
TEST_F(CluChromeHistoryTest, ACopyTheSourceChangedUnderIsTakenAgain) {
  const base::FilePath file = profile_.Append("History");
  int copies = 0;
  int changes = 1;
  SetHistoryCopiedCallbackForTesting(base::BindLambdaForTesting([&] {
    ++copies;
    if (changes > 0) {
      --changes;
      base::File::Info info;
      ASSERT_TRUE(base::GetFileInfo(file, &info));
      ASSERT_TRUE(base::TouchFile(file, info.last_accessed,
                                  info.last_modified + base::Seconds(10)));
    }
  }));
  const HistoryReadResult once = ReadChromeHistory(file, kNow, tmp_.GetPath());
  EXPECT_FALSE(once.error);
  EXPECT_EQ(once.rows.size(), 4u);
  EXPECT_EQ(copies, 2);

  copies = 0;
  changes = 100;
  EXPECT_EQ(ReadChromeHistory(file, kNow, tmp_.GetPath()).error,
            kHistoryUnreadable);
  EXPECT_EQ(copies, 3);
  SetHistoryCopiedCallbackForTesting(base::RepeatingClosure());
  EXPECT_TRUE(Names(tmp_.GetPath()).empty());
}

// The copy's folder is made when it is missing (Rio's own folder in the user data directory).
TEST_F(CluChromeHistoryTest, MakesItsTempRoot) {
  const base::FilePath root = tmp_.GetPath().AppendASCII("CluImportTemp");
  EXPECT_FALSE(
      ReadChromeHistory(profile_.Append("History"), kNow, root).error);
  EXPECT_TRUE(base::DirectoryExists(root));
  EXPECT_TRUE(Names(root).empty());
}

TEST_F(CluChromeHistoryTest, ConvertsChromeTimes) {
  EXPECT_EQ(ToChromeTime(0), 11'644'473'600'000'000);
  EXPECT_GT(ToChromeTime(kNow), int64_t{1} << 53);
}

TEST_F(CluChromeHistoryTest, AMissingDamagedOrForeignFileIsAnErrorResult) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath file = dir.GetPath().Append("History");
  EXPECT_EQ(ReadChromeHistory(file, kNow, tmp_.GetPath()).error, kHistoryUnreadable);
  ASSERT_TRUE(base::WriteFile(file, std::string("SQLite format 3\0 but truncated garbage", 38)));
  EXPECT_EQ(ReadChromeHistory(file, kNow, tmp_.GetPath()).error, kHistoryUnreadable);
  // A real SQLite file without Chrome's tables.
  {
    sql::Database db(sql::test::kTestTag);
    const base::FilePath other = dir.GetPath().Append("Other");
    ASSERT_TRUE(db.Open(other));
    ASSERT_TRUE(db.Execute("CREATE TABLE t (x INTEGER)"));
    db.Close();
    EXPECT_EQ(ReadChromeHistory(other, kNow, tmp_.GetPath()).error, kHistoryUnreadable);
  }
  EXPECT_TRUE(Names(tmp_.GetPath()).empty());
}

}  // namespace
}  // namespace clu
