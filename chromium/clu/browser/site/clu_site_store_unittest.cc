// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_store.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/cstring_view.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

// CluSiteStore on a temp file: rows survive a reopen, and junk rows (the repo cases of docs/plan/08-extensions.md
// "DB") read as repaired or skipped, never fatal.
namespace clu {
namespace {

constexpr int64_t kNow = 1'800'000'000'000;

class CluSiteStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    path_ = dir_.GetPath().AppendASCII("Clu").AppendASCII("site.db");
  }

  // Runs raw SQL against the file, as a corrupt or older build might have left it.
  void Raw(base::cstring_view sql) {
    sql::Database db(sql::Database::Tag("CluModel"));
    ASSERT_TRUE(db.Open(path_));
    ASSERT_TRUE(db.Execute(sql)) << sql;
  }

  base::ScopedTempDir dir_;
  base::FilePath path_;
};

TEST_F(CluSiteStoreTest, ExceptionsTweaksAndTheRulesVersionSurviveAReopen) {
  {
    CluSiteStore store(path_);
    EXPECT_TRUE(store.Load().persistent);
    EXPECT_TRUE(store.SetException("a.test", true, kNow));
    EXPECT_TRUE(store.SetException("b.test", true, kNow));
    EXPECT_TRUE(store.SetException("b.test", false, kNow));
    SiteTweak t;
    t.site = "a.test";
    t.enabled = false;
    t.css = "h1{color:red}";
    t.js = "x()";
    t.controls = TweakControls{.hue = 20};
    t.zaps = {Zap{.selector = "#promo", .label = "div", .created_at = 9}};
    t.updated_at = kNow;
    EXPECT_TRUE(store.PutTweak(t));
    SiteTweak gone;
    gone.site = "c.test";
    EXPECT_TRUE(store.PutTweak(gone));
    EXPECT_TRUE(store.DeleteTweak("c.test"));
    EXPECT_TRUE(store.SetBlockerVersion("0123456789abcdef"));
  }
  CluSiteStore store(path_);
  const SiteStoreLoad load = store.Load();
  EXPECT_EQ(load.exceptions, (std::set<std::string>{"a.test"}));
  ASSERT_EQ(load.tweaks.size(), 1u);
  const SiteTweak& t = load.tweaks.at("a.test");
  EXPECT_FALSE(t.enabled);
  EXPECT_EQ(t.css, "h1{color:red}");
  EXPECT_EQ(t.js, "x()");
  EXPECT_EQ(t.controls, (TweakControls{.hue = 20}));
  EXPECT_EQ(t.zaps, (std::vector<Zap>{
                        Zap{.selector = "#promo", .label = "div", .created_at = 9}}));
  EXPECT_EQ(t.updated_at, kNow);
  EXPECT_EQ(load.blocker_version, "0123456789abcdef");
  EXPECT_EQ(load.skipped_rows, 0);
}

TEST_F(CluSiteStoreTest, JunkRowsAreRepairedOrSkipped) {
  { CluSiteStore(path_).Load(); }
  Raw("INSERT INTO blocking_exceptions VALUES ('www.bad.test', 1), (42, 1), "
      "(NULL, 1), ('ok.test', 'x')");
  // A blob for js: TEXT affinity would turn a number into text.
  Raw("INSERT INTO site_tweaks VALUES ('junk.test', 'yes', NULL, X'0102', "
      "'{broken', 'garbage', 'later'), ('BAD SITE', 1, '', '', NULL, '[]', 0), "
      "('off.test', 0, 'p{}', '', 'null', '[\"#a\", {\"selector\":\"b{\"}]', "
      "1.5)");
  CluSiteStore store(path_);
  const SiteStoreLoad load = store.Load();
  EXPECT_EQ(load.exceptions, (std::set<std::string>{"ok.test"}));
  // Three bad exception sites and one bad tweak site; the rows stay in the file.
  EXPECT_EQ(load.skipped_rows, 4);
  EXPECT_EQ(load.junk_zaps, 1);
  ASSERT_EQ(load.tweaks.size(), 2u);
  const SiteTweak& junk = load.tweaks.at("junk.test");
  EXPECT_TRUE(junk.enabled);
  EXPECT_EQ(junk.css, "");
  EXPECT_EQ(junk.js, "");
  EXPECT_EQ(junk.controls, std::nullopt);
  EXPECT_TRUE(junk.zaps.empty());
  EXPECT_EQ(junk.updated_at, 0);
  const SiteTweak& off = load.tweaks.at("off.test");
  EXPECT_FALSE(off.enabled);
  ASSERT_EQ(off.zaps.size(), 1u);
  EXPECT_EQ(off.zaps[0].selector, "#a");
  EXPECT_EQ(off.updated_at, 1.5);
}

TEST_F(CluSiteStoreTest, WritesRefuseBadSites) {
  CluSiteStore store(path_);
  store.Load();
  EXPECT_FALSE(store.SetException("www.a.test", true, kNow));
  SiteTweak t;
  t.site = "a.test/../b";
  EXPECT_FALSE(store.PutTweak(t));
  EXPECT_TRUE(store.Load().tweaks.empty());
}

TEST_F(CluSiteStoreTest, AnUnopenableFileFallsBackToMemory) {
  // A directory where the file should be.
  ASSERT_TRUE(base::CreateDirectory(path_));
  CluSiteStore store(path_);
  const SiteStoreLoad load = store.Load();
  EXPECT_FALSE(load.persistent);
  EXPECT_TRUE(store.SetException("a.test", true, kNow));
  EXPECT_EQ(store.Load().exceptions, (std::set<std::string>{"a.test"}));
}

TEST_F(CluSiteStoreTest, AnEmptyPathKeepsRowsInMemory) {
  CluSiteStore store{base::FilePath()};
  EXPECT_FALSE(store.Load().persistent);
  EXPECT_TRUE(store.SetException("a.test", true, kNow));
}

}  // namespace
}  // namespace clu
