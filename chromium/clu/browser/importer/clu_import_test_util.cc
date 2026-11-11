// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_test_util.h"

#include <memory>
#include <string>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "clu/browser/importer/clu_arc_secrets.h"
#include "clu/browser/importer/clu_chrome_history.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"

namespace clu {

base::FilePath ImportFixtureRoot() {
  base::FilePath src;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src));
  return src.AppendASCII("clu").AppendASCII("test").AppendASCII("data").AppendASCII(
      "import");
}

namespace {

// <profile>/History from <profile>/History.sql.
bool BuildHistory(const base::FilePath& profile, int64_t now_ms) {
  std::string script;
  if (!base::ReadFileToString(profile.Append("History.sql"), &script)) {
    return false;
  }
  base::ReplaceSubstringsAfterOffset(&script, 0, "{{NOW}}",
                                     base::NumberToString(ToChromeTime(now_ms)));
  sql::Database db(sql::test::kTestTag);
  return db.Open(profile.Append("History")) &&
         db.ExecuteScriptForTesting(script);
}

bool InsertLogin(sql::Database& db, const char* origin, const char* realm, const char* user,
                 base::span<const uint8_t> password, int64_t created, bool blocked) {
  sql::Statement s(db.GetUniqueStatement(
      "INSERT INTO logins (origin_url, action_url, signon_realm, username_element, username_value, "
      "password_element, password_value, date_created, blacklisted_by_user) VALUES (?, ?, ?, 'email', ?, 'pw', ?, ?, ?)"));
  s.BindString(0, origin);
  s.BindString(1, origin);
  s.BindString(2, realm);
  s.BindString(3, user);
  s.BindBlob(4, password);
  s.BindInt64(5, created);
  s.BindBool(6, blocked);
  return s.Run();
}

bool InsertCookie(sql::Database& db, const SafeStorageKey& key, const char* host, const char* name,
                  const char* value, int64_t created, int64_t expires) {
  const auto hash = crypto::hash::Sha256(std::string_view(host));
  std::string plain(base::as_string_view(hash));
  plain += value;
  sql::Statement s(db.GetUniqueStatement(
      "INSERT INTO cookies (host_key, name, value, encrypted_value, path, creation_utc, expires_utc, last_access_utc, "
      "is_secure, is_httponly, is_persistent, samesite, priority) VALUES (?, ?, '', ?, '/', ?, ?, ?, 1, 1, 1, 1, 1)"));
  s.BindString(0, host);
  s.BindString(1, name);
  s.BindBlob(2, EncryptSafeStorageValueForTesting(key, plain));
  s.BindInt64(3, created);
  s.BindInt64(4, expires);
  s.BindInt64(5, created);
  return s.Run();
}

}  // namespace

bool BuildSecrets(const base::FilePath& profile, int64_t now_ms) {
  const SafeStorageKey key = *DeriveSafeStorageKey(kFakeSafeStorageSecret);
  const SafeStorageKey other = *DeriveSafeStorageKey("some-other-key");
  const int64_t now = ToChromeTime(now_ms);
  const int64_t day = int64_t{86400} * 1000 * 1000;

  sql::Database logins(sql::test::kTestTag);
  if (!logins.Open(profile.Append("Login Data")) ||
      !logins.ExecuteScriptForTesting(
          "CREATE TABLE meta (key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);"
          "INSERT INTO meta VALUES ('version', '43');"
          "CREATE TABLE logins (origin_url VARCHAR NOT NULL, action_url VARCHAR, username_element VARCHAR, "
          "username_value VARCHAR, password_element VARCHAR, password_value BLOB, signon_realm VARCHAR NOT NULL, "
          "date_created INTEGER NOT NULL, blacklisted_by_user INTEGER NOT NULL)") ||
      !InsertLogin(logins, "https://lantern.example/login", "https://lantern.example/", "ada@lantern.example",
                   EncryptSafeStorageValueForTesting(key, "fixture-pass-1"), now - 3 * day, false) ||
      !InsertLogin(logins, "https://orchard.example/", "https://orchard.example/", "grace",
                   EncryptSafeStorageValueForTesting(key, "fixture-pass-2"), now - 2 * day, false) ||
      !InsertLogin(logins, "https://never.example/", "https://never.example/", "legacy-user",
                   EncryptSafeStorageValueForTesting(key, "legacy-pass"), now - day, true) ||
      !InsertLogin(logins, "https://sealed.example/", "https://sealed.example/", "x",
                   EncryptSafeStorageValueForTesting(other, "not-ours"), now - day, false) ||
      !InsertLogin(logins, "javascript:alert(1)", "javascript:", "y",
                   EncryptSafeStorageValueForTesting(key, "junk"), now - day, false)) {
    return false;
  }

  sql::Database cookies(sql::test::kTestTag);
  return cookies.Open(profile.Append("Cookies")) &&
         cookies.ExecuteScriptForTesting(
             "CREATE TABLE meta (key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);"
             "INSERT INTO meta VALUES ('version', '24');"
             "CREATE TABLE cookies (creation_utc INTEGER NOT NULL, host_key TEXT NOT NULL, top_frame_site_key TEXT "
             "NOT NULL DEFAULT '', name TEXT NOT NULL, value TEXT NOT NULL, encrypted_value BLOB NOT NULL, path TEXT "
             "NOT NULL, expires_utc INTEGER NOT NULL, is_secure INTEGER NOT NULL, is_httponly INTEGER NOT NULL, "
             "last_access_utc INTEGER NOT NULL, has_expires INTEGER NOT NULL DEFAULT 1, is_persistent INTEGER NOT "
             "NULL, priority INTEGER NOT NULL, samesite INTEGER NOT NULL)") &&
         InsertCookie(cookies, key, ".lantern.example", "session", "fixture-cookie-1", now - day, now + 30 * day) &&
         InsertCookie(cookies, key, "orchard.example", "sid", "fixture-cookie-2", now - day, now + day) &&
         InsertCookie(cookies, key, "old.example", "gone", "expired", now - 9 * day, now - day) &&
         InsertCookie(cookies, other, "sealed.example", "sid", "not-ours", now - day, now + day);
}

namespace {

// A LevelDB at `dir` with `rows`; Local Storage's and IndexedDB's folders are LevelDBs.
bool BuildLevelDb(const base::FilePath& dir, std::initializer_list<std::pair<std::string, std::string>> rows) {
  leveldb_env::Options options;
  options.create_if_missing = true;
  std::unique_ptr<leveldb::DB> db;
  if (!base::CreateDirectory(dir) || !leveldb_env::OpenDB(options, dir.AsUTF8Unsafe(), &db).ok()) {
    return false;
  }
  for (const auto& [key, value] : rows) {
    if (!db->Put(leveldb::WriteOptions(), key, value).ok()) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool BuildSiteData(const base::FilePath& profile) {
  // Preferences: the profile's own, or a new one, with the permissions and (when it has none) a search engine.
  base::DictValue prefs;
  std::string text;
  if (base::ReadFileToString(profile.Append("Preferences"), &text)) {
    std::optional<base::Value> parsed = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return false;
    }
    prefs = std::move(*parsed).TakeDict();
  }
  prefs.SetByDottedPath("profile.content_settings.exceptions.notifications",
                        base::DictValue().Set(std::string(kFixturePermissionSite) + ":443,*",
                                              base::DictValue().Set("setting", 1)));
  // An ask is Rio's default already, so it doesn't come over.
  prefs.SetByDottedPath("profile.content_settings.exceptions.geolocation",
                        base::DictValue().Set("https://orchard.example:443,*", base::DictValue().Set("setting", 3)));
  if (!prefs.FindDictByDottedPath("default_search_provider_data.template_url_data")) {
    prefs.SetByDottedPath("default_search_provider_data.template_url_data",
                          base::DictValue()
                              .Set("short_name", kFixtureSearchEngine)
                              .Set("keyword", "search.example")
                              .Set("url", "https://search.example/find?q={searchTerms}"));
  }
  std::optional<std::string> json = base::WriteJson(prefs);
  if (!json || !base::WriteFile(profile.Append("Preferences"), *json)) {
    return false;
  }

  // Web Data as Chrome writes it since schema 134 for a signed-in user: the saved address is an account address
  // (record_type 1) and comes over; the account's read-only home record (record_type 2) stays behind.
  sql::Database web_data(sql::test::kTestTag);
  if (!web_data.Open(profile.Append("Web Data")) ||
      !web_data.ExecuteScriptForTesting(
          "CREATE TABLE addresses (guid VARCHAR PRIMARY KEY, use_count INTEGER NOT NULL DEFAULT 0, use_date "
          "INTEGER NOT NULL DEFAULT 0, date_modified INTEGER NOT NULL DEFAULT 0, language_code VARCHAR, label "
          "VARCHAR, initial_creator_id INTEGER DEFAULT 0, last_modifier_id INTEGER DEFAULT 0, record_type INTEGER);"
          "CREATE TABLE address_type_tokens (guid VARCHAR, type INTEGER, value VARCHAR, "
          "verification_status INTEGER DEFAULT 0, observations BLOB, PRIMARY KEY (guid, type));"
          "INSERT INTO addresses (guid, use_count, record_type) VALUES "
          "('00000000-0000-4000-8000-000000000001', 3, 1), ('00000000-0000-4000-8000-000000000002', 9, 2);"
          "INSERT INTO address_type_tokens (guid, type, value) VALUES "
          "('00000000-0000-4000-8000-000000000002', 7, 'Account Fixture'),"
          "('00000000-0000-4000-8000-000000000001', 7, 'Ada Fixture'),"
          "('00000000-0000-4000-8000-000000000001', 77, '1 Fixture Way'),"
          "('00000000-0000-4000-8000-000000000001', 33, 'Testville'),"
          "('00000000-0000-4000-8000-000000000001', 35, '00000'),"
          "('00000000-0000-4000-8000-000000000001', 36, 'US');")) {
    return false;
  }
  web_data.Close();

  // Local Storage: one site with one item ("META:" holds its LocalStorageOriginMetaData proto: last_modified 1,
  // size_bytes 12; a value starts with its format byte, 1 for Latin-1).
  const std::string origin = kFixturePermissionSite;
  return BuildLevelDb(profile.Append("Local Storage").Append("leveldb"),
                      {{"VERSION", "1"},
                       {"META:" + origin, std::string("\x08\x01\x10\x0c", 4)},
                       {"_" + origin + std::string(1, '\0') + kFixtureLocalStorageKey, std::string("\x01yes", 4)}}) &&
         BuildLevelDb(profile.Append("IndexedDB").Append(kFixtureIndexedDbDir), {}) &&
         base::CreateDirectory(profile.Append("IndexedDB").Append("https_lantern.example_0.indexeddb.blob"));
}

bool AddPartitionedCookie(const base::FilePath& profile,
                          const char* host,
                          const char* name,
                          const char* top_frame_site_key,
                          int64_t now_ms) {
  const int64_t now = ToChromeTime(now_ms);
  const int64_t day = int64_t{86400} * 1000 * 1000;
  sql::Database cookies(sql::test::kTestTag);
  if (!cookies.Open(profile.Append("Cookies"))) {
    return false;
  }
  sql::Statement s(cookies.GetUniqueStatement(
      "INSERT INTO cookies (creation_utc, host_key, top_frame_site_key, name, value, encrypted_value, path, "
      "expires_utc, is_secure, is_httponly, last_access_utc, is_persistent, priority, samesite) "
      "VALUES (?, ?, ?, ?, 'partitioned', x'', '/', ?, 1, 0, ?, 1, 1, 0)"));
  s.BindInt64(0, now - day);
  s.BindString(1, host);
  s.BindString(2, top_frame_site_key);
  s.BindString(3, name);
  s.BindInt64(4, now + day);
  s.BindInt64(5, now - day);
  return s.Run();
}

bool BuildSidebarSecrets(const base::FilePath& to, int64_t now_ms) {
  return BuildSecrets(SidebarUserDataDir(to).Append("Default"), now_ms);
}

bool CopyImportFixture(const base::FilePath& to, int64_t now_ms) {
  // `to` exists, so the copy lands at <to>/Library.
  if (!base::CreateDirectory(to) ||
      !base::CopyDirectory(ImportFixtureRoot().Append("Library"), to, true)) {
    return false;
  }
  return BuildHistory(ChromeDataDir(to).Append("Profile 1"), now_ms) &&
         BuildHistory(SidebarUserDataDir(to).Append("Default"), now_ms) &&
         BuildHistory(SidebarUserDataDir(to).Append("Profile 1"), now_ms) &&
         BuildSidebarSecrets(to, now_ms) &&
         BuildSecrets(ChromeDataDir(to).Append("Profile 1"), now_ms) &&
         BuildSiteData(SidebarUserDataDir(to).Append("Default")) &&
         BuildSiteData(ChromeDataDir(to).Append("Profile 1"));
}

}  // namespace clu
