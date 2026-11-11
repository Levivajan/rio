// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_menu_items.h"

#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"

namespace clu {

namespace {

constexpr size_t kLabelMax = 60;

// A space or folder name as a menu label: at most 60 characters with an ellipsis, "Untitled" when empty.
std::string Label(std::string_view name) {
  std::u16string text = base::UTF8ToUTF16(name);
  if (text.empty()) {
    return "Untitled";
  }
  if (text.size() > kLabelMax) {
    text.resize(kLabelMax - 1);
    text += u"…";
  }
  return base::UTF16ToUTF8(text);
}

std::vector<MenuItem> IconItems(const std::optional<std::string>& icon) {
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item(kMenuSetIcon, "Change Icon…"));
  if (icon) {
    items.push_back(MenuItem::Item(kMenuRemoveIcon, "Remove Icon"));
  }
  return items;
}

std::string CountLabel(std::string_view verb, size_t n, std::string_view rest) {
  return base::StrCat({verb, " ", base::NumberToString(n), " Tabs", rest});
}

}  // namespace

MenuItem::MenuItem() = default;
MenuItem::MenuItem(const MenuItem&) = default;
MenuItem& MenuItem::operator=(const MenuItem&) = default;
MenuItem::MenuItem(MenuItem&&) = default;
MenuItem& MenuItem::operator=(MenuItem&&) = default;
MenuItem::~MenuItem() = default;

// static
MenuItem MenuItem::Item(std::string id, std::string label, bool enabled) {
  MenuItem item;
  item.id = std::move(id);
  item.label = std::move(label);
  item.enabled = enabled;
  return item;
}

// static
MenuItem MenuItem::Keyed(std::string id,
                         std::string label,
                         std::string command,
                         bool enabled) {
  MenuItem item = Item(std::move(id), std::move(label), enabled);
  item.keys = std::move(command);
  return item;
}

// static
MenuItem MenuItem::Alternate(std::string id, std::string label, bool enabled) {
  MenuItem item = Item(std::move(id), std::move(label), enabled);
  item.alternate = true;
  return item;
}

// static
MenuItem MenuItem::Check(std::string id,
                         std::string label,
                         bool checked,
                         bool enabled) {
  MenuItem item = Item(std::move(id), std::move(label), enabled);
  item.checked = checked;
  return item;
}

// static
MenuItem MenuItem::Separator() {
  MenuItem item;
  item.separator = true;
  return item;
}

// static
MenuItem MenuItem::Submenu(std::string id,
                           std::string label,
                           std::vector<MenuItem> items) {
  MenuItem item;
  item.id = std::move(id);
  item.label = std::move(label);
  item.submenu = std::move(items);
  return item;
}

MoveToSpace::MoveToSpace() = default;
MoveToSpace::MoveToSpace(const MoveToSpace&) = default;
MoveToSpace& MoveToSpace::operator=(const MoveToSpace&) = default;
MoveToSpace::~MoveToSpace() = default;

RowMenuInput::RowMenuInput() = default;
RowMenuInput::RowMenuInput(const RowMenuInput&) = default;
RowMenuInput& RowMenuInput::operator=(const RowMenuInput&) = default;
RowMenuInput::~RowMenuInput() = default;

std::string MoveToId(const MoveToPick& pick, std::string_view prefix) {
  switch (pick.kind) {
    case MoveToPick::Kind::kFavorite:
      return base::StrCat({prefix, ".favorite"});
    case MoveToPick::Kind::kFolder:
      return base::StrCat(
          {prefix, ":", pick.space_id, ":folder:", pick.folder_id});
    case MoveToPick::Kind::kPinned:
      return base::StrCat({prefix, ":", pick.space_id, ":pinned"});
    case MoveToPick::Kind::kToday:
      return base::StrCat({prefix, ":", pick.space_id, ":today"});
  }
}

std::optional<MoveToPick> ParseMoveTo(std::string_view id,
                                      std::string_view prefix) {
  if (id == base::StrCat({prefix, ".favorite"})) {
    return MoveToPick{.kind = MoveToPick::Kind::kFavorite};
  }
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      id, ":", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() < 3 || parts[0] != prefix || parts[1].empty()) {
    return std::nullopt;
  }
  MoveToPick pick;
  pick.space_id = std::string(parts[1]);
  if (parts.size() == 3 && parts[2] == "pinned") {
    pick.kind = MoveToPick::Kind::kPinned;
    return pick;
  }
  if (parts.size() == 3 && parts[2] == "today") {
    pick.kind = MoveToPick::Kind::kToday;
    return pick;
  }
  if (parts.size() == 4 && parts[2] == "folder" && !parts[3].empty()) {
    pick.kind = MoveToPick::Kind::kFolder;
    pick.folder_id = std::string(parts[3]);
    return pick;
  }
  return std::nullopt;
}

std::vector<MenuItem> MoveToItems(const TabNode* node,
                                  const std::vector<MoveToSpace>& spaces,
                                  bool favorites_full,
                                  std::string_view prefix) {
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item(
      MoveToId({.kind = MoveToPick::Kind::kFavorite}, prefix), "Favorites",
      !(node && node->section == Section::kFavorite) && !favorites_full));
  if (!spaces.empty()) {
    items.push_back(MenuItem::Separator());
  }
  for (const MoveToSpace& space : spaces) {
    const bool here = node && node->space_id == space.id;
    std::vector<MenuItem> sub;
    sub.push_back(MenuItem::Item(
        MoveToId({.kind = MoveToPick::Kind::kPinned, .space_id = space.id},
                 prefix),
        "Pinned",
        !(here && node->section == Section::kPinned &&
          node->parent_id.empty())));
    sub.push_back(MenuItem::Item(
        MoveToId({.kind = MoveToPick::Kind::kToday, .space_id = space.id},
                 prefix),
        "Today", !(here && node->section == Section::kToday)));
    if (!space.folders.empty()) {
      sub.push_back(MenuItem::Separator());
      for (const auto& [folder_id, name] : space.folders) {
        sub.push_back(MenuItem::Item(
            MoveToId({.kind = MoveToPick::Kind::kFolder,
                      .space_id = space.id,
                      .folder_id = folder_id},
                     prefix),
            Label(name), !node || node->parent_id != folder_id));
      }
    }
    items.push_back(MenuItem::Submenu(base::StrCat({prefix, ":", space.id}),
                                      Label(space.name), std::move(sub)));
  }
  return items;
}

std::vector<MenuItem> RowMoveToItems(const TabNode& node,
                                     const std::vector<MoveToSpace>& spaces) {
  std::vector<MenuItem> items;
  const MoveToPick::Kind kind = node.section == Section::kToday
                                    ? MoveToPick::Kind::kToday
                                    : MoveToPick::Kind::kPinned;
  // The row's own space is checked and stays enabled; picking it leaves the row.
  for (const MoveToSpace& space : spaces) {
    items.push_back(MenuItem::Check(
        MoveToId({.kind = kind, .space_id = space.id}, kMenuMoveTo),
        Label(space.name),
        node.section != Section::kFavorite && node.space_id == space.id));
  }
  if (!items.empty()) {
    items.push_back(MenuItem::Separator());
  }
  items.push_back(MenuItem::Item(kMenuMoveToNewSpace, "New Space…"));
  return items;
}

namespace {

std::vector<MenuItem> ShareItems(const RowMenuInput& input, bool has_link) {
  std::vector<MenuItem> items;
  for (size_t i = 0; i < input.share_services.size(); ++i) {
    items.push_back(MenuItem::Item(
        base::StrCat({kMenuShareService, base::NumberToString(i)}),
        input.share_services[i], has_link));
  }
  if (items.empty()) {
    items.push_back(MenuItem::Item(kMenuCopyLink, "Copy Link", has_link));
  }
  return items;
}

}  // namespace

std::vector<MenuItem> RowMenuItems(const RowMenuInput& input) {
  const TabNode& tab = input.node;
  const bool pinned = tab.section == Section::kPinned;
  const bool favorite = tab.section == Section::kFavorite;
  const bool has_link = !tab.url.empty() && tab.url != "about:blank";
  std::vector<MenuItem> items;
  items.push_back(pinned ? MenuItem::Keyed(kMenuCopyLink, "Copy Link",
                                           "edit.copyUrl", has_link)
                         : MenuItem::Item(kMenuCopyLink, "Copy Link", has_link));
  items.push_back(
      MenuItem::Submenu(kMenuShare, "Share", ShareItems(input, has_link)));
  items.push_back(MenuItem::Separator());
  if (favorite && input.has_unread) {
    items.push_back(MenuItem::Check(kMenuUnreadBadge, "Show Unread Badge",
                                    input.unread_badge));
    items.push_back(MenuItem::Separator());
  }
  for (MenuItem& item : IconItems(tab.custom_icon)) {
    items.push_back(std::move(item));
  }
  items.push_back(MenuItem::Item(kMenuRename, "Rename…"));
  items.push_back(MenuItem::Item(kMenuMute, tab.muted ? "Unmute" : "Mute",
                                 tab.muted || input.audible));
  if (!favorite) {
    items.push_back(
        MenuItem::Item(kMenuCustomize, "Customize Page…", has_link));
  }
  // A Today row has no Edit Pinned Page.
  if (tab.pinned_url && tab.section != Section::kToday) {
    std::vector<MenuItem> sub;
    sub.push_back(MenuItem::Item(kMenuPinnedUrlCurrent,
                                 "Replace Pinned URL with Current",
                                 input.off_pinned));
    sub.push_back(MenuItem::Item(kMenuPinnedUrlEnter, "Edit…"));
    items.push_back(
        MenuItem::Submenu(kMenuPinnedUrl, "Edit Pinned Page", std::move(sub)));
  }
  items.push_back(MenuItem::Separator());
  items.push_back(MenuItem::Item(
      kMenuOpenInSplit, pinned ? "Add Split View" : "Open in Split View"));
  items.push_back(MenuItem::Item(kMenuDuplicate, "Duplicate"));
  items.push_back(MenuItem::Submenu(kMenuMoveTo, "Move to",
                                    RowMoveToItems(tab, input.spaces)));
  items.push_back(MenuItem::Separator());
  if (pinned) {
    items.push_back(MenuItem::Item(kMenuPin, "Remove Pin"));
    return items;
  }
  if (favorite) {
    items.push_back(MenuItem::Item(kMenuFavorite, "Remove Favorite"));
    return items;
  }
  items.push_back(MenuItem::Item(kMenuArchive, "Archive Tab"));
  items.push_back(MenuItem::Alternate(kMenuArchiveOthers, "Archive Other Tabs",
                                      input.others > 0));
  items.push_back(MenuItem::Item(kMenuArchiveBelow, "Archive Tabs Below",
                                 input.below > 0));
  items.push_back(MenuItem::Alternate(kMenuArchiveAbove, "Archive Tabs Above",
                                      input.above > 0));
  return items;
}

std::vector<MenuItem> PrivateRowMenuItems(const std::string& url, bool muted) {
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item(kMenuCopyLink, "Copy Link",
                                 !url.empty() && url != "about:blank"));
  // The normal rows' labels.
  items.push_back(MenuItem::Item(kMenuRename, "Rename…"));
  items.push_back(MenuItem::Item(kMenuDuplicate, "Duplicate"));
  items.push_back(MenuItem::Item(kMenuMute, muted ? "Unmute" : "Mute"));
  items.push_back(MenuItem::Separator());
  items.push_back(MenuItem::Item(kMenuClose, "Close"));
  return items;
}

std::vector<MenuItem> MultiMenuItems(size_t count,
                                     const std::vector<MoveToSpace>& spaces,
                                     bool favorites_full,
                                     bool can_split) {
  std::vector<MenuItem> items;
  items.push_back(
      MenuItem::Item(kMenuBulkArchive, CountLabel("Archive", count, "")));
  items.push_back(MenuItem::Submenu(
      kMenuBulkMoveTo, CountLabel("Move", count, " To"),
      MoveToItems(nullptr, spaces, favorites_full, kMenuBulkMoveTo)));
  items.push_back(MenuItem::Item(
      kMenuBulkSplit, "Add to Split View",
      can_split && count >= 2 && count <= kMaxSplitTabs));
  items.push_back(MenuItem::Separator());
  items.push_back(MenuItem::Item(kMenuBulkCopy, "Copy URLs"));
  items.push_back(MenuItem::Item(kMenuBulkCopyMarkdown, "Copy URLs as Markdown"));
  items.push_back(MenuItem::Separator());
  items.push_back(
      MenuItem::Item(kMenuBulkClose, CountLabel("Close", count, "")));
  return items;
}

std::vector<MenuItem> FolderMenuItems(const TabNode& folder,
                                      bool can_nest,
                                      bool has_tabs,
                                      const std::vector<MoveToSpace>& spaces) {
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item(kMenuFolderShare, "Share Folder…", has_tabs));
  items.push_back(MenuItem::Separator());
  if (can_nest) {
    items.push_back(
        MenuItem::Item(kMenuNewFolderInside, "New Nested Folder…"));
  }
  items.push_back(MenuItem::Item(
      kMenuFolderToSpace,
      base::StrCat({"Turn “", Label(folder.title_override.value_or("")),
                    "” into Space"})));
  items.push_back(
      MenuItem::Item(kMenuFolderCopyLinks, "Copy All Links", has_tabs));
  // The Option alternate of Copy All Links.
  items.push_back(MenuItem::Alternate(kMenuFolderCopyLinksMarkdown,
                                      "Copy All Links as Markdown", has_tabs));
  items.push_back(MenuItem::Separator());
  for (MenuItem& item : IconItems(folder.custom_icon)) {
    items.push_back(std::move(item));
  }
  items.push_back(MenuItem::Item(kMenuRename, "Rename…"));
  items.push_back(MenuItem::Separator());
  items.push_back(MenuItem::Item(kMenuFolderDuplicate, "Duplicate"));
  items.push_back(MenuItem::Submenu(kMenuMoveTo, "Move to",
                                    RowMoveToItems(folder, spaces)));
  items.push_back(MenuItem::Separator());
  items.push_back(MenuItem::Item(kMenuDeleteFolder, "Delete"));
  return items;
}

std::vector<MenuItem> EmptyMenuItems(bool can_paste) {
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item(kMenuNewTab, "New Tab"));
  items.push_back(MenuItem::Item(kMenuNewFolder, "New Folder"));
  items.push_back(MenuItem::Item(kMenuPasteAndGo, "Paste and Go", can_paste));
  return items;
}

std::vector<MenuItem> NewMenuItems() {
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item(kMenuNewSpace, "New Space"));
  items.push_back(MenuItem::Item(kMenuNewFolder, "New Folder"));
  items.push_back(MenuItem::Separator());
  items.push_back(MenuItem::Keyed(kMenuNewSplit, "New Split", "split.add"));
  items.push_back(
      MenuItem::Keyed(kMenuNewTab, "New Tab", "commandbar.newTab"));
  return items;
}

std::vector<MenuItem> MoreMenuItems() {
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Keyed(kMenuReload, "Reload", "nav.reload"));
  items.push_back(MenuItem::Keyed(kMenuToggleSidebar, "Toggle Sidebar",
                                  "sidebar.toggle"));
  return items;
}

std::vector<std::string> EnabledLeaves(const std::vector<MenuItem>& items) {
  std::vector<std::string> out;
  for (const MenuItem& item : items) {
    if (item.separator || !item.enabled) {
      continue;
    }
    if (!item.submenu.empty()) {
      std::vector<std::string> inner = EnabledLeaves(item.submenu);
      out.insert(out.end(), inner.begin(), inner.end());
    } else {
      out.push_back(item.id);
    }
  }
  return out;
}

}  // namespace clu
