// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_COMMAND_CLU_INDEX_SYNC_H_
#define CLU_BROWSER_COMMAND_CLU_INDEX_SYNC_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

class GURL;

// The command bar's history index sync (04-command "History index sync"). The browser keeps the profile's last read
// of its top history rows and a change log, so an overlay that already holds the index at `rev` gets only the rows
// changed since.
namespace clu {

// Rows one sync sends at most (the bar's budget is 10,000 history rows).
inline constexpr size_t kIndexLimit = 10000;
// An overlay further behind than this many changes gets the full index again.
inline constexpr size_t kMaxIndexDelta = 2000;
// The longest title a row keeps, in UTF-16 units.
inline constexpr size_t kMaxIndexTitle = 1024;

// IndexRow in src/shared/ipc/command.ts, kind 'history'. The URL is the key.
struct IndexRow {
  std::string url;
  std::string title;
  int visit_count = 0;
  int typed_count = 0;
  // Unix epoch ms.
  double last_visit_at = 0;

  bool operator==(const IndexRow&) const = default;
};

// A history row as the index takes it, or nullopt for junk: an invalid or empty URL, a scheme other than http and
// https, a URL longer than the bridge carries. The title is cut to kMaxIndexTitle units with control characters
// dropped; negative counts and times read as 0.
std::optional<IndexRow> CleanIndexRow(const GURL& url,
                                      std::u16string_view title,
                                      int visit_count,
                                      int typed_count,
                                      base::Time last_visit);

// Output<'command.syncIndex'>: { rev, full, upserts, deletes }.
struct IndexDelta {
  IndexDelta();
  IndexDelta(IndexDelta&&);
  IndexDelta& operator=(IndexDelta&&);
  ~IndexDelta();

  int64_t rev = 0;
  bool full = false;
  std::vector<IndexRow> upserts;
  std::vector<std::string> deletes;

  base::Value ToValue() const;
};

class CluIndexSync {
 public:
  CluIndexSync();
  CluIndexSync(const CluIndexSync&) = delete;
  CluIndexSync& operator=(const CluIndexSync&) = delete;
  ~CluIndexSync();

  // Takes a fresh read of the profile's rows (cleaned, most recent first, at most kIndexLimit; later duplicates of
  // a URL are dropped) and logs every row that was added, changed or went away since the last read.
  void Refresh(std::vector<IndexRow> rows);

  // What an overlay at `since_rev` is missing. 0, a rev from the future (another run) or one older than the log get
  // everything.
  IndexDelta Sync(int64_t since_rev) const;

  bool loaded() const { return loaded_; }
  int64_t rev() const { return rev_; }
  size_t size() const { return rows_.size(); }

 private:
  // One counter for every profile, so a rev is never reused.
  static int64_t NextRev();

  bool loaded_ = false;
  std::map<std::string, IndexRow, std::less<>> rows_;
  // Changed keys in rev order; anything at or below `floor_` was dropped from the log.
  std::vector<std::pair<int64_t, std::string>> changes_;
  int64_t floor_ = 0;
  int64_t rev_ = 0;
};

}  // namespace clu

#endif  // CLU_BROWSER_COMMAND_CLU_INDEX_SYNC_H_
