// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MULTITASK_CLU_SPLIT_RULES_H_
#define CLU_BROWSER_MULTITASK_CLU_SPLIT_RULES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/values.h"

// Split view rules (docs/plan/05-multitask.md "Split view",
// src/shared/split.ts), pure so clu_unittests covers them without a browser. A
// split is 2 to 4 tab rows of one space, side by side or stacked. Two panes
// show in Chromium's native split with the first pane's share of the room;
// three and four show at even shares, the active page in MultiContentsView and
// the others in views the shell hosts (CluShell::SetExtraPanes).
namespace clu {

// MAX_PANES in src/shared/split.ts.
inline constexpr size_t kMaxSplitPanes = 4;
// MIN_PANE_WIDTH and MIN_PANE_HEIGHT in src/shared/split.ts.
inline constexpr int kMinPaneWidth = 220;
inline constexpr int kMinPaneHeight = 160;
// SIZE_TOLERANCE in src/shared/split.ts.
inline constexpr double kSplitSizeTolerance = 0.001;
// A stored layout larger than this is junk (parseSplitLayout's 64 KiB cap).
inline constexpr size_t kMaxSplitLayoutBytes = 64 * 1024;
// The stored share of the first pane stays inside this margin, so a pane never
// collapses to nothing.
inline constexpr double kMinSplitRatio = 0.05;
// SPLIT_FULL_TOAST in src/shared/split.ts.
inline constexpr char kSplitFullToast[] = "A split holds up to 4 tabs";

enum class SplitDir { kRow, kCol };
enum class SplitEdge { kLeft, kRight, kTop, kBottom };

std::string_view SplitDirName(SplitDir dir);

// One split of a profile (tab_splits): its panes in tree order, left to right
// or top to bottom.
struct SplitRow {
  SplitRow();
  SplitRow(const SplitRow&);
  SplitRow& operator=(const SplitRow&);
  SplitRow(SplitRow&&);
  SplitRow& operator=(SplitRow&&);
  ~SplitRow();

  std::string id;
  std::string space_id;
  SplitDir dir = SplitDir::kRow;
  // The first pane's share of the room, in (0, 1).
  double ratio = 0.5;
  // Each pane's share in a split of 3 or 4, in pane order; empty is even.
  std::vector<double> sizes;
  std::vector<std::string> tab_ids;
  std::string focused_tab_id;
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

// A tab_splits row as read, before repair.
struct RawSplitRow {
  RawSplitRow();
  RawSplitRow(const RawSplitRow&);
  RawSplitRow& operator=(const RawSplitRow&);
  ~RawSplitRow();

  std::optional<std::string> id;
  std::optional<std::string> space_id;
  std::optional<std::string> layout;
  std::optional<std::string> focused_tab_id;
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

// What a stored layout says: its panes in tree order, the root's direction, and
// the first pane's share when the root is a group of exactly two panes.
struct ParsedSplitLayout {
  ParsedSplitLayout();
  ParsedSplitLayout(const ParsedSplitLayout&);
  ParsedSplitLayout& operator=(const ParsedSplitLayout&);
  ~ParsedSplitLayout();

  std::vector<std::string> tab_ids;
  SplitDir dir = SplitDir::kRow;
  std::optional<double> ratio;
  // The root's sizes when it is a flat group of 3 or 4 panes.
  std::vector<double> sizes;
};

// splitLayoutSchema in src/shared/split.ts: a group root of 2 to 4 unique panes
// (UUID v4 tab ids), groups of 2 to 4 children with one finite size in (0, 1)
// per child summing to 1 within 0.001, at most 2 levels, no unknown keys. Junk
// of any shape returns nullopt.
std::optional<ParsedSplitLayout> ParseSplitLayout(std::string_view json);

// The SplitNode root a split shows as (state.splits[id].root) and stores as
// (tab_splits.layout).
base::DictValue SplitRootValue(const SplitRow& split);
std::string SplitLayoutJson(const SplitRow& split);

// A tab row as the split repair sees it.
struct SplitMember {
  std::string id;
  std::string space_id;
  // Favorites have no space and never join a split.
  bool favorite = false;
  std::optional<std::string> split_id;
};

struct SplitRepairResult {
  SplitRepairResult();
  SplitRepairResult(SplitRepairResult&&);
  SplitRepairResult& operator=(SplitRepairResult&&);
  ~SplitRepairResult();

  std::vector<SplitRow> splits;
  // Tab rows whose split_id changes: the split they now belong to, or nullopt
  // to clear it.
  std::vector<std::pair<std::string, std::optional<std::string>>> member_fixes;
  // tab_splits rows to delete (dissolved or junk).
  std::vector<std::string> dropped;
  // Stored rows whose layout was rebuilt or trimmed: written back.
  std::vector<std::string> rewritten;
};

// Load-time repair (05 "Loading saved splits"). `members` are the profile's tab
// rows in sidebar order.
// - A row without a UUID v4 id is dropped; a second row with the same id is
// skipped.
// - An invalid layout is rebuilt as a flat row from the tabs carrying that
// split id, in sidebar order.
// - Panes that are not tab rows of the split's space, favorites, or tabs
// another split already took leave it; more
//   than kMaxSplitPanes keep the first ones.
// - Fewer than 2 panes dissolve the split: its tabs become normal tabs.
// - A tab whose split_id names no surviving split is cleared, one that a split
// holds gets its id.
// - The focused tab falls back to the first pane, the ratio to 0.5 and then
// into the margin.
SplitRepairResult RepairSplits(const std::vector<RawSplitRow>& rows,
                               const std::vector<SplitMember>& members);

// Where a tab dropped on an edge of `target` lands: the direction, and whether
// it becomes the first pane.
struct DropPlacement {
  SplitDir dir = SplitDir::kRow;
  bool dropped_first = false;
};
DropPlacement PlaceDrop(SplitEdge edge);

// The first pane's share after a resize to `ratio` over `avail` DIP: each pane
// keeps at least `min_pane` while the room allows; a room smaller than two
// minimum panes splits evenly (05 "computeBounds": sizes scale and the minimum
// is ignored). Junk ratios keep `current`.
double ClampSplitRatio(double ratio, int avail, int min_pane, double current);

// The cross-field rules zod refines and mojom cannot carry
// (src/shared/ipc/split.ts).
bool UniqueIds(base::span<const std::string> ids);
bool SizesSumToOne(base::span<const double> sizes);

// The shares a split of `count` panes shows: `sizes` when it has one finite
// share in (0, 1) per pane summing to 1, else even.
std::vector<double> PaneShares(base::span<const double> sizes, size_t count);

// View ▸ Expand Current Split: pane `index` of `count` takes the room the
// others leave at one DIP over `min_pane` each. Empty when `index` is out of
// range or the room gives the pane nothing more than the others.
std::vector<double> ExpandedShares(size_t count,
                                   size_t index,
                                   int room,
                                   int min_pane);

// split.focusPane: the pane `index` (1-based) names, or the pane `step` away
// from `current` with wrap-around; nullopt when there is none.
std::optional<std::string> PaneAt(base::span<const std::string> panes,
                                  int index);
std::optional<std::string> StepPane(base::span<const std::string> panes,
                                    std::string_view current,
                                    int step);

}  // namespace clu

#endif  // CLU_BROWSER_MULTITASK_CLU_SPLIT_RULES_H_
