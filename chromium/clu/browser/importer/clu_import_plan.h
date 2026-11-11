// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_IMPORT_PLAN_H_
#define CLU_BROWSER_IMPORTER_CLU_IMPORT_PLAN_H_

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "clu/browser/importer/clu_chrome_bookmarks.h"
#include "clu/browser/model/clu_tab_node.h"

namespace clu {

class CluTabTree;

// Imported trees to tab rows of one profile's model. The import map says which source keys a previous run made: a key
// whose row still exists with the same kind is skipped (re-import adds nothing) and stays the parent of its new
// children; a key whose row is gone is made again. Folders that would sit deeper than 8 levels (an imported folder
// moved down by the user, say) are flattened into their parent. Pure: the tree is only read.
struct ImportRows {
  ImportRows();
  ImportRows(const ImportRows&) = delete;
  ImportRows& operator=(const ImportRows&) = delete;
  ImportRows(ImportRows&&);
  ImportRows& operator=(ImportRows&&);
  ~ImportRows();

  // For CluModelService::AddImportedRows(), positions dense per list.
  std::vector<TabNode> rows;
  // {source key, row id} for the import map.
  std::vector<std::pair<std::string, std::string>> mapped;
  // New tab rows (folders not counted).
  int tabs = 0;
  // Favorites the profile already had (by URL) or that came twice.
  int duplicates = 0;
  // Favorites past the 12-favorite cap.
  int over_cap = 0;
};

using NewIdCallback = base::RepeatingCallback<std::string()>;

// Pinned rows: `nodes` in preorder land in `space_id`'s pinned tree, roots at the end of its pinned root.
ImportRows PlanPinnedRows(const std::vector<ImportNode>& nodes,
                          const std::string& space_id,
                          const std::map<std::string, std::string>& import_map,
                          const CluTabTree& tree,
                          const NewIdCallback& new_id,
                          int64_t now);

// Favorites: tabs only (folders in `nodes` are ignored), appended up to the cap, skipping URLs the profile's
// favorites already hold.
ImportRows PlanFavoriteRows(const std::vector<ImportNode>& nodes,
                            const std::map<std::string, std::string>& import_map,
                            const CluTabTree& tree,
                            const NewIdCallback& new_id,
                            int64_t now);

// Today rows: tabs only (folders in `nodes` are ignored), at the end of `space_id`'s Today list in order.
ImportRows PlanTodayRows(const std::vector<ImportNode>& nodes,
                         const std::string& space_id,
                         const std::map<std::string, std::string>& import_map,
                         const CluTabTree& tree,
                         const NewIdCallback& new_id,
                         int64_t now);

// The space colors a sidebar import gives new spaces, in turn (SPACE_COLORS order).
std::string_view ImportSpaceColor(size_t space_count);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_IMPORT_PLAN_H_
