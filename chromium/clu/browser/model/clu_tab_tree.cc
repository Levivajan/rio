// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tab_tree.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/types/expected_macros.h"
#include "base/uuid.h"
#include "clu/browser/model/clu_tab_rules.h"

namespace clu {

namespace {

const std::vector<std::string>& EmptyList() {
  static const base::NoDestructor<std::vector<std::string>> empty;
  return *empty;
}

std::string NewId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

}  // namespace

// Structs ----------------------------------------------------------------------

std::string_view SectionName(Section section) {
  switch (section) {
    case Section::kFavorite:
      return "favorite";
    case Section::kPinned:
      return "pinned";
    case Section::kToday:
      return "today";
  }
}

std::optional<Section> SectionFromName(std::string_view name) {
  if (name == "favorite") {
    return Section::kFavorite;
  }
  if (name == "pinned") {
    return Section::kPinned;
  }
  if (name == "today") {
    return Section::kToday;
  }
  return std::nullopt;
}

std::string_view KindName(NodeKind kind) {
  return kind == NodeKind::kFolder ? "folder" : "tab";
}

std::optional<NodeKind> KindFromName(std::string_view name) {
  if (name == "tab") {
    return NodeKind::kTab;
  }
  if (name == "folder") {
    return NodeKind::kFolder;
  }
  return std::nullopt;
}

std::string_view TreeErrorReason(TreeError error) {
  switch (error) {
    case TreeError::kNotFound:
      return "not-found";
    case TreeError::kBadTarget:
      return "bad-target";
    case TreeError::kFolderNotAllowed:
      return "folder-not-allowed";
    case TreeError::kFavoritesFull:
      return "favorites-full";
    case TreeError::kTooDeep:
      return "too-deep";
    case TreeError::kCycle:
      return "cycle";
    case TreeError::kBadUrl:
      return "blocked-url";
  }
}

std::string_view TreeErrorText(TreeError error) {
  switch (error) {
    case TreeError::kNotFound:
      return "That item no longer exists";
    case TreeError::kBadTarget:
      return "Can't move there";
    case TreeError::kFolderNotAllowed:
      return "Folders can only go in Pinned";
    case TreeError::kFavoritesFull:
      return "Favorites are full";
    case TreeError::kTooDeep:
      return "Folders can nest 8 levels deep";
    case TreeError::kCycle:
      return "A folder can't go inside itself";
    case TreeError::kBadUrl:
      return "This page can't be opened in a tab";
  }
}

TabNode::TabNode() = default;
TabNode::TabNode(const TabNode&) = default;
TabNode& TabNode::operator=(const TabNode&) = default;
TabNode::TabNode(TabNode&&) = default;
TabNode& TabNode::operator=(TabNode&&) = default;
TabNode::~TabNode() = default;

DropTarget::DropTarget() = default;
DropTarget::DropTarget(const DropTarget&) = default;
DropTarget& DropTarget::operator=(const DropTarget&) = default;
DropTarget::~DropTarget() = default;

// static
DropTarget DropTarget::Favorite(int index) {
  DropTarget target;
  target.section = Section::kFavorite;
  target.index = index;
  return target;
}

// static
DropTarget DropTarget::Pinned(std::string space_id,
                              std::string parent_id,
                              int index) {
  DropTarget target;
  target.section = Section::kPinned;
  target.space_id = std::move(space_id);
  target.parent_id = std::move(parent_id);
  target.index = index;
  return target;
}

// static
DropTarget DropTarget::Today(std::string space_id, int index) {
  DropTarget target;
  target.section = Section::kToday;
  target.space_id = std::move(space_id);
  target.index = index;
  return target;
}

SpaceTabState::SpaceTabState() = default;
SpaceTabState::SpaceTabState(const SpaceTabState&) = default;
SpaceTabState& SpaceTabState::operator=(const SpaceTabState&) = default;
SpaceTabState::~SpaceTabState() = default;

NewTab::NewTab() = default;
NewTab::NewTab(const NewTab&) = default;
NewTab& NewTab::operator=(const NewTab&) = default;
NewTab::~NewTab() = default;

TreeChanges::TreeChanges() = default;
TreeChanges::TreeChanges(const TreeChanges&) = default;
TreeChanges& TreeChanges::operator=(const TreeChanges&) = default;
TreeChanges::TreeChanges(TreeChanges&&) = default;
TreeChanges& TreeChanges::operator=(TreeChanges&&) = default;
TreeChanges::~TreeChanges() = default;

RemovedRow::RemovedRow() = default;
RemovedRow::RemovedRow(const RemovedRow&) = default;
RemovedRow& RemovedRow::operator=(const RemovedRow&) = default;
RemovedRow::~RemovedRow() = default;

std::string ContainerKey(Section section,
                         std::string_view space_id,
                         std::string_view parent_id) {
  switch (section) {
    case Section::kFavorite:
      return "favorite|";
    case Section::kToday:
      return base::StrCat({"today|", space_id});
    case Section::kPinned:
      return parent_id.empty() ? base::StrCat({"pinned|", space_id})
                               : base::StrCat({"folder|", parent_id});
  }
}

std::string ContainerKeyOf(const TabNode& node) {
  return ContainerKey(node.section, node.space_id, node.parent_id);
}

// CluTabTree --------------------------------------------------------------------

CluTabTree::CluTabTree() = default;
CluTabTree::~CluTabTree() = default;

void CluTabTree::SetSpaces(std::vector<std::string> space_ids) {
  spaces_ = std::move(space_ids);
}

bool CluTabTree::HasSpace(std::string_view space_id) const {
  return !space_id.empty() && std::ranges::contains(spaces_, space_id);
}

void CluTabTree::Load(std::vector<TabNode> nodes,
                      std::map<std::string, SpaceTabState> states) {
  nodes_.clear();
  lists_.clear();
  space_states_.clear();
  std::ranges::stable_sort(nodes, {}, &TabNode::position);
  for (TabNode& node : nodes) {
    const std::string key = ContainerKeyOf(node);
    const std::string id = node.id;
    lists_[key].push_back(id);
    nodes_.emplace(id, std::move(node));
  }
  for (auto& [key, list] : lists_) {
    for (size_t i = 0; i < list.size(); ++i) {
      nodes_.find(list[i])->second.position = static_cast<int>(i);
    }
  }
  for (auto& [space_id, state] : states) {
    space_states_.emplace(space_id, std::move(state));
  }
  changes_ = TreeChanges();
}

const TabNode* CluTabTree::Get(std::string_view id) const {
  const auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : &it->second;
}

std::vector<const TabNode*> CluTabTree::All() const {
  std::vector<const TabNode*> out;
  out.reserve(nodes_.size());
  for (const auto& [id, node] : nodes_) {
    out.push_back(&node);
  }
  return out;
}

const std::vector<std::string>& CluTabTree::Favorites() const {
  return ListAtOrEmpty(ContainerKey(Section::kFavorite, {}, {}));
}

const std::vector<std::string>& CluTabTree::Pinned(
    std::string_view space_id) const {
  return ListAtOrEmpty(ContainerKey(Section::kPinned, space_id, {}));
}

const std::vector<std::string>& CluTabTree::Today(
    std::string_view space_id) const {
  return ListAtOrEmpty(ContainerKey(Section::kToday, space_id, {}));
}

const std::vector<std::string>& CluTabTree::Children(
    std::string_view folder_id) const {
  return ListAtOrEmpty(ContainerKey(Section::kPinned, {}, folder_id));
}

const std::vector<std::string>& CluTabTree::ListOf(const TabNode& node) const {
  return ListAtOrEmpty(ContainerKeyOf(node));
}

int CluTabTree::Depth(std::string_view id) const {
  int depth = 0;
  const TabNode* current = Get(id);
  while (current && current->is_folder() &&
         depth <= static_cast<int>(nodes_.size())) {
    ++depth;
    current = current->parent_id.empty() ? nullptr : Get(current->parent_id);
  }
  return depth;
}

int CluTabTree::Height(std::string_view id) const {
  const TabNode* node = Get(id);
  if (!node || !node->is_folder()) {
    return 0;
  }
  int max = 0;
  for (const std::string& child : Children(id)) {
    max = std::max(max, Height(child));
  }
  return max + 1;
}

std::vector<std::string> CluTabTree::TabsUnder(std::string_view id) const {
  const TabNode* node = Get(id);
  if (!node) {
    return {};
  }
  if (node->is_tab()) {
    return {node->id};
  }
  std::vector<std::string> out;
  for (const std::string& child : Children(id)) {
    std::vector<std::string> inner = TabsUnder(child);
    out.insert(out.end(), inner.begin(), inner.end());
  }
  return out;
}

std::vector<std::string> CluTabTree::SiblingTabs(const TabNode& node) const {
  std::vector<std::string> out;
  for (const std::string& id : ListOf(node)) {
    const TabNode* sibling = Get(id);
    if (sibling && sibling->is_tab()) {
      out.push_back(id);
    }
  }
  return out;
}

base::expected<CluTabTree::Place, TreeError> CluTabTree::PlaceOf(
    const DropTarget& target) const {
  Place place;
  place.section = target.section;
  if (target.section == Section::kFavorite) {
    place.key = ContainerKey(Section::kFavorite, {}, {});
    return place;
  }
  if (!HasSpace(target.space_id)) {
    return base::unexpected(TreeError::kBadTarget);
  }
  place.space_id = target.space_id;
  if (target.section == Section::kToday) {
    place.key = ContainerKey(Section::kToday, target.space_id, {});
    return place;
  }
  if (!target.parent_id.empty()) {
    const TabNode* parent = Get(target.parent_id);
    if (!parent || !parent->is_folder() || parent->section != Section::kPinned ||
        parent->space_id != target.space_id) {
      return base::unexpected(TreeError::kBadTarget);
    }
  }
  place.parent_id = target.parent_id;
  place.key = ContainerKey(Section::kPinned, target.space_id, target.parent_id);
  return place;
}

bool CluTabTree::IsWithin(std::string_view id,
                          std::string_view ancestor_id) const {
  const TabNode* current = Get(id);
  for (size_t guard = 0; current && guard <= nodes_.size(); ++guard) {
    if (current->id == ancestor_id) {
      return true;
    }
    current = current->parent_id.empty() ? nullptr : Get(current->parent_id);
  }
  return false;
}

std::vector<std::string> CluTabTree::Descendants(
    std::string_view folder_id) const {
  std::vector<std::string> out;
  for (const std::string& id : Children(folder_id)) {
    const TabNode* node = Get(id);
    if (!node) {
      continue;
    }
    out.push_back(id);
    if (node->is_folder()) {
      std::vector<std::string> inner = Descendants(id);
      out.insert(out.end(), inner.begin(), inner.end());
    }
  }
  return out;
}

std::vector<std::string>& CluTabTree::ListAt(const std::string& key) {
  return lists_[key];
}

const std::vector<std::string>& CluTabTree::ListAtOrEmpty(
    const std::string& key) const {
  const auto it = lists_.find(key);
  return it == lists_.end() ? EmptyList() : it->second;
}

void CluTabTree::Take(const std::string& key, std::string_view id) {
  std::vector<std::string>& list = ListAt(key);
  std::erase(list, id);
  Renumber(key);
}

void CluTabTree::Put(const std::string& key, const std::string& id, int index) {
  std::vector<std::string>& list = ListAt(key);
  const int at = std::clamp(index, 0, static_cast<int>(list.size()));
  list.insert(list.begin() + at, id);
  Renumber(key);
}

void CluTabTree::Renumber(const std::string& key) {
  const std::vector<std::string>& list = ListAt(key);
  for (size_t i = 0; i < list.size(); ++i) {
    auto it = nodes_.find(list[i]);
    if (it != nodes_.end() && it->second.position != static_cast<int>(i)) {
      it->second.position = static_cast<int>(i);
      Touch(list[i]);
    }
  }
}

void CluTabTree::Touch(const std::string& id) {
  changes_.upserts.insert(id);
  changes_.deletes.erase(id);
}

base::expected<std::string, TreeError> CluTabTree::InsertTab(
    const NewTab& init,
    const DropTarget& target,
    int64_t now) {
  ASSIGN_OR_RETURN(const Place to, PlaceOf(target));
  if (to.section == Section::kFavorite &&
      Favorites().size() >= static_cast<size_t>(kMaxFavorites)) {
    return base::unexpected(TreeError::kFavoritesFull);
  }
  if (!IsRestorableTabUrl(init.url)) {
    return base::unexpected(TreeError::kBadUrl);
  }
  const std::string id = init.id.empty() ? NewId() : init.id;
  if (nodes_.contains(id)) {
    return base::unexpected(TreeError::kBadTarget);
  }
  TabNode node;
  node.id = id;
  node.space_id = to.space_id;
  node.section = to.section;
  node.kind = NodeKind::kTab;
  node.parent_id = to.parent_id;
  node.url = init.url;
  if (to.section != Section::kToday) {
    node.pinned_url = init.url;
  } else {
    node.nav_state = init.nav_state;
  }
  node.title = CleanTitle(init.title);
  node.muted = init.muted;
  node.last_active_at = init.last_active_at.value_or(now);
  node.created_at = now;
  nodes_.emplace(id, std::move(node));
  Touch(id);
  Put(to.key, id, target.index);
  return id;
}

base::expected<std::string, TreeError> CluTabTree::CreateFolder(
    std::string_view space_id,
    std::string_view parent_id,
    std::string_view name,
    std::optional<int> index,
    int64_t now,
    std::string_view id) {
  ASSIGN_OR_RETURN(const Place to,
                   PlaceOf(DropTarget::Pinned(std::string(space_id),
                                              std::string(parent_id), 0)));
  if ((parent_id.empty() ? 0 : Depth(parent_id)) + 1 > kMaxFolderDepth) {
    return base::unexpected(TreeError::kTooDeep);
  }
  const std::string folder_id = id.empty() ? NewId() : std::string(id);
  if (nodes_.contains(folder_id)) {
    return base::unexpected(TreeError::kBadTarget);
  }
  const std::string clean = CleanName(name);
  TabNode node;
  node.id = folder_id;
  node.space_id = to.space_id;
  node.section = Section::kPinned;
  node.kind = NodeKind::kFolder;
  node.parent_id = to.parent_id;
  node.title_override = clean.empty() ? std::string(kDefaultFolderName) : clean;
  node.created_at = now;
  nodes_.emplace(folder_id, std::move(node));
  Touch(folder_id);
  const int at = index.value_or(static_cast<int>(ListAt(to.key).size()));
  Put(to.key, folder_id, at);
  return folder_id;
}

base::expected<void, TreeError> CluTabTree::ValidateMove(
    std::string_view id,
    const DropTarget& target) const {
  const TabNode* node = Get(id);
  if (!node) {
    return base::unexpected(TreeError::kNotFound);
  }
  ASSIGN_OR_RETURN(const Place to, PlaceOf(target));
  if (node->is_folder() && to.section != Section::kPinned) {
    return base::unexpected(TreeError::kFolderNotAllowed);
  }
  if (node->is_folder() && !to.parent_id.empty()) {
    if (IsWithin(to.parent_id, node->id)) {
      return base::unexpected(TreeError::kCycle);
    }
    if (Depth(to.parent_id) + Height(node->id) > kMaxFolderDepth) {
      return base::unexpected(TreeError::kTooDeep);
    }
  }
  const bool already_there = node->section == Section::kFavorite;
  if (to.section == Section::kFavorite && !already_there &&
      Favorites().size() >= static_cast<size_t>(kMaxFavorites)) {
    return base::unexpected(TreeError::kFavoritesFull);
  }
  return base::ok();
}

void CluTabTree::ApplyMove(TabNode& node,
                           const Place& to,
                           int index,
                           int64_t now) {
  const std::string id = node.id;
  Take(ContainerKeyOf(node), id);
  Touch(id);
  const bool was_anchored = node.anchored();
  const bool anchored = to.section != Section::kToday;
  if (node.is_tab()) {
    // The F8 move table.
    if (!was_anchored && anchored) {
      node.pinned_url = node.url;
      node.nav_state.reset();
    } else if (was_anchored && !anchored) {
      node.pinned_url.reset();
      node.last_active_at = now;
    }
  }
  node.section = to.section;
  node.space_id = to.space_id;
  node.parent_id = to.parent_id;
  if (node.is_folder()) {
    for (const std::string& inner : Descendants(id)) {
      TabNode& descendant = nodes_.find(inner)->second;
      if (descendant.space_id != to.space_id) {
        descendant.space_id = to.space_id;
        Touch(inner);
      }
    }
  }
  Put(to.key, id, index);
}

base::expected<void, TreeError> CluTabTree::Move(std::string_view id,
                                                 const DropTarget& target,
                                                 int64_t now) {
  RETURN_IF_ERROR(ValidateMove(id, target));
  ASSIGN_OR_RETURN(const Place to, PlaceOf(target));
  ApplyMove(nodes_.find(id)->second, to, target.index, now);
  return base::ok();
}

base::expected<void, TreeError> CluTabTree::MoveMany(
    const std::vector<std::string>& ids,
    const DropTarget& target,
    int64_t now) {
  std::vector<std::string> unique;
  for (const std::string& id : ids) {
    if (!std::ranges::contains(unique, id)) {
      unique.push_back(id);
    }
  }
  if (unique.empty()) {
    return base::unexpected(TreeError::kNotFound);
  }
  ASSIGN_OR_RETURN(const Place to, PlaceOf(target));
  // All or none: every node passes the rules first, and Favorites must take every one that is not a favorite yet.
  size_t incoming_favorites = 0;
  for (const std::string& id : unique) {
    RETURN_IF_ERROR(ValidateMove(id, target));
    if (to.section == Section::kFavorite &&
        Get(id)->section != Section::kFavorite) {
      ++incoming_favorites;
    }
  }
  if (to.section == Section::kFavorite &&
      Favorites().size() + incoming_favorites >
          static_cast<size_t>(kMaxFavorites)) {
    return base::unexpected(TreeError::kFavoritesFull);
  }
  // A moved folder and a node inside it: only the folder moves, the node goes with it.
  // Decided before erasing: erase_if moves elements while its predicate runs.
  std::set<std::string> nested;
  for (const std::string& id : unique) {
    for (const std::string& other : unique) {
      if (other != id && Get(other)->is_folder() && IsWithin(id, other)) {
        nested.insert(id);
        break;
      }
    }
  }
  std::erase_if(unique,
                [&](const std::string& id) { return nested.contains(id); });

  std::vector<std::string> rest;
  for (const std::string& id : ListAtOrEmpty(to.key)) {
    if (!std::ranges::contains(unique, id)) {
      rest.push_back(id);
    }
  }
  const std::optional<std::string> anchor =
      target.index >= 0 && static_cast<size_t>(target.index) < rest.size()
          ? std::optional<std::string>(rest[target.index])
          : std::nullopt;
  for (const std::string& id : unique) {
    TabNode& node = nodes_.find(id)->second;
    // Each lands just before the anchor row, so the batch keeps its order.
    std::vector<std::string> without = ListAtOrEmpty(to.key);
    std::erase(without, id);
    int index = static_cast<int>(without.size());
    if (anchor) {
      const auto it = std::ranges::find(without, *anchor);
      if (it != without.end()) {
        index = static_cast<int>(it - without.begin());
      }
    }
    ApplyMove(node, to, index, now);
  }
  return base::ok();
}

std::optional<RemovedRow> CluTabTree::RemoveTab(std::string_view id) {
  const auto it = nodes_.find(id);
  if (it == nodes_.end() || !it->second.is_tab()) {
    return std::nullopt;
  }
  RemovedRow removed;
  removed.node = it->second;
  const std::string key = ContainerKeyOf(removed.node);
  removed.siblings = ListAtOrEmpty(key);
  const auto at = std::ranges::find(removed.siblings, id);
  removed.index = static_cast<int>(at - removed.siblings.begin());
  const std::string node_id = removed.node.id;
  nodes_.erase(it);
  Take(key, node_id);
  changes_.upserts.erase(node_id);
  changes_.deletes.insert(node_id);
  for (auto& [space_id, state] : space_states_) {
    if (state.active_tab_id == node_id) {
      state.active_tab_id.clear();
      changes_.spaces.insert(space_id);
    }
  }
  return removed;
}

base::expected<std::vector<std::string>, TreeError> CluTabTree::DeleteFolder(
    std::string_view id,
    int64_t now) {
  const TabNode* folder = Get(id);
  if (!folder) {
    return base::unexpected(TreeError::kNotFound);
  }
  if (!folder->is_folder()) {
    return base::unexpected(TreeError::kBadTarget);
  }
  const std::string folder_id = folder->id;
  const std::string space_id = folder->space_id;
  const std::vector<std::string> inner = Descendants(folder_id);
  std::vector<std::string> tabs;
  std::vector<std::string> folders = {folder_id};
  for (const std::string& node_id : inner) {
    if (Get(node_id)->is_tab()) {
      tabs.push_back(node_id);
    } else {
      folders.push_back(node_id);
    }
  }
  Take(ContainerKeyOf(*folder), folder_id);
  for (const std::string& f : folders) {
    lists_.erase(ContainerKey(Section::kPinned, {}, f));
    nodes_.erase(f);
    changes_.upserts.erase(f);
    changes_.deletes.insert(f);
  }
  const std::string today = ContainerKey(Section::kToday, space_id, {});
  for (size_t i = 0; i < tabs.size(); ++i) {
    TabNode& tab = nodes_.find(tabs[i])->second;
    tab.section = Section::kToday;
    tab.parent_id.clear();
    tab.pinned_url.reset();
    tab.last_active_at = now;
    Touch(tabs[i]);
    Put(today, tabs[i], static_cast<int>(i));
  }
  return tabs;
}

std::vector<TabNode> CluTabTree::TakeSpace(std::string_view space_id,
                                           SpaceTabState* state) {
  std::vector<TabNode> out;
  if (state) {
    *state = SpaceState(space_id);
  }
  // Roots first, then each folder's children, so AddNodes() always finds a row's folder before the row.
  std::vector<std::string> ids;
  for (const std::string& id :
       ListAtOrEmpty(ContainerKey(Section::kPinned, space_id, {}))) {
    ids.push_back(id);
  }
  for (const std::string& id : Today(space_id)) {
    ids.push_back(id);
  }
  for (size_t i = 0; i < ids.size(); ++i) {
    const TabNode* node = Get(ids[i]);
    if (node && node->is_folder()) {
      for (const std::string& child : Children(node->id)) {
        ids.push_back(child);
      }
    }
  }
  for (const std::string& id : ids) {
    const auto it = nodes_.find(id);
    if (it == nodes_.end()) {
      continue;
    }
    out.push_back(it->second);
    nodes_.erase(it);
    changes_.upserts.erase(id);
    changes_.deletes.insert(id);
  }
  lists_.erase(ContainerKey(Section::kPinned, space_id, {}));
  lists_.erase(ContainerKey(Section::kToday, space_id, {}));
  for (const TabNode& node : out) {
    if (node.is_folder()) {
      lists_.erase(ContainerKey(Section::kPinned, {}, node.id));
    }
  }
  ForgetSpace(space_id);
  return out;
}

std::vector<TabNode> CluTabTree::TakeSubtree(std::string_view id) {
  std::vector<TabNode> out;
  const TabNode* root = Get(id);
  if (!root) {
    return out;
  }
  std::vector<std::string> ids = {root->id};
  if (root->is_folder()) {
    for (const std::string& inner : Descendants(root->id)) {
      ids.push_back(inner);
    }
  }
  Take(ContainerKeyOf(*root), root->id);
  for (const std::string& node_id : ids) {
    const auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
      continue;
    }
    if (it->second.is_folder()) {
      lists_.erase(ContainerKey(Section::kPinned, {}, node_id));
    }
    out.push_back(it->second);
    nodes_.erase(it);
    changes_.upserts.erase(node_id);
    changes_.deletes.insert(node_id);
    for (auto& [space_id, state] : space_states_) {
      if (state.active_tab_id == node_id) {
        state.active_tab_id.clear();
        changes_.spaces.insert(space_id);
      }
    }
  }
  return out;
}

std::vector<std::string> CluTabTree::AddNodes(std::vector<TabNode> nodes) {
  std::vector<std::string> added;
  // Each touched list and where its new rows start: rows already there keep their places.
  std::map<std::string, size_t> starts;
  // Folders before their children: a row whose folder is not in yet waits for a later pass. The passes decide only
  // what joins; each list is put in the order of the incoming positions afterwards, so a child that waited a pass
  // does not land after its siblings.
  bool progress = true;
  while (progress && !nodes.empty()) {
    progress = false;
    std::vector<TabNode> waiting;
    for (TabNode& node : nodes) {
      const bool placed_space =
          node.section == Section::kFavorite || HasSpace(node.space_id);
      if (nodes_.contains(node.id) || !placed_space) {
        continue;
      }
      if (!node.parent_id.empty()) {
        const TabNode* parent = Get(node.parent_id);
        if (!parent) {
          waiting.push_back(std::move(node));
          continue;
        }
        if (!parent->is_folder() || parent->space_id != node.space_id) {
          continue;
        }
      }
      const std::string key = ContainerKeyOf(node);
      const std::string id = node.id;
      std::vector<std::string>& list = lists_[key];
      starts.emplace(key, list.size());
      list.push_back(id);
      nodes_.emplace(id, std::move(node));
      Touch(id);
      added.push_back(id);
      progress = true;
    }
    nodes = std::move(waiting);
  }
  for (const auto& [key, start] : starts) {
    std::vector<std::string>& list = lists_[key];
    std::stable_sort(list.begin() + start, list.end(),
                     [&](const std::string& a, const std::string& b) {
                       return nodes_.at(a).position < nodes_.at(b).position;
                     });
    Renumber(key);
  }
  return added;
}

void CluTabTree::SetSpaceState(std::string_view space_id, SpaceTabState state) {
  MutableSpaceState(space_id) = std::move(state);
}

void CluTabTree::ForgetSpace(std::string_view space_id) {
  const auto it = space_states_.find(space_id);
  if (it != space_states_.end()) {
    space_states_.erase(it);
    changes_.spaces.insert(std::string(space_id));
  }
}

TabNode* CluTabTree::Mutable(std::string_view id) {
  const auto it = nodes_.find(id);
  if (it == nodes_.end()) {
    return nullptr;
  }
  Touch(it->first);
  return &it->second;
}

const SpaceTabState& CluTabTree::SpaceState(std::string_view space_id) const {
  static const base::NoDestructor<SpaceTabState> empty;
  const auto it = space_states_.find(space_id);
  return it == space_states_.end() ? *empty : it->second;
}

SpaceTabState& CluTabTree::MutableSpaceState(std::string_view space_id) {
  changes_.spaces.insert(std::string(space_id));
  auto it = space_states_.find(space_id);
  if (it == space_states_.end()) {
    it = space_states_.emplace(std::string(space_id), SpaceTabState()).first;
  }
  return it->second;
}

std::vector<std::string> CluTabTree::SidebarOrder(
    std::string_view space_id,
    std::string_view active_tab_id,
    const std::set<std::string>& skip) const {
  std::vector<std::string> out;
  if (!HasSpace(space_id)) {
    return out;
  }
  for (const std::string& id : Favorites()) {
    out.push_back(id);
  }
  // Whether `tab_id` sits anywhere under `refs`.
  auto contains = [&](const auto& self, const std::vector<std::string>& refs,
                      std::string_view tab_id, int depth) -> bool {
    if (depth > kMaxFolderDepth + 1) {
      return false;
    }
    for (const std::string& id : refs) {
      const TabNode* node = Get(id);
      if (!node) {
        continue;
      }
      if (node->is_tab() ? id == tab_id
                         : self(self, Children(id), tab_id, depth + 1)) {
        return true;
      }
    }
    return false;
  };
  auto walk = [&](const auto& self, const std::vector<std::string>& refs,
                  int depth) -> void {
    if (depth > kMaxFolderDepth + 1) {
      return;
    }
    for (const std::string& id : refs) {
      const TabNode* node = Get(id);
      if (!node) {
        continue;
      }
      if (node->is_tab()) {
        out.push_back(id);
      } else if (!node->collapsed) {
        self(self, Children(id), depth + 1);
      } else if (!active_tab_id.empty() &&
                 contains(contains, Children(id), active_tab_id, depth + 1)) {
        out.push_back(std::string(active_tab_id));
      }
    }
  };
  const std::vector<std::string>& pinned = Pinned(space_id);
  if (!SpaceState(space_id).pinned_collapsed) {
    walk(walk, pinned, 0);
  } else if (!active_tab_id.empty() &&
             contains(contains, pinned, active_tab_id, 0)) {
    out.push_back(std::string(active_tab_id));
  }
  for (const std::string& id : Today(space_id)) {
    out.push_back(id);
  }
  std::erase_if(out, [&](const std::string& id) {
    return std::ranges::contains(skip, id) || !Get(id);
  });
  return out;
}

TreeChanges CluTabTree::TakeChanges() {
  TreeChanges out = std::move(changes_);
  changes_ = TreeChanges();
  return out;
}

void CluTabTree::RestoreChanges(const TreeChanges& changes) {
  for (const std::string& id : changes.upserts) {
    if (!changes_.deletes.contains(id)) {
      changes_.upserts.insert(id);
    }
  }
  for (const std::string& id : changes.deletes) {
    if (!changes_.upserts.contains(id)) {
      changes_.deletes.insert(id);
    }
  }
  changes_.spaces.insert(changes.spaces.begin(), changes.spaces.end());
}

std::optional<std::string> NextActive(
    const std::vector<std::string>& siblings,
    std::string_view closed_id,
    const std::set<std::string>& removed,
    const std::vector<std::string>& mru,
    base::FunctionRef<bool(std::string_view)> usable) {
  auto gone = [&](std::string_view id) {
    return id == closed_id || std::ranges::contains(removed, id);
  };
  const auto at = std::ranges::find(siblings, closed_id);
  if (at != siblings.end()) {
    std::vector<std::string> rest;
    size_t index = 0;
    for (auto it = siblings.begin(); it != siblings.end(); ++it) {
      if (gone(*it)) {
        continue;
      }
      if (it < at) {
        ++index;
      }
      rest.push_back(*it);
    }
    // The row that takes the closed row's place, then the one above it.
    if (index < rest.size() && usable(rest[index])) {
      return rest[index];
    }
    if (index > 0 && index - 1 < rest.size() && usable(rest[index - 1])) {
      return rest[index - 1];
    }
  }
  for (const std::string& id : mru) {
    if (!gone(id) && usable(id)) {
      return id;
    }
  }
  return std::nullopt;
}

}  // namespace clu
