// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/command/clu_index_sync.h"

#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

IndexRow Row(const std::string& url,
             const std::string& title = "t",
             int visits = 1) {
  IndexRow row;
  row.url = url;
  row.title = title;
  row.visit_count = visits;
  row.last_visit_at = 1000;
  return row;
}

std::vector<std::string> Urls(const std::vector<IndexRow>& rows) {
  std::vector<std::string> out;
  for (const IndexRow& row : rows) {
    out.push_back(row.url);
  }
  return out;
}

// Junk rows (04-command "History index sync"): no URL, an unparseable one, a scheme other than http and https.
TEST(CluIndexSyncTest, JunkRowsAreSkippedAndTitlesCleaned) {
  const base::Time at = base::Time::FromMillisecondsSinceUnixEpoch(5000);
  EXPECT_FALSE(CleanIndexRow(GURL(), u"x", 1, 0, at));
  EXPECT_FALSE(CleanIndexRow(GURL("not a url"), u"x", 1, 0, at));
  EXPECT_FALSE(CleanIndexRow(GURL("ftp://a.test/"), u"x", 1, 0, at));
  EXPECT_FALSE(CleanIndexRow(GURL("file:///etc/passwd"), u"x", 1, 0, at));
  EXPECT_FALSE(CleanIndexRow(GURL("javascript:alert(1)"), u"x", 1, 0, at));
  EXPECT_FALSE(CleanIndexRow(GURL("chrome://history"), u"x", 1, 0, at));
  EXPECT_FALSE(CleanIndexRow(
      GURL(base::StrCat({"https://a.test/", std::string(40000, 'a')})), u"x",
      1, 0, at));

  const std::optional<IndexRow> row = CleanIndexRow(
      GURL("https://a.test/p"), u"  A\ttab\x01 and\x7f more  ", -3, 2, at);
  ASSERT_TRUE(row);
  EXPECT_EQ(row->url, "https://a.test/p");
  EXPECT_EQ(row->title, "A tab and more");
  EXPECT_EQ(row->visit_count, 0);
  EXPECT_EQ(row->typed_count, 2);
  EXPECT_EQ(row->last_visit_at, 5000);

  // Huge titles are cut to kMaxIndexTitle units; a null time reads as 0.
  const std::optional<IndexRow> huge =
      CleanIndexRow(GURL("https://a.test/"), std::u16string(5000, u'x'), 1, 0,
                    base::Time());
  ASSERT_TRUE(huge);
  EXPECT_EQ(huge->title.size(), kMaxIndexTitle);
  EXPECT_EQ(huge->last_visit_at, 0);
  // A lone lead surrogate at the cut is dropped, never sent.
  std::u16string split(kMaxIndexTitle - 1, u'y');
  split += u"\U0001F600";
  EXPECT_EQ(CleanIndexRow(GURL("https://a.test/"), split, 1, 0, at)->title,
            std::string(kMaxIndexTitle - 1, 'y'));
}

TEST(CluIndexSyncTest, FirstSyncIsFullAndLaterOnesSendOnlyChanges) {
  CluIndexSync sync;
  EXPECT_FALSE(sync.loaded());
  sync.Refresh({Row("https://a.test/"), Row("https://b.test/")});
  ASSERT_TRUE(sync.loaded());

  IndexDelta first = sync.Sync(0);
  EXPECT_TRUE(first.full);
  EXPECT_EQ(Urls(first.upserts),
            (std::vector<std::string>{"https://a.test/", "https://b.test/"}));
  const int64_t rev = first.rev;

  // Nothing changed: an empty delta at the same rev.
  sync.Refresh({Row("https://a.test/"), Row("https://b.test/")});
  IndexDelta same = sync.Sync(rev);
  EXPECT_FALSE(same.full);
  EXPECT_EQ(same.rev, rev);
  EXPECT_TRUE(same.upserts.empty());
  EXPECT_TRUE(same.deletes.empty());

  // a changed title, b went away, c is new.
  sync.Refresh({Row("https://a.test/", "renamed"), Row("https://c.test/")});
  IndexDelta delta = sync.Sync(rev);
  EXPECT_FALSE(delta.full);
  EXPECT_GT(delta.rev, rev);
  EXPECT_EQ(Urls(delta.upserts),
            (std::vector<std::string>{"https://a.test/", "https://c.test/"}));
  EXPECT_EQ(delta.deletes, std::vector<std::string>{"https://b.test/"});
  EXPECT_EQ(delta.upserts[0].title, "renamed");

  // A rev from the future (another run) or 0 gets everything again.
  EXPECT_TRUE(sync.Sync(delta.rev + 100).full);
  EXPECT_TRUE(sync.Sync(-1).full);
  EXPECT_FALSE(sync.Sync(delta.rev).full);
}

TEST(CluIndexSyncTest, AClientTooFarBehindGetsTheFullIndex) {
  CluIndexSync sync;
  sync.Refresh({Row("https://start.test/")});
  const int64_t old_rev = sync.rev();
  std::vector<IndexRow> many;
  for (size_t i = 0; i <= kMaxIndexDelta; ++i) {
    many.push_back(
        Row(base::StrCat({"https://n", base::NumberToString(i), ".test/"})));
  }
  sync.Refresh(many);
  EXPECT_TRUE(sync.Sync(old_rev).full);
  // A client that kept up still gets a delta: one row more is one change.
  const int64_t now = sync.rev();
  many.push_back(Row("https://only.test/"));
  sync.Refresh(std::move(many));
  IndexDelta delta = sync.Sync(now);
  EXPECT_FALSE(delta.full);
  EXPECT_EQ(Urls(delta.upserts),
            std::vector<std::string>{"https://only.test/"});
}

TEST(CluIndexSyncTest, RowsAreCappedAndTheMostRecentDuplicateWins) {
  CluIndexSync sync;
  std::vector<IndexRow> rows;
  rows.push_back(Row("https://dup.test/", "newest", 9));
  rows.push_back(Row("https://dup.test/", "older", 1));
  for (size_t i = 0; i < kIndexLimit + 50; ++i) {
    rows.push_back(
        Row(base::StrCat({"https://r", base::NumberToString(i), ".test/"})));
  }
  sync.Refresh(std::move(rows));
  EXPECT_EQ(sync.size(), kIndexLimit);
  IndexDelta all = sync.Sync(0);
  ASSERT_EQ(all.upserts.size(), kIndexLimit);
  for (const IndexRow& row : all.upserts) {
    if (row.url == "https://dup.test/") {
      EXPECT_EQ(row.title, "newest");
      EXPECT_EQ(row.visit_count, 9);
    }
  }
}

TEST(CluIndexSyncTest, RevsAreNeverReusedAcrossProfiles) {
  CluIndexSync one;
  CluIndexSync two;
  one.Refresh({Row("https://a.test/")});
  two.Refresh({Row("https://b.test/")});
  EXPECT_NE(one.rev(), two.rev());
}

TEST(CluIndexSyncTest, TheDeltaSerializesAsTheContractSays) {
  CluIndexSync sync;
  sync.Refresh({Row("https://a.test/", "A", 3)});
  const base::Value value = sync.Sync(0).ToValue();
  const base::DictValue& dict = value.GetDict();
  EXPECT_EQ(dict.FindBool("full"), true);
  ASSERT_TRUE(dict.FindDouble("rev"));
  const base::ListValue* upserts = dict.FindList("upserts");
  ASSERT_TRUE(upserts);
  ASSERT_EQ(upserts->size(), 1u);
  const base::DictValue& row = (*upserts)[0].GetDict();
  EXPECT_EQ(*row.FindString("key"), "https://a.test/");
  EXPECT_EQ(*row.FindString("kind"), "history");
  EXPECT_EQ(*row.FindString("title"), "A");
  EXPECT_EQ(row.FindInt("visitCount"), 3);
  EXPECT_EQ(row.FindInt("typedCount"), 0);
  EXPECT_EQ(row.FindDouble("lastVisitAt"), 1000);
  EXPECT_TRUE(dict.FindList("deletes")->empty());
}

}  // namespace
}  // namespace clu
