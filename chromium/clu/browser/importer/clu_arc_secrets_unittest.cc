// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_arc_secrets.h"

#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "clu/browser/importer/clu_chrome_history.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_import_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

// The other browser's saved passwords and cookies, from the fixture encrypted with a fake keychain secret.
namespace clu {
namespace {

constexpr int64_t kNowMs = 1'794'052'800'123;

class CluArcSecretsTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(home_.CreateUniqueTempDir());
    ASSERT_TRUE(CopyImportFixture(home_.GetPath(), kNowMs));
    key_ = *DeriveSafeStorageKey(kFakeSafeStorageSecret);
  }
  base::FilePath Profile() const { return SidebarUserDataDir(home_.GetPath()).Append("Default"); }

  base::ScopedTempDir home_;
  SafeStorageKey key_{};
};

TEST_F(CluArcSecretsTest, DerivesTheMacKeyAndRoundTripsAValue) {
  EXPECT_EQ(DeriveSafeStorageKey("peanuts"), DeriveSafeStorageKey("peanuts"));
  EXPECT_NE(DeriveSafeStorageKey("peanuts"), DeriveSafeStorageKey("almonds"));
  EXPECT_FALSE(DeriveSafeStorageKey(""));
  const std::vector<uint8_t> sealed = EncryptSafeStorageValueForTesting(key_, "hello");
  EXPECT_EQ(DecryptSafeStorageValue(key_, sealed), "hello");
  EXPECT_NE(DecryptSafeStorageValue(*DeriveSafeStorageKey("wrong"), sealed), "hello");
  const std::string junk = "v11abc";
  EXPECT_FALSE(DecryptSafeStorageValue(key_, base::as_byte_span(junk)));
  EXPECT_FALSE(DecryptSafeStorageValue(key_, {}));
}

TEST_F(CluArcSecretsTest, ReadsLoginsAndSkipsJunkAndForeignKeys) {
  SidebarSecretsSkips skips;
  const auto logins = ReadSidebarLogins(Profile().Append("Login Data"), key_, &skips);
  ASSERT_TRUE(logins);
  ASSERT_EQ(logins->size(), 3u);
  EXPECT_EQ((*logins)[0].origin_url, "https://lantern.example/login");
  EXPECT_EQ((*logins)[0].username, "ada@lantern.example");
  EXPECT_EQ((*logins)[0].password, "fixture-pass-1");
  EXPECT_EQ((*logins)[1].password, "fixture-pass-2");
  // The never-save row still has a username and password in the file; PasswordStore::AddLogins CHECKs that a
  // never-save form has neither, so the reader drops them and keeps the site.
  EXPECT_EQ((*logins)[2].signon_realm, "https://never.example/");
  EXPECT_TRUE((*logins)[2].blocked_by_user);
  EXPECT_TRUE((*logins)[2].username.empty());
  EXPECT_TRUE((*logins)[2].password.empty());
  EXPECT_EQ(skips.undecryptable, 1);
  EXPECT_EQ(skips.bad_rows, 1);
}

TEST_F(CluArcSecretsTest, ReadsLiveCookiesAndDropsTheHostHash) {
  SidebarSecretsSkips skips;
  const auto cookies = ReadSidebarCookies(Profile().Append("Cookies"), key_, ToChromeTime(kNowMs), &skips);
  ASSERT_TRUE(cookies);
  ASSERT_EQ(cookies->size(), 2u);
  EXPECT_EQ((*cookies)[0].host_key, ".lantern.example");
  EXPECT_EQ((*cookies)[0].value, "fixture-cookie-1");
  EXPECT_TRUE((*cookies)[0].secure);
  EXPECT_TRUE((*cookies)[0].httponly);
  EXPECT_EQ((*cookies)[1].value, "fixture-cookie-2");
  EXPECT_EQ(skips.undecryptable, 1);
}

TEST_F(CluArcSecretsTest, AWrongKeyDecryptsNothing) {
  const SafeStorageKey wrong = *DeriveSafeStorageKey("not-the-secret");
  SidebarSecretsSkips skips;
  const auto logins = ReadSidebarLogins(Profile().Append("Login Data"), wrong, &skips);
  ASSERT_TRUE(logins);
  for (const SidebarLogin& l : *logins) {
    EXPECT_NE(l.password, "fixture-pass-1");
    EXPECT_NE(l.password, "fixture-pass-2");
  }
}

TEST_F(CluArcSecretsTest, CorruptOrMissingFilesGiveNullopt) {
  SidebarSecretsSkips skips;
  const base::FilePath junk = home_.GetPath().Append("junk");
  ASSERT_TRUE(base::WriteFile(junk, "not a database"));
  EXPECT_FALSE(ReadSidebarLogins(junk, key_, &skips));
  EXPECT_FALSE(ReadSidebarCookies(junk, key_, 0, &skips));
  EXPECT_FALSE(ReadSidebarLogins(home_.GetPath().Append("missing"), key_, &skips));
}

}  // namespace
}  // namespace clu
