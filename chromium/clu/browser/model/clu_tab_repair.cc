// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tab_repair.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/strcat.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "clu/browser/ui/webui/bridge/validation.h"

namespace clu {

namespace {

constexpr double kNoPosition = std::numeric_limits<double>::max();

std::optional<int64_t> Whole(const std::optional<double>& value) {
  if (!value || !std::isfinite(*value)) {
    return std::nullopt;
  }
  return static_cast<int64_t>(*value);
}

std::string ListKey(const TabNode& n) {
  return ContainerKeyOf(n);
}

// Folder level per folder id (a root folder is 1). Assumes no cycles.
std::map<std::string, int> FolderDepths(
    const std::vector<TabNode>& nodes,
    const std::map<std::string, size_t>& by_id) {
  std::map<std::string, int> depth;
  for (const TabNode& start : nodes) {
    if (!start.is_folder() || depth.contains(start.id)) {
      continue;
    }
    std::vector<const TabNode*> chain;
    const TabNode* current = &start;
    int base = 0;
    while (current && current->is_folder()) {
      if (const auto known = depth.find(current->id); known != depth.end()) {
        base = known->second;
        break;
      }
      chain.push_back(current);
      if (chain.size() > nodes.size()) {
        break;
      }
      const auto parent = by_id.find(current->parent_id);
      current = current->parent_id.empty() || parent == by_id.end()
                    ? nullptr
                    : &nodes[parent->second];
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      depth[(*it)->id] = ++base;
    }
  }
  return depth;
}

}  // namespace

RawTabRow::RawTabRow() = default;
RawTabRow::RawTabRow(const RawTabRow&) = default;
RawTabRow& RawTabRow::operator=(const RawTabRow&) = default;
RawTabRow::~RawTabRow() = default;

RepairResult::RepairResult() = default;
RepairResult::RepairResult(const RepairResult&) = default;
RepairResult& RepairResult::operator=(const RepairResult&) = default;
RepairResult::RepairResult(RepairResult&&) = default;
RepairResult& RepairResult::operator=(RepairResult&&) = default;
RepairResult::~RepairResult() = default;

RepairResult RepairRows(const std::vector<RawTabRow>& rows,
                        const std::vector<std::string>& spaces,
                        int64_t now) {
  RepairResult result;
  auto note = [&](const char* kind) { ++result.kinds[kind]; };
  const std::set<std::string> space_ids(spaces.begin(), spaces.end());
  const std::string first_space = spaces.empty() ? std::string() : spaces[0];

  std::vector<TabNode> nodes;
  std::map<std::string, double> raw_position;
  std::map<std::string, int64_t> rowid_of;
  std::set<std::string> relocated;
  std::map<std::string, std::vector<double>> raw_groups;
  std::set<std::string> seen_ids;

  for (const RawTabRow& raw : rows) {
    if (!raw.id || !IsUuidV4(*raw.id) || seen_ids.contains(*raw.id)) {
      result.deleted.push_back(raw.rowid);
      note("bad-id");
      continue;
    }
    const std::optional<Section> section =
        raw.section ? SectionFromName(*raw.section) : std::nullopt;
    const std::optional<NodeKind> kind =
        raw.kind ? KindFromName(*raw.kind) : std::nullopt;
    if (!section || !kind) {
      result.deleted.push_back(raw.rowid);
      note("bad-section-or-kind");
      continue;
    }
    seen_ids.insert(*raw.id);
    TabNode n;
    n.id = *raw.id;
    n.space_id = raw.space_id.value_or(std::string());
    n.section = *section;
    n.kind = *kind;
    n.parent_id = raw.parent_id.value_or(std::string());
    n.url = raw.url.value_or(std::string());
    n.pinned_url = raw.pinned_url;
    n.title = raw.title.value_or(std::string());
    n.title_override = raw.title_override;
    n.muted = raw.muted && *raw.muted == 1;
    n.collapsed = raw.collapsed && *raw.collapsed == 1;
    n.split_id = raw.split_id;
    n.nav_state = raw.nav_state;
    n.last_active_at = Whole(raw.last_active_at).value_or(0);
    n.created_at = Whole(raw.created_at).value_or(0);
    n.custom_icon = raw.custom_icon;
    const double position =
        raw.position && std::isfinite(*raw.position) ? *raw.position
                                                     : kNoPosition;
    raw_position[n.id] = position;
    rowid_of[n.id] = raw.rowid;
    raw_groups[base::StrCat({SectionName(n.section), "|",
                             n.section == Section::kFavorite ? "" : n.space_id,
                             "|", n.parent_id})]
        .push_back(raw.position && std::isfinite(*raw.position)
                       ? *raw.position
                       : std::numeric_limits<double>::quiet_NaN());

    // Plain field junk: wrong types, oversize strings, clocks in the future, nav state where none belongs.
    bool field_junk = false;
    if (!raw.title) {
      field_junk = true;
    }
    const std::string title = CleanTitle(n.title);
    if (title != n.title) {
      field_junk = true;
      n.title = title;
    }
    if (n.title_override) {
      const std::string name = CleanName(*n.title_override);
      if (name != *n.title_override) {
        field_junk = true;
        n.title_override = name;
      }
      if (n.title_override->empty()) {
        n.title_override.reset();
      }
    }
    if (n.split_id && !IsUuidV4(*n.split_id)) {
      n.split_id.reset();
      field_junk = true;
    }
    if (!Whole(raw.last_active_at) || n.last_active_at < 0 ||
        n.last_active_at > now) {
      n.last_active_at = std::clamp<int64_t>(n.last_active_at, 0, now);
      field_junk = true;
    }
    if (n.nav_state &&
        (n.section != Section::kToday || n.is_folder() ||
         n.nav_state->size() > kMaxJsonTextBytes)) {
      n.nav_state.reset();
      field_junk = true;
    }
    if (field_junk) {
      note("fields");
    }

    // Space.
    if (n.section == Section::kFavorite) {
      if (!n.space_id.empty()) {
        n.space_id.clear();
        note("favorite-space");
      }
    } else if (!space_ids.contains(n.space_id)) {
      note(n.space_id.empty() ? "missing-space" : "unknown-space");
      if (first_space.empty()) {
        result.deleted.push_back(raw.rowid);
        continue;
      }
      n.space_id = first_space;
      n.parent_id.clear();
      relocated.insert(n.id);
    }

    // Folders live only in pinned.
    if (n.is_folder() && n.section != Section::kPinned) {
      note("folder-outside-pinned");
      if (n.space_id.empty()) {
        n.space_id = first_space;
      }
      if (n.space_id.empty()) {
        result.deleted.push_back(raw.rowid);
        continue;
      }
      n.section = Section::kPinned;
      n.parent_id.clear();
      n.pinned_url.reset();
      relocated.insert(n.id);
    }

    if (n.is_tab()) {
      if (!IsRestorableTabUrl(n.url)) {
        note("invalid-url");
        n.url = "about:blank";
      }
      if (n.section != Section::kToday) {
        if (!n.pinned_url || !IsRestorableTabUrl(*n.pinned_url)) {
          note(n.pinned_url ? "invalid-url" : "pinned-url");
          n.pinned_url = n.url;
        }
      } else if (n.pinned_url) {
        n.pinned_url.reset();
        note("pinned-url");
      }
    } else {
      n.url.clear();
      n.pinned_url.reset();
      n.title.clear();
    }

    if (n.custom_icon && !IsOneEmoji(*n.custom_icon)) {
      n.custom_icon.reset();
      note("custom-icon");
    }
    nodes.push_back(std::move(n));
  }

  std::map<std::string, size_t> by_id;
  for (size_t i = 0; i < nodes.size(); ++i) {
    by_id[nodes[i].id] = i;
  }
  auto find = [&](const std::string& id) -> TabNode* {
    const auto it = by_id.find(id);
    return it == by_id.end() ? nullptr : &nodes[it->second];
  };

  // Parents: a folder of the same space in pinned; anything else goes to the root.
  for (TabNode& n : nodes) {
    if (n.parent_id.empty()) {
      continue;
    }
    const TabNode* p = n.parent_id == n.id ? nullptr : find(n.parent_id);
    if (n.section == Section::kPinned && p && p->is_folder() &&
        p->section == Section::kPinned && p->space_id == n.space_id) {
      continue;
    }
    n.parent_id.clear();
    relocated.insert(n.id);
    note("orphan-parent");
  }

  // Cycles: broken at the row that closed the loop, the newest member.
  std::map<std::string, int> visit;
  for (const TabNode& start : nodes) {
    std::vector<TabNode*> path;
    TabNode* current = find(start.id);
    while (current && !current->parent_id.empty() &&
           visit[current->id] != 2) {
      if (visit[current->id] == 1) {
        const auto loop_start = std::ranges::find(path, current);
        TabNode* closer = *loop_start;
        for (auto it = loop_start; it != path.end(); ++it) {
          TabNode* b = *it;
          if (b->created_at > closer->created_at ||
              (b->created_at == closer->created_at && b->id > closer->id)) {
            closer = b;
          }
        }
        closer->parent_id.clear();
        relocated.insert(closer->id);
        note("cycle");
        break;
      }
      visit[current->id] = 1;
      path.push_back(current);
      current = find(current->parent_id);
    }
    for (TabNode* p : path) {
      visit[p->id] = 2;
    }
  }

  // Depth: the shallowest folder past level 8 moves to the root, until none is left.
  for (;;) {
    const std::map<std::string, int> depth = FolderDepths(nodes, by_id);
    TabNode* worst = nullptr;
    int worst_depth = 0;
    for (TabNode& n : nodes) {
      const auto d = depth.find(n.id);
      if (n.is_folder() && d != depth.end() && d->second > kMaxFolderDepth &&
          (!worst || d->second < worst_depth)) {
        worst = &n;
        worst_depth = d->second;
      }
    }
    if (!worst) {
      break;
    }
    worst->parent_id.clear();
    relocated.insert(worst->id);
    note("too-deep");
  }

  auto stored_order = [&](const TabNode* a, const TabNode* b) {
    const double pa = raw_position[a->id];
    const double pb = raw_position[b->id];
    if (pa != pb) {
      return pa < pb;
    }
    if (a->created_at != b->created_at) {
      return a->created_at < b->created_at;
    }
    return a->id < b->id;
  };

  // Favorites cap: the extras move to the end of the first space's pinned root.
  std::vector<TabNode*> favorites;
  for (TabNode& n : nodes) {
    if (n.section == Section::kFavorite) {
      favorites.push_back(&n);
    }
  }
  std::set<std::string> dropped;
  if (favorites.size() > static_cast<size_t>(kMaxFavorites)) {
    std::ranges::sort(favorites, stored_order);
    for (size_t i = kMaxFavorites; i < favorites.size(); ++i) {
      note("favorites-over-cap");
      TabNode* n = favorites[i];
      if (first_space.empty()) {
        dropped.insert(n->id);
        continue;
      }
      n->section = Section::kPinned;
      n->space_id = first_space;
      n->parent_id.clear();
      relocated.insert(n->id);
    }
  }

  // Positions: any group whose stored positions are not exactly 0..n-1 is junk; every list is renumbered.
  for (auto& [group, positions] : raw_groups) {
    std::vector<double> sorted = positions;
    std::ranges::sort(sorted);
    for (size_t i = 0; i < sorted.size(); ++i) {
      if (!(sorted[i] == static_cast<double>(i))) {
        note("positions");
        break;
      }
    }
  }
  std::map<std::string, std::vector<TabNode*>> lists;
  for (TabNode& n : nodes) {
    if (!dropped.contains(n.id)) {
      lists[ListKey(n)].push_back(&n);
    }
  }
  for (auto& [key, list] : lists) {
    std::vector<TabNode*> stay;
    std::vector<TabNode*> moved;
    for (TabNode* n : list) {
      (relocated.contains(n->id) ? moved : stay).push_back(n);
    }
    std::ranges::sort(stay, stored_order);
    int position = 0;
    for (TabNode* n : stay) {
      n->position = position++;
    }
    for (TabNode* n : moved) {
      n->position = position++;
    }
  }
  for (const std::string& id : dropped) {
    result.deleted.push_back(rowid_of[id]);
  }
  for (TabNode& n : nodes) {
    if (!dropped.contains(n.id)) {
      result.nodes.push_back(std::move(n));
    }
  }
  std::erase_if(result.deleted, [](int64_t rowid) { return rowid < 0; });
  return result;
}

std::vector<std::string> CheckTree(const std::vector<TabNode>& nodes,
                                   const std::vector<std::string>& spaces) {
  std::vector<std::string> out;
  const std::set<std::string> space_ids(spaces.begin(), spaces.end());
  std::map<std::string, size_t> by_id;
  for (size_t i = 0; i < nodes.size(); ++i) {
    by_id[nodes[i].id] = i;
  }
  std::map<std::string, std::vector<int>> lists;
  int favorites = 0;
  for (const TabNode& n : nodes) {
    if (n.is_folder() && n.section != Section::kPinned) {
      out.push_back(n.id + ": folder outside pinned");
    }
    if (n.section == Section::kFavorite) {
      if (!n.space_id.empty()) {
        out.push_back(n.id + ": favorite with a space");
      }
      ++favorites;
    } else if (!space_ids.contains(n.space_id)) {
      out.push_back(n.id + ": no space");
    }
    if (!n.parent_id.empty()) {
      const auto p = by_id.find(n.parent_id);
      const TabNode* parent = p == by_id.end() ? nullptr : &nodes[p->second];
      if (!parent || !parent->is_folder() ||
          parent->section != Section::kPinned ||
          n.section != Section::kPinned || parent->space_id != n.space_id) {
        out.push_back(n.id + ": bad parent");
      }
    }
    if (n.is_tab()) {
      if (!IsRestorableTabUrl(n.url)) {
        out.push_back(n.id + ": bad url");
      }
      if ((n.section == Section::kToday) != !n.pinned_url.has_value()) {
        out.push_back(n.id + ": pinned url does not match section");
      }
    }
    if (n.custom_icon && !IsOneEmoji(*n.custom_icon)) {
      out.push_back(n.id + ": bad custom icon");
    }
    lists[ContainerKeyOf(n)].push_back(n.position);
  }
  if (favorites > kMaxFavorites) {
    out.push_back(base::StrCat({base::NumberToString(favorites), " favorites"}));
  }
  for (auto& [key, positions] : lists) {
    std::ranges::sort(positions);
    for (size_t i = 0; i < positions.size(); ++i) {
      if (positions[i] != static_cast<int>(i)) {
        out.push_back(key + ": positions not dense");
        break;
      }
    }
  }
  bool cycle = false;
  for (const TabNode& n : nodes) {
    std::set<std::string> seen;
    const TabNode* current = &n;
    while (current && !current->parent_id.empty()) {
      if (!seen.insert(current->id).second) {
        out.push_back(n.id + ": cycle");
        cycle = true;
        break;
      }
      const auto p = by_id.find(current->parent_id);
      current = p == by_id.end() ? nullptr : &nodes[p->second];
    }
  }
  if (!cycle) {
    for (const auto& [id, depth] : FolderDepths(nodes, by_id)) {
      if (depth > kMaxFolderDepth) {
        out.push_back(base::StrCat(
            {id, ": depth ", base::NumberToString(depth)}));
      }
    }
  }
  return out;
}

}  // namespace clu
