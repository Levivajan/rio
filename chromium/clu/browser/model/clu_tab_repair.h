// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_TAB_REPAIR_H_
#define CLU_BROWSER_MODEL_CLU_TAB_REPAIR_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "clu/browser/model/clu_tab_node.h"

// Load-time repair of tab_nodes rows (src/main/tabs/tabStore.repair.ts, docs/plan/02-tabs.md "Junk input"). Pure:
// rows in, a valid tree out. CluStore applies the result in one transaction and logs one line per repair kind. A
// corrupt row never stops startup.
namespace clu {

// One tab_nodes row as SQLite holds it. Any column may hold junk: a text column reads as nullopt unless it holds
// text, a number column unless it holds a number.
struct RawTabRow {
  RawTabRow();
  RawTabRow(const RawTabRow&);
  RawTabRow& operator=(const RawTabRow&);
  ~RawTabRow();

  int64_t rowid = -1;
  std::optional<std::string> id;
  std::optional<std::string> space_id;
  std::optional<std::string> section;
  std::optional<std::string> kind;
  std::optional<std::string> parent_id;
  std::optional<double> position;
  std::optional<std::string> url;
  std::optional<std::string> pinned_url;
  std::optional<std::string> title;
  std::optional<std::string> title_override;
  std::optional<double> muted;
  std::optional<double> collapsed;
  std::optional<std::string> split_id;
  std::optional<std::string> nav_state;
  std::optional<double> last_active_at;
  std::optional<double> created_at;
  std::optional<std::string> custom_icon;
};

struct RepairResult {
  RepairResult();
  RepairResult(const RepairResult&);
  RepairResult& operator=(const RepairResult&);
  RepairResult(RepairResult&&);
  RepairResult& operator=(RepairResult&&);
  ~RepairResult();

  // Valid nodes, positions dense in every list.
  std::vector<TabNode> nodes;
  // rowids to delete: rows with no usable id, section or kind, or no space to live in.
  std::vector<int64_t> deleted;
  // Repair kind ("orphan-parent", "cycle", ...) to how many rows it touched.
  std::map<std::string, int> kinds;
};

// `spaces` in sidebar order; the first one takes rows whose space is gone. `now` caps clocks in the future.
RepairResult RepairRows(const std::vector<RawTabRow>& rows,
                        const std::vector<std::string>& spaces,
                        int64_t now);

// Every invariant of a valid tree (checkTree in tabStore.repair.ts); empty means valid. Used by tests.
std::vector<std::string> CheckTree(const std::vector<TabNode>& nodes,
                                   const std::vector<std::string>& spaces);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_TAB_REPAIR_H_
