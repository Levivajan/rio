// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_site_data.h"

#include <algorithm>
#include <string_view>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_util.h"
#include "clu/browser/importer/clu_arc_secrets.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace clu {

namespace {

constexpr char kPermissionTypes[][24] = {"notifications", "media_stream_camera", "media_stream_mic", "geolocation"};
constexpr int kAllow = 1;
constexpr int kBlock = 2;
// Values longer than this are junk.
constexpr size_t kMaxFieldBytes = 1024;
constexpr size_t kMaxPatternBytes = 2048;

// Autofill field types an address brings (components/autofill/core/browser/field_types.h): names, email, phone,
// company, street, city, state, zip. The country (ADDRESS_HOME_COUNTRY, 36) is read apart.
constexpr int kAddressFields[] = {3, 5, 7, 9, 14, 33, 34, 35, 60, 77};
constexpr int kCountryField = 36;
// AutofillProfile::RecordType in the addresses table's record_type column: kLocalOrSyncable (0) and kAccount (1) are
// addresses the user saved in Chrome, signed out or signed in; kAccountHome and kAccountWork (2, 3) are read-only
// records Chrome reads from the Google account.
constexpr int kLocalRecordType = 0;
constexpr int kAccountRecordType = 1;

// "https://example.com:443,*": the primary pattern, when it names a site (not everything) and the secondary is "*".
std::optional<std::string> PrimaryPattern(std::string_view key) {
  const size_t comma = key.find(',');
  const std::string_view primary = key.substr(0, comma);
  const std::string_view secondary = comma == std::string_view::npos ? "*" : key.substr(comma + 1);
  if (primary.empty() || primary == "*" || secondary != "*" || primary.size() > kMaxPatternBytes ||
      !base::IsStringASCII(primary) || primary.find_first_of(" \t\r\n") != std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(primary);
}

}  // namespace

std::optional<base::DictValue> ReadJsonDict(const base::FilePath& file) {
  std::string text;
  if (!base::PathExists(file) || base::IsLink(file) ||
      !base::ReadFileToStringWithMaxSize(file, &text, kMaxPrefsBytes)) {
    return std::nullopt;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC, kMaxImportJsonDepth);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  return std::move(*value).TakeDict();
}

std::vector<ImportedPermission> ReadSitePermissions(const base::DictValue& prefs, int* skipped) {
  std::vector<ImportedPermission> out;
  const base::DictValue* exceptions = prefs.FindDictByDottedPath("profile.content_settings.exceptions");
  if (!exceptions) {
    return out;
  }
  for (const char* type : kPermissionTypes) {
    const base::DictValue* sites = exceptions->FindDict(type);
    if (!sites) {
      continue;
    }
    for (const auto [key, entry] : *sites) {
      const std::optional<int> setting = entry.is_dict() ? entry.GetDict().FindInt("setting") : std::nullopt;
      if (setting != kAllow && setting != kBlock) {
        // Asks and missing settings are Rio's default already.
        continue;
      }
      std::optional<std::string> pattern = PrimaryPattern(key);
      if (!pattern || out.size() >= kMaxImportedPermissions) {
        ++*skipped;
        continue;
      }
      out.push_back({.type = type, .pattern = std::move(*pattern), .setting = *setting});
    }
  }
  return out;
}

std::optional<ImportedSearchEngine> ReadDefaultSearchEngine(const base::DictValue& prefs) {
  const base::DictValue* data = prefs.FindDictByDottedPath("default_search_provider_data.template_url_data");
  const std::string* name = data ? data->FindString("short_name") : nullptr;
  const std::string* keyword = data ? data->FindString("keyword") : nullptr;
  const std::string* url = data ? data->FindString("url") : nullptr;
  const GURL parsed(url ? *url : std::string());
  if (!name || !keyword || !url || name->empty() || keyword->empty() || !parsed.is_valid() ||
      !parsed.SchemeIs(url::kHttpsScheme) || url->find("{searchTerms}") == std::string::npos || url->size() >= 2048) {
    return std::nullopt;
  }
  return ImportedSearchEngine{.name = name->substr(0, 200), .keyword = keyword->substr(0, 200), .url = *url};
}

std::vector<ImportedAddress> ReadAutofillAddresses(const base::FilePath& web_data_copy, int* skipped) {
  std::vector<ImportedAddress> out;
  sql::Database db(sql::Database::Tag("CluModel"));
  if (!OpenCopy(db, web_data_copy)) {
    return out;
  }
  // Web Data from schema 134 on keeps local and account addresses in one pair of tables, told apart by record_type;
  // older files keep the local ones in tables of their own. The addresses the user saved come over, local or account;
  // the account's read-only home and work records stay.
  const bool unified = db.DoesTableExist("addresses") && db.DoesTableExist("address_type_tokens") &&
                       db.DoesColumnExist("addresses", "record_type");
  if (!unified && (!db.DoesTableExist("local_addresses") || !db.DoesTableExist("local_addresses_type_tokens"))) {
    return out;
  }
  // Most used first, so the cap keeps the ones the user fills most.
  sql::Statement guids(db.GetUniqueStatement(
      unified ? base::cstring_view(
                    "SELECT guid FROM addresses WHERE record_type IN (?, ?) ORDER BY use_count DESC")
              : base::cstring_view("SELECT guid FROM local_addresses ORDER BY use_count DESC")));
  sql::Statement tokens(db.GetUniqueStatement(
      unified ? base::cstring_view("SELECT type, value FROM address_type_tokens WHERE guid = ?")
              : base::cstring_view("SELECT type, value FROM local_addresses_type_tokens WHERE guid = ?")));
  if (!guids.is_valid() || !tokens.is_valid()) {
    return out;
  }
  if (unified) {
    guids.BindInt(0, kLocalRecordType);
    guids.BindInt(1, kAccountRecordType);
  }
  while (guids.Step()) {
    if (out.size() >= kMaxImportedAddresses) {
      ++*skipped;
      continue;
    }
    tokens.Reset(/*clear_bound_vars=*/true);
    tokens.BindString(0, guids.ColumnString(0));
    ImportedAddress address;
    while (tokens.Step()) {
      const int type = tokens.ColumnInt(0);
      std::string value = tokens.ColumnString(1);
      if (value.empty() || value.size() > kMaxFieldBytes || !base::IsStringUTF8(value)) {
        continue;
      }
      if (type == kCountryField) {
        if (value.size() == 2 && base::IsAsciiAlpha(value[0]) && base::IsAsciiAlpha(value[1])) {
          address.country = base::ToUpperASCII(value);
        }
      } else if (std::ranges::find(kAddressFields, type) != std::end(kAddressFields)) {
        address.fields.emplace_back(type, std::move(value));
      }
    }
    if (address.fields.empty()) {
      ++*skipped;
      continue;
    }
    out.push_back(std::move(address));
  }
  return out;
}

SiteDataRead ReadSiteData(const base::FilePath& source_profile,
                          const base::FilePath& target_profile,
                          SiteDataOptions options,
                          const base::FilePath& temp_root) {
  SiteDataRead read;
  if (source_profile.empty() || !base::DirectoryExists(source_profile)) {
    return read;
  }
  if (options.storage && !target_profile.empty()) {
    read.storage = StageSiteStorage(source_profile, target_profile);
    read.skipped += read.storage.skipped;
  }
  if (options.permissions || options.search_engine) {
    if (std::optional<base::DictValue> prefs = ReadJsonDict(source_profile.Append("Preferences"))) {
      if (options.permissions) {
        read.permissions = ReadSitePermissions(*prefs, &read.skipped);
      }
      if (options.search_engine) {
        read.search_engine = ReadDefaultSearchEngine(*prefs);
      }
    }
  }
  base::FilePath dir;
  if (options.addresses && !temp_root.empty() && base::CreateDirectory(temp_root) &&
      base::CreateTemporaryDirInDir(temp_root, "sitedata", &dir)) {
    if (std::optional<base::FilePath> copy = CopyWithSideFiles(source_profile, "Web Data", dir)) {
      read.addresses = ReadAutofillAddresses(*copy, &read.skipped);
    }
    base::DeletePathRecursively(dir);
  }
  return read;
}

}  // namespace clu
