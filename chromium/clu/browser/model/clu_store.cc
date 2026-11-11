// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_store.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "clu/browser/model/clu_nav_state.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/ui/webui/bridge/validation.h"
#include "sql/error_delegate_util.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace clu {

namespace {

constexpr char kProfileIdKey[] = "clu.profile_id";
constexpr char kArchiveAfterKey[] = "tabs.archiveAfterHours";
constexpr int kArchivePageMax = 100;
constexpr int kReopenScan = 50;
constexpr size_t kMaxArchiveTitle = 1024;
std::optional<std::string> Text(sql::Statement& s, int column) {
  if (s.GetColumnType(column) != sql::ColumnType::kText) {
    return std::nullopt;
  }
  return s.ColumnString(column);
}

std::optional<double> Number(sql::Statement& s, int column) {
  switch (s.GetColumnType(column)) {
    case sql::ColumnType::kInteger:
      return static_cast<double>(s.ColumnInt64(column));
    case sql::ColumnType::kFloat:
      return s.ColumnDouble(column);
    default:
      return std::nullopt;
  }
}

void BindOptional(sql::Statement& s,
                  int index,
                  const std::optional<std::string>& value) {
  if (value) {
    s.BindString(index, *value);
  } else {
    s.BindNull(index);
  }
}

void BindOrNull(sql::Statement& s, int index, const std::string& value) {
  if (value.empty()) {
    s.BindNull(index);
  } else {
    s.BindString(index, value);
  }
}

bool InsertNode(sql::Database& db, const TabNode& n) {
  sql::Statement s(db.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO tab_nodes (id, space_id, section, kind, "
      "parent_id, position, url, pinned_url, title, title_override, muted, "
      "collapsed, split_id, nav_state, last_active_at, created_at, "
      "custom_icon) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
      "?)"));
  s.BindString(0, n.id);
  BindOrNull(s, 1, n.space_id);
  s.BindString(2, SectionName(n.section));
  s.BindString(3, KindName(n.kind));
  BindOrNull(s, 4, n.parent_id);
  s.BindInt(5, n.position);
  s.BindString(6, n.url);
  BindOptional(s, 7, n.pinned_url);
  s.BindString(8, n.title);
  BindOptional(s, 9, n.title_override);
  s.BindInt(10, n.muted ? 1 : 0);
  s.BindInt(11, n.collapsed ? 1 : 0);
  BindOptional(s, 12, n.split_id);
  BindOptional(s, 13, n.nav_state);
  s.BindInt64(14, n.last_active_at);
  s.BindInt64(15, n.created_at);
  BindOptional(s, 16, n.custom_icon);
  return s.Run();
}

bool InsertSplit(sql::Database& db, const SplitRow& split) {
  sql::Statement s(db.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO tab_splits (id, space_id, layout, "
      "focused_tab_id, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)"));
  s.BindString(0, split.id);
  s.BindString(1, split.space_id);
  s.BindString(2, SplitLayoutJson(split));
  s.BindString(3, split.focused_tab_id);
  s.BindInt64(4, split.created_at);
  s.BindInt64(5, split.updated_at);
  return s.Run();
}

bool DeleteSplit(sql::Database& db, const std::string& id) {
  sql::Statement s(db.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM tab_splits WHERE id = ?"));
  s.BindString(0, id);
  return s.Run();
}

// Near sidebar order for the split repair: pinned rows before Today, then by
// list and position. Folder children sort after root rows, so the pane order of
// a split rebuilt from tabs in folders is best-effort.
std::vector<SplitMember> SplitMembers(const std::vector<TabNode>& nodes) {
  std::vector<const TabNode*> tabs;
  for (const TabNode& node : nodes) {
    if (node.is_tab()) {
      tabs.push_back(&node);
    }
  }
  std::stable_sort(
      tabs.begin(), tabs.end(), [](const TabNode* a, const TabNode* b) {
        return std::tie(a->space_id, a->section, a->parent_id, a->position) <
               std::tie(b->space_id, b->section, b->parent_id, b->position);
      });
  std::vector<SplitMember> members;
  for (const TabNode* node : tabs) {
    members.push_back({.id = node->id,
                       .space_id = node->space_id,
                       .favorite = node->section == Section::kFavorite,
                       .split_id = node->split_id});
  }
  return members;
}

bool WriteSpaceState(sql::Database& db,
                     const std::string& space_id,
                     const SpaceTabState& state) {
  sql::Statement s(db.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO space_tab_state (space_id, active_tab_id, "
      "pinned_collapsed) VALUES (?, ?, ?)"));
  s.BindString(0, space_id);
  BindOrNull(s, 1, state.active_tab_id);
  s.BindInt(2, state.pinned_collapsed ? 1 : 0);
  return s.Run();
}

// "archivedAt:id", the newest-first paging key.
std::optional<std::pair<int64_t, std::string>> ParseCursor(
    const std::string& cursor) {
  const size_t colon = cursor.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 17 ||
      cursor.size() - colon - 1 == 0 || cursor.size() - colon - 1 > 64) {
    return std::nullopt;
  }
  int64_t at = 0;
  if (!base::StringToInt64(std::string_view(cursor).substr(0, colon), &at)) {
    return std::nullopt;
  }
  return std::make_pair(at, cursor.substr(colon + 1));
}

}  // namespace

SpaceRow::SpaceRow() = default;
SpaceRow::SpaceRow(const SpaceRow&) = default;
SpaceRow& SpaceRow::operator=(const SpaceRow&) = default;
SpaceRow::~SpaceRow() = default;

ArchiveRow::ArchiveRow() = default;
ArchiveRow::ArchiveRow(const ArchiveRow&) = default;
ArchiveRow& ArchiveRow::operator=(const ArchiveRow&) = default;
ArchiveRow::ArchiveRow(ArchiveRow&&) = default;
ArchiveRow& ArchiveRow::operator=(ArchiveRow&&) = default;
ArchiveRow::~ArchiveRow() = default;

ArchiveSnapshot::ArchiveSnapshot() = default;
ArchiveSnapshot::ArchiveSnapshot(const ArchiveSnapshot&) = default;
ArchiveSnapshot& ArchiveSnapshot::operator=(const ArchiveSnapshot&) = default;
ArchiveSnapshot::ArchiveSnapshot(ArchiveSnapshot&&) = default;
ArchiveSnapshot& ArchiveSnapshot::operator=(ArchiveSnapshot&&) = default;
ArchiveSnapshot::~ArchiveSnapshot() = default;

StoreLoad::StoreLoad() = default;
StoreLoad::StoreLoad(StoreLoad&&) = default;
StoreLoad& StoreLoad::operator=(StoreLoad&&) = default;
StoreLoad::~StoreLoad() = default;

StoreWrite::StoreWrite() = default;
StoreWrite::StoreWrite(StoreWrite&&) = default;
StoreWrite& StoreWrite::operator=(StoreWrite&&) = default;
StoreWrite::~StoreWrite() = default;

StoreWrite::ArchiveBatch::ArchiveBatch() = default;
StoreWrite::ArchiveBatch::ArchiveBatch(std::vector<ArchiveSnapshot> snapshots,
                                       ArchiveReason reason,
                                       int64_t now)
    : snapshots(std::move(snapshots)), reason(reason), now(now) {}
StoreWrite::ArchiveBatch::ArchiveBatch(const ArchiveBatch&) = default;
StoreWrite::ArchiveBatch& StoreWrite::ArchiveBatch::operator=(
    const ArchiveBatch&) = default;
StoreWrite::ArchiveBatch::ArchiveBatch(ArchiveBatch&&) = default;
StoreWrite::ArchiveBatch& StoreWrite::ArchiveBatch::operator=(ArchiveBatch&&) =
    default;
StoreWrite::ArchiveBatch::~ArchiveBatch() = default;

ArchivePage::ArchivePage() = default;
ArchivePage::ArchivePage(ArchivePage&&) = default;
ArchivePage& ArchivePage::operator=(ArchivePage&&) = default;
ArchivePage::~ArchivePage() = default;

CluStore::CluStore(base::FilePath path)
    : path_(std::move(path)), db_(sql::Database::Tag("CluModel")) {
  // Errors are logged, never fatal: a corrupt file moves aside for the backup or a fresh file, a file that still cannot
  // be opened falls back to memory, and a failed write is kept by the model for the next one.
  db_.set_error_callback(base::BindRepeating(&CluStore::OnDatabaseError,
                                             base::Unretained(this)));
}

CluStore::~CluStore() = default;

void CluStore::OnDatabaseError(int error, sql::Statement* statement) {
  LOG(ERROR) << "clu: sqlite error " << error;
  // Corruption only gets worse: the poisoned handle fails every call quietly, and the next launch's quick check moves
  // the file aside and brings the backup back (Open()). Inside Open() the check itself handles it.
  if (open_ && sql::IsErrorCatastrophic(error) && db_.is_open()) {
    db_.Poison();
  }
}

bool CluStore::OpenFile() {
  // A failed Open() can leave the handle open (a file that is not SQLite), and the next Open() would refuse it.
  if (!db_.Open(path_)) {
    db_.Close();
    return false;
  }
  // A file that is not SQLite, or one SQLite finds damaged, fails here. A damaged schema can fail the
  // prepare, and stepping an invalid statement is fatal in debug builds.
  sql::Statement check(db_.GetUniqueStatement("PRAGMA quick_check"));
  if (check.is_valid() && check.Step() && check.ColumnString(0) == "ok") {
    return true;
  }
  db_.Close();
  return false;
}

bool CluStore::Open(int64_t now) {
  if (open_) {
    return true;
  }
  if (!path_.empty() && base::CreateDirectory(path_.DirName())) {
    if (OpenFile()) {
      persistent_ = true;
    } else if (base::DirectoryExists(path_)) {
      // Not a damaged database but something else in its place: leave it, and the rows stay in memory.
    } else {
      // The damaged file and its journals move aside as clu.db.corrupt-<time>, then the backup comes
      // back if it passes the check, else Clu starts fresh. dataHealth.recovered says which, and a toast says it
      // once.
      const std::string suffix =
          base::StrCat({".corrupt-", base::NumberToString(now)});
      for (const char* ext : {"", "-journal", "-wal", "-shm"}) {
        const base::FilePath from(path_.value() + ext);
        if (base::PathExists(from)) {
          base::Move(from, base::FilePath(path_.value() + suffix + ext));
        }
      }
      const base::FilePath backup = BackupPath();
      if (base::PathExists(backup) && base::CopyFile(backup, path_) &&
          OpenFile()) {
        recovered_ = "backup";
        persistent_ = true;
      } else {
        base::DeleteFile(path_);
        if (OpenFile()) {
          recovered_ = "fresh";
          persistent_ = true;
        }
      }
      LOG(WARNING) << "clu: " << path_ << " was unreadable; "
                   << (recovered_ ? *recovered_ : std::string("memory"));
    }
    if (!persistent_) {
      LOG(ERROR) << "clu: cannot open " << path_ << "; rows stay in memory";
      // A failed Open() can leave the handle open (or poisoned); OpenInMemory() refuses an open Database.
      db_.Close();
    }
  }
  if (!persistent_ && !db_.OpenInMemory()) {
    return false;
  }
  if (sql::MetaTable::RazeIfIncompatible(&db_, kCluStoreVersion,
                                         kCluStoreVersion) ==
      sql::RazeIfIncompatibleResult::kFailed) {
    return false;
  }
  if (!meta_.Init(&db_, kCluStoreVersion, kCluStoreVersion) ||
      !CreateSchema()) {
    return false;
  }
  open_ = true;
  return true;
}

bool CluStore::CreateSchema() {
  // No CHECK constraints: a row with a junk section must still load, so the repair can fix it (docs/PLAN.md 3.11).
  return db_.Execute(
             "CREATE TABLE IF NOT EXISTS spaces (id TEXT PRIMARY KEY, name "
             "TEXT, icon_kind TEXT, icon_value TEXT, color TEXT, position "
             "INTEGER, created_at INTEGER, updated_at INTEGER)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS tab_nodes (id TEXT PRIMARY KEY, "
             "space_id TEXT, section TEXT, kind TEXT, parent_id TEXT, position "
             "INTEGER, url TEXT, pinned_url TEXT, title TEXT, title_override "
             "TEXT, muted INTEGER, collapsed INTEGER, split_id TEXT, nav_state "
             "TEXT, last_active_at INTEGER, created_at INTEGER, custom_icon "
             "TEXT)") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS tab_nodes_list ON tab_nodes "
             "(space_id, section, parent_id, position)") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS tab_nodes_idle ON tab_nodes (section, "
             "last_active_at)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS space_tab_state (space_id TEXT "
             "PRIMARY KEY, active_tab_id TEXT, pinned_collapsed INTEGER)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS archived_tabs (id TEXT PRIMARY KEY, "
             "space_id TEXT, title TEXT, url TEXT, nav_state TEXT, reason "
             "TEXT, "
             "group_id TEXT, archived_at INTEGER)") &&
         db_.Execute(
             "CREATE INDEX IF NOT EXISTS archived_tabs_recent ON archived_tabs "
             "(archived_at DESC)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, "
             "value_json TEXT, updated_at INTEGER)") &&
         // M8: what an import made, so a re-import adds nothing.
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS import_map (source TEXT NOT NULL, "
             "source_key TEXT NOT NULL, clu_id TEXT, PRIMARY KEY (source, "
             "source_key))") &&
         // M6 (docs/plan/05-multitask.md "Persistence"); tab_nodes.split_id
         // names the row.
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS tab_splits (id TEXT PRIMARY KEY, "
             "space_id TEXT, layout TEXT, focused_tab_id TEXT, created_at "
             "INTEGER, updated_at INTEGER)");
}

std::vector<SpaceRow> CluStore::ReadSpaces(int64_t now) {
  std::vector<SpaceRow> spaces;
  sql::Statement s(db_.GetUniqueStatement(
      "SELECT id, name, icon_kind, icon_value, color FROM spaces ORDER BY "
      "position, created_at, id"));
  while (s.Step()) {
    const std::optional<std::string> id = Text(s, 0);
    if (!id || !IsUuidV4(*id)) {
      LOG(WARNING) << "clu: space row with a bad id skipped";
      continue;
    }
    SpaceRow row;
    row.id = *id;
    row.name = CleanName(Text(s, 1).value_or(std::string()));
    if (row.name.empty()) {
      row.name = kDefaultSpaceName;
    }
    const std::optional<std::string> kind = Text(s, 2);
    const std::optional<std::string> value = Text(s, 3);
    if (kind == "emoji" && value && IsOneEmoji(*value)) {
      row.icon_kind = "emoji";
      row.icon_value = *value;
    } else if (kind == "symbol" && value && IsSpaceSymbol(*value)) {
      // A symbol this build does not bundle reads as the letter icon.
      row.icon_kind = "symbol";
      row.icon_value = *value;
    }
    row.color = Text(s, 4).value_or("blue");
    spaces.push_back(std::move(row));
  }
  if (!spaces.empty()) {
    return spaces;
  }
  SpaceRow home;
  home.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  home.name = kDefaultSpaceName;
  sql::Statement insert(db_.GetUniqueStatement(
      "INSERT INTO spaces (id, name, icon_kind, icon_value, color, position, "
      "created_at, updated_at) VALUES (?, ?, 'letter', NULL, ?, 0, ?, ?)"));
  insert.BindString(0, home.id);
  insert.BindString(1, home.name);
  insert.BindString(2, home.color);
  insert.BindInt64(3, now);
  insert.BindInt64(4, now);
  if (!insert.Run()) {
    LOG(ERROR) << "clu: cannot store the default space";
  }
  spaces.push_back(std::move(home));
  return spaces;
}

base::FilePath CluStore::BackupPath() const {
  return base::FilePath(path_.value() + ".bak");
}

void CluStore::MaybeBackUp(int64_t now) {
  // At most once a day, from a file that passed the check this launch. No transaction is open on this
  // sequence, so the copy is a consistent file.
  if (!persistent_ || path_.empty()) {
    return;
  }
  const base::FilePath backup = BackupPath();
  base::File::Info info;
  if (base::GetFileInfo(backup, &info) &&
      now - info.last_modified.InMillisecondsSinceUnixEpoch() <
          base::Days(1).InMilliseconds()) {
    return;
  }
  const base::FilePath tmp(backup.value() + ".tmp");
  if (!base::CopyFile(path_, tmp) || !base::Move(tmp, backup)) {
    base::DeleteFile(tmp);
    LOG(WARNING) << "clu: no backup of " << path_ << " this time";
  }
}

StoreLoad CluStore::Load(int64_t now) {
  StoreLoad load;
  if (!Open(now)) {
    load.persistent = false;
    SpaceRow home;
    home.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    home.name = kDefaultSpaceName;
    load.spaces.push_back(std::move(home));
    load.profile_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    return load;
  }
  load.persistent = persistent_;

  std::string profile_id;
  if (!meta_.GetValue(kProfileIdKey, &profile_id) || !IsUuidV4(profile_id)) {
    profile_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    if (!meta_.SetValue(kProfileIdKey, profile_id)) {
      LOG(ERROR) << "clu: cannot store the profile id";
    }
  }
  load.profile_id = profile_id;
  load.spaces = ReadSpaces(now);
  std::vector<std::string> space_ids;
  for (const SpaceRow& space : load.spaces) {
    space_ids.push_back(space.id);
  }

  std::vector<RawTabRow> raw;
  bool read_ok = true;
  {
    sql::Statement s(db_.GetUniqueStatement(
        "SELECT rowid, id, space_id, section, kind, parent_id, position, url, "
        "pinned_url, title, title_override, muted, collapsed, split_id, "
        "nav_state, last_active_at, created_at, custom_icon FROM tab_nodes"));
    while (s.Step()) {
      RawTabRow row;
      row.rowid = s.ColumnInt64(0);
      row.id = Text(s, 1);
      row.space_id = Text(s, 2);
      row.section = Text(s, 3);
      row.kind = Text(s, 4);
      row.parent_id = Text(s, 5);
      row.position = Number(s, 6);
      row.url = Text(s, 7);
      row.pinned_url = Text(s, 8);
      row.title = Text(s, 9);
      row.title_override = Text(s, 10);
      row.muted = Number(s, 11);
      row.collapsed = Number(s, 12);
      row.split_id = Text(s, 13);
      row.nav_state = Text(s, 14);
      row.last_active_at = Number(s, 15);
      row.created_at = Number(s, 16);
      row.custom_icon = Text(s, 17);
      raw.push_back(std::move(row));
    }
    // A read that stopped partway is not the end of the table: the repair below must not rewrite tab_nodes from it.
    read_ok = s.Succeeded();
  }
  load.tree = RepairRows(raw, space_ids, now);
  for (const auto& [kind, count] : load.tree.kinds) {
    LOG(WARNING) << "clu: tab_nodes repair " << kind << " count " << count;
  }

  // F2: after a restart an anchored tab is back at its pinned URL, without history.
  bool reset = false;
  for (TabNode& node : load.tree.nodes) {
    if (node.is_tab() && node.pinned_url && node.url != *node.pinned_url) {
      node.url = *node.pinned_url;
      reset = true;
    }
  }

  {
    sql::Statement s(db_.GetUniqueStatement(
        "SELECT space_id, active_tab_id, pinned_collapsed FROM "
        "space_tab_state"));
    std::set<std::string> tab_ids;
    for (const TabNode& node : load.tree.nodes) {
      if (node.is_tab()) {
        tab_ids.insert(node.id);
      }
    }
    while (s.Step()) {
      const std::optional<std::string> space_id = Text(s, 0);
      if (!space_id ||
          std::ranges::find(space_ids, *space_id) == space_ids.end()) {
        continue;
      }
      SpaceTabState state;
      const std::optional<std::string> active = Text(s, 1);
      if (active && tab_ids.contains(*active)) {
        state.active_tab_id = *active;
      }
      state.pinned_collapsed = Number(s, 2) == 1;
      load.space_states.emplace(*space_id, std::move(state));
    }
    read_ok = read_ok && s.Succeeded();
  }

  {
    sql::Statement s(db_.GetUniqueStatement(
        "SELECT value_json FROM settings WHERE key = ?"));
    s.BindString(0, kArchiveAfterKey);
    if (s.Step()) {
      if (const std::optional<std::string> json = Text(s, 0)) {
        const std::optional<base::Value> value =
            base::JSONReader::Read(*json, base::JSON_PARSE_RFC);
        if (value && (value->is_int() || value->is_double())) {
          load.archive_after_hours = value->GetDouble();
        }
      }
    }
  }
  {
    sql::Statement s(db_.GetUniqueStatement(
        "SELECT value_json FROM settings WHERE key = ?"));
    s.BindString(0, kUnreadBadgeOffKey);
    if (s.Step()) {
      if (const std::optional<std::string> json = Text(s, 0)) {
        const std::optional<base::Value> value =
            base::JSONReader::Read(*json, base::JSON_PARSE_RFC);
        if (value && value->is_list()) {
          for (const base::Value& id : value->GetList()) {
            if (load.unread_badge_off.size() >= kMaxUnreadBadgeOff) {
              break;
            }
            if (id.is_string() && !id.GetString().empty() &&
                id.GetString().size() <= 64) {
              load.unread_badge_off.push_back(id.GetString());
            }
          }
        }
      }
    }
  }

  // Splits (M6): junk rows rebuild from their tabs or dissolve, and every tab
  // carries the split that holds it.
  SplitRepairResult splits;
  {
    std::vector<RawSplitRow> rows;
    sql::Statement s(db_.GetUniqueStatement(
        "SELECT id, space_id, layout, focused_tab_id, created_at, updated_at "
        "FROM tab_splits"));
    while (s.Step()) {
      RawSplitRow row;
      row.id = Text(s, 0);
      row.space_id = Text(s, 1);
      row.layout = Text(s, 2);
      row.focused_tab_id = Text(s, 3);
      row.created_at = static_cast<int64_t>(Number(s, 4).value_or(0));
      row.updated_at = static_cast<int64_t>(Number(s, 5).value_or(0));
      rows.push_back(std::move(row));
    }
    read_ok = read_ok && s.Succeeded();
    splits = RepairSplits(rows, SplitMembers(load.tree.nodes));
  }
  if (!splits.member_fixes.empty() || !splits.dropped.empty() ||
      !splits.rewritten.empty()) {
    LOG(WARNING) << "clu: tab_splits repair: " << splits.dropped.size()
                 << " dropped, " << splits.rewritten.size() << " rebuilt, "
                 << splits.member_fixes.size() << " tabs fixed";
    std::map<std::string, std::optional<std::string>> fixes(
        splits.member_fixes.begin(), splits.member_fixes.end());
    for (TabNode& node : load.tree.nodes) {
      if (const auto it = fixes.find(node.id); it != fixes.end()) {
        node.split_id = it->second;
      }
    }
  }
  // A failed read repairs only the rows in memory, like tab_nodes below.
  if (read_ok && (!splits.member_fixes.empty() || !splits.dropped.empty() ||
                  !splits.rewritten.empty())) {
    sql::Transaction transaction(&db_);
    bool ok = transaction.Begin();
    for (const std::string& id : splits.dropped) {
      ok = ok && DeleteSplit(db_, id);
    }
    for (const SplitRow& split : splits.splits) {
      if (std::ranges::find(splits.rewritten, split.id) !=
          splits.rewritten.end()) {
        ok = ok && InsertSplit(db_, split);
      }
    }
    for (const auto& [id, split_id] : splits.member_fixes) {
      sql::Statement fix(db_.GetCachedStatement(
          SQL_FROM_HERE, "UPDATE tab_nodes SET split_id = ? WHERE id = ?"));
      BindOptional(fix, 0, split_id);
      fix.BindString(1, id);
      ok = ok && fix.Run();
    }
    if (ok) {
      transaction.Commit();
    } else {
      transaction.Rollback();
      LOG(ERROR) << "clu: tab_splits repair not written";
    }
  }
  load.splits = std::move(splits.splits);

  // Every repair, and the F2 reset, lands in one transaction: the stored rows become exactly the tree just read.
  if (!read_ok) {
    LOG(ERROR) << "clu: tab_nodes read failed; the stored rows are left as "
                  "they are";
  } else if (!load.tree.kinds.empty() || !load.tree.deleted.empty() ||
             reset) {
    sql::Transaction transaction(&db_);
    bool ok = transaction.Begin();
    if (ok) {
      ok = db_.Execute("DELETE FROM tab_nodes");
      for (const TabNode& node : load.tree.nodes) {
        ok = ok && InsertNode(db_, node);
      }
      ok = ok && db_.Execute(
                     "DELETE FROM space_tab_state WHERE space_id NOT IN "
                     "(SELECT id FROM spaces)");
      for (const auto& [space_id, state] : load.space_states) {
        ok = ok && WriteSpaceState(db_, space_id, state);
      }
    }
    if (ok) {
      transaction.Commit();
    } else {
      transaction.Rollback();
      LOG(ERROR) << "clu: tab_nodes repair not written";
    }
  }
  load.recovered = recovered_;
  MaybeBackUp(now);
  return load;
}

bool CluStore::Write(StoreWrite write) {
  if (!open_ || write.empty()) {
    return false;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }
  bool ok = !fail_writes_for_testing_;
  for (StoreWrite::ArchiveBatch& batch : write.archive) {
    std::vector<std::string> written;
    ok = ok && InsertArchive(std::move(batch.snapshots), batch.reason,
                             batch.now, &written);
  }
  for (const std::string& id : write.deletes) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE, "DELETE FROM tab_nodes WHERE id = ?"));
    s.BindString(0, id);
    ok = ok && s.Run();
  }
  for (const TabNode& node : write.upserts) {
    ok = ok && InsertNode(db_, node);
  }
  for (const auto& [space_id, state] : write.spaces) {
    ok = ok && WriteSpaceState(db_, space_id, state);
  }
  for (const std::string& id : write.split_deletes) {
    ok = ok && DeleteSplit(db_, id);
  }
  for (const SplitRow& split : write.split_upserts) {
    ok = ok && InsertSplit(db_, split);
  }
  if (!ok) {
    transaction.Rollback();
    LOG(ERROR) << "clu: tab write rolled back";
    return false;
  }
  return transaction.Commit();
}

std::vector<std::string> CluStore::AddArchive(
    std::vector<ArchiveSnapshot> snapshots,
    ArchiveReason reason,
    int64_t now) {
  std::vector<std::string> written;
  if (!open_) {
    return written;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin() ||
      !InsertArchive(std::move(snapshots), reason, now, &written) ||
      !transaction.Commit()) {
    written.clear();
  }
  return written;
}

bool CluStore::InsertArchive(std::vector<ArchiveSnapshot> snapshots,
                             ArchiveReason reason,
                             int64_t now,
                             std::vector<std::string>* written) {
  // A row the tab policy refuses is dropped on purpose, which is not a failure.
  std::erase_if(snapshots, [](const ArchiveSnapshot& s) {
    return !IsUuidV4(s.archive_id) || !IsRestorableTabUrl(s.url);
  });
  // Members of one split archived together form a group that restores as a split (PLAN decision 55).
  std::map<std::string, int> split_counts;
  for (const ArchiveSnapshot& s : snapshots) {
    if (s.split_id) {
      ++split_counts[*s.split_id];
    }
  }
  std::map<std::string, std::string> groups;
  for (const auto& [split_id, count] : split_counts) {
    if (count >= 2) {
      groups[split_id] = base::Uuid::GenerateRandomV4().AsLowercaseString();
    }
  }
  for (ArchiveSnapshot& snap : snapshots) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT OR REPLACE INTO archived_tabs (id, space_id, title, url, "
        "nav_state, reason, group_id, archived_at) VALUES (?, ?, ?, ?, ?, ?, "
        "?, ?)"));
    std::optional<std::string> nav;
    if (snap.nav_state) {
      if (std::optional<NavState> parsed = ParseNavState(*snap.nav_state)) {
        nav = CappedNavStateJson(std::move(*parsed));
      }
    }
    std::string title;
    base::TruncateUTF8ToByteSize(CleanTitle(snap.title), kMaxArchiveTitle,
                                 &title);
    s.BindString(0, snap.archive_id);
    BindOrNull(s, 1, IsUuidV4(snap.space_id) ? snap.space_id : std::string());
    s.BindString(2, title);
    s.BindString(3, snap.url);
    BindOptional(s, 4, nav);
    s.BindString(5, ArchiveReasonName(reason));
    std::optional<std::string> group;
    if (snap.split_id) {
      if (const auto it = groups.find(*snap.split_id); it != groups.end()) {
        group = it->second;
      }
    }
    BindOptional(s, 6, group);
    s.BindInt64(7, now);
    if (!s.Run()) {
      return false;
    }
    written->push_back(snap.archive_id);
  }
  return true;
}

std::optional<ArchiveRow> CluStore::ReadArchiveRow(sql::Statement& s) {
  const std::optional<std::string> id = Text(s, 0);
  auto junk = [&](const char* why) -> std::optional<ArchiveRow> {
    const std::string key = id.value_or("?").substr(0, 64);
    if (logged_junk_.insert(key).second) {
      LOG(WARNING) << "clu: unreadable archive row skipped (" << why << ")";
    }
    return std::nullopt;
  };
  if (!id || id->empty()) {
    return junk("id");
  }
  const std::optional<std::string> reason_name = Text(s, 5);
  const std::optional<ArchiveReason> reason =
      reason_name ? ArchiveReasonFromName(*reason_name) : std::nullopt;
  if (!reason) {
    return junk("reason");
  }
  const std::optional<std::string> url = Text(s, 3);
  if (!url || url->empty() || url->size() > kMaxUrlLength) {
    return junk("url");
  }
  const std::optional<double> archived_at = Number(s, 7);
  if (!archived_at) {
    return junk("archived_at");
  }
  ArchiveRow row;
  row.id = *id;
  const std::optional<std::string> space_id = Text(s, 1);
  row.space_id = space_id && IsUuidV4(*space_id) ? *space_id : std::string();
  std::string title;
  base::TruncateUTF8ToByteSize(CleanTitle(Text(s, 2).value_or(std::string())),
                               kMaxArchiveTitle, &title);
  row.title = title.empty() ? DisplayHost(*url) : title;
  row.url = *url;
  if (const std::optional<std::string> nav = Text(s, 4)) {
    // A corrupt nav state reads as none: the row restores at its URL.
    if (std::optional<NavState> parsed = ParseNavState(*nav)) {
      row.nav_state = SerializeNavState(*parsed);
    }
  }
  row.reason = *reason;
  row.group_id = Text(s, 6);
  if (row.group_id && row.group_id->empty()) {
    row.group_id.reset();
  }
  row.archived_at = static_cast<int64_t>(*archived_at);
  return row;
}

ArchivePage CluStore::ListArchive(const std::string& query,
                                  const std::string& cursor,
                                  int limit) {
  ArchivePage page;
  if (!open_) {
    return page;
  }
  limit = std::clamp(limit, 1, kArchivePageMax);
  std::optional<std::pair<int64_t, std::string>> after;
  if (!cursor.empty()) {
    after = ParseCursor(cursor);
    if (!after) {
      return page;
    }
  }
  const std::string trimmed(
      base::TrimWhitespaceASCII(query.substr(0, 1024), base::TRIM_ALL));
  const std::string pattern = LikePattern(trimmed);
  sql::Statement s(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, space_id, title, url, nav_state, reason, group_id, "
      "archived_at FROM archived_tabs WHERE (? = 0 OR archived_at < ? OR "
      "(archived_at = ? AND id < ?)) AND (? = '' OR title LIKE ? ESCAPE '\\' "
      "OR url LIKE ? ESCAPE '\\') ORDER BY archived_at DESC, id DESC LIMIT ?"));
  s.BindInt(0, after ? 1 : 0);
  s.BindInt64(1, after ? after->first : 0);
  s.BindInt64(2, after ? after->first : 0);
  s.BindString(3, after ? after->second : std::string());
  s.BindString(4, trimmed);
  s.BindString(5, pattern);
  s.BindString(6, pattern);
  s.BindInt(7, limit);
  int read = 0;
  std::string last_id;
  int64_t last_at = 0;
  while (s.Step()) {
    ++read;
    // The cursor follows the last row read, junk or not, so a skipped row never stops paging.
    last_id = Text(s, 0).value_or(std::string());
    last_at = static_cast<int64_t>(Number(s, 7).value_or(0));
    if (std::optional<ArchiveRow> row = ReadArchiveRow(s)) {
      page.rows.push_back(std::move(*row));
    }
  }
  if (read == limit && !last_id.empty()) {
    page.next_cursor =
        base::StrCat({base::NumberToString(last_at), ":", last_id});
  }
  return page;
}

std::vector<ArchiveRow> CluStore::GetArchive(
    const std::vector<std::string>& ids) {
  std::vector<ArchiveRow> rows;
  if (!open_) {
    return rows;
  }
  for (const std::string& id : ids) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id, space_id, title, url, nav_state, reason, group_id, "
        "archived_at FROM archived_tabs WHERE id = ?"));
    s.BindString(0, id);
    if (s.Step()) {
      if (std::optional<ArchiveRow> row = ReadArchiveRow(s)) {
        rows.push_back(std::move(*row));
      }
    }
  }
  return rows;
}

int CluStore::RemoveArchive(const std::vector<std::string>& ids) {
  if (!open_ || ids.empty()) {
    return 0;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return 0;
  }
  int removed = 0;
  for (const std::string& id : ids) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE, "DELETE FROM archived_tabs WHERE id = ?"));
    s.BindString(0, id);
    if (s.Run()) {
      removed += static_cast<int>(db_.GetLastChangeCount());
    }
  }
  return transaction.Commit() ? removed : 0;
}

int CluStore::RemoveArchiveBetween(int64_t begin, int64_t end) {
  if (!open_ || end <= begin) {
    return 0;
  }
  sql::Statement s(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE FROM archived_tabs WHERE archived_at >= ? AND archived_at < ?"));
  s.BindInt64(0, begin);
  s.BindInt64(1, end);
  return s.Run() ? static_cast<int>(db_.GetLastChangeCount()) : 0;
}

std::map<std::string, std::string> CluStore::GetImportMap(
    const std::string& source,
    const std::vector<std::string>& keys) {
  std::map<std::string, std::string> out;
  if (!open_) {
    return out;
  }
  for (const std::string& key : keys) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT clu_id FROM import_map WHERE source = ? AND source_key = ?"));
    s.BindString(0, source);
    s.BindString(1, key);
    if (s.Step()) {
      out.emplace(key, s.ColumnString(0));
    }
  }
  return out;
}

bool CluStore::PutImportMap(
    const std::string& source,
    std::vector<std::pair<std::string, std::string>> rows) {
  if (!open_ || rows.empty()) {
    return false;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }
  for (const auto& [key, clu_id] : rows) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT OR REPLACE INTO import_map (source, source_key, clu_id) "
        "VALUES (?, ?, ?)"));
    s.BindString(0, source);
    s.BindString(1, key);
    s.BindString(2, clu_id);
    if (!s.Run()) {
      return false;
    }
  }
  return transaction.Commit();
}

int CluStore::Prune(int64_t now) {
  if (!open_) {
    return 0;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return 0;
  }
  int removed = 0;
  {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE, "DELETE FROM archived_tabs WHERE archived_at <= ?"));
    s.BindInt64(0, now - kArchiveRetentionMs);
    if (s.Run()) {
      removed += static_cast<int>(db_.GetLastChangeCount());
    }
  }
  {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "DELETE FROM archived_tabs WHERE id IN (SELECT id FROM archived_tabs "
        "ORDER BY archived_at DESC, id DESC LIMIT -1 OFFSET ?)"));
    s.BindInt(0, kArchiveMaxRows);
    if (s.Run()) {
      removed += static_cast<int>(db_.GetLastChangeCount());
    }
  }
  if (!transaction.Commit()) {
    return 0;
  }
  if (removed > 0) {
    LOG(WARNING) << "clu: archive pruned " << removed << " rows";
  }
  return removed;
}

std::vector<ArchiveRow> CluStore::TakeArchive(
    const std::vector<std::string>& ids) {
  return Claim(GetArchive(ids));
}

std::vector<ArchiveRow> CluStore::TakeNewestReopenable() {
  return Claim(NewestReopenable());
}

std::vector<ArchiveRow> CluStore::Claim(std::vector<ArchiveRow> rows) {
  // A row the policy refuses stays, so the archive can still delete it (02-tabs F7).
  std::vector<std::string> ids;
  for (const ArchiveRow& row : rows) {
    if (IsRestorableTabUrl(row.url)) {
      ids.push_back(row.id);
    }
  }
  if (!ids.empty() && RemoveArchive(ids) != static_cast<int>(ids.size())) {
    LOG(ERROR) << "clu: restored archive rows not removed";
  }
  return rows;
}

std::vector<ArchiveRow> CluStore::NewestReopenable() {
  std::vector<ArchiveRow> out;
  if (!open_) {
    return out;
  }
  std::optional<ArchiveRow> newest;
  {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id, space_id, title, url, nav_state, reason, group_id, "
        "archived_at FROM archived_tabs WHERE reason IN ('close', 'clear', "
        "'delete', 'mini') ORDER BY archived_at DESC, rowid DESC LIMIT ?"));
    s.BindInt(0, kReopenScan);
    // A row the tab policy refuses would come back on every press, so the fallback looks past it.
    while (s.Step()) {
      std::optional<ArchiveRow> row = ReadArchiveRow(s);
      if (row && IsRestorableTabUrl(row->url)) {
        newest = std::move(row);
        break;
      }
    }
  }
  if (!newest) {
    return out;
  }
  const bool batch = newest->reason == ArchiveReason::kClear ||
                     newest->reason == ArchiveReason::kDelete;
  if (batch) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id, space_id, title, url, nav_state, reason, group_id, "
        "archived_at FROM archived_tabs WHERE reason = ? AND archived_at = ? "
        "ORDER BY rowid"));
    s.BindString(0, ArchiveReasonName(newest->reason));
    s.BindInt64(1, newest->archived_at);
    while (s.Step()) {
      if (std::optional<ArchiveRow> row = ReadArchiveRow(s)) {
        out.push_back(std::move(*row));
      }
    }
  } else if (newest->group_id) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT id, space_id, title, url, nav_state, reason, group_id, "
        "archived_at FROM archived_tabs WHERE group_id = ? AND reason IN "
        "('close', 'clear', 'delete', 'mini') ORDER BY rowid"));
    s.BindString(0, *newest->group_id);
    while (s.Step()) {
      if (std::optional<ArchiveRow> row = ReadArchiveRow(s)) {
        out.push_back(std::move(*row));
      }
    }
  }
  if (out.empty()) {
    out.push_back(std::move(*newest));
  }
  return out;
}

bool CluStore::ReplaceSpaces(std::vector<SpaceRow> spaces, int64_t now) {
  if (!open_) {
    return false;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }
  bool ok = db_.Execute("DELETE FROM spaces");
  for (size_t i = 0; ok && i < spaces.size(); ++i) {
    const SpaceRow& space = spaces[i];
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT OR REPLACE INTO spaces (id, name, icon_kind, icon_value, "
        "color, position, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, "
        "?)"));
    s.BindString(0, space.id);
    s.BindString(1, space.name);
    s.BindString(2, space.icon_kind);
    if (space.icon_kind == "emoji" || space.icon_kind == "symbol") {
      s.BindString(3, space.icon_value);
    } else {
      s.BindNull(3);
    }
    s.BindString(4, space.color);
    s.BindInt(5, static_cast<int>(i));
    s.BindInt64(6, now);
    s.BindInt64(7, now);
    ok = s.Run();
  }
  if (!ok) {
    transaction.Rollback();
    LOG(ERROR) << "clu: space mirror write rolled back";
    return false;
  }
  return transaction.Commit();
}

bool CluStore::SetSetting(const std::string& key,
                          const std::string& value_json,
                          int64_t now) {
  if (!open_) {
    return false;
  }
  sql::Statement s(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO settings (key, value_json, updated_at) VALUES "
      "(?, ?, ?)"));
  s.BindString(0, key);
  s.BindString(1, value_json);
  s.BindInt64(2, now);
  return s.Run();
}

}  // namespace clu
