// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_plan.h"

#include <array>
#include <set>
#include <string_view>

#include "clu/browser/model/clu_tab_tree.h"

namespace clu {

namespace {

constexpr std::array<std::string_view, 12> kColors = {
    "slate", "red",  "orange", "amber",  "lime",   "green",
    "teal",  "cyan", "blue",   "indigo", "violet", "pink"};

// The id a previous run made for `key`, when that row still exists with the same kind.
const TabNode* Alive(const std::map<std::string, std::string>& import_map,
                     const CluTabTree& tree,
                     const std::string& key,
                     bool folder) {
  const auto it = import_map.find(key);
  if (it == import_map.end()) {
    return nullptr;
  }
  const TabNode* node = tree.Get(it->second);
  return node && node->is_folder() == folder ? node : nullptr;
}

TabNode NewRow(const ImportNode& n, const std::string& id, int64_t now) {
  TabNode row;
  row.id = id;
  row.kind = n.folder ? NodeKind::kFolder : NodeKind::kTab;
  if (n.folder) {
    row.title_override = n.title;
    row.collapsed = n.collapsed;
  } else {
    row.url = n.url;
    row.pinned_url = n.url;
    row.title = n.title;
    row.title_override = n.title_override;
  }
  row.created_at = now;
  row.last_active_at = now;
  return row;
}

}  // namespace

ImportRows::ImportRows() = default;
ImportRows::ImportRows(ImportRows&&) = default;
ImportRows& ImportRows::operator=(ImportRows&&) = default;
ImportRows::~ImportRows() = default;

ImportRows PlanPinnedRows(const std::vector<ImportNode>& nodes,
                          const std::string& space_id,
                          const std::map<std::string, std::string>& import_map,
                          const CluTabTree& tree,
                          const NewIdCallback& new_id,
                          int64_t now) {
  ImportRows out;
  // Where each source key's children go: the folder row (made now or kept), its space and its level.
  struct Place {
    std::string folder_id;
    std::string space_id;
    int depth = 0;
  };
  std::map<std::string, Place> places;
  // Positions of new rows per list, after the rows already there.
  std::map<std::string, int> next_position;
  auto position_in = [&](const std::string& list_key) {
    return next_position[list_key]++;
  };

  for (const ImportNode& n : nodes) {
    Place parent{std::string(), space_id, 0};
    if (!n.parent_key.empty()) {
      const auto it = places.find(n.parent_key);
      // A parent that was skipped (a junk row) takes its subtree along.
      if (it == places.end()) {
        continue;
      }
      parent = it->second;
    }
    if (const TabNode* kept = Alive(import_map, tree, n.key, n.folder)) {
      if (n.folder) {
        places[n.key] = {kept->id, kept->space_id, tree.Depth(kept->id)};
      }
      continue;
    }
    if (n.folder && parent.depth + 1 > kMaxFolderDepth) {
      // Too deep: its children go into the parent, in order.
      places[n.key] = parent;
      continue;
    }
    const std::string id = new_id.Run();
    TabNode row = NewRow(n, id, now);
    row.section = Section::kPinned;
    row.space_id = parent.space_id;
    row.parent_id = parent.folder_id;
    row.position = position_in(parent.space_id + "/" + parent.folder_id);
    if (n.folder) {
      places[n.key] = {id, parent.space_id, parent.depth + 1};
    } else {
      ++out.tabs;
    }
    out.mapped.emplace_back(n.key, id);
    out.rows.push_back(std::move(row));
  }
  return out;
}

ImportRows PlanFavoriteRows(const std::vector<ImportNode>& nodes,
                            const std::map<std::string, std::string>& import_map,
                            const CluTabTree& tree,
                            const NewIdCallback& new_id,
                            int64_t now) {
  ImportRows out;
  std::set<std::string> urls;
  for (const std::string& id : tree.Favorites()) {
    if (const TabNode* f = tree.Get(id)) {
      urls.insert(f->pinned_url.value_or(f->url));
      urls.insert(f->url);
    }
  }
  size_t count = tree.Favorites().size();
  for (const ImportNode& n : nodes) {
    if (n.folder || Alive(import_map, tree, n.key, /*folder=*/false)) {
      continue;
    }
    if (!urls.insert(n.url).second) {
      ++out.duplicates;
      continue;
    }
    if (count >= static_cast<size_t>(kMaxFavorites)) {
      ++out.over_cap;
      continue;
    }
    const std::string id = new_id.Run();
    TabNode row = NewRow(n, id, now);
    row.section = Section::kFavorite;
    row.position = static_cast<int>(count++);
    ++out.tabs;
    out.mapped.emplace_back(n.key, id);
    out.rows.push_back(std::move(row));
  }
  return out;
}

ImportRows PlanTodayRows(const std::vector<ImportNode>& nodes,
                         const std::string& space_id,
                         const std::map<std::string, std::string>& import_map,
                         const CluTabTree& tree,
                         const NewIdCallback& new_id,
                         int64_t now) {
  ImportRows out;
  int position = 0;
  for (const ImportNode& n : nodes) {
    if (n.folder || Alive(import_map, tree, n.key, /*folder=*/false)) {
      continue;
    }
    const std::string id = new_id.Run();
    TabNode row = NewRow(n, id, now);
    // A Today row has no pinned URL (F2).
    row.pinned_url.reset();
    row.section = Section::kToday;
    row.space_id = space_id;
    row.position = position++;
    ++out.tabs;
    out.mapped.emplace_back(n.key, id);
    out.rows.push_back(std::move(row));
  }
  return out;
}

std::string_view ImportSpaceColor(size_t space_count) {
  return kColors[space_count % kColors.size()];
}

}  // namespace clu
