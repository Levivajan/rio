// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_STORE_H_
#define CLU_BROWSER_MODEL_CLU_STORE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "clu/browser/model/clu_archive_rules.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/model/clu_tab_repair.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "sql/database.h"
#include "sql/meta_table.h"

namespace sql {
class Statement;
}  // namespace sql

// One profile's Clu rows in SQLite (docs/chromium/PLAN.md 1.3 "Model and storage", the docs/PLAN.md 7.2 schema):
// spaces, tab_nodes, space_tab_state, archived_tabs and the Clu settings. The file is <profile>/Clu/clu.db, so a
// deleted profile takes its rows along and an off-the-record profile has none.
//
// Blocking: CluModelService owns it through a base::SequenceBound on a MayBlock sequence and never calls it on the UI
// thread. Every reader validates rows and repairs or skips junk; a corrupt row never stops startup.
namespace clu {

// The settings key of the favorites whose unread badge is off (a JSON list of row ids), and its cap.
inline constexpr char kUnreadBadgeOffKey[] = "tabs.unreadBadgeOff";
inline constexpr size_t kMaxUnreadBadgeOff = 256;

inline constexpr int kCluStoreVersion = 1;
inline constexpr char kCluStoreDirName[] = "Clu";
inline constexpr char kCluStoreFileName[] = "clu.db";
inline constexpr char kDefaultSpaceName[] = "Home";

struct SpaceRow {
  SpaceRow();
  SpaceRow(const SpaceRow&);
  SpaceRow& operator=(const SpaceRow&);
  ~SpaceRow();

  std::string id;
  std::string name;
  // "letter", "emoji" or "symbol" (one of SPACE_SYMBOLS in icon_value). The columns are free text, so a symbol
  // needs no schema change: rows written before symbols existed read as they were.
  std::string icon_kind = "letter";
  std::string icon_value;
  std::string color = "blue";
};

struct ArchiveRow {
  ArchiveRow();
  ArchiveRow(const ArchiveRow&);
  ArchiveRow& operator=(const ArchiveRow&);
  ArchiveRow(ArchiveRow&&);
  ArchiveRow& operator=(ArchiveRow&&);
  ~ArchiveRow();

  std::string id;
  std::string space_id;
  std::string title;
  std::string url;
  std::optional<std::string> nav_state;
  ArchiveReason reason = ArchiveReason::kClose;
  std::optional<std::string> group_id;
  int64_t archived_at = 0;
};

// What a closing tab leaves for its archive row (TabSnapshot in src/shared/archive.ts).
struct ArchiveSnapshot {
  ArchiveSnapshot();
  ArchiveSnapshot(const ArchiveSnapshot&);
  ArchiveSnapshot& operator=(const ArchiveSnapshot&);
  ArchiveSnapshot(ArchiveSnapshot&&);
  ArchiveSnapshot& operator=(ArchiveSnapshot&&);
  ~ArchiveSnapshot();

  // The archive row id, made by the caller so the closed stack can name the row before the write lands.
  std::string archive_id;
  std::string space_id;
  std::string title;
  std::string url;
  std::optional<std::string> nav_state;
  std::optional<std::string> split_id;
};

struct StoreLoad {
  StoreLoad();
  StoreLoad(const StoreLoad&) = delete;
  StoreLoad& operator=(const StoreLoad&) = delete;
  StoreLoad(StoreLoad&&);
  StoreLoad& operator=(StoreLoad&&);
  ~StoreLoad();

  // False when the file could not be opened and the rows live in memory for this run (dataHealth.persistent).
  bool persistent = true;
  // The profile's Clu id (ProfileState.id), made on first open.
  std::string profile_id;
  // In sidebar order; never empty.
  std::vector<SpaceRow> spaces;
  // Repaired rows, anchored tabs already back at their pinned URL (F2).
  RepairResult tree;
  std::map<std::string, SpaceTabState> space_states;
  // tabs.archiveAfterHours as stored, if it is a number.
  std::optional<double> archive_after_hours;
  // tabs.unreadBadgeOff: the favorites whose Show Unread Badge is off. Strings only, at most
  // kMaxUnreadBadgeOff of them; ids of rows that are gone are dropped by the next write.
  std::vector<std::string> unread_badge_off;
  // M8: "backup" or "fresh" when clu.db was unreadable this launch and moved aside.
  std::optional<std::string> recovered;
  // Repaired tab_splits rows (RepairSplits); the tree's split ids already match
  // them.
  std::vector<SplitRow> splits;
};

struct StoreWrite {
  StoreWrite();
  StoreWrite(const StoreWrite&) = delete;
  StoreWrite& operator=(const StoreWrite&) = delete;
  StoreWrite(StoreWrite&&);
  StoreWrite& operator=(StoreWrite&&);
  ~StoreWrite();

  // Tabs leaving for the archive in this write: their archive rows land in the same transaction as the tab_nodes
  // deletes, so a failed write loses neither.
  struct ArchiveBatch {
    ArchiveBatch();
    ArchiveBatch(std::vector<ArchiveSnapshot> snapshots,
                 ArchiveReason reason,
                 int64_t now);
    ArchiveBatch(const ArchiveBatch&);
    ArchiveBatch& operator=(const ArchiveBatch&);
    ArchiveBatch(ArchiveBatch&&);
    ArchiveBatch& operator=(ArchiveBatch&&);
    ~ArchiveBatch();

    std::vector<ArchiveSnapshot> snapshots;
    ArchiveReason reason = ArchiveReason::kClose;
    int64_t now = 0;
  };

  bool empty() const {
    return upserts.empty() && deletes.empty() && spaces.empty() &&
           archive.empty() && split_upserts.empty() && split_deletes.empty();
  }

  std::vector<TabNode> upserts;
  std::vector<std::string> deletes;
  std::vector<std::pair<std::string, SpaceTabState>> spaces;
  std::vector<ArchiveBatch> archive;
  // tab_splits rows (docs/chromium/m6.md).
  std::vector<SplitRow> split_upserts;
  std::vector<std::string> split_deletes;
};

struct ArchivePage {
  ArchivePage();
  ArchivePage(const ArchivePage&) = delete;
  ArchivePage& operator=(const ArchivePage&) = delete;
  ArchivePage(ArchivePage&&);
  ArchivePage& operator=(ArchivePage&&);
  ~ArchivePage();

  std::vector<ArchiveRow> rows;
  std::optional<std::string> next_cursor;
};

class CluStore {
 public:
  // An empty `path` keeps everything in memory (tests, and the fallback when the file cannot be opened).
  explicit CluStore(base::FilePath path);
  CluStore(const CluStore&) = delete;
  CluStore& operator=(const CluStore&) = delete;
  ~CluStore();

  // Opens or creates the file, migrates, reads and repairs every row in one transaction, resets anchored tabs to
  // their pinned URL, and makes the default space ("Home") when there is none. A file that fails SQLite's quick
  // check is moved aside as clu.db.corrupt-<now> and clu.db.bak comes back, else a fresh file (StoreLoad::recovered);
  // a file that passed is copied to clu.db.bak at most once a day.
  StoreLoad Load(int64_t now);

  // Writes the rows one structural change touched, and the archive rows of tabs that left, in one transaction. False
  // when nothing was written (the caller keeps the changes for the next write).
  bool Write(StoreWrite write);

  // One row per snapshot whose URL the tab policy allows, in one transaction; snapshots of one split (two or more
  // members) share a group id so they restore as a split. Returns the ids written.
  std::vector<std::string> AddArchive(std::vector<ArchiveSnapshot> snapshots,
                                      ArchiveReason reason,
                                      int64_t now);
  // Newest first; `query` matches title and URL literally; `cursor` is the previous page's next_cursor.
  ArchivePage ListArchive(const std::string& query,
                          const std::string& cursor,
                          int limit);
  // Readable rows with these ids, in the order given; junk rows are skipped and logged once.
  std::vector<ArchiveRow> GetArchive(const std::vector<std::string>& ids);
  int RemoveArchive(const std::vector<std::string>& ids);
  // Clear browsing data with "Archived tabs": rows archived in [begin, end), epoch ms.
  int RemoveArchiveBetween(int64_t begin, int64_t end);
  // The import map (M8): the Clu id each source key of `source` was imported as, for the keys that have one.
  std::map<std::string, std::string> GetImportMap(
      const std::string& source,
      const std::vector<std::string>& keys);
  // Records {source key, Clu id} rows in one transaction.
  bool PutImportMap(const std::string& source,
                    std::vector<std::pair<std::string, std::string>> rows);
  // Rows at least 90 days old and the oldest beyond 5000.
  int Prune(int64_t now);
  // What Cmd+Shift+T brings back with the closed stack empty: the newest readable row with reason close, clear,
  // delete or mini whose URL the policy allows, widened to its Clear or delete batch (same reason and time) or its
  // split group, in insertion order. Never an auto row.
  std::vector<ArchiveRow> NewestReopenable();
  // GetArchive() and NewestReopenable() for a restore: the rows the tab policy allows leave the archive in the same
  // call, so two restores in flight never both get one row.
  std::vector<ArchiveRow> TakeArchive(const std::vector<std::string>& ids);
  std::vector<ArchiveRow> TakeNewestReopenable();

  // The profile's spaces as CluSpacesService holds them, in switcher order, replacing the stored ones in one
  // transaction. The rows are a mirror: Local State is the source, and the mirror lets the profile's spaces come back
  // when Local State is lost (CluSpaceList::AdoptProfile()). Load() also keeps only space states of these spaces.
  bool ReplaceSpaces(std::vector<SpaceRow> spaces, int64_t now);

  // A Clu setting of this profile (`value_json` is JSON text).
  bool SetSetting(const std::string& key,
                  const std::string& value_json,
                  int64_t now);

  // A round trip through the store's sequence (tests wait for earlier writes with it).
  void Noop() {}

  // For tests: Write() fails as a full disk would, after its transaction began.
  void SetFailWritesForTesting(bool fail) { fail_writes_for_testing_ = fail; }

  // For tests: the database, to seed junk rows.
  sql::Database& database_for_testing() { return db_; }

 private:
  bool Open(int64_t now);
  // Opens the file and runs SQLite's quick check; false (and closed) when either fails.
  bool OpenFile();
  base::FilePath BackupPath() const;
  void MaybeBackUp(int64_t now);
  bool CreateSchema();
  void OnDatabaseError(int error, sql::Statement* statement);
  // AddArchive() inside the caller's transaction. False when a row could not be written.
  bool InsertArchive(std::vector<ArchiveSnapshot> snapshots,
                     ArchiveReason reason,
                     int64_t now,
                     std::vector<std::string>* written);
  std::optional<ArchiveRow> ReadArchiveRow(sql::Statement& statement);
  std::vector<SpaceRow> ReadSpaces(int64_t now);
  std::vector<ArchiveRow> Claim(std::vector<ArchiveRow> rows);

  const base::FilePath path_;
  sql::Database db_;
  // Archive rows already logged as junk, so each is logged once.
  std::set<std::string> logged_junk_;
  sql::MetaTable meta_;
  bool open_ = false;
  bool persistent_ = false;
  std::optional<std::string> recovered_;
  bool fail_writes_for_testing_ = false;
};

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_STORE_H_
