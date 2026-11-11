// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_site_storage.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/leveldatabase/src/include/leveldb/iterator.h"
#include "third_party/leveldatabase/src/include/leveldb/write_batch.h"

namespace clu {

namespace {

// Local Storage's LevelDB in a profile, and its keys: "META:<origin>" per site, "METAACCESS:<origin>", and
// "_<origin>\0<key>" per item (components/services/storage/dom_storage/local_storage_impl.cc).
constexpr char kLocalStorageDir[] = "Local Storage";
constexpr char kLevelDbDir[] = "leveldb";
constexpr char kIndexedDbDir[] = "IndexedDB";
constexpr char kIndexedDbSuffix[] = ".indexeddb.leveldb";
constexpr char kIndexedDbBlobSuffix[] = ".indexeddb.blob";
constexpr std::string_view kMetaPrefix = "META:";
constexpr std::string_view kMetaAccessPrefix = "METAACCESS:";
// A profile has a few thousand sites at most; past this a batch is junk.
constexpr size_t kMaxSites = 20000;
// A batch whose Local Storage didn't merge is kept for the next launch, this many launches at most, so a target that
// never opens doesn't keep it forever. The count lives in the batch.
constexpr int kMaxMergeAttempts = 3;
constexpr char kMergeAttemptsFile[] = "merge-attempts";

std::unique_ptr<leveldb::DB> OpenLevelDb(const base::FilePath& dir, bool create) {
  leveldb_env::Options options;
  options.create_if_missing = create;
  std::unique_ptr<leveldb::DB> db;
  if (!leveldb_env::OpenDB(options, dir.AsUTF8Unsafe(), &db).ok()) {
    return nullptr;
  }
  return db;
}

// The origins with a "META:" key in a closed Local Storage LevelDB.
std::set<std::string> LocalStorageOrigins(leveldb::DB* db) {
  std::set<std::string> origins;
  std::unique_ptr<leveldb::Iterator> it(db->NewIterator(leveldb::ReadOptions()));
  for (it->Seek(leveldb::Slice(kMetaPrefix.data(), kMetaPrefix.size()));
       it->Valid() && it->key().starts_with(leveldb::Slice(kMetaPrefix.data(), kMetaPrefix.size())) &&
       origins.size() < kMaxSites;
       it->Next()) {
    std::string origin = it->key().ToString().substr(kMetaPrefix.size());
    if (!origin.empty()) {
      origins.insert(std::move(origin));
    }
  }
  return origins;
}

bool IsPlainDir(const base::FilePath& path) {
  return base::DirectoryExists(path) && !base::IsLink(path);
}

bool CopyDir(const base::FilePath& from, const base::FilePath& to) {
  return base::CreateDirectory(to.DirName()) && base::CopyDirectoryNoFollow(from, to, /*recursive=*/true);
}

// Copies the staged origins the target doesn't have into the target's Local Storage. Returns false when either side
// doesn't open.
bool MergeLocalStorage(const base::FilePath& staged, const base::FilePath& target) {
  std::unique_ptr<leveldb::DB> from = OpenLevelDb(staged, /*create=*/false);
  if (!from) {
    return false;
  }
  if (!base::PathExists(target)) {
    from.reset();
    return base::CreateDirectory(target.DirName()) && base::Move(staged, target);
  }
  std::unique_ptr<leveldb::DB> to = OpenLevelDb(target, /*create=*/false);
  if (!to) {
    return false;
  }
  const std::set<std::string> have = LocalStorageOrigins(to.get());
  for (const std::string& origin : LocalStorageOrigins(from.get())) {
    if (have.contains(origin)) {
      continue;
    }
    leveldb::WriteBatch batch;
    for (std::string_view prefix : {kMetaPrefix, kMetaAccessPrefix}) {
      const std::string key = std::string(prefix) + origin;
      std::string value;
      if (from->Get(leveldb::ReadOptions(), key, &value).ok()) {
        batch.Put(key, value);
      }
    }
    const std::string items = "_" + origin + std::string(1, '\0');
    std::unique_ptr<leveldb::Iterator> it(from->NewIterator(leveldb::ReadOptions()));
    for (it->Seek(items); it->Valid() && it->key().starts_with(items); it->Next()) {
      batch.Put(it->key(), it->value());
    }
    if (!to->Write(leveldb::WriteOptions(), &batch).ok()) {
      return false;
    }
  }
  return true;
}

// Returns false when the batch's Local Storage didn't merge (the target didn't open or a write failed).
bool MergeBatch(const base::FilePath& batch, const base::FilePath& profile) {
  bool merged = true;
  const base::FilePath staged_ls = batch.Append(kLocalStorageDir).Append(kLevelDbDir);
  if (IsPlainDir(staged_ls) &&
      !MergeLocalStorage(staged_ls, profile.Append(kLocalStorageDir).Append(kLevelDbDir))) {
    LOG(WARNING) << "clu: staged Local Storage didn't merge";
    merged = false;
  }
  const base::FilePath target_idb = profile.Append(kIndexedDbDir);
  base::FileEnumerator dbs(batch.Append(kIndexedDbDir), false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath db = dbs.Next(); !db.empty(); db = dbs.Next()) {
    const std::string name = db.BaseName().AsUTF8Unsafe();
    const base::FilePath to = target_idb.Append(db.BaseName());
    // A site Rio already has IndexedDB for keeps its own; a moved database takes its blob folder along.
    if (!name.ends_with(kIndexedDbSuffix) || base::PathExists(to)) {
      continue;
    }
    if (!base::CreateDirectory(target_idb) || !base::Move(db, to)) {
      merged = false;
      continue;
    }
    const std::string blob =
        name.substr(0, name.size() - std::string_view(kIndexedDbSuffix).size()) + kIndexedDbBlobSuffix;
    if (IsPlainDir(db.DirName().AppendASCII(blob)) && !base::PathExists(target_idb.AppendASCII(blob))) {
      base::Move(db.DirName().AppendASCII(blob), target_idb.AppendASCII(blob));
    }
  }
  return merged;
}

// Counts a failed merge in `batch`. Returns whether the batch has launches left.
bool KeepForRetry(const base::FilePath& batch) {
  const base::FilePath file = batch.AppendASCII(kMergeAttemptsFile);
  std::string text;
  int attempts = 0;
  // A junk count parses to whatever leads it, clamped here.
  if (base::ReadFileToStringWithMaxSize(file, &text, 16) && !base::StringToInt(text, &attempts)) {
    attempts = kMaxMergeAttempts;
  }
  attempts = std::clamp(attempts, 0, kMaxMergeAttempts) + 1;
  return attempts < kMaxMergeAttempts && base::WriteFile(file, base::NumberToString(attempts));
}

}  // namespace

StagedSiteStorage StageSiteStorage(const base::FilePath& source_profile, const base::FilePath& target_profile) {
  StagedSiteStorage staged;
  const base::FilePath root = target_profile.Append(kPendingSiteStorageDirName);
  base::FilePath batch;
  if (!base::CreateDirectory(root) || !base::CreateTemporaryDirInDir(root, FILE_PATH_LITERAL("batch"), &batch)) {
    return staged;
  }
  const base::FilePath ls = source_profile.Append(kLocalStorageDir).Append(kLevelDbDir);
  if (base::PathExists(ls)) {
    const base::FilePath to = batch.Append(kLocalStorageDir).Append(kLevelDbDir);
    if (!IsPlainDir(ls) || !CopyDir(ls, to)) {
      ++staged.skipped;
    } else {
      // The source browser may be running: its LOCK file came over but nothing holds it in the copy.
      std::unique_ptr<leveldb::DB> db = OpenLevelDb(to, /*create=*/false);
      if (db) {
        staged.local_storage = static_cast<int>(LocalStorageOrigins(db.get()).size());
      } else {
        ++staged.skipped;
        base::DeletePathRecursively(to.DirName());
      }
    }
  }
  const base::FilePath idb = source_profile.Append(kIndexedDbDir);
  if (IsPlainDir(idb)) {
    base::FileEnumerator dbs(idb, false, base::FileEnumerator::DIRECTORIES);
    for (base::FilePath db = dbs.Next(); !db.empty() && staged.indexed_db < static_cast<int>(kMaxSites);
         db = dbs.Next()) {
      const std::string name = db.BaseName().AsUTF8Unsafe();
      if (!name.ends_with(kIndexedDbSuffix)) {
        continue;
      }
      if (base::IsLink(db) || !CopyDir(db, batch.Append(kIndexedDbDir).Append(db.BaseName()))) {
        ++staged.skipped;
        continue;
      }
      const base::FilePath blob =
          idb.AppendASCII(name.substr(0, name.size() - std::string_view(kIndexedDbSuffix).size()) +
                          kIndexedDbBlobSuffix);
      if (IsPlainDir(blob)) {
        CopyDir(blob, batch.Append(kIndexedDbDir).Append(blob.BaseName()));
      }
      ++staged.indexed_db;
    }
  }
  if (staged.sites() == 0) {
    base::DeletePathRecursively(batch);
  }
  return staged;
}

void MergePendingSiteStorage(const base::FilePath& user_data_dir) {
  base::FileEnumerator profiles(user_data_dir, false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath profile = profiles.Next(); !profile.empty(); profile = profiles.Next()) {
    const base::FilePath root = profile.Append(kPendingSiteStorageDirName);
    if (!IsPlainDir(root)) {
      continue;
    }
    std::vector<base::FilePath> batches;
    base::FileEnumerator found(root, false, base::FileEnumerator::DIRECTORIES);
    for (base::FilePath batch = found.Next(); !batch.empty(); batch = found.Next()) {
      batches.push_back(batch);
    }
    bool kept = false;
    for (const base::FilePath& batch : batches) {
      // A batch that didn't merge stays for the next launch; whatever is left of a merged one (a site Rio already
      // had) goes, since the import is done once.
      if (!MergeBatch(batch, profile) && KeepForRetry(batch)) {
        kept = true;
        continue;
      }
      base::DeletePathRecursively(batch);
    }
    if (!kept) {
      base::DeletePathRecursively(root);
    }
  }
}

}  // namespace clu
