// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_SITE_DATA_H_
#define CLU_BROWSER_IMPORTER_CLU_SITE_DATA_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"
#include "clu/browser/importer/clu_site_storage.h"

// The rest of a Chrome or sidebar profile's site data, read the same way for both: site permissions
// from Preferences, saved addresses from a copy of Web Data, the default search engine from Preferences, and the site
// storage staged by clu_site_storage.h. The source is only read. Blocking.
namespace clu {

// Preferences files past this size are not read.
inline constexpr size_t kMaxPrefsBytes = 32 * 1024 * 1024;
// Past these counts a profile's rows are junk.
inline constexpr size_t kMaxImportedPermissions = 5000;
inline constexpr size_t kMaxImportedAddresses = 500;

// A site permission: its content settings type by Preferences name ("notifications", "media_stream_camera",
// "media_stream_mic", "geolocation"), the site pattern ("https://example.com:443") and allow (1) or block (2).
struct ImportedPermission {
  std::string type;
  std::string pattern;
  int setting = 0;
};

// A saved address: Autofill field types (components/autofill FieldType values) and their values, the country an
// ISO code.
struct ImportedAddress {
  std::string country;
  std::vector<std::pair<int, std::string>> fields;
};

struct ImportedSearchEngine {
  std::string name;
  std::string keyword;
  std::string url;
};

struct SiteDataOptions {
  bool storage = false;
  bool permissions = false;
  bool addresses = false;
  bool search_engine = false;
};

struct SiteDataRead {
  StagedSiteStorage storage;
  std::vector<ImportedPermission> permissions;
  std::vector<ImportedAddress> addresses;
  std::optional<ImportedSearchEngine> search_engine;
  // Junk permissions and addresses, and rows past the caps.
  int skipped = 0;
};

// A JSON file's top-level dictionary, or nullopt for a missing, linked, oversized or non-JSON file.
std::optional<base::DictValue> ReadJsonDict(const base::FilePath& file);
// The allowed and blocked sites of each permission type above; asks, wildcards and junk patterns are left out.
std::vector<ImportedPermission> ReadSitePermissions(const base::DictValue& prefs, int* skipped);
// The default search engine a Preferences file sets, when it is an https URL with {searchTerms}.
std::optional<ImportedSearchEngine> ReadDefaultSearchEngine(const base::DictValue& prefs);
// The addresses in a copy of Web Data (its local_addresses tables).
std::vector<ImportedAddress> ReadAutofillAddresses(const base::FilePath& web_data_copy, int* skipped);
// Everything `options` asks for from `source_profile`; site storage is staged in `target_profile`. Web Data is
// copied into a folder under `temp_root` and the copy deleted.
SiteDataRead ReadSiteData(const base::FilePath& source_profile,
                          const base::FilePath& target_profile,
                          SiteDataOptions options,
                          const base::FilePath& temp_root);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_SITE_DATA_H_
