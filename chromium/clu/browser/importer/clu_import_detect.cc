// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_detect.h"

#include <algorithm>
#include <map>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "clu/browser/importer/clu_import_rules.h"

namespace clu {

namespace {

constexpr char kProfilePrefix[] = "Profile ";
constexpr size_t kMaxProfileName = 100;

bool IsFile(const base::FilePath& path) {
  base::File::Info info;
  return base::GetFileInfo(path, &info) && !info.is_directory;
}

int ProfileNumber(std::string_view dir) {
  int n = 0;
  return dir == "Default" ||
                 !base::StringToInt(dir.substr(sizeof(kProfilePrefix) - 1), &n)
             ? 0
             : n;
}

std::map<std::string, std::string> ProfileNames(const base::FilePath& dir) {
  std::map<std::string, std::string> names;
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(dir.Append("Local State"), &text,
                                         kMaxLocalStateBytes)) {
    return names;
  }
  std::optional<base::Value> data =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC, kMaxImportJsonDepth);
  const base::DictValue* profile =
      data && data->is_dict() ? data->GetDict().FindDict("profile") : nullptr;
  const base::DictValue* cache = profile ? profile->FindDict("info_cache") : nullptr;
  if (!cache) {
    return names;
  }
  for (const auto [key, value] : *cache) {
    const base::DictValue* entry = value.GetIfDict();
    const std::string* name = entry ? entry->FindString("name") : nullptr;
    if (!name) {
      continue;
    }
    std::string clean = ImportTitle(*name);
    if (clean.empty()) {
      continue;
    }
    if (clean.size() > kMaxProfileName) {
      base::TruncateUTF8ToByteSize(clean, kMaxProfileName, &clean);
    }
    names.emplace(key, clean);
  }
  return names;
}

}  // namespace

bool IsChromeProfileDir(std::string_view dir) {
  if (dir == "Default") {
    return true;
  }
  if (!dir.starts_with(kProfilePrefix)) {
    return false;
  }
  const std::string_view digits = dir.substr(sizeof(kProfilePrefix) - 1);
  return !digits.empty() && digits.size() <= 3 &&
         std::all_of(digits.begin(), digits.end(),
                     [](char c) { return base::IsAsciiDigit(c); });
}

base::FilePath ChromeDataDir(const base::FilePath& root) {
  return root.Append("Library")
      .Append("Application Support")
      .Append("Google")
      .Append("Chrome");
}

std::vector<ChromeProfileInfo> DetectChrome(const base::FilePath& root) {
  std::vector<ChromeProfileInfo> out;
  if (root.empty()) {
    return out;
  }
  const base::FilePath dir = ChromeDataDir(root);
  if (!base::DirectoryExists(dir)) {
    return out;
  }
  const std::map<std::string, std::string> names = ProfileNames(dir);
  base::FileEnumerator entries(dir, /*recursive=*/false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = entries.Next(); !path.empty();
       path = entries.Next()) {
    const std::string name = path.BaseName().AsUTF8Unsafe();
    if (!IsChromeProfileDir(name)) {
      continue;
    }
    ChromeProfileInfo info;
    info.dir = name;
    const auto it = names.find(name);
    info.name = it != names.end() ? it->second : name;
    info.has_bookmarks = IsFile(path.Append("Bookmarks"));
    info.has_history = IsFile(path.Append("History"));
    if (info.has_bookmarks || info.has_history) {
      out.push_back(std::move(info));
    }
  }
  std::sort(out.begin(), out.end(),
            [](const ChromeProfileInfo& a, const ChromeProfileInfo& b) {
              return ProfileNumber(a.dir) < ProfileNumber(b.dir);
            });
  return out;
}

std::optional<base::FilePath> ChromeProfilePath(const base::FilePath& root,
                                                std::string_view dir) {
  if (root.empty() || !IsChromeProfileDir(dir)) {
    return std::nullopt;
  }
  const base::FilePath path = ChromeDataDir(root).AppendASCII(dir);
  return base::DirectoryExists(path) ? std::optional<base::FilePath>(path)
                                     : std::nullopt;
}

base::FilePath SidebarFilePath(const base::FilePath& root) {
  return root.Append("Library")
      .Append("Application Support")
      .Append("Arc")
      .Append("StorableSidebar.json");
}

bool HasSidebarFile(const base::FilePath& root) {
  return !root.empty() && IsFile(SidebarFilePath(root));
}

base::FilePath SidebarUserDataDir(const base::FilePath& root) {
  return root.Append("Library")
      .Append("Application Support")
      .Append("Arc")
      .Append("User Data");
}

std::optional<base::FilePath> SidebarProfileDir(const base::FilePath& root,
                                                std::string_view profile_key) {
  constexpr std::string_view kCustom = "custom:";
  std::string_view dir;
  if (profile_key == "default") {
    dir = "Default";
  } else if (profile_key.starts_with(kCustom)) {
    dir = profile_key.substr(kCustom.size());
  }
  if (root.empty() || !IsChromeProfileDir(dir)) {
    return std::nullopt;
  }
  return SidebarUserDataDir(root).AppendASCII(dir);
}

std::optional<base::FilePath> SidebarHistoryPath(const base::FilePath& root,
                                                 std::string_view profile_key) {
  const std::optional<base::FilePath> dir = SidebarProfileDir(root, profile_key);
  if (!dir) {
    return std::nullopt;
  }
  const base::FilePath path = dir->Append("History");
  return IsFile(path) ? std::optional<base::FilePath>(path) : std::nullopt;
}

}  // namespace clu
