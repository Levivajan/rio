// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_arc_keychain.h"

#include <vector>

#include "base/logging.h"
#include "clu/browser/importer/clu_arc_secrets.h"
#include "crypto/apple/keychain_v2.h"
#include "third_party/boringssl/src/include/openssl/mem.h"

namespace clu {

std::optional<std::string> ReadSidebarSafeStorageSecret() {
  return ReadSafeStorageSecret(kSidebarSafeStorageService, kSidebarSafeStorageAccount);
}

std::optional<std::string> ReadSafeStorageSecret(const char* service, const char* account) {
  base::expected<std::vector<uint8_t>, OSStatus> found =
      crypto::apple::KeychainV2::GetInstance().FindGenericPassword(service, account);
  if (!found.has_value()) {
    // errSecUserCanceled or errSecAuthFailed when the user denied the prompt, errSecItemNotFound without the item.
    LOG(WARNING) << "clu: no Safe Storage key for the import (" << found.error() << ")";
    return std::nullopt;
  }
  std::string secret(found->begin(), found->end());
  OPENSSL_cleanse(found->data(), found->size());
  return secret;
}

}  // namespace clu
