// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_TAB_TREE_H_
#define CLU_BROWSER_MODEL_CLU_TAB_TREE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/types/expected.h"
#include "clu/browser/model/clu_tab_node.h"

// The sidebar tree of one profile in memory: favorites, then per space a pinned tree with folders to depth 8 and a
// Today list. Every structural change checks the F8 move rules (docs/plan/02-tabs.md) before it touches anything, so
// a refused change leaves the tree as it was. Positions stay dense in every list. Changes are recorded so the store
// can write exactly the rows that changed, in one transaction (CluStore::Write).
namespace clu {

struct NewTab {
  NewTab();
  NewTab(const NewTab&);
  NewTab& operator=(const NewTab&);
  ~NewTab();

  // A fresh UUID when empty. A live page keeps its Clu tab id as its row id.
  std::string id;
  std::string url;
  std::string title;
  std::optional<std::string> nav_state;
  bool muted = false;
  std::optional<int64_t> last_active_at;
};

// The rows a change touched since the last TakeChanges(): written or deleted rows and space states.
struct TreeChanges {
  TreeChanges();
  TreeChanges(const TreeChanges&);
  TreeChanges& operator=(const TreeChanges&);
  TreeChanges(TreeChanges&&);
  TreeChanges& operator=(TreeChanges&&);
  ~TreeChanges();

  bool empty() const {
    return upserts.empty() && deletes.empty() && spaces.empty();
  }

  std::set<std::string> upserts;
  std::set<std::string> deletes;
  std::set<std::string> spaces;
};

// A tab row that left the tree: the list it was in, before the removal, and its place there.
struct RemovedRow {
  RemovedRow();
  RemovedRow(const RemovedRow&);
  RemovedRow& operator=(const RemovedRow&);
  ~RemovedRow();

  TabNode node;
  std::vector<std::string> siblings;
  int index = 0;
};

class CluTabTree {
 public:
  CluTabTree();
  CluTabTree(const CluTabTree&) = delete;
  CluTabTree& operator=(const CluTabTree&) = delete;
  ~CluTabTree();

  // Spaces in sidebar order. Rows of a space that is not here are never placed (the store repairs them at load).
  void SetSpaces(std::vector<std::string> space_ids);
  const std::vector<std::string>& spaces() const { return spaces_; }
  bool HasSpace(std::string_view space_id) const;

  // Replaces the tree with repaired rows (CluTabRepair) and space states; records no changes.
  void Load(std::vector<TabNode> nodes,
            std::map<std::string, SpaceTabState> states);

  const TabNode* Get(std::string_view id) const;
  std::vector<const TabNode*> All() const;
  size_t size() const { return nodes_.size(); }

  const std::vector<std::string>& Favorites() const;
  const std::vector<std::string>& Pinned(std::string_view space_id) const;
  const std::vector<std::string>& Today(std::string_view space_id) const;
  const std::vector<std::string>& Children(std::string_view folder_id) const;
  // The list `node` sits in.
  const std::vector<std::string>& ListOf(const TabNode& node) const;

  // Folder level (a root folder is 1); 0 for anything else.
  int Depth(std::string_view id) const;
  // Folder levels a node brings along: 0 for a tab, 1 for a folder without subfolders.
  int Height(std::string_view id) const;
  // The tab ids under `id` in sidebar order (`id` itself for a tab).
  std::vector<std::string> TabsUnder(std::string_view id) const;
  // Tab rows of the list `node` sits in, folders left out, in order.
  std::vector<std::string> SiblingTabs(const TabNode& node) const;

  base::expected<std::string, TreeError> InsertTab(const NewTab& init,
                                                   const DropTarget& target,
                                                   int64_t now);
  // A folder in a space's pinned tree. An empty name is "Untitled". `index` defaults to the end of the list.
  base::expected<std::string, TreeError> CreateFolder(
      std::string_view space_id,
      std::string_view parent_id,
      std::string_view name,
      std::optional<int> index,
      int64_t now,
      std::string_view id = {});

  // Every F8 rule, without changing anything.
  base::expected<void, TreeError> ValidateMove(std::string_view id,
                                               const DropTarget& target) const;
  base::expected<void, TreeError> Move(std::string_view id,
                                       const DropTarget& target,
                                       int64_t now);
  // Moves the nodes, in the given order, to one place, all or none. `target.index` counts the target list without
  // the moved nodes; they land together before the row at that index now (multi-select, F8).
  base::expected<void, TreeError> MoveMany(const std::vector<std::string>& ids,
                                           const DropTarget& target,
                                           int64_t now);

  // Deletes a tab row; nullopt for an unknown id or a folder.
  std::optional<RemovedRow> RemoveTab(std::string_view id);
  // Deletes a folder and its subfolders. Their tabs go, flattened and in order, to the top of Today with the Today
  // clock starting now (F3). Returns those tab ids.
  base::expected<std::vector<std::string>, TreeError> DeleteFolder(
      std::string_view id,
      int64_t now);

  // Every row of a space (its pinned tree with folders, and Today) leaves the tree as it was, parents and positions
  // kept, in an order AddNodes() can take back; the space's state comes along in `state`. Rows go as deletes, so the
  // store drops them. Moving a space to another profile (spaces.setProfile) takes its rows from one profile's tree
  // and adds them to the other's.
  std::vector<TabNode> TakeSpace(std::string_view space_id,
                                 SpaceTabState* state);
  // A tab, or a folder with everything under it, leaves the tree the same way (a cross-profile move). Empty for an
  // unknown id.
  std::vector<TabNode> TakeSubtree(std::string_view id);
  // Rows from TakeSpace() or TakeSubtree() join as they are: each list keeps the order of their positions, after any
  // rows already there. Rows whose id the tree has, whose space is not here, or whose folder is missing are skipped.
  // Returns the ids added.
  std::vector<std::string> AddNodes(std::vector<TabNode> nodes);
  void SetSpaceState(std::string_view space_id, SpaceTabState state);
  // The space's state goes (its rows have left).
  void ForgetSpace(std::string_view space_id);

  // A row to change in place (title, URL, mute, icon, collapsed, nav state, last_active_at). Records the row as
  // changed. Null for an unknown id. Structural fields (section, space, parent, position) go through Move().
  TabNode* Mutable(std::string_view id);

  const SpaceTabState& SpaceState(std::string_view space_id) const;
  SpaceTabState& MutableSpaceState(std::string_view space_id);

  // F12: favorites left to right, then the space's visible pinned rows, then Today. Folder rows do not count; a
  // collapsed folder (or a collapsed Pinned section) hides its tabs except `active_tab_id`, which counts at that
  // place. Ids in `skip` (a pending Clear) are left out.
  std::vector<std::string> SidebarOrder(
      std::string_view space_id,
      std::string_view active_tab_id,
      const std::set<std::string>& skip = {}) const;

  TreeChanges TakeChanges();
  // Changes taken for a write that failed go back, under any change made since (a row deleted since stays deleted).
  void RestoreChanges(const TreeChanges& changes);

 private:
  struct Place {
    std::string key;
    Section section = Section::kToday;
    std::string space_id;
    std::string parent_id;
  };

  base::expected<Place, TreeError> PlaceOf(const DropTarget& target) const;
  bool IsWithin(std::string_view id, std::string_view ancestor_id) const;
  std::vector<std::string> Descendants(std::string_view folder_id) const;
  std::vector<std::string>& ListAt(const std::string& key);
  const std::vector<std::string>& ListAtOrEmpty(const std::string& key) const;
  void Take(const std::string& key, std::string_view id);
  void Put(const std::string& key, const std::string& id, int index);
  void Renumber(const std::string& key);
  void Touch(const std::string& id);
  void ApplyMove(TabNode& node, const Place& to, int index, int64_t now);

  std::vector<std::string> spaces_;
  std::map<std::string, TabNode, std::less<>> nodes_;
  std::map<std::string, std::vector<std::string>, std::less<>> lists_;
  std::map<std::string, SpaceTabState, std::less<>> space_states_;
  TreeChanges changes_;
};

// The list key of a node (containerKey in tabStore.repair.ts). Folder children are keyed by folder id, so a folder
// moves with its list.
std::string ContainerKey(Section section,
                         std::string_view space_id,
                         std::string_view parent_id);
std::string ContainerKeyOf(const TabNode& node);

// F9 next active tab after `closed_id` leaves: the row that takes its place in `siblings`, else the one above, else
// the window's most recent tab (`mru`, most recent first) that `usable` accepts, else none. `removed` holds every row
// leaving in the same batch.
std::optional<std::string> NextActive(
    const std::vector<std::string>& siblings,
    std::string_view closed_id,
    const std::set<std::string>& removed,
    const std::vector<std::string>& mru,
    base::FunctionRef<bool(std::string_view)> usable);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_TAB_TREE_H_
