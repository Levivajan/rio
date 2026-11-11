// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_TAB_NODE_H_
#define CLU_BROWSER_MODEL_CLU_TAB_NODE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// One sidebar row of a profile's tab tree (docs/plan/02-tabs.md "Model", PLAN 7.2 tab_nodes). A tab row is a tab
// whether or not a page exists for it: a row without a live WebContents is unloaded. Each Clu profile is one
// Chromium profile with its own model, so rows carry no profile id; the model's profile is theirs.
namespace clu {

// MAX_FAVORITES, MAX_FOLDER_DEPTH and MAX_FOLDER_NAME in src/shared/tabs.ts.
// The grid grows by rows of up to 4 tiles, so 48 is only a sanity cap.
inline constexpr int kMaxFavorites = 48;
// A root folder is level 1, so a folder at level 8 holds tabs but no folder.
inline constexpr int kMaxFolderDepth = 8;
inline constexpr size_t kMaxFolderName = 200;
// The cap on a stored page title (UTF-8 bytes; the state cap is 1,024 UTF-16 units on top).
inline constexpr size_t kMaxNodeTitle = 4096;
inline constexpr char kDefaultFolderName[] = "Untitled";

enum class Section { kFavorite, kPinned, kToday };
enum class NodeKind { kTab, kFolder };

std::string_view SectionName(Section section);
std::optional<Section> SectionFromName(std::string_view name);
std::string_view KindName(NodeKind kind);
std::optional<NodeKind> KindFromName(std::string_view name);

struct TabNode {
  TabNode();
  TabNode(const TabNode&);
  TabNode& operator=(const TabNode&);
  TabNode(TabNode&&);
  TabNode& operator=(TabNode&&);
  ~TabNode();

  bool is_tab() const { return kind == NodeKind::kTab; }
  bool is_folder() const { return kind == NodeKind::kFolder; }
  // Favorites and pinned tabs: they have a pinned URL and reset to it on close (F9).
  bool anchored() const { return section != Section::kToday; }

  std::string id;
  // Empty only for favorites.
  std::string space_id;
  Section section = Section::kToday;
  NodeKind kind = NodeKind::kTab;
  // The folder this row sits in; empty at a list root. Pinned rows only.
  std::string parent_id;
  // Dense within the row's list.
  int position = 0;
  // The current URL; equals pinned_url after a reset. Empty for folders.
  std::string url;
  // Set for anchored tabs.
  std::optional<std::string> pinned_url;
  std::string title;
  // A rename; a folder's name.
  std::optional<std::string> title_override;
  bool muted = false;
  // Folders only.
  bool collapsed = false;
  std::optional<std::string> split_id;
  // JSON (src/shared/navState.ts), Today tabs only.
  std::optional<std::string> nav_state;
  // Milliseconds since the Unix epoch.
  int64_t last_active_at = 0;
  int64_t created_at = 0;
  // One emoji grapheme on a folder or a pinned row.
  std::optional<std::string> custom_icon;
};

// Where a structural change puts a node (DropTarget in src/shared/tabs.ts). `index` counts the target list without
// the node being moved.
struct DropTarget {
  DropTarget();
  DropTarget(const DropTarget&);
  DropTarget& operator=(const DropTarget&);
  ~DropTarget();

  static DropTarget Favorite(int index);
  static DropTarget Pinned(std::string space_id,
                           std::string parent_id,
                           int index);
  static DropTarget Today(std::string space_id, int index);

  Section section = Section::kToday;
  // Pinned and Today.
  std::string space_id;
  // Pinned only: the folder, or empty for the pinned root.
  std::string parent_id;
  int index = 0;
};

// Why the tree refused a change (MoveRejection in src/shared/tabs.ts, plus a URL the tab policy refuses).
enum class TreeError {
  kNotFound,
  kBadTarget,
  kFolderNotAllowed,
  kFavoritesFull,
  kTooDeep,
  kCycle,
  kBadUrl,
};

// The IpcError reason the UI branches on ("favorites-full"), and the toast text (MOVE_REJECTION_TEXT).
std::string_view TreeErrorReason(TreeError error);
std::string_view TreeErrorText(TreeError error);

// Per space: the tab it shows when a window opens on it, and whether its Pinned section is collapsed
// (space_tab_state).
struct SpaceTabState {
  SpaceTabState();
  SpaceTabState(const SpaceTabState&);
  SpaceTabState& operator=(const SpaceTabState&);
  ~SpaceTabState();

  std::string active_tab_id;
  bool pinned_collapsed = false;
};

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_TAB_NODE_H_
