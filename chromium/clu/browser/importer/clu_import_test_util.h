// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_IMPORT_TEST_UTIL_H_
#define CLU_BROWSER_IMPORTER_CLU_IMPORT_TEST_UTIL_H_

#include <cstdint>

#include "base/files/file_path.h"

// The import fixtures (clu/test/data/import): a home directory with two Chrome profiles and a sidebar file. Tests
// copy it, so the source files stay untouched.
namespace clu {

// <src>/clu/test/data/import.
base::FilePath ImportFixtureRoot();

// Copies the fixture home to `to` and builds "Profile 1/History" from History.sql with times relative to `now_ms`
// (the file is never committed: its times would go stale). True on success.
bool CopyImportFixture(const base::FilePath& to, int64_t now_ms);

// The fake keychain secret the fixture's Login Data and Cookies are encrypted with. Never a real key.
inline constexpr char kFakeSafeStorageSecret[] = "clu-fixture-not-a-real-key";
// Builds "Login Data" (2 logins, 1 blocked site whose legacy row still has a username and password, 1 sealed with
// another key, 1 junk origin) and "Cookies" (version 24: 2 live cookies, 1 expired, 1 sealed with another key) in the
// browser profile folder `profile`, encrypted with kFakeSafeStorageSecret.
bool BuildSecrets(const base::FilePath& profile, int64_t now_ms);
// BuildSecrets for the sidebar file's Default profile under the fixture copy at `to`. CopyImportFixture calls it, and
// BuildSecrets for Chrome's "Profile 1".
bool BuildSidebarSecrets(const base::FilePath& to, int64_t now_ms);
// Adds a live, secure, plaintext cookie `name` for `host` in the partition `top_frame_site_key` (Chromium's serialized
// top-level site, or junk) to the Cookies file BuildSecrets made in `profile`.
bool AddPartitionedCookie(const base::FilePath& profile,
                          const char* host,
                          const char* name,
                          const char* top_frame_site_key,
                          int64_t now_ms);

// Site data: the site the fixture's Local Storage item, IndexedDB database and allowed notifications
// belong to, its Local Storage key, its IndexedDB folder, and the search engine a profile without one gets.
inline constexpr char kFixturePermissionSite[] = "https://lantern.example";
inline constexpr char kFixtureLocalStorageKey[] = "signed_in";
inline constexpr char kFixtureIndexedDbDir[] = "https_lantern.example_0.indexeddb.leveldb";
inline constexpr char kFixtureSearchEngine[] = "Fixture Search";
// Adds to the browser profile folder `profile`: Preferences permissions (notifications allowed for the site, an ask
// for another) and a search engine when it has none, a Web Data with one address (Ada Fixture, Testville, US), a
// Local Storage LevelDB with one item and an empty IndexedDB database for the site. CopyImportFixture calls it for
// Chrome's "Profile 1" and the sidebar file's Default profile.
bool BuildSiteData(const base::FilePath& profile);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_IMPORT_TEST_UTIL_H_
