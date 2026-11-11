// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_ARC_KEYCHAIN_H_
#define CLU_BROWSER_IMPORTER_CLU_ARC_KEYCHAIN_H_

#include <optional>
#include <string>

namespace clu {

// The other browser's Safe Storage secret from the login keychain (kSidebarSafeStorageService, account
// kSidebarSafeStorageAccount). macOS shows its own Allow prompt; nullopt when the user denies it or there is no item.
// Blocking. The secret is never logged.
std::optional<std::string> ReadSidebarSafeStorageSecret();
// The same for any browser's Safe Storage item (Google Chrome's: kChromeSafeStorageService, kChromeSafeStorageAccount).
std::optional<std::string> ReadSafeStorageSecret(const char* service, const char* account);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_ARC_KEYCHAIN_H_
