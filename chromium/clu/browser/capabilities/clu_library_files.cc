// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_library_files.h"

#include <algorithm>
#include <array>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "chrome/common/chrome_paths.h"

#if BUILDFLAG(IS_MAC)
#include <CoreFoundation/CoreFoundation.h>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/strings/sys_string_conversions.h"
#endif

namespace clu {

namespace {

struct TestFolders {
  base::FilePath downloads;
  base::FilePath media;
};

TestFolders& Overrides() {
  static base::NoDestructor<TestFolders> folders;
  return *folders;
}

constexpr auto kImages = std::to_array<std::string_view>(
    {".png", ".jpg", ".jpeg", ".gif", ".heic", ".webp", ".tiff", ".bmp"});
constexpr auto kVideos =
    std::to_array<std::string_view>({".mov", ".mp4", ".m4v", ".webm"});

// The screenshot folder macOS writes to: the com.apple.screencapture "location" default, "~" expanded, else the
// Desktop.
base::FilePath ScreenshotFolder() {
#if BUILDFLAG(IS_MAC)
  base::apple::ScopedCFTypeRef<CFPropertyListRef> value(
      CFPreferencesCopyAppValue(CFSTR("location"),
                                CFSTR("com.apple.screencapture")));
  if (CFStringRef location =
          base::apple::CFCast<CFStringRef>(value.get())) {
    std::string path = base::SysCFStringRefToUTF8(location);
    if (path == "~" || path.starts_with("~/")) {
      path = base::GetHomeDir().AsUTF8Unsafe() + path.substr(1);
    }
    const base::FilePath folder = base::FilePath::FromUTF8Unsafe(path);
    if (folder.IsAbsolute() && base::DirectoryExists(folder)) {
      return folder;
    }
  }
#endif
  base::FilePath desktop;
  base::PathService::Get(base::DIR_USER_DESKTOP, &desktop);
  return desktop;
}

}  // namespace

base::FilePath LibraryFolder(LibrarySource source) {
  const TestFolders& overrides = Overrides();
  if (source == LibrarySource::kDownloads) {
    if (!overrides.downloads.empty()) {
      return overrides.downloads;
    }
    base::FilePath downloads;
    base::PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &downloads);
    return downloads;
  }
  if (!overrides.media.empty()) {
    return overrides.media;
  }
  return ScreenshotFolder();
}

void SetLibraryFoldersForTesting(const base::FilePath& downloads,
                                 const base::FilePath& media) {
  Overrides() = {downloads, media};
}

std::string LibraryKindOf(const base::FilePath& name) {
  const std::string ext = base::ToLowerASCII(name.FinalExtension());
  if (std::ranges::find(kImages, ext) != kImages.end()) {
    return "image";
  }
  if (std::ranges::find(kVideos, ext) != kVideos.end()) {
    return "video";
  }
  return "file";
}

std::vector<LibraryFile> ListLibraryFolder(const base::FilePath& folder,
                                           LibrarySource source) {
  std::vector<LibraryFile> files;
  if (folder.empty()) {
    return files;
  }
  base::FileEnumerator it(folder, /*recursive=*/false,
                          base::FileEnumerator::FILES);
  for (base::FilePath path = it.Next(); !path.empty(); path = it.Next()) {
    const base::FileEnumerator::FileInfo info = it.GetInfo();
    const base::FilePath name = info.GetName();
    const std::string utf8 = name.AsUTF8Unsafe();
    if (utf8.empty() || utf8.starts_with(".") || info.IsDirectory() ||
        base::IsLink(path) || name.MatchesFinalExtension(".crdownload") ||
        name.MatchesFinalExtension(".download")) {
      continue;
    }
    LibraryFile file{utf8, info.GetSize(), info.GetLastModifiedTime(),
                     LibraryKindOf(name)};
    if (source == LibrarySource::kMedia && file.kind == "file") {
      continue;
    }
    files.push_back(std::move(file));
  }
  std::ranges::sort(files, [](const LibraryFile& a, const LibraryFile& b) {
    return a.modified != b.modified ? a.modified > b.modified : a.name < b.name;
  });
  if (files.size() > kMaxLibraryFiles) {
    files.resize(kMaxLibraryFiles);
  }
  return files;
}

base::ListValue LibraryFilesValue(const std::vector<LibraryFile>& files) {
  base::ListValue rows;
  for (const LibraryFile& file : files) {
    rows.Append(base::DictValue()
                    .Set("name", file.name)
                    .Set("size", static_cast<double>(file.size))
                    .Set("modifiedAt",
                         file.modified.InMillisecondsFSinceUnixEpochIgnoringNull())
                    .Set("kind", file.kind));
  }
  return rows;
}

std::optional<base::FilePath> ResolveLibraryFile(const base::FilePath& folder,
                                                 std::string_view name) {
  if (folder.empty() || name.empty() || name.starts_with(".") ||
      name.find('/') != std::string_view::npos ||
      name.find('\\') != std::string_view::npos ||
      name.find('\0') != std::string_view::npos) {
    return std::nullopt;
  }
  const base::FilePath path =
      folder.Append(base::FilePath::FromUTF8Unsafe(name));
  if (path.ReferencesParent() || path.DirName() != folder ||
      base::IsLink(path) || !base::PathExists(path) ||
      base::DirectoryExists(path)) {
    return std::nullopt;
  }
  return path;
}

std::optional<base::FilePath> RenameLibraryFile(const base::FilePath& folder,
                                                std::string_view name,
                                                std::string_view new_name) {
  const std::optional<base::FilePath> from = ResolveLibraryFile(folder, name);
  if (!from || new_name.empty() || new_name.size() > 255 ||
      new_name.starts_with(".") ||
      new_name.find_first_of(std::string_view("/\\:\0", 4)) !=
          std::string_view::npos) {
    return std::nullopt;
  }
  const base::FilePath to =
      folder.Append(base::FilePath::FromUTF8Unsafe(new_name));
  if (to.ReferencesParent() || to.DirName() != folder || base::IsLink(to) ||
      base::PathExists(to) || !base::Move(*from, to)) {
    return std::nullopt;
  }
  return to;
}

}  // namespace clu
