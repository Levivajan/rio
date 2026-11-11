// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_SITE_STORAGE_H_
#define CLU_BROWSER_IMPORTER_CLU_SITE_STORAGE_H_

#include "base/files/file_path.h"

// Site storage import: the Local Storage and IndexedDB of the sites in a Chrome or sidebar profile, so a
// site that keeps its sign-in there stays signed in. The Rio profile is open while it imports and its storage service
// may hold its LevelDB files, so nothing is written into them then: the source's files are copied into a staging
// folder in the Rio profile, and MergePendingSiteStorage() moves them in at the next launch, before any profile (and
// so any storage) opens. A site the Rio profile already has storage for keeps its own.
namespace clu {

// The staging folder in a Rio profile directory.
inline constexpr base::FilePath::CharType kPendingSiteStorageDirName[] =
    FILE_PATH_LITERAL("CluPendingSiteStorage");

struct StagedSiteStorage {
  // Sites (origins) with Local Storage, and with IndexedDB databases, that were staged.
  int local_storage = 0;
  int indexed_db = 0;
  // Links, unreadable folders and copies that failed.
  int skipped = 0;
  int sites() const { return local_storage + indexed_db; }
};

// Copies `source_profile`'s Local Storage and IndexedDB into a new batch under `target_profile`'s staging folder. The
// source is only read. Blocking.
StagedSiteStorage StageSiteStorage(const base::FilePath& source_profile, const base::FilePath& target_profile);

// For every profile directory under `user_data_dir` with a staging folder: each batch's Local Storage origins and
// IndexedDB databases the profile doesn't have yet go in, then the batch is deleted. A batch that didn't merge (the
// profile's Local Storage didn't open, a write or move failed) stays for the next launch, a few launches at most. Runs
// before profiles load (its LevelDB files are closed then). Blocking.
void MergePendingSiteStorage(const base::FilePath& user_data_dir);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_SITE_STORAGE_H_
