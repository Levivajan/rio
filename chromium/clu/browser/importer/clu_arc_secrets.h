// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_ARC_SECRETS_H_
#define CLU_BROWSER_IMPORTER_CLU_ARC_SECRETS_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"

namespace sql {
class Database;
}  // namespace sql

// Saved passwords and cookies of the other browser's Chromium profiles. Its Login Data and Cookies files
// are Chromium's, with values encrypted the macOS way ("v10": AES-128-CBC with a key derived from the keychain item
// "<browser> Safe Storage" by PBKDF2-HMAC-SHA1, salt "saltysalt", 1003 rounds, IV of 16 spaces). Callers pass a copy
// of each file (the browser may be running) and the keychain secret; nothing here touches the keychain or logs a
// value. Rows that don't decrypt or carry a bad URL are skipped and counted. Blocking.
namespace clu {

// The keychain item that holds the other browser's key, and its account.
inline constexpr char kSidebarSafeStorageService[] = "Arc Safe Storage";
inline constexpr char kSidebarSafeStorageAccount[] = "Arc";
// Google Chrome's.
inline constexpr char kChromeSafeStorageService[] = "Chrome Safe Storage";
inline constexpr char kChromeSafeStorageAccount[] = "Chrome";

using SafeStorageKey = std::array<uint8_t, 16>;

// The AES key for a keychain secret. An empty secret gives nullopt.
std::optional<SafeStorageKey> DeriveSafeStorageKey(std::string_view secret);
// A "v10" value decrypted, or nullopt when it isn't one or doesn't decrypt.
std::optional<std::string> DecryptSafeStorageValue(const SafeStorageKey& key,
                                                   base::span<const uint8_t> blob);
// Tests: `plaintext` encrypted as a "v10" value.
std::vector<uint8_t> EncryptSafeStorageValueForTesting(const SafeStorageKey& key,
                                                       std::string_view plaintext);

// A saved password. `password` is plaintext: keep it in memory only, never log or write it anywhere but the
// password store.
struct SidebarLogin {
  std::string origin_url;
  std::string action_url;
  std::string signon_realm;
  std::string username_element;
  std::string username;
  std::string password_element;
  std::string password;
  // Chrome microseconds (since 1601).
  int64_t date_created = 0;
  bool blocked_by_user = false;
};

struct SidebarCookie {
  std::string host_key;
  std::string name;
  std::string value;
  std::string path;
  // Chrome microseconds; 0 is unset.
  int64_t creation_utc = 0;
  int64_t expires_utc = 0;
  int64_t last_access_utc = 0;
  bool secure = false;
  bool httponly = false;
  bool persistent = false;
  // Chromium's CookieSameSite (-1 unspecified, 0 none, 1 lax, 2 strict) and CookiePriority (0 low, 1 medium, 2 high).
  int samesite = -1;
  int priority = 1;
  // The partition: Chromium's serialized top-level site, empty for an unpartitioned cookie.
  std::string top_frame_site_key;
  bool has_cross_site_ancestor = false;
};

struct SidebarSecretsSkips {
  // Values that weren't "v10" or didn't decrypt with the key.
  int undecryptable = 0;
  // Logins without an http(s) origin, cookies without a host or name.
  int bad_rows = 0;
  // Rows past the cap.
  int over_cap = 0;
};

// At most this many rows of each file are read.
inline constexpr size_t kMaxSidebarLogins = 20000;
inline constexpr size_t kMaxSidebarCookies = 50000;

// Rows of a Login Data copy, or nullopt when it isn't a readable Login Data file.
std::optional<std::vector<SidebarLogin>> ReadSidebarLogins(
    const base::FilePath& login_data,
    const SafeStorageKey& key,
    SidebarSecretsSkips* skips);
// The rows of a profile's two password stores as one list: a login in both (the same signon realm, origin, username
// and form fields) is kept once, the copy created last. The cap applies to the merged list.
std::vector<SidebarLogin> MergeSidebarLogins(std::vector<SidebarLogin> profile,
                                             std::vector<SidebarLogin> account,
                                             SidebarSecretsSkips* skips);
// Rows of a Cookies copy, expired ones left out (not counted), or nullopt when it isn't a readable Cookies file.
// Cookies files from version 24 on put the SHA-256 of the host before each value; it is checked and dropped.
std::optional<std::vector<SidebarCookie>> ReadSidebarCookies(
    const base::FilePath& cookies,
    const SafeStorageKey& key,
    int64_t now_chrome_us,
    SidebarSecretsSkips* skips);

// What a browser profile's Login Data and Cookies hold.
struct ProfileSecrets {
  std::vector<SidebarLogin> logins;
  std::vector<SidebarCookie> cookies;
  SidebarSecretsSkips skips;
};

// Copies <profile>/Login Data and <profile>/Cookies (with their -journal and -wal files) into a new folder under
// `temp_root`, reads the copies and deletes them: the browser may be running and hold its files open, and they are
// only read. A file that is missing, a link or not a Chromium file gives no rows.
ProfileSecrets ReadProfileSecrets(const base::FilePath& profile,
                                  const SafeStorageKey& key,
                                  bool passwords,
                                  bool cookies,
                                  int64_t now_chrome_us,
                                  const base::FilePath& temp_root);

// Copies <profile>/<name> and its -journal and -wal files into `dir`; the copy's path, or nullopt when there is no
// such regular file (missing, a folder or a link). Site data reads its copy of Web Data the same way.
std::optional<base::FilePath> CopyWithSideFiles(const base::FilePath& profile,
                                                const char* name,
                                                const base::FilePath& dir);
// Opens a copied SQLite file and checks it; false for a corrupt or non-SQLite file.
bool OpenCopy(sql::Database& db, const base::FilePath& path);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_ARC_SECRETS_H_
