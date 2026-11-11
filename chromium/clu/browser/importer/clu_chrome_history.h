// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_CHROME_HISTORY_H_
#define CLU_BROWSER_IMPORTER_CLU_CHROME_HISTORY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

// The Chrome History reader (docs/plan/07-data.md and R8). Chrome keeps History locked while it runs, so the file and
// its journal or WAL are copied into a fresh directory under `tmp_root` (made if missing) and only the copy is
// opened; the copy is removed before it returns. A running Chrome commits history every few seconds: a copy during
// which the files changed is taken again (three tries), and a copy that fails SQLite's quick_check is an unreadable
// file.
// Rows: non-hidden http and https URLs last visited in the 90 days before `now`, newest 50,000, with Chrome's
// 1601-epoch microseconds converted to Unix milliseconds. Blocking: runs on a MayBlock sequence (and in unit tests).
namespace clu {

inline constexpr char kHistoryUnreadable[] =
    "Chrome’s History file couldn’t be read.";
// Milliseconds between 1601-01-01 and 1970-01-01.
inline constexpr int64_t kChromeEpochDeltaMs = 11'644'473'600'000;
inline constexpr int64_t kImportHistoryDays = 90;

struct ImportedVisit {
  std::string url;
  std::string title;
  int visit_count = 1;
  int typed_count = 0;
  // Epoch ms.
  int64_t last_visit_at = 0;

  bool operator==(const ImportedVisit&) const = default;
};

struct HistoryReadResult {
  HistoryReadResult();
  HistoryReadResult(const HistoryReadResult&) = delete;
  HistoryReadResult& operator=(const HistoryReadResult&) = delete;
  HistoryReadResult(HistoryReadResult&&);
  HistoryReadResult& operator=(HistoryReadResult&&);
  ~HistoryReadResult();

  std::optional<std::string> error;
  // Newest first.
  std::vector<ImportedVisit> rows;
  int skipped = 0;
};

// Unix ms to Chrome's time (microseconds since 1601).
int64_t ToChromeTime(int64_t ms);

HistoryReadResult ReadChromeHistory(const base::FilePath& history_path,
                                    int64_t now_ms,
                                    const base::FilePath& tmp_root);

// Runs after each copy of the History file, before its side files: a test changes the source there.
void SetHistoryCopiedCallbackForTesting(base::RepeatingClosure callback);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_CHROME_HISTORY_H_
