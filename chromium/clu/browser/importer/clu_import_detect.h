// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_IMPORT_DETECT_H_
#define CLU_BROWSER_IMPORTER_CLU_IMPORT_DETECT_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

// Import detection (docs/plan/07-data.md and R8).
// Detection only checks that files exist and reads profile names from Chrome's Local State; nothing else is read
// until the user imports. Every path resolves under an import root (the home directory, or the fixture root in
// tests), and a profile directory comes from a validated name, never a payload path. Blocking.
namespace clu {

struct ChromeProfileInfo {
  std::string dir;
  std::string name;
  bool has_bookmarks = false;
  bool has_history = false;

  bool operator==(const ChromeProfileInfo&) const = default;
};

// ^(Default|Profile \d{1,3})$ (the import.run payload rule).
bool IsChromeProfileDir(std::string_view dir);

// <root>/Library/Application Support/Google/Chrome.
base::FilePath ChromeDataDir(const base::FilePath& root);
// Profiles with a Bookmarks or History file, Default first, then by profile number. Names come from Local State's
// profile.info_cache; a missing, huge or malformed Local State gives the directory names.
std::vector<ChromeProfileInfo> DetectChrome(const base::FilePath& root);
// The profile directory for a valid name, when it exists.
std::optional<base::FilePath> ChromeProfilePath(const base::FilePath& root,
                                                std::string_view dir);

// The other browser's sidebar file under `root` (with the two below, the one place Clu names that browser's on-disk
// folder), and whether it is a regular file.
base::FilePath SidebarFilePath(const base::FilePath& root);
bool HasSidebarFile(const base::FilePath& root);
// That browser's Chromium profiles: <root>/Library/Application Support/Arc/User Data.
base::FilePath SidebarUserDataDir(const base::FilePath& root);
// The directory of the sidebar file's source profile `profile_key` (the rule below), whether or not it exists.
std::optional<base::FilePath> SidebarProfileDir(const base::FilePath& root,
                                                std::string_view profile_key);
// The Chromium-format History file of the sidebar file's source profile `profile_key` ("default" is Default,
// "custom:<dir>" the profile directory <dir>, which must be a Chromium profile directory name), when it is a regular
// file.
std::optional<base::FilePath> SidebarHistoryPath(const base::FilePath& root,
                                                 std::string_view profile_key);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_IMPORT_DETECT_H_
