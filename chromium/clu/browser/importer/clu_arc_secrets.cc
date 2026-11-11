// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_arc_secrets.h"

#include <map>
#include <tuple>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/cstring_view.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "crypto/aes_cbc.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/mem.h"

namespace clu {

namespace {

constexpr char kSalt[] = "saltysalt";
constexpr unsigned kRounds = 1003;
constexpr char kPrefix[] = "v10";
constexpr size_t kPrefixLen = 3;
// The hash of the host Cookies files put before each value from this version on.
constexpr int kHashedCookiesVersion = 24;
// A value longer than this is junk.
constexpr size_t kMaxValue = 64 * 1024;

constexpr std::array<uint8_t, crypto::aes_cbc::kBlockSize> Iv() {
  std::array<uint8_t, crypto::aes_cbc::kBlockSize> iv{};
  iv.fill(' ');
  return iv;
}

}  // namespace

// Opens a copy read-only for the rows below; corrupt files fail here or at the first statement.
bool OpenCopy(sql::Database& db, const base::FilePath& path) {
  db.set_error_callback(base::BindRepeating([](int, sql::Statement*) {}));
  if (!db.Open(path)) {
    return false;
  }
  sql::Statement check(db.GetUniqueStatement("PRAGMA quick_check"));
  return check.is_valid() && check.Step() && check.ColumnString(0) == "ok";
}

namespace {

int MetaVersion(sql::Database& db) {
  if (!db.DoesTableExist("meta")) {
    return 0;
  }
  sql::Statement s(db.GetUniqueStatement("SELECT value FROM meta WHERE key='version'"));
  int version = 0;
  return s.is_valid() && s.Step() && base::StringToInt(s.ColumnString(0), &version) ? version : 0;
}

}  // namespace

std::optional<SafeStorageKey> DeriveSafeStorageKey(std::string_view secret) {
  if (secret.empty()) {
    return std::nullopt;
  }
  SafeStorageKey key{};
  if (!PKCS5_PBKDF2_HMAC_SHA1(secret.data(), secret.size(),
                              reinterpret_cast<const uint8_t*>(kSalt), sizeof(kSalt) - 1, kRounds,
                              key.size(), key.data())) {
    return std::nullopt;
  }
  return key;
}

std::optional<std::string> DecryptSafeStorageValue(const SafeStorageKey& key,
                                                   base::span<const uint8_t> blob) {
  if (blob.size() <= kPrefixLen || blob.size() > kMaxValue ||
      base::as_string_view(blob.first(kPrefixLen)) != kPrefix) {
    return std::nullopt;
  }
  base::span<const uint8_t> cipher = blob.subspan(kPrefixLen);
  if (cipher.size() % crypto::aes_cbc::kBlockSize != 0) {
    return std::nullopt;
  }
  static constexpr auto kIv = Iv();
  std::optional<std::vector<uint8_t>> plain = crypto::aes_cbc::Decrypt(key, kIv, cipher);
  if (!plain) {
    return std::nullopt;
  }
  std::string out(base::as_string_view(*plain));
  OPENSSL_cleanse(plain->data(), plain->size());
  return out;
}

std::vector<uint8_t> EncryptSafeStorageValueForTesting(const SafeStorageKey& key,
                                                       std::string_view plaintext) {
  static constexpr auto kIv = Iv();
  const std::string_view prefix(kPrefix);
  std::vector<uint8_t> out(prefix.begin(), prefix.end());
  std::vector<uint8_t> cipher = crypto::aes_cbc::Encrypt(key, kIv, base::as_byte_span(plaintext));
  out.insert(out.end(), cipher.begin(), cipher.end());
  return out;
}

std::optional<std::vector<SidebarLogin>> ReadSidebarLogins(const base::FilePath& login_data,
                                                           const SafeStorageKey& key,
                                                           SidebarSecretsSkips* skips) {
  sql::Database db(sql::Database::Tag("CluModel"));
  if (!OpenCopy(db, login_data) || !db.DoesTableExist("logins")) {
    return std::nullopt;
  }
  sql::Statement s(db.GetUniqueStatement(
      "SELECT origin_url, action_url, signon_realm, username_element, username_value, password_element, "
      "password_value, date_created, blacklisted_by_user FROM logins ORDER BY date_created"));
  if (!s.is_valid()) {
    return std::nullopt;
  }
  std::vector<SidebarLogin> logins;
  while (s.Step()) {
    if (logins.size() >= kMaxSidebarLogins) {
      ++skips->over_cap;
      continue;
    }
    SidebarLogin login;
    login.blocked_by_user = s.ColumnBool(8);
    std::optional<std::string> origin = ImportUrl(s.ColumnString(0));
    if (!origin || s.ColumnString(2).empty()) {
      ++skips->bad_rows;
      continue;
    }
    if (!login.blocked_by_user) {
      std::optional<std::string> password = DecryptSafeStorageValue(key, s.ColumnBlob(6));
      if (!password) {
        ++skips->undecryptable;
        continue;
      }
      login.password = std::move(*password);
    }
    login.origin_url = std::move(*origin);
    login.action_url = ImportUrl(s.ColumnString(1)).value_or(std::string());
    login.signon_realm = s.ColumnString(2);
    login.username_element = s.ColumnString(3);
    // A never-save row keeps no username or password, as Chromium's password store requires; a legacy or corrupt
    // row may still carry them.
    if (!login.blocked_by_user) {
      login.username = s.ColumnString(4);
    }
    login.password_element = s.ColumnString(5);
    login.date_created = s.ColumnInt64(7);
    logins.push_back(std::move(login));
  }
  return logins;
}

std::vector<SidebarLogin> MergeSidebarLogins(std::vector<SidebarLogin> profile,
                                             std::vector<SidebarLogin> account,
                                             SidebarSecretsSkips* skips) {
  using Key = std::tuple<std::string, std::string, std::string, std::string, std::string>;
  const auto key_of = [](const SidebarLogin& login) {
    return Key(login.signon_realm, login.origin_url, login.username_element, login.username,
               login.password_element);
  };
  std::vector<SidebarLogin> out;
  std::map<Key, size_t> index;
  for (std::vector<SidebarLogin>* list : {&profile, &account}) {
    for (SidebarLogin& login : *list) {
      auto [it, added] = index.try_emplace(key_of(login), out.size());
      if (added) {
        out.push_back(std::move(login));
      } else if (login.date_created > out[it->second].date_created) {
        out[it->second] = std::move(login);
      }
    }
  }
  if (out.size() > kMaxSidebarLogins) {
    skips->over_cap += static_cast<int>(out.size() - kMaxSidebarLogins);
    out.resize(kMaxSidebarLogins);
  }
  return out;
}

std::optional<std::vector<SidebarCookie>> ReadSidebarCookies(const base::FilePath& cookies,
                                                             const SafeStorageKey& key,
                                                             int64_t now_chrome_us,
                                                             SidebarSecretsSkips* skips) {
  sql::Database db(sql::Database::Tag("CluModel"));
  if (!OpenCopy(db, cookies) || !db.DoesTableExist("cookies")) {
    return std::nullopt;
  }
  const bool hashed = MetaVersion(db) >= kHashedCookiesVersion;
  // A partitioned cookie belongs to one top-level site only: it keeps its partition, so it never goes to every site
  // that embeds its host. Older files have no partitions, or no ancestor bit.
  const bool partitions = db.DoesColumnExist("cookies", "top_frame_site_key");
  const bool ancestors = partitions && db.DoesColumnExist("cookies", "has_cross_site_ancestor");
  const std::string query = base::StrCat(
      {"SELECT host_key, name, value, encrypted_value, path, creation_utc, expires_utc, last_access_utc, "
       "is_secure, is_httponly, is_persistent, samesite, priority, ",
       partitions ? "top_frame_site_key, " : "'', ", ancestors ? "has_cross_site_ancestor " : "1 ",
       "FROM cookies ORDER BY creation_utc"});
  sql::Statement s(db.GetUniqueStatement(query));
  if (!s.is_valid()) {
    return std::nullopt;
  }
  std::vector<SidebarCookie> out;
  while (s.Step()) {
    SidebarCookie c;
    c.host_key = s.ColumnString(0);
    c.name = s.ColumnString(1);
    c.expires_utc = s.ColumnInt64(6);
    c.persistent = s.ColumnBool(10);
    if (c.persistent && c.expires_utc > 0 && c.expires_utc <= now_chrome_us) {
      continue;
    }
    if (out.size() >= kMaxSidebarCookies) {
      ++skips->over_cap;
      continue;
    }
    if (c.host_key.empty() || c.host_key.size() > 255 || c.name.size() > 4096) {
      ++skips->bad_rows;
      continue;
    }
    base::span<const uint8_t> encrypted = s.ColumnBlob(3);
    if (encrypted.empty()) {
      c.value = s.ColumnString(2);
    } else {
      std::optional<std::string> value = DecryptSafeStorageValue(key, encrypted);
      if (value && hashed) {
        const auto host_hash = crypto::hash::Sha256(c.host_key);
        if (value->size() < host_hash.size() ||
            base::as_byte_span(*value).first(host_hash.size()) != base::span(host_hash)) {
          value.reset();
        } else {
          value->erase(0, host_hash.size());
        }
      }
      if (!value) {
        ++skips->undecryptable;
        continue;
      }
      c.value = std::move(*value);
    }
    if (c.name.empty() && c.value.empty()) {
      ++skips->bad_rows;
      continue;
    }
    c.path = s.ColumnString(4);
    c.creation_utc = s.ColumnInt64(5);
    c.last_access_utc = s.ColumnInt64(7);
    c.secure = s.ColumnBool(8);
    c.httponly = s.ColumnBool(9);
    c.samesite = s.ColumnInt(11);
    c.priority = s.ColumnInt(12);
    c.top_frame_site_key = s.ColumnString(13);
    c.has_cross_site_ancestor = s.ColumnBool(14);
    out.push_back(std::move(c));
  }
  return out;
}

// Copies `name` and its side files into `dir`; the copy's path, or nullopt when there is no such regular file.
std::optional<base::FilePath> CopyWithSideFiles(const base::FilePath& profile,
                                                const char* name,
                                                const base::FilePath& dir) {
  const base::FilePath from = profile.Append(name);
  if (!base::PathExists(from) || base::DirectoryExists(from) || base::IsLink(from)) {
    return std::nullopt;
  }
  const base::FilePath to = dir.Append(name);
  if (!base::CopyFile(from, to)) {
    return std::nullopt;
  }
  for (const char* ext : {"-journal", "-wal"}) {
    const base::FilePath side(from.value() + ext);
    if (base::PathExists(side)) {
      base::CopyFile(side, base::FilePath(to.value() + ext));
    }
  }
  return to;
}

ProfileSecrets ReadProfileSecrets(const base::FilePath& profile,
                                  const SafeStorageKey& key,
                                  bool passwords,
                                  bool cookies,
                                  int64_t now_chrome_us,
                                  const base::FilePath& temp_root) {
  ProfileSecrets read;
  base::FilePath dir;
  if (profile.empty() || temp_root.empty() || !base::CreateDirectory(temp_root) ||
      !base::CreateTemporaryDirInDir(temp_root, "secrets", &dir)) {
    return read;
  }
  if (passwords) {
    if (std::optional<base::FilePath> copy = CopyWithSideFiles(profile, "Login Data", dir)) {
      read.logins = ReadSidebarLogins(*copy, key, &read.skips).value_or(std::vector<SidebarLogin>());
    }
    // Passwords saved to the account (signed in without full sync) live in a second store. Its rows merge into the
    // same list under the same cap.
    if (std::optional<base::FilePath> copy = CopyWithSideFiles(profile, "Login Data For Account", dir)) {
      read.logins = MergeSidebarLogins(
          std::move(read.logins),
          ReadSidebarLogins(*copy, key, &read.skips).value_or(std::vector<SidebarLogin>()), &read.skips);
    }
  }
  if (cookies) {
    if (std::optional<base::FilePath> copy = CopyWithSideFiles(profile, "Cookies", dir)) {
      read.cookies = ReadSidebarCookies(*copy, key, now_chrome_us, &read.skips).value_or(std::vector<SidebarCookie>());
    }
  }
  base::DeletePathRecursively(dir);
  return read;
}

}  // namespace clu
