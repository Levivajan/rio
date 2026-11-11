// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_IMPORT_RULES_H_
#define CLU_BROWSER_IMPORTER_CLU_IMPORT_RULES_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/values.h"

// Rules every importer shares (docs/plan/07-data.md). Pure.
namespace clu {

// Caps: 10,000 bookmarks or sidebar nodes per run, 50,000 history URLs, a 64 MB Bookmarks file, a 50 MB sidebar
// file, 8 MB of Local State, URLs of 8,192 characters, titles of 1,024.
inline constexpr size_t kMaxImportItems = 10'000;
inline constexpr size_t kMaxImportHistory = 50'000;
inline constexpr size_t kMaxBookmarksBytes = 64 * 1024 * 1024;
inline constexpr size_t kMaxSidebarBytes = 50 * 1024 * 1024;
inline constexpr size_t kMaxLocalStateBytes = 8 * 1024 * 1024;
inline constexpr size_t kMaxImportUrl = 8192;
inline constexpr size_t kMaxImportTitle = 1024;
// JSON nesting the readers accept; a deeper file is refused as damaged (base::JSONReader's own limit is 200).
inline constexpr size_t kMaxImportJsonDepth = 200;
// A folder at level 8 (the top "Imported from Chrome" folder is 1) holds tabs but no folder (kMaxFolderDepth).
inline constexpr int kMaxImportFolderDepth = 8;

// A URL an import keeps: http or https, at most 8,192 characters, in its canonical form; else nullopt.
std::optional<std::string> ImportUrl(const base::Value* raw);
std::optional<std::string> ImportUrl(std::string_view raw);
// Control and format characters become spaces, runs of white space collapse, trimmed, at most 1,024 UTF-16 units
// without splitting a surrogate pair; "" when nothing is left or `raw` is not a string.
std::string ImportTitle(const base::Value* raw);
std::string ImportTitle(std::string_view raw);
// "Imported" or "Imported <digits>": the profile names a broken earlier sidebar import gave.
bool IsBrokenImportName(std::string_view name);
// The URL's host, or the URL itself when it has none.
std::string ImportHost(std::string_view url);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_IMPORT_RULES_H_
