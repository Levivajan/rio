// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_LIBRARY_FILES_H_
#define CLU_BROWSER_CAPABILITIES_CLU_LIBRARY_FILES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "base/values.h"

// The Library's folders (download.files and download.fileAct): Downloads lists the files in the Mac's
// Downloads folder, Media the screenshots and screen recordings in the folder `defaults read com.apple.screencapture
// location` names (the Desktop when unset). macOS asks the user for Files and Folders access the first time a folder
// is read. Pure file system code, run on a blocking pool thread; the WebUI names a file only by its source and bare
// name, never by a path.
namespace clu {

enum class LibrarySource { kDownloads, kMedia };

struct LibraryFile {
  std::string name;
  int64_t size = 0;
  base::Time modified;
  // "image", "video" or "file", from the extension.
  std::string kind;
};

// At most this many files per folder, the newest.
inline constexpr size_t kMaxLibraryFiles = 500;

// The folder a source lists. Tests point both at temp folders.
base::FilePath LibraryFolder(LibrarySource source);
void SetLibraryFoldersForTesting(const base::FilePath& downloads,
                                 const base::FilePath& media);

// "image", "video" or "file" for a file name.
std::string LibraryKindOf(const base::FilePath& name);

// The regular files directly in `folder`, newest first, at most kMaxLibraryFiles. Hidden files, folders, symlinks
// and partial downloads (.crdownload, .download) are left out; for kMedia only images and videos are listed.
std::vector<LibraryFile> ListLibraryFolder(const base::FilePath& folder,
                                           LibrarySource source);

// The rows of download.files: [{name, size, modifiedAt, kind}].
base::ListValue LibraryFilesValue(const std::vector<LibraryFile>& files);

// The file `name` directly inside `folder`, or nullopt when the name is empty, has a separator or "..", starts with a
// dot, or is not an existing regular file there (a symlink included).
std::optional<base::FilePath> ResolveLibraryFile(const base::FilePath& folder,
                                                 std::string_view name);

// The Library menu's Rename: moves the file `name` in `folder` to `new_name` in the same folder. nullopt when
// `name` does not resolve, `new_name` breaks ResolveLibraryFile's rules or longer than 255 bytes, or is taken.
std::optional<base::FilePath> RenameLibraryFile(const base::FilePath& folder,
                                                std::string_view name,
                                                std::string_view new_name);

// The Library menu's Move to Trash, through macOS's Trash so Put Back works; false when it refuses. Blocking.
bool TrashLibraryFile(const base::FilePath& path);

// The Library menu's Copy and Copy Path: the file on the general pasteboard as Finder's Copy puts it, or its
// path as text. False when nothing landed (no NSString or NSURL for the path, or the pasteboard refused). UI thread.
bool CopyLibraryFile(const base::FilePath& path, bool as_path);

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_LIBRARY_FILES_H_
