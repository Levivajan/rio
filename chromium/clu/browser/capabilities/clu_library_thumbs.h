// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_LIBRARY_THUMBS_H_
#define CLU_BROWSER_CAPABILITIES_CLU_LIBRARY_THUMBS_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

// A Library file's thumbnail (download.thumb): QuickLook draws it, as Finder does, so a PDF shows its
// first page, a sheet its cells and a screenshot itself; a file QuickLook has no preview for gets its icon.
namespace clu {

// The edge of the square a thumbnail fits in, in pixels: a Media tile's thumbnail spans about 110 points, so
// 256 stays sharp at 2x; the hover peek's 40 point tile scales it down.
inline constexpr int kLibraryThumbPixels = 256;

// Asks QuickLook for `path`'s thumbnail and runs `callback` on the calling sequence with it as a PNG data URL, or
// with "" when QuickLook has nothing. QuickLook renders out of process; `path` was resolved by ResolveLibraryFile.
void LibraryThumbnail(const base::FilePath& path,
                      base::OnceCallback<void(std::string)> callback);

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_LIBRARY_THUMBS_H_
