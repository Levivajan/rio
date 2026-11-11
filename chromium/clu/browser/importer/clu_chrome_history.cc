// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_chrome_history.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace clu {

namespace {

constexpr int64_t kDayMs = 24 * 60 * 60 * 1000;
constexpr int kCopyAttempts = 3;
constexpr const char* kSideFiles[] = {"-journal", "-wal"};

base::RepeatingClosure& HistoryCopiedCallback() {
  static base::NoDestructor<base::RepeatingClosure> callback;
  return *callback;
}

// Size and modification time of the History file and its side files (absent ones as -1).
std::vector<std::pair<int64_t, base::Time>> Stamp(const base::FilePath& history) {
  std::vector<std::pair<int64_t, base::Time>> stamp;
  for (const char* ext : {"", kSideFiles[0], kSideFiles[1]}) {
    base::File::Info info;
    if (base::GetFileInfo(base::FilePath(history.value() + ext), &info)) {
      stamp.emplace_back(info.size, info.last_modified);
    } else {
      stamp.emplace_back(-1, base::Time());
    }
  }
  return stamp;
}

// Copies History and its side files to `copy`; false when a copy failed or the source changed on every try.
bool CopyHistory(const base::FilePath& history, const base::FilePath& copy) {
  for (int attempt = 0; attempt < kCopyAttempts; ++attempt) {
    const auto before = Stamp(history);
    bool copied = base::CopyFile(history, copy);
    if (HistoryCopiedCallback()) {
      HistoryCopiedCallback().Run();
    }
    for (const char* ext : kSideFiles) {
      const base::FilePath side(history.value() + ext);
      const base::FilePath side_copy(copy.value() + ext);
      base::DeleteFile(side_copy);
      if (copied && base::PathExists(side)) {
        copied = base::CopyFile(side, side_copy);
      }
    }
    if (!copied) {
      return false;
    }
    if (Stamp(history) == before) {
      return true;
    }
  }
  return false;
}

int Count(int64_t v) {
  return v > 0 ? static_cast<int>(std::min<int64_t>(v, 1'000'000)) : 0;
}

}  // namespace

HistoryReadResult::HistoryReadResult() = default;
HistoryReadResult::HistoryReadResult(HistoryReadResult&&) = default;
HistoryReadResult& HistoryReadResult::operator=(HistoryReadResult&&) = default;
HistoryReadResult::~HistoryReadResult() = default;

int64_t ToChromeTime(int64_t ms) {
  return (ms + kChromeEpochDeltaMs) * 1000;
}

HistoryReadResult ReadChromeHistory(const base::FilePath& history_path,
                                    int64_t now_ms,
                                    const base::FilePath& tmp_root) {
  HistoryReadResult result;
  base::ScopedTempDir copy_dir;
  if (!base::PathExists(history_path) || !base::CreateDirectory(tmp_root) ||
      !copy_dir.CreateUniqueTempDirUnderPath(tmp_root)) {
    result.error = kHistoryUnreadable;
    return result;
  }
  const base::FilePath copy = copy_dir.GetPath().AppendASCII("History");
  if (!CopyHistory(history_path, copy)) {
    result.error = kHistoryUnreadable;
    return result;
  }
  {
    // The tag is Clu's database tag (tools/metrics sql histograms). The copy is opened writable, so SQLite can roll
    // back a hot journal it came with; the original is never opened.
    sql::Database db(sql::Database::Tag("CluModel"));
    db.set_error_callback(base::BindRepeating([](int error, sql::Statement*) {
      LOG(WARNING) << "clu: import read Chrome History, sqlite error " << error;
    }));
    if (!db.Open(copy)) {
      result.error = kHistoryUnreadable;
      return result;
    }
    // Pages from two different commits read as a damaged file, or as rows that don't belong together.
    sql::Statement check(db.GetUniqueStatement("PRAGMA quick_check"));
    if (!check.is_valid() || !check.Step() || check.ColumnString(0) != "ok") {
      result.error = kHistoryUnreadable;
      return result;
    }
    sql::Statement s(db.GetUniqueStatement(
        "SELECT url, title, visit_count, typed_count, last_visit_time FROM urls "
        "WHERE hidden = 0 AND last_visit_time >= ? AND last_visit_time <= ? "
        "AND (url LIKE 'http://%' OR url LIKE 'https://%') "
        "ORDER BY last_visit_time DESC LIMIT ?"));
    // A SQLite file without Chrome's tables fails the prepare; stepping it would be fatal in debug builds.
    if (!s.is_valid()) {
      result.error = kHistoryUnreadable;
      return result;
    }
    s.BindInt64(0, ToChromeTime(now_ms - kImportHistoryDays * kDayMs));
    s.BindInt64(1, ToChromeTime(now_ms));
    s.BindInt64(2, static_cast<int64_t>(kMaxImportHistory));
    while (s.Step()) {
      const std::optional<std::string> url = ImportUrl(s.ColumnString(0));
      const int64_t at = s.ColumnInt64(4) / 1000 - kChromeEpochDeltaMs;
      if (!url || at < 0) {
        ++result.skipped;
        continue;
      }
      ImportedVisit row;
      row.url = *url;
      row.title = ImportTitle(s.ColumnString(1));
      row.visit_count = std::max(1, Count(s.ColumnInt64(2)));
      row.typed_count = Count(s.ColumnInt64(3));
      row.last_visit_at = at;
      result.rows.push_back(std::move(row));
    }
    if (!s.Succeeded()) {
      result.rows.clear();
      result.skipped = 0;
      result.error = kHistoryUnreadable;
    }
  }
  // The copy goes with copy_dir; the original was only read.
  return result;
}

void SetHistoryCopiedCallbackForTesting(base::RepeatingClosure callback) {
  HistoryCopiedCallback() = std::move(callback);
}

}  // namespace clu
