// Copyright 2026 The Rio Authors. All rights reserved.

#include <Security/Security.h>

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "clu/browser/importer/clu_arc_keychain.h"
#include "clu/browser/importer/clu_arc_secrets.h"
#include "clu/browser/importer/clu_chrome_history.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "crypto/apple/scoped_fake_keychain_v2.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

// Google Chrome's sign-ins. Its profile folders under Library/Application Support/Google/Chrome, the copy
// of a profile's Login Data and Cookies decrypted with the fixture's fake key, and the keychain item "Chrome Safe
// Storage" read through Chromium's fake keychain. Never a real profile or keychain.
namespace clu {
namespace {

constexpr int64_t kNowMs = 1'794'052'800'123;

class CluChromeSecretsTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ASSERT_TRUE(CopyImportFixture(home_.GetPath(), kNowMs));
    key_ = *DeriveSafeStorageKey(kFakeSafeStorageSecret);
  }
  base::FilePath Work() const { return *ChromeProfilePath(home_.GetPath(), "Profile 1"); }
  bool TempIsEmpty() const {
    return base::FileEnumerator(temp_.GetPath(), true,
                                base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES)
        .Next()
        .empty();
  }

  base::ScopedTempDir home_;
  base::ScopedTempDir temp_;
  SafeStorageKey key_{};
};

TEST_F(CluChromeSecretsTest, FindsChromeProfilesUnderGoogleChrome) {
  EXPECT_EQ(ChromeDataDir(home_.GetPath()),
            home_.GetPath().Append("Library").Append("Application Support").Append("Google").Append("Chrome"));
  const std::vector<ChromeProfileInfo> found = DetectChrome(home_.GetPath());
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[1].dir, "Profile 1");
  EXPECT_EQ(found[1].name, "Work");
  EXPECT_EQ(Work(), ChromeDataDir(home_.GetPath()).Append("Profile 1"));
  EXPECT_TRUE(base::PathExists(Work().Append("Cookies")));
  EXPECT_TRUE(base::PathExists(Work().Append("Login Data")));
  EXPECT_FALSE(ChromeProfilePath(home_.GetPath(), "System Profile"));
  EXPECT_STREQ(kChromeSafeStorageService, "Chrome Safe Storage");
  EXPECT_STREQ(kChromeSafeStorageAccount, "Chrome");
}

TEST_F(CluChromeSecretsTest, CopiesAndDecryptsCookiesAndPasswordsAndLeavesChromesFilesAlone) {
  std::string cookies_before, logins_before;
  ASSERT_TRUE(base::ReadFileToString(Work().Append("Cookies"), &cookies_before));
  ASSERT_TRUE(base::ReadFileToString(Work().Append("Login Data"), &logins_before));

  ProfileSecrets read = ReadProfileSecrets(Work(), key_, /*passwords=*/true, /*cookies=*/true,
                                           ToChromeTime(kNowMs), temp_.GetPath());
  // Two live cookies; the expired one is left out, the one sealed with another key skipped.
  ASSERT_EQ(read.cookies.size(), 2u);
  EXPECT_EQ(read.cookies[0].host_key, ".lantern.example");
  EXPECT_EQ(read.cookies[0].value, "fixture-cookie-1");
  EXPECT_EQ(read.cookies[1].value, "fixture-cookie-2");
  // Two logins and one blocked site; one sealed with another key and one junk origin skipped.
  ASSERT_EQ(read.logins.size(), 3u);
  EXPECT_EQ(read.logins[0].password, "fixture-pass-1");
  // The never-save row comes without the username and password its legacy row still has.
  EXPECT_TRUE(read.logins[2].blocked_by_user);
  EXPECT_TRUE(read.logins[2].username.empty());
  EXPECT_TRUE(read.logins[2].password.empty());
  EXPECT_EQ(read.skips.undecryptable, 2);
  EXPECT_EQ(read.skips.bad_rows, 1);

  // The copies are gone and Chrome's files are as they were.
  EXPECT_TRUE(TempIsEmpty());
  std::string cookies_after, logins_after;
  ASSERT_TRUE(base::ReadFileToString(Work().Append("Cookies"), &cookies_after));
  ASSERT_TRUE(base::ReadFileToString(Work().Append("Login Data"), &logins_after));
  EXPECT_EQ(cookies_after, cookies_before);
  EXPECT_EQ(logins_after, logins_before);
}

TEST_F(CluChromeSecretsTest, ReadsOnlyWhatWasTickedAndNothingWithAnotherKeyOrNoFiles) {
  ProfileSecrets cookies_only = ReadProfileSecrets(Work(), key_, false, true, ToChromeTime(kNowMs), temp_.GetPath());
  EXPECT_TRUE(cookies_only.logins.empty());
  EXPECT_EQ(cookies_only.cookies.size(), 2u);

  ProfileSecrets wrong = ReadProfileSecrets(Work(), *DeriveSafeStorageKey("not-the-secret"), true, true,
                                            ToChromeTime(kNowMs), temp_.GetPath());
  EXPECT_TRUE(wrong.cookies.empty());
  for (const SidebarLogin& l : wrong.logins) {
    EXPECT_TRUE(l.password.empty() || l.password.find("fixture-pass") == std::string::npos);
  }
  EXPECT_GT(wrong.skips.undecryptable, 0);

  // Default has neither file; a missing profile or temp folder gives nothing.
  ProfileSecrets none = ReadProfileSecrets(*ChromeProfilePath(home_.GetPath(), "Default"), key_, true, true,
                                           ToChromeTime(kNowMs), temp_.GetPath());
  EXPECT_TRUE(none.logins.empty());
  EXPECT_TRUE(none.cookies.empty());
  EXPECT_TRUE(ReadProfileSecrets(base::FilePath(), key_, true, true, 0, temp_.GetPath()).cookies.empty());
  EXPECT_TRUE(ReadProfileSecrets(Work(), key_, true, true, 0, base::FilePath()).cookies.empty());
  EXPECT_TRUE(TempIsEmpty());
}

// Passwords saved to the account sit in Login Data For Account; they come over with the profile's own, under one cap.
TEST_F(CluChromeSecretsTest, ReadsTheAccountPasswordStoreToo) {
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::CopyFile(Work().Append("Login Data"), profile.GetPath().Append("Login Data For Account")));
  ProfileSecrets account = ReadProfileSecrets(profile.GetPath(), key_, true, false, 0, temp_.GetPath());
  ASSERT_EQ(account.logins.size(), 3u);
  EXPECT_EQ(account.logins[0].password, "fixture-pass-1");
  EXPECT_EQ(account.skips.undecryptable, 1);
  EXPECT_EQ(account.skips.bad_rows, 1);

  // Both stores holding the same logins: each comes once.
  ASSERT_TRUE(base::CopyFile(Work().Append("Login Data"), profile.GetPath().Append("Login Data")));
  ProfileSecrets both = ReadProfileSecrets(profile.GetPath(), key_, true, false, 0, temp_.GetPath());
  EXPECT_EQ(both.logins.size(), 3u);
  // Passwords not ticked: neither store is read.
  EXPECT_TRUE(ReadProfileSecrets(profile.GetPath(), key_, false, true, 0, temp_.GetPath()).logins.empty());
  EXPECT_TRUE(TempIsEmpty());
}

// A partitioned cookie keeps its partition: one row per top-level site, each with its own key and ancestor bit. A file
// with partitions but no ancestor column counts them as embedded cross-site.
TEST_F(CluChromeSecretsTest, KeepsPartitionedCookiesPartitioned) {
  const int64_t now = ToChromeTime(kNowMs);
  const int64_t day = int64_t{86'400'000'000};
  // Created after the fixture's cookies, so they read last.
  const auto insert = [&](sql::Database& cookies, const char* site, std::optional<bool> ancestor, int64_t created) {
    const std::string query =
        ancestor ? "INSERT INTO cookies (creation_utc, host_key, top_frame_site_key, name, value, encrypted_value, "
                   "path, expires_utc, is_secure, is_httponly, last_access_utc, is_persistent, priority, samesite, "
                   "has_cross_site_ancestor) VALUES (?, 'widget.example', ?, 'w', 'partitioned', x'', '/', ?, 1, 0, "
                   "?, 1, 1, -1, ?)"
                 : "INSERT INTO cookies (creation_utc, host_key, top_frame_site_key, name, value, encrypted_value, "
                   "path, expires_utc, is_secure, is_httponly, last_access_utc, is_persistent, priority, samesite) "
                   "VALUES (?, 'widget.example', ?, 'w', 'partitioned', x'', '/', ?, 1, 0, ?, 1, 1, -1)";
    sql::Statement s(cookies.GetUniqueStatement(query));
    s.BindInt64(0, created);
    s.BindString(1, site);
    s.BindInt64(2, now + day);
    s.BindInt64(3, created);
    if (ancestor) {
      s.BindBool(4, *ancestor);
    }
    return s.Run();
  };
  {
    sql::Database cookies(sql::test::kTestTag);
    ASSERT_TRUE(cookies.Open(Work().Append("Cookies")));
    ASSERT_TRUE(insert(cookies, "https://news.example", std::nullopt, now - day + 1));
  }
  ProfileSecrets read = ReadProfileSecrets(Work(), key_, false, true, ToChromeTime(kNowMs), temp_.GetPath());
  ASSERT_EQ(read.cookies.size(), 3u);
  EXPECT_EQ(read.cookies[0].top_frame_site_key, "");
  EXPECT_EQ(read.cookies[2].host_key, "widget.example");
  EXPECT_EQ(read.cookies[2].top_frame_site_key, "https://news.example");
  EXPECT_TRUE(read.cookies[2].has_cross_site_ancestor);

  {
    sql::Database cookies(sql::test::kTestTag);
    ASSERT_TRUE(cookies.Open(Work().Append("Cookies")));
    ASSERT_TRUE(cookies.Execute(
        "ALTER TABLE cookies ADD COLUMN has_cross_site_ancestor INTEGER NOT NULL DEFAULT 1"));
    ASSERT_TRUE(insert(cookies, "https://shop.example", false, now - day + 2));
  }
  read = ReadProfileSecrets(Work(), key_, false, true, ToChromeTime(kNowMs), temp_.GetPath());
  ASSERT_EQ(read.cookies.size(), 4u);
  EXPECT_EQ(read.cookies[2].top_frame_site_key, "https://news.example");
  EXPECT_TRUE(read.cookies[2].has_cross_site_ancestor);
  EXPECT_EQ(read.cookies[3].top_frame_site_key, "https://shop.example");
  EXPECT_FALSE(read.cookies[3].has_cross_site_ancestor);
}

// A login in both password stores comes once, the copy created last, and the cap applies after the merge.
TEST(CluChromeLoginsMergeTest, KeepsTheNewerCopyOfALoginInBothStores) {
  const auto login = [](std::string username, std::string password, int64_t created) {
    SidebarLogin l;
    l.origin_url = "https://example.com/";
    l.signon_realm = "https://example.com/";
    l.username_element = "user";
    l.password_element = "pass";
    l.username = std::move(username);
    l.password = std::move(password);
    l.date_created = created;
    return l;
  };
  SidebarSecretsSkips skips;
  std::vector<SidebarLogin> profile = {login("alice", "new", 200), login("bob", "b", 100)};
  std::vector<SidebarLogin> account = {login("alice", "old", 100), login("carol", "c", 100)};
  std::vector<SidebarLogin> merged = MergeSidebarLogins(std::move(profile), std::move(account), &skips);
  ASSERT_EQ(merged.size(), 3u);
  EXPECT_EQ(merged[0].username, "alice");
  EXPECT_EQ(merged[0].password, "new");
  EXPECT_EQ(merged[1].username, "bob");
  EXPECT_EQ(merged[2].username, "carol");
  EXPECT_EQ(skips.over_cap, 0);

  // The account copy is the newer one.
  merged = MergeSidebarLogins({login("alice", "old", 100)}, {login("alice", "new", 300)}, &skips);
  ASSERT_EQ(merged.size(), 1u);
  EXPECT_EQ(merged[0].password, "new");

  // A different form field is a different login.
  SidebarLogin other = login("alice", "x", 100);
  other.password_element = "pw";
  EXPECT_EQ(MergeSidebarLogins({login("alice", "y", 100)}, {other}, &skips).size(), 2u);

  // Duplicates no longer use up the cap: a full profile store plus its own copies in the account store fits.
  std::vector<SidebarLogin> full;
  for (size_t i = 0; i < kMaxSidebarLogins; ++i) {
    full.push_back(login("u" + std::to_string(i), "p", 100));
  }
  std::vector<SidebarLogin> copies = full;
  copies.push_back(login("extra", "p", 100));
  merged = MergeSidebarLogins(std::move(full), std::move(copies), &skips);
  EXPECT_EQ(merged.size(), kMaxSidebarLogins);
  EXPECT_EQ(skips.over_cap, 1);
}

TEST_F(CluChromeSecretsTest, AJunkCookiesFileGivesNoRows) {
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(profile.GetPath().Append("Cookies"), "not a database"));
  ProfileSecrets read = ReadProfileSecrets(profile.GetPath(), key_, true, true, 0, temp_.GetPath());
  EXPECT_TRUE(read.cookies.empty());
  EXPECT_TRUE(read.logins.empty());
}

// The mock keychain answers with its own fake secret; a refused prompt gives none, so the import skips the sign-ins.
TEST(CluChromeKeychainTest, ReadsChromeSafeStorageAndNothingWhenRefused) {
  crypto::apple::ScopedFakeKeychainV2 keychain("test");
  const std::optional<std::string> secret =
      ReadSafeStorageSecret(kChromeSafeStorageService, kChromeSafeStorageAccount);
  ASSERT_TRUE(secret);
  EXPECT_EQ(*secret, keychain.GetEncryptionPassword());
  EXPECT_TRUE(DeriveSafeStorageKey(*secret));
  keychain.SetFindGenericResult(errSecUserCanceled);
  EXPECT_FALSE(ReadSafeStorageSecret(kChromeSafeStorageService, kChromeSafeStorageAccount));
  keychain.SetFindGenericResult(errSecItemNotFound);
  EXPECT_FALSE(ReadSafeStorageSecret(kChromeSafeStorageService, kChromeSafeStorageAccount));
}

}  // namespace
}  // namespace clu
