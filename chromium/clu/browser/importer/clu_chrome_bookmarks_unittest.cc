// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_chrome_bookmarks.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

// The Bookmarks parser on the import fixture.
namespace clu {
namespace {

std::string FixtureText() {
  std::string text;
  CHECK(base::ReadFileToString(
      ChromeDataDir(ImportFixtureRoot()).Append("Profile 1").Append("Bookmarks"), &text));
  return text;
}

// The titles of `parent_key`'s children, in order.
std::vector<std::string> Titles(const BookmarksResult& r, const std::string& parent_key) {
  std::vector<std::string> out;
  for (const ImportNode& n : r.nodes) {
    if (n.parent_key == parent_key) {
      out.push_back(n.title);
    }
  }
  return out;
}

const ImportNode* Child(const BookmarksResult& r,
                        const std::string& parent_key,
                        const std::string& title) {
  for (const ImportNode& n : r.nodes) {
    if (n.parent_key == parent_key && n.title == title) {
      return &n;
    }
  }
  return nullptr;
}

int MaxFolderDepth(const BookmarksResult& r) {
  std::map<std::string, int> depth;
  int max = 0;
  for (const ImportNode& n : r.nodes) {
    if (!n.folder) {
      continue;
    }
    const int d = n.parent_key.empty() ? 1 : depth[n.parent_key] + 1;
    depth[n.key] = d;
    max = std::max(max, d);
  }
  return max;
}

TEST(CluChromeBookmarksTest, MapsTheFixtureToOneFolderTree) {
  const BookmarksResult r = ParseChromeBookmarks(FixtureText(), "Profile 1");
  ASSERT_FALSE(r.error);
  EXPECT_EQ(r.skipped, 2);
  EXPECT_EQ(r.bookmarks, 13);
  EXPECT_EQ(r.folders, 6);
  ASSERT_FALSE(r.nodes.empty());
  const ImportNode& root = r.nodes[0];
  EXPECT_EQ(root.title, kChromeImportFolderTitle);
  EXPECT_EQ(root.key, "Profile 1/root");
  EXPECT_TRUE(root.parent_key.empty());
  // Mobile bookmarks is empty in the fixture, so it is left out.
  EXPECT_EQ(Titles(r, root.key),
            (std::vector<std::string>{"Bookmarks bar", "Other bookmarks"}));
  const ImportNode* bar = Child(r, root.key, "Bookmarks bar");
  ASSERT_TRUE(bar);
  EXPECT_EQ(Titles(r, bar->key),
            (std::vector<std::string>{"Harbor", "kettle.example", "Level 1"}));
  const ImportNode* kettle = Child(r, bar->key, "kettle.example");
  ASSERT_TRUE(kettle);
  EXPECT_FALSE(kettle->folder);
  EXPECT_EQ(kettle->url, "https://kettle.example/notes");
  EXPECT_EQ(Titles(r, Child(r, root.key, "Other bookmarks")->key),
            (std::vector<std::string>{"Docs"}));

  // Top folder 1, Bookmarks bar 2, Level 1 is 3 ... Level 6 is 8; Levels 7 to 10 dissolve into Level 6, in order.
  const ImportNode* f = Child(r, bar->key, "Level 1");
  for (int level = 2; level <= 6; ++level) {
    ASSERT_TRUE(f);
    f = Child(r, f->key, "Level " + base::NumberToString(level));
  }
  ASSERT_TRUE(f);
  EXPECT_EQ(Titles(r, f->key), (std::vector<std::string>{"Page 6", "Page 7", "Page 8",
                                                          "Page 9", "Page 10"}));
  EXPECT_EQ(MaxFolderDepth(r), kMaxImportFolderDepth);
  // Preorder: every node's parent comes before it.
  std::set<std::string> seen;
  for (const ImportNode& n : r.nodes) {
    EXPECT_TRUE(n.parent_key.empty() || seen.contains(n.parent_key)) << n.key;
    seen.insert(n.key);
  }
}

TEST(CluChromeBookmarksTest, KeysNodesByProfileDirAndGuidSoAReimportFindsThem) {
  const BookmarksResult a = ParseChromeBookmarks(FixtureText(), "Profile 1");
  const BookmarksResult b = ParseChromeBookmarks(FixtureText(), "Profile 1");
  ASSERT_EQ(a.nodes.size(), b.nodes.size());
  for (size_t i = 0; i < a.nodes.size(); ++i) {
    EXPECT_EQ(a.nodes[i].key, b.nodes[i].key);
  }
  const ImportNode* bar = Child(a, "Profile 1/root", "Bookmarks bar");
  ASSERT_TRUE(bar);
  EXPECT_EQ(Child(a, bar->key, "Harbor")->key,
            "Profile 1/guid:00000000-0000-4000-8000-000000000201");
}

TEST(CluChromeBookmarksTest, ADamagedFileIsAnErrorResult) {
  const std::string text = FixtureText();
  for (const std::string& junk : {text.substr(0, text.size() / 2), std::string(),
                                  std::string("[1, 2]"), std::string(R"({"roots": null})")}) {
    const BookmarksResult r = ParseChromeBookmarks(junk, "Default");
    EXPECT_EQ(r.error, kBookmarksDamaged);
    EXPECT_TRUE(r.nodes.empty());
  }
}

TEST(CluChromeBookmarksTest, JunkNodesHugeTitlesAndMissingRootsNeverThrow) {
  const std::string big(5000, 'x');
  const std::string doc =
      R"({"roots":{"bookmark_bar":{"type":"folder","children":[null,7,"str",)"
      R"({"type":"url","url":"not a url"},{"type":"url","url":42},)"
      R"({"type":"url","name":")" + big + R"(","url":"https://big.example/"},)"
      R"({"type":"folder","name":"","children":[]},{"type":"folder","name":"no children"}]},)"
      R"("other":"junk"}})";
  const BookmarksResult r = ParseChromeBookmarks(doc, "Default");
  ASSERT_FALSE(r.error);
  const ImportNode* bar = Child(r, "Default/root", "Bookmarks bar");
  ASSERT_TRUE(bar);
  EXPECT_EQ(Titles(r, bar->key),
            (std::vector<std::string>{std::string(1024, 'x'), kUntitledFolderTitle}));
  EXPECT_EQ(r.skipped, 6);
  const BookmarksResult empty = ParseChromeBookmarks(R"({"roots":{}})", "Default");
  EXPECT_FALSE(empty.error);
  EXPECT_TRUE(empty.nodes.empty());
  EXPECT_EQ(empty.bookmarks + empty.folders + empty.skipped, 0);
}

TEST(CluChromeBookmarksTest, StopsAt10000ItemsAndCountsTheRest) {
  std::string children;
  for (size_t i = 0; i < kMaxImportItems + 5; ++i) {
    if (i) {
      children += ",";
    }
    const std::string n = base::NumberToString(i);
    children += R"({"type":"url","name":"n)" + n + R"(","url":"https://a.example/)" + n + "\"}";
  }
  const BookmarksResult r = ParseChromeBookmarks(
      R"({"roots":{"other":{"type":"folder","children":[)" + children + "]}}}", "Default");
  ASSERT_FALSE(r.error);
  EXPECT_EQ(r.bookmarks, static_cast<int>(kMaxImportItems));
  EXPECT_EQ(r.skipped, 5);
}

TEST(CluChromeBookmarksTest, DeepTreesFlattenAndTooDeepFilesAreRefused) {
  // 90 levels parse (180 JSON levels; the reader allows 200) and flatten to depth 8; 3,000 levels are refused as
  // damaged instead of overflowing anything.
  auto nested = [](int n) {
    std::string open, close;
    for (int i = 0; i < n; ++i) {
      open += R"({"type":"folder","name":"f","children":[)";
      close += "]}";
    }
    return R"({"roots":{"other":{"type":"folder","children":[)" + open +
           R"({"type":"url","name":"bottom","url":"https://deep.example/"})" + close +
           "]}}}";
  };
  const BookmarksResult r = ParseChromeBookmarks(nested(90), "Default");
  ASSERT_FALSE(r.error);
  EXPECT_EQ(r.bookmarks, 1);
  EXPECT_EQ(MaxFolderDepth(r), kMaxImportFolderDepth);
  EXPECT_EQ(ParseChromeBookmarks(nested(3000), "Default").error, kBookmarksDamaged);
}

}  // namespace
}  // namespace clu
