// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_site_data.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "clu/browser/importer/clu_site_storage.h"
#include "sql/database.h"
#include "sql/test/test_helpers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"

// The site storage, site permissions, saved addresses and default search engine of the fixture
// Chrome and sidebar profiles, read through the shared code both imports use. Never a real profile.
namespace clu {
namespace {

constexpr int64_t kNowMs = 1'794'052'800'123;
constexpr SiteDataOptions kAll{.storage = true, .permissions = true, .addresses = true, .search_engine = true};

std::unique_ptr<leveldb::DB> Open(const base::FilePath& dir, bool create = false) {
  leveldb_env::Options options;
  options.create_if_missing = create;
  std::unique_ptr<leveldb::DB> db;
  return leveldb_env::OpenDB(options, dir.AsUTF8Unsafe(), &db).ok() ? std::move(db) : nullptr;
}

std::string Get(leveldb::DB* db, const std::string& key) {
  std::string value;
  return db->Get(leveldb::ReadOptions(), key, &value).ok() ? value : "<none>";
}

std::string ItemKey(const std::string& origin, const std::string& key) {
  return "_" + origin + std::string(1, '\0') + key;
}

bool IsEmptyDir(const base::FilePath& dir) {
  return base::FileEnumerator(dir, true, base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES)
      .Next()
      .empty();
}

class CluSiteDataTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    ASSERT_TRUE(rio_.CreateUniqueTempDir());
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ASSERT_TRUE(CopyImportFixture(home_.GetPath(), kNowMs));
  }
  base::FilePath Chrome() const { return ChromeDataDir(home_.GetPath()).Append("Profile 1"); }
  base::FilePath Arc() const { return SidebarUserDataDir(home_.GetPath()).Append("Default"); }
  base::FilePath Target() const { return rio_.GetPath().Append("Default"); }

  base::ScopedTempDir home_;
  base::ScopedTempDir rio_;
  base::ScopedTempDir temp_;
};

TEST_F(CluSiteDataTest, ReadsEachKindFromTheChromeAndArcFixtures) {
  for (const base::FilePath& source : {Chrome(), Arc()}) {
    SCOPED_TRACE(source.value());
    std::string prefs_before;
    ASSERT_TRUE(base::ReadFileToString(source.Append("Preferences"), &prefs_before));
    const SiteDataRead read = ReadSiteData(source, Target(), kAll, temp_.GetPath());
    EXPECT_EQ(read.skipped, 0);
    EXPECT_EQ(read.storage.local_storage, 1);
    EXPECT_EQ(read.storage.indexed_db, 1);
    // Notifications allowed for the site; the geolocation ask stays behind.
    ASSERT_EQ(read.permissions.size(), 1u);
    EXPECT_EQ(read.permissions[0].type, "notifications");
    EXPECT_EQ(read.permissions[0].pattern, "https://lantern.example:443");
    EXPECT_EQ(read.permissions[0].setting, 1);
    ASSERT_EQ(read.addresses.size(), 1u);
    EXPECT_EQ(read.addresses[0].country, "US");
    EXPECT_THAT(read.addresses[0].fields,
                testing::UnorderedElementsAre(testing::Pair(7, "Ada Fixture"), testing::Pair(77, "1 Fixture Way"),
                                              testing::Pair(33, "Testville"), testing::Pair(35, "00000")));
    ASSERT_TRUE(read.search_engine);
    EXPECT_EQ(read.search_engine->name, source == Chrome() ? kFixtureSearchEngine : "Kagi");
    // The source is only read; the Web Data copy is gone.
    std::string prefs_after;
    ASSERT_TRUE(base::ReadFileToString(source.Append("Preferences"), &prefs_after));
    EXPECT_EQ(prefs_after, prefs_before);
    EXPECT_TRUE(base::PathExists(source.Append("Local Storage").Append("leveldb")));
    EXPECT_TRUE(IsEmptyDir(temp_.GetPath()));
    ASSERT_TRUE(base::DeletePathRecursively(Target()));
  }
}

TEST_F(CluSiteDataTest, ReadsOnlyWhatItIsAsked) {
  const SiteDataRead read = ReadSiteData(Chrome(), Target(), SiteDataOptions{.addresses = true}, temp_.GetPath());
  EXPECT_EQ(read.storage.sites(), 0);
  EXPECT_TRUE(read.permissions.empty());
  EXPECT_FALSE(read.search_engine);
  EXPECT_EQ(read.addresses.size(), 1u);
  EXPECT_FALSE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
  // A missing profile reads nothing.
  const SiteDataRead none = ReadSiteData(home_.GetPath().Append("nope"), Target(), kAll, temp_.GetPath());
  EXPECT_EQ(none.storage.sites(), 0);
  EXPECT_TRUE(none.addresses.empty());
}

TEST_F(CluSiteDataTest, SkipsJunkPermissions) {
  std::optional<base::Value> prefs = base::JSONReader::Read(R"({"profile":{"content_settings":{"exceptions":{
      "notifications":{"*,*":{"setting":1},"https://ok.example:443,*":{"setting":2},
                       "https://two.example:443,https://other.example:443":{"setting":1},
                       "https://bad example:443,*":{"setting":1},"https://ask.example:443,*":{"setting":3},
                       "https://text.example:443,*":"junk"},
      "media_stream_camera":[1,2],
      "unknown_type":{"https://x.example:443,*":{"setting":1}}}}}})",
                                                            base::JSON_PARSE_RFC);
  ASSERT_TRUE(prefs);
  int skipped = 0;
  const std::vector<ImportedPermission> read = ReadSitePermissions(prefs->GetDict(), &skipped);
  ASSERT_EQ(read.size(), 1u);
  EXPECT_EQ(read[0].pattern, "https://ok.example:443");
  EXPECT_EQ(read[0].setting, 2);
  // The wildcard, the embedded-only pattern and the pattern with a space.
  EXPECT_EQ(skipped, 3);
}

TEST_F(CluSiteDataTest, LeavesOutNonHttpsSearchEngines) {
  for (const char* url : {"http://plain.example/?q={searchTerms}", "https://noterms.example/", "javascript:x"}) {
    base::DictValue prefs;
    prefs.SetByDottedPath("default_search_provider_data.template_url_data",
                          base::DictValue().Set("short_name", "S").Set("keyword", "s").Set("url", url));
    EXPECT_FALSE(ReadDefaultSearchEngine(prefs)) << url;
  }
}

TEST_F(CluSiteDataTest, SkipsJunkAddressesAndCorruptWebData) {
  const base::FilePath web_data = temp_.GetPath().Append("Web Data");
  {
    sql::Database db(sql::test::kTestTag);
    ASSERT_TRUE(db.Open(web_data));
    ASSERT_TRUE(db.ExecuteScriptForTesting(
        "CREATE TABLE local_addresses (guid VARCHAR PRIMARY KEY, use_count INTEGER);"
        "CREATE TABLE local_addresses_type_tokens (guid VARCHAR, type INTEGER, value VARCHAR);"
        "INSERT INTO local_addresses VALUES ('a', 1), ('b', 9);"
        "INSERT INTO local_addresses_type_tokens VALUES ('a', 999, 'not a field'), ('a', 36, 'USA'),"
        "('b', 9, 'grace@lantern.example'), ('b', 36, 'de');"));
  }
  int skipped = 0;
  const std::vector<ImportedAddress> read = ReadAutofillAddresses(web_data, &skipped);
  ASSERT_EQ(read.size(), 1u);
  EXPECT_EQ(read[0].country, "DE");
  EXPECT_THAT(read[0].fields, testing::ElementsAre(testing::Pair(9, "grace@lantern.example")));
  EXPECT_EQ(skipped, 1);
  ASSERT_TRUE(base::WriteFile(web_data, "not sqlite"));
  skipped = 0;
  EXPECT_TRUE(ReadAutofillAddresses(web_data, &skipped).empty());
}

// Web Data from schema 134 on: local and account addresses share `addresses` and `address_type_tokens`, the local
// ones with record_type 0 and the ones a signed-in user saved with 1, and the local_* tables are gone. Both come over,
// most used first; the account's read-only home and work records (2, 3) stay.
TEST_F(CluSiteDataTest, ReadsSavedAddressesFromTheUnifiedTables) {
  const base::FilePath web_data = temp_.GetPath().Append("Web Data");
  {
    sql::Database db(sql::test::kTestTag);
    ASSERT_TRUE(db.Open(web_data));
    ASSERT_TRUE(db.ExecuteScriptForTesting(
        "CREATE TABLE addresses (guid VARCHAR PRIMARY KEY, use_count INTEGER NOT NULL DEFAULT 0,"
        " use_date INTEGER NOT NULL DEFAULT 0, date_modified INTEGER NOT NULL DEFAULT 0, language_code VARCHAR,"
        " label VARCHAR, initial_creator_id INTEGER DEFAULT 0, last_modifier_id INTEGER DEFAULT 0,"
        " record_type INTEGER);"
        "CREATE TABLE address_type_tokens (guid VARCHAR, type INTEGER, value VARCHAR, verification_status INTEGER"
        " DEFAULT 0, observations BLOB, PRIMARY KEY (guid, type));"
        "INSERT INTO addresses (guid, use_count, record_type) VALUES ('local-few', 2, 0), ('account', 50, 1),"
        " ('local-most', 9, 0), ('home', 80, 2), ('work', 70, 3);"
        "INSERT INTO address_type_tokens (guid, type, value) VALUES ('local-few', 7, 'Ada Fixture'),"
        " ('local-few', 36, 'us'), ('account', 7, 'Account Person'), ('local-most', 33, 'Testville'),"
        " ('local-most', 999, 'not a field'), ('home', 7, 'Home Record'), ('work', 7, 'Work Record');"));
    ASSERT_FALSE(db.DoesTableExist("local_addresses"));
  }
  int skipped = 0;
  const std::vector<ImportedAddress> read = ReadAutofillAddresses(web_data, &skipped);
  ASSERT_EQ(read.size(), 3u);
  EXPECT_THAT(read[0].fields, testing::ElementsAre(testing::Pair(7, "Account Person")));
  EXPECT_THAT(read[1].fields, testing::ElementsAre(testing::Pair(33, "Testville")));
  EXPECT_THAT(read[2].fields, testing::ElementsAre(testing::Pair(7, "Ada Fixture")));
  EXPECT_EQ(read[2].country, "US");
  EXPECT_EQ(skipped, 0);
}

TEST_F(CluSiteDataTest, StagedStorageMergesAtTheNextLaunchAndKeepsRiosOwnSites) {
  const StagedSiteStorage staged = StageSiteStorage(Chrome(), Target());
  EXPECT_EQ(staged.sites(), 2);
  EXPECT_TRUE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
  // Rio's profile already has Local Storage: a site of its own, and its own value for an item of a second import.
  {
    std::unique_ptr<leveldb::DB> db = Open(Target().Append("Local Storage").Append("leveldb"), /*create=*/true);
    ASSERT_TRUE(db);
    ASSERT_TRUE(db->Put(leveldb::WriteOptions(), "META:https://rio.example", "m").ok());
    ASSERT_TRUE(db->Put(leveldb::WriteOptions(), ItemKey("https://rio.example", "k"), "\x01mine").ok());
  }
  MergePendingSiteStorage(rio_.GetPath());
  EXPECT_FALSE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
  {
    std::unique_ptr<leveldb::DB> db = Open(Target().Append("Local Storage").Append("leveldb"));
    ASSERT_TRUE(db);
    EXPECT_EQ(Get(db.get(), ItemKey(kFixturePermissionSite, kFixtureLocalStorageKey)), "\x01yes");
    EXPECT_NE(Get(db.get(), std::string("META:") + kFixturePermissionSite), "<none>");
    EXPECT_EQ(Get(db.get(), ItemKey("https://rio.example", "k")), "\x01mine");
    // A second import of the same site keeps the value Rio has now.
    ASSERT_TRUE(db->Put(leveldb::WriteOptions(), ItemKey(kFixturePermissionSite, kFixtureLocalStorageKey), "\x01now")
                    .ok());
  }
  EXPECT_TRUE(base::DirectoryExists(Target().Append("IndexedDB").Append(kFixtureIndexedDbDir)));
  EXPECT_TRUE(base::DirectoryExists(Target().Append("IndexedDB").Append("https_lantern.example_0.indexeddb.blob")));
  ASSERT_EQ(StageSiteStorage(Arc(), Target()).sites(), 2);
  MergePendingSiteStorage(rio_.GetPath());
  std::unique_ptr<leveldb::DB> db = Open(Target().Append("Local Storage").Append("leveldb"));
  ASSERT_TRUE(db);
  EXPECT_EQ(Get(db.get(), ItemKey(kFixturePermissionSite, kFixtureLocalStorageKey)), "\x01now");
  // The source stayed as it was.
  EXPECT_TRUE(base::DirectoryExists(Chrome().Append("IndexedDB").Append(kFixtureIndexedDbDir)));
}

TEST_F(CluSiteDataTest, StagedStorageMovesInWhenRioHasNone) {
  ASSERT_EQ(StageSiteStorage(Arc(), Target()).local_storage, 1);
  MergePendingSiteStorage(rio_.GetPath());
  std::unique_ptr<leveldb::DB> db = Open(Target().Append("Local Storage").Append("leveldb"));
  ASSERT_TRUE(db);
  EXPECT_EQ(Get(db.get(), ItemKey(kFixturePermissionSite, kFixtureLocalStorageKey)), "\x01yes");
}

TEST_F(CluSiteDataTest, StagedStorageThatDidNotMergeStaysForTheNextLaunches) {
  ASSERT_EQ(StageSiteStorage(Chrome(), Target()).local_storage, 1);
  // Rio's Local Storage doesn't open (a damaged LevelDB): the batch stays for the next launch.
  const base::FilePath target_ls = Target().Append("Local Storage").Append("leveldb");
  ASSERT_TRUE(base::CreateDirectory(target_ls));
  ASSERT_TRUE(base::WriteFile(target_ls.Append("CURRENT"), "garbage"));
  MergePendingSiteStorage(rio_.GetPath());
  ASSERT_TRUE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
  EXPECT_FALSE(IsEmptyDir(Target().Append(kPendingSiteStorageDirName)));
  // Repaired by the next launch, it merges then and the staging folder goes.
  ASSERT_TRUE(base::DeletePathRecursively(target_ls));
  MergePendingSiteStorage(rio_.GetPath());
  EXPECT_FALSE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
  {
    std::unique_ptr<leveldb::DB> db = Open(target_ls);
    ASSERT_TRUE(db);
    EXPECT_EQ(Get(db.get(), ItemKey(kFixturePermissionSite, kFixtureLocalStorageKey)), "\x01yes");
  }

  // A target that never opens gives the batch up after a few launches, with a junk count in it too.
  ASSERT_TRUE(base::DeletePathRecursively(target_ls));
  ASSERT_TRUE(base::CreateDirectory(target_ls));
  ASSERT_TRUE(base::WriteFile(target_ls.Append("CURRENT"), "garbage"));
  ASSERT_EQ(StageSiteStorage(Chrome(), Target()).local_storage, 1);
  MergePendingSiteStorage(rio_.GetPath());
  MergePendingSiteStorage(rio_.GetPath());
  EXPECT_TRUE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
  MergePendingSiteStorage(rio_.GetPath());
  EXPECT_FALSE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
  ASSERT_EQ(StageSiteStorage(Chrome(), Target()).local_storage, 1);
  base::FileEnumerator batches(Target().Append(kPendingSiteStorageDirName), false, base::FileEnumerator::DIRECTORIES);
  const base::FilePath batch = batches.Next();
  ASSERT_FALSE(batch.empty());
  ASSERT_TRUE(base::WriteFile(batch.Append("merge-attempts"), "99999999999999"));
  MergePendingSiteStorage(rio_.GetPath());
  EXPECT_FALSE(base::PathExists(Target().Append(kPendingSiteStorageDirName)));
}

TEST_F(CluSiteDataTest, DoesNotStageLinksOrJunk) {
  const base::FilePath source = temp_.GetPath().Append("Linked");
  ASSERT_TRUE(base::CreateDirectory(source.Append("Local Storage")));
  ASSERT_TRUE(base::CreateSymbolicLink(Chrome().Append("Local Storage").Append("leveldb"),
                                       source.Append("Local Storage").Append("leveldb")));
  ASSERT_TRUE(base::CreateDirectory(source.Append("IndexedDB").Append("not-a-database")));
  ASSERT_TRUE(base::CreateDirectory(source.Append("IndexedDB")));
  ASSERT_TRUE(base::CreateSymbolicLink(Chrome().Append("IndexedDB").Append(kFixtureIndexedDbDir),
                                       source.Append("IndexedDB").Append("https_link.example_0.indexeddb.leveldb")));
  const StagedSiteStorage staged = StageSiteStorage(source, Target());
  EXPECT_EQ(staged.sites(), 0);
  EXPECT_EQ(staged.skipped, 2);
  EXPECT_TRUE(IsEmptyDir(Target().Append(kPendingSiteStorageDirName)));
  // A Local Storage folder that isn't a LevelDB is skipped too.
  const base::FilePath junk = temp_.GetPath().Append("Junk");
  ASSERT_TRUE(base::CreateDirectory(junk.Append("Local Storage").Append("leveldb")));
  ASSERT_TRUE(base::WriteFile(junk.Append("Local Storage").Append("leveldb").Append("CURRENT"), "garbage"));
  EXPECT_EQ(StageSiteStorage(junk, Target()).local_storage, 0);
}

}  // namespace
}  // namespace clu
