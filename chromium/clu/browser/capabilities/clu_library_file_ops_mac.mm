// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_library_files.h"

#import <AppKit/AppKit.h>

#include "base/apple/foundation_util.h"
#include "base/strings/sys_string_conversions.h"

namespace clu {

bool TrashLibraryFile(const base::FilePath& path) {
  NSURL* url = base::apple::FilePathToNSURL(path);
  return url && [NSFileManager.defaultManager trashItemAtURL:url
                                            resultingItemURL:nil
                                                       error:nil];
}

bool CopyLibraryFile(const base::FilePath& path, bool as_path) {
  // A path that is not valid UTF-8 has no NSString (and no NSURL): nothing to copy, and the clipboard stays as it was.
  id item = as_path ? static_cast<id>(base::SysUTF8ToNSString(path.value()))
                    : static_cast<id>(base::apple::FilePathToNSURL(path));
  if (!item) {
    return false;
  }
  NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
  [pasteboard clearContents];
  return [pasteboard writeObjects:@[ item ]];
}

}  // namespace clu
