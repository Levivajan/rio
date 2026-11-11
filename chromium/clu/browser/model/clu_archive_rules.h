// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_ARCHIVE_RULES_H_
#define CLU_BROWSER_MODEL_CLU_ARCHIVE_RULES_H_

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "clu/browser/model/clu_tab_node.h"

// The archive module's rules (src/main/archive): the auto-archive selection, the closed stack behind Cmd+Shift+T and the
// per-window recent-tab list. Pure, on an injected `now`, so clu_unittests drives every threshold.
namespace clu {

enum class ArchiveReason { kClose, kClear, kAuto, kDelete, kMini };

std::string_view ArchiveReasonName(ArchiveReason reason);
std::optional<ArchiveReason> ArchiveReasonFromName(std::string_view name);

// "Archive tabs after" (tabs.archiveAfterHours, PLAN 7.3): 12, 24, 168 or 720 hours. Anything else reads as 12.
inline constexpr int kDefaultArchiveAfterHours = 12;
int ArchiveAfterHours(std::optional<double> stored);
bool IsArchiveAfterHours(double hours);
int64_t ArchiveAfterMs(int hours);

// Retention (PLAN decision 17): rows at least 90 days old, or beyond 5000 per profile, are pruned in the sweep.
inline constexpr int64_t kArchiveRetentionMs = int64_t{90} * 24 * 60 * 60 * 1000;
inline constexpr int kArchiveMaxRows = 5000;
// The sweep runs every 5 minutes, at launch, and when the setting changes (02-tabs "Timings").
inline constexpr int64_t kSweepIntervalMs = 5 * 60 * 1000;

// A Today tab as the sweep sees it.
struct SweepTab {
  SweepTab();
  SweepTab(const SweepTab&);
  SweepTab& operator=(const SweepTab&);
  ~SweepTab();

  std::string id;
  Section section = Section::kToday;
  int64_t last_active_at = 0;
  std::optional<std::string> split_id;
  // Active in a window, visible (a shown split pane, Peek, PiP), audible or captured: never archived now, rechecked
  // on the next sweep.
  bool held = false;
};

// sweep() in src/main/archive/sweeper.ts: Today tabs idle for at least `threshold_ms` archive, a split as one unit by
// its newest member. A negative idle time (a clock change) waits. A tab that was never active idles from the first
// sweep that saw it (`first_seen`, kept by the caller between sweeps), not from 1970.
std::vector<std::string> SelectIdleTabs(const std::vector<SweepTab>& tabs,
                                        int64_t now,
                                        int64_t threshold_ms,
                                        std::map<std::string, int64_t>& first_seen);

// One Cmd+Shift+T entry: archive rows of a Today close or a Clear batch, or an anchored close with its last URL and
// history (02-tabs F14).
struct ClosedEntry {
  ClosedEntry();
  ClosedEntry(const ClosedEntry&);
  ClosedEntry& operator=(const ClosedEntry&);
  ~ClosedEntry();

  enum class Kind { kRows, kAnchored };
  Kind kind = Kind::kRows;
  std::vector<std::string> archive_ids;
  std::string tab_id;
  std::string url;
  std::optional<std::string> nav_state;
  int seq = 0;
};

// The closed stack of one profile: in memory, at most 50 entries, newest last. Auto-archived rows never enter.
class ClosedStack {
 public:
  static constexpr size_t kMaxEntries = 50;

  ClosedStack();
  ClosedStack(const ClosedStack&) = delete;
  ClosedStack& operator=(const ClosedStack&) = delete;
  ~ClosedStack();

  // Rows with reason close, clear, delete or mini; any other reason is ignored.
  void PushRows(std::vector<std::string> archive_ids, ArchiveReason reason);
  void PushAnchored(std::string tab_id,
                    std::string url,
                    std::optional<std::string> nav_state);
  // Pops the newest entry.
  std::optional<ClosedEntry> Pop();
  bool empty() const { return entries_.empty(); }
  const std::deque<ClosedEntry>& entries() const { return entries_; }

 private:
  std::deque<ClosedEntry> entries_;
  int seq_ = 0;
};

// The recent tabs of one window, most recent first, across spaces (src/main/archive/mru.ts).
class TabMru {
 public:
  TabMru();
  TabMru(const TabMru&) = delete;
  TabMru& operator=(const TabMru&) = delete;
  ~TabMru();

  // Seeds the list from last_active_at once (never-active tabs are not recent; ties by id).
  void SeedIfEmpty(const std::vector<std::pair<std::string, int64_t>>& tabs);
  void Touch(std::string_view tab_id);
  void Remove(std::string_view tab_id);
  const std::vector<std::string>& list() const { return list_; }

 private:
  bool seeded_ = false;
  std::vector<std::string> list_;
};

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_ARCHIVE_RULES_H_
