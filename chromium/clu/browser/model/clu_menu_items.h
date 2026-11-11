// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_MENU_ITEMS_H_
#define CLU_BROWSER_MODEL_CLU_MENU_ITEMS_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "clu/browser/model/clu_tab_node.h"

// The sidebar menus tabs owns (PLAN 8.2, src/main/tabs/rowMenu.ts): the row, folder, multi-select, empty-area and +
// menus, as a tree of items with string ids. Pure; CluTabMenu turns a tree into a native menu and runs the pick.
namespace clu {

inline constexpr char kMenuCopyLink[] = "tabs.copyLink";
inline constexpr char kMenuRename[] = "tabs.rename";
inline constexpr char kMenuDuplicate[] = "tabs.duplicate";
inline constexpr char kMenuMute[] = "tabs.mute";
inline constexpr char kMenuPin[] = "tabs.pin";
inline constexpr char kMenuFavorite[] = "tabs.favorite";
inline constexpr char kMenuPinnedUrl[] = "tabs.pinnedUrl";
inline constexpr char kMenuPinnedUrlCurrent[] = "tabs.pinnedUrl.current";
inline constexpr char kMenuPinnedUrlEnter[] = "tabs.pinnedUrl.enter";
inline constexpr char kMenuSetIcon[] = "tabs.setIcon";
inline constexpr char kMenuRemoveIcon[] = "tabs.removeIcon";
inline constexpr char kMenuMoveTo[] = "tabs.moveTo";
inline constexpr char kMenuToNewWindow[] = "tabs.toNewWindow";
inline constexpr char kMenuOpenInSplit[] = "tabs.openInSplit";
inline constexpr char kMenuArchive[] = "tabs.archive";
inline constexpr char kMenuArchiveOthers[] = "tabs.archiveOthers";
inline constexpr char kMenuArchiveAbove[] = "tabs.archiveAbove";
inline constexpr char kMenuArchiveBelow[] = "tabs.archiveBelow";
inline constexpr char kMenuClose[] = "tabs.close";
inline constexpr char kMenuNewTab[] = "tabs.newTab";
inline constexpr char kMenuNewFolder[] = "folders.new";
inline constexpr char kMenuPasteAndGo[] = "tabs.pasteAndGo";
inline constexpr char kMenuNewFolderInside[] = "folders.newInside";
inline constexpr char kMenuDeleteFolder[] = "folders.delete";
inline constexpr char kMenuBulkArchive[] = "tabs.bulk.archive";
inline constexpr char kMenuBulkMoveTo[] = "tabs.bulk.moveTo";
inline constexpr char kMenuBulkSplit[] = "tabs.bulk.addToSplit";
inline constexpr char kMenuBulkCopy[] = "tabs.bulk.copyUrls";
inline constexpr char kMenuBulkCopyMarkdown[] = "tabs.bulk.copyUrlsMarkdown";
inline constexpr char kMenuBulkClose[] = "tabs.bulk.close";
// Customize Page… opens Site tweaks for the row's page.
inline constexpr char kMenuCustomize[] = "tabs.customize";
inline constexpr char kMenuFolderCopyLinks[] = "folders.copyLinks";
inline constexpr char kMenuFolderCopyLinksMarkdown[] = "folders.copyLinksMarkdown";
// The folder menu's Share Folder… (macOS's share picker with the folder's links), Turn "name" into Space (a new space
// whose Pinned holds the folder's rows) and Duplicate (a copy right after it).
inline constexpr char kMenuFolderShare[] = "folders.share";
inline constexpr char kMenuFolderToSpace[] = "folders.toSpace";
inline constexpr char kMenuFolderDuplicate[] = "folders.duplicate";
// A favorite's Show Unread Badge: a check item after Share, only on a favorite whose page shows an unread count.
inline constexpr char kMenuUnreadBadge[] = "tabs.unreadBadge";
// The + menu's New Space and New Split, and the "..." menu's two items, run the key table's commands of these ids.
inline constexpr char kMenuNewSpace[] = "spaces.newSpace";
inline constexpr char kMenuNewSplit[] = "split.add";
inline constexpr char kMenuReload[] = "nav.reload";
inline constexpr char kMenuToggleSidebar[] = "sidebar.toggle";
inline constexpr char kMenuShare[] = "tabs.share";
// Share > one of macOS's sharing services, by its index: "tabs.share:2".
inline constexpr char kMenuShareService[] = "tabs.share:";
inline constexpr char kMenuMoveToNewSpace[] = "tabs.moveTo.newSpace";
// Add to Split View takes 2 to 4 tabs (PLAN 8.2).
inline constexpr size_t kMaxSplitTabs = 4;

struct MenuItem {
  MenuItem();
  MenuItem(const MenuItem&);
  MenuItem& operator=(const MenuItem&);
  MenuItem(MenuItem&&);
  MenuItem& operator=(MenuItem&&);
  ~MenuItem();

  static MenuItem Item(std::string id, std::string label, bool enabled = true);
  static MenuItem Separator();
  static MenuItem Submenu(std::string id,
                          std::string label,
                          std::vector<MenuItem> items);
  // An item that shows the key of a command of the key table (command_table.h) at its right edge.
  static MenuItem Keyed(std::string id,
                        std::string label,
                        std::string command,
                        bool enabled = true);
  // The Option alternate of the item before it: macOS shows it in that item's place while Option is held.
  static MenuItem Alternate(std::string id,
                            std::string label,
                            bool enabled = true);
  // An item the menu draws with a checkmark when `checked` (a submenu's current choice).
  static MenuItem Check(std::string id,
                        std::string label,
                        bool checked,
                        bool enabled = true);

  bool separator = false;
  std::string id;
  std::string label;
  bool enabled = true;
  std::vector<MenuItem> submenu;
  // The key table command whose accelerator the item shows; empty for none.
  std::string keys;
  bool alternate = false;
  bool checked = false;
};

// A Move To destination: a space with its top-level folders, in sidebar order.
struct MoveToSpace {
  MoveToSpace();
  MoveToSpace(const MoveToSpace&);
  MoveToSpace& operator=(const MoveToSpace&);
  ~MoveToSpace();

  std::string id;
  std::string name;
  std::vector<std::pair<std::string, std::string>> folders;  // id, name
};

// A Move To pick: Favorites, a space's Pinned or Today, or one of its top-level folders.
struct MoveToPick {
  enum class Kind { kFavorite, kPinned, kToday, kFolder };
  Kind kind = Kind::kFavorite;
  std::string space_id;
  std::string folder_id;
};

std::string MoveToId(const MoveToPick& pick, std::string_view prefix);
std::optional<MoveToPick> ParseMoveTo(std::string_view id,
                                      std::string_view prefix);

// Move To > Favorites, then each space > Pinned, Today and its top-level folders: two submenu levels. The row's own
// place is disabled; the multi-select menu passes no node and kMenuBulkMoveTo.
// A row's Move to: one item per space (the row keeps its section there; a favorite goes to the space's Pinned), the
// row's own space disabled, then New Space….
std::vector<MenuItem> RowMoveToItems(const TabNode& node,
                                     const std::vector<MoveToSpace>& spaces);
std::vector<MenuItem> MoveToItems(const TabNode* node,
                                  const std::vector<MoveToSpace>& spaces,
                                  bool favorites_full,
                                  std::string_view prefix);

struct RowMenuInput {
  RowMenuInput();
  RowMenuInput(const RowMenuInput&);
  RowMenuInput& operator=(const RowMenuInput&);
  ~RowMenuInput();

  TabNode node;
  bool off_pinned = false;
  std::vector<MoveToSpace> spaces;
  bool favorites_full = false;
  // Whether the page plays sound now: Mute is off while it is silent and not muted.
  bool audible = false;
  // A favorite's Show Unread Badge check.
  bool unread_badge = true;
  // The page's title carries an unread count ("(3) Inbox"): only then does a favorite's menu list Show Unread Badge
  //.
  bool has_unread = false;
  // macOS's sharing services for the row's link (Share ▸), by name; empty leaves Share with Copy Link only.
  std::vector<std::string> share_services;
  // Today rows the three bulk archive items would take.
  size_t above = 0;
  size_t below = 0;
  size_t others = 0;
};

// The row menu of a tab, in a fixed order for its kind. Favorite tiles have no Rename, anchored rows get Edit Pinned
// Page; Today rows have no Pin item (⌘D, a drag or Move to pins them) and end with Archive Tab and Archive Tabs Below,
// whose Option alternates are Archive Other Tabs and Archive Tabs Above.
std::vector<MenuItem> RowMenuItems(const RowMenuInput& input);
// The row menu of a private window's page: Copy Link, Rename, Duplicate, Mute and Close. A private window keeps no
// rows, so it has no Pin, Favorites, Move To or Archive.
std::vector<MenuItem> PrivateRowMenuItems(const std::string& url, bool muted);
// Archive N Tabs, Move N Tabs To, Add to Split View (2 to 4, while split view can take them), the two copies, Close N
// Tabs.
std::vector<MenuItem> MultiMenuItems(size_t count,
                                     const std::vector<MoveToSpace>& spaces,
                                     bool favorites_full,
                                     bool can_split);
// The folder menu: Share Folder…, New Nested Folder… while below the depth cap, Turn "name" into Space,
// the two link copies while the folder holds tabs, the icon items, Rename…, Duplicate, Move to (one item per space),
// Delete.
std::vector<MenuItem> FolderMenuItems(const TabNode& folder,
                                      bool can_nest,
                                      bool has_tabs,
                                      const std::vector<MoveToSpace>& spaces);
// New Tab, New Folder, and Paste and Go while the clipboard holds text.
std::vector<MenuItem> EmptyMenuItems(bool can_paste);
// The bottom bar's + menu: New Space, New Folder, New Split, New Tab.
std::vector<MenuItem> NewMenuItems();
// The narrow top row's "..." menu: Reload and Toggle Sidebar, the two controls it folds away.
std::vector<MenuItem> MoreMenuItems();

// Ids of the enabled leaves: only those may run.
std::vector<std::string> EnabledLeaves(const std::vector<MenuItem>& items);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_MENU_ITEMS_H_
