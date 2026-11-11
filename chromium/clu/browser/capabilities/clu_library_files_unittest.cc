// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_library_files.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

// The Library's folders on temp folders, never the real Downloads or Desktop.
namespace clu {
namespace {

class CluLibraryFilesTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  base::FilePath Write(const std::string& name, int age_minutes) {
    const base::FilePath path = dir_.GetPath().AppendASCII(name);
    EXPECT_TRUE(base::WriteFile(path, "x"));
    const base::Time when = base::Time::Now() - base::Minutes(age_minutes);
    EXPECT_TRUE(base::TouchFile(path, when, when));
    return path;
  }

  std::vector<std::string> Names(LibrarySource source) {
    std::vector<std::string> names;
    for (const LibraryFile& file : ListLibraryFolder(dir_.GetPath(), source)) {
      names.push_back(file.name);
    }
    return names;
  }

  base::ScopedTempDir dir_;
};

TEST_F(CluLibraryFilesTest, DownloadsListsFilesNewestFirst) {
  Write("old.pdf", 30);
  Write("new.zip", 1);
  Write("Screenshot 2026-11-09 at 19.00.00.png", 10);
  EXPECT_EQ(Names(LibrarySource::kDownloads),
            (std::vector<std::string>{"new.zip",
                                      "Screenshot 2026-11-09 at 19.00.00.png",
                                      "old.pdf"}));
}

TEST_F(CluLibraryFilesTest, SkipsHiddenPartialFoldersAndLinks) {
  Write(".DS_Store", 1);
  Write("movie.mp4.crdownload", 1);
  Write("page.download", 1);
  ASSERT_TRUE(base::CreateDirectory(dir_.GetPath().AppendASCII("folder")));
  const base::FilePath real = Write("real.txt", 5);
  ASSERT_TRUE(base::CreateSymbolicLink(real, dir_.GetPath().AppendASCII("link.txt")));
  EXPECT_EQ(Names(LibrarySource::kDownloads), (std::vector<std::string>{"real.txt"}));
}

TEST_F(CluLibraryFilesTest, MediaListsOnlyImagesAndVideos) {
  Write("Screen Recording.MOV", 2);
  Write("Screenshot.png", 3);
  Write("notes.txt", 1);
  EXPECT_EQ(Names(LibrarySource::kMedia),
            (std::vector<std::string>{"Screen Recording.MOV", "Screenshot.png"}));
  EXPECT_EQ(LibraryKindOf(base::FilePath("a.HEIC")), "image");
  EXPECT_EQ(LibraryKindOf(base::FilePath("a.webm")), "video");
  EXPECT_EQ(LibraryKindOf(base::FilePath("noext")), "file");
}

TEST_F(CluLibraryFilesTest, MissingFolderListsNothing) {
  EXPECT_TRUE(ListLibraryFolder(dir_.GetPath().AppendASCII("gone"),
                                LibrarySource::kDownloads)
                  .empty());
  EXPECT_TRUE(ListLibraryFolder(base::FilePath(), LibrarySource::kMedia).empty());
}

TEST_F(CluLibraryFilesTest, RowsCarryNameSizeTimeAndKind) {
  Write("shot.png", 1);
  const base::ListValue rows = LibraryFilesValue(
      ListLibraryFolder(dir_.GetPath(), LibrarySource::kMedia));
  ASSERT_EQ(rows.size(), 1u);
  const base::DictValue& row = rows[0].GetDict();
  EXPECT_EQ(*row.FindString("name"), "shot.png");
  EXPECT_EQ(*row.FindDouble("size"), 1);
  EXPECT_GT(*row.FindDouble("modifiedAt"), 0);
  EXPECT_EQ(*row.FindString("kind"), "image");
}

TEST_F(CluLibraryFilesTest, ResolvesOnlyBareNamesOfFilesInTheFolder) {
  const base::FilePath file = Write("report.pdf", 1);
  ASSERT_TRUE(base::CreateDirectory(dir_.GetPath().AppendASCII("sub")));
  ASSERT_TRUE(base::CreateSymbolicLink(file, dir_.GetPath().AppendASCII("link.pdf")));
  EXPECT_EQ(ResolveLibraryFile(dir_.GetPath(), "report.pdf"), file);
  for (const char* junk : {"", "..", "../report.pdf", "sub/report.pdf", "/etc/hosts",
                           ".hidden", "sub", "link.pdf", "missing.pdf"}) {
    EXPECT_FALSE(ResolveLibraryFile(dir_.GetPath(), junk)) << junk;
  }
  EXPECT_FALSE(ResolveLibraryFile(base::FilePath(), "report.pdf"));
}

// Rename keeps the file in its folder and never overwrites another.
TEST_F(CluLibraryFilesTest, RenamesOnlyToFreeBareNamesInTheSameFolder) {
  const base::FilePath file = Write("Screenshot 1.png", 1);
  Write("taken.png", 2);
  ASSERT_TRUE(base::CreateDirectory(dir_.GetPath().AppendASCII("sub")));
  for (const char* junk : {"", "..", "../out.png", "sub/out.png", "/tmp/out.png",
                           ".hidden.png", "a:b.png", "taken.png", "sub"}) {
    EXPECT_FALSE(RenameLibraryFile(dir_.GetPath(), "Screenshot 1.png", junk)) << junk;
    EXPECT_TRUE(base::PathExists(file)) << junk;
  }
  EXPECT_FALSE(RenameLibraryFile(dir_.GetPath(), "Screenshot 1.png", std::string(256, 'a')));
  EXPECT_FALSE(RenameLibraryFile(dir_.GetPath(), "missing.png", "out.png"));
  const std::optional<base::FilePath> to =
      RenameLibraryFile(dir_.GetPath(), "Screenshot 1.png", "Trip.png");
  ASSERT_TRUE(to);
  EXPECT_EQ(*to, dir_.GetPath().AppendASCII("Trip.png"));
  EXPECT_TRUE(base::PathExists(*to));
  EXPECT_FALSE(base::PathExists(file));
}

// A path with no NSString copies nothing and says so, so the Library shows no "Copied" toast. Only the
// failing case runs here: a successful copy would overwrite the Mac's real clipboard.
TEST_F(CluLibraryFilesTest, CopyOfAPathThatIsNotUtf8Fails) {
  const base::FilePath junk = dir_.GetPath().Append("bad\xff\xfe.zip");
  EXPECT_FALSE(CopyLibraryFile(junk, /*as_path=*/true));
}

TEST_F(CluLibraryFilesTest, TestFoldersOverrideTheRealOnes) {
  const base::FilePath media = dir_.GetPath().AppendASCII("media");
  SetLibraryFoldersForTesting(dir_.GetPath(), media);
  EXPECT_EQ(LibraryFolder(LibrarySource::kDownloads), dir_.GetPath());
  EXPECT_EQ(LibraryFolder(LibrarySource::kMedia), media);
  SetLibraryFoldersForTesting(base::FilePath(), base::FilePath());
  EXPECT_NE(LibraryFolder(LibrarySource::kDownloads), dir_.GetPath());
}

}  // namespace
}  // namespace clu
