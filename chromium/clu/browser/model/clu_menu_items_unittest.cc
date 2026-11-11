// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_menu_items.h"

#include <algorithm>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// The PLAN 8.2 menus tabs owns (src/main/tabs/rowMenu.test.ts): which items each row kind gets, their enabled state,
// and Move To in two submenu levels.
namespace clu {
namespace {

const MenuItem* Find(const std::vector<MenuItem>& items, std::string_view id) {
  for (const MenuItem& item : items) {
    if (item.id == id) {
      return &item;
    }
    if (const MenuItem* inner = Find(item.submenu, id)) {
      return inner;
    }
  }
  return nullptr;
}

std::vector<MoveToSpace> Spaces() {
  MoveToSpace home;
  home.id = "home";
  home.name = "Home";
  home.folders = {{"f1", "Reading"}};
  MoveToSpace work;
  work.id = "work";
  work.name = "Work";
  return {home, work};
}

RowMenuInput Input(Section section) {
  RowMenuInput input;
  input.node.id = "t";
  input.node.section = section;
  input.node.space_id = section == Section::kFavorite ? "" : "home";
  input.node.url = "https://a.test/";
  if (section != Section::kToday) {
    input.node.pinned_url = "https://a.test/";
  }
  input.spaces = Spaces();
  return input;
}

TEST(CluMenuItemsTest, TodayRowMenu) {
  RowMenuInput input = Input(Section::kToday);
  input.above = 1;
  const std::vector<MenuItem> items = RowMenuItems(input);
  EXPECT_TRUE(Find(items, kMenuRename));
  // The Today menu has no Pin item, and no Close or Add to Favorites.
  EXPECT_FALSE(Find(items, kMenuPin));
  EXPECT_FALSE(Find(items, kMenuClose));
  EXPECT_FALSE(Find(items, kMenuFavorite));
  EXPECT_FALSE(Find(items, kMenuPinnedUrl));
  EXPECT_TRUE(Find(items, kMenuSetIcon));
  EXPECT_TRUE(Find(items, kMenuArchiveAbove)->enabled);
  EXPECT_FALSE(Find(items, kMenuArchiveBelow)->enabled);
  EXPECT_FALSE(Find(items, kMenuArchiveOthers)->enabled);
}

TEST(CluMenuItemsTest, PinnedRowMenuHasPinnedUrlAndIconItems) {
  RowMenuInput input = Input(Section::kPinned);
  input.above = 3;
  input.node.custom_icon = "\xF0\x9F\x98\x80";
  const std::vector<MenuItem> items = RowMenuItems(input);
  EXPECT_EQ(Find(items, kMenuPin)->label, "Remove Pin");
  ASSERT_TRUE(Find(items, kMenuPinnedUrl));
  EXPECT_FALSE(Find(items, kMenuPinnedUrlCurrent)->enabled);
  EXPECT_TRUE(Find(items, kMenuRemoveIcon));
}

std::vector<std::string> Labels(const std::vector<MenuItem>& items) {
  std::vector<std::string> labels;
  for (const MenuItem& item : items) {
    labels.push_back(item.separator ? "-" : item.label);
  }
  return labels;
}

// Each row kind's menu: its items, in its order, with its separators. Share lists macOS's sharing services; a
// favorite's Show Unread Badge is a check item. Option alternates sit right after the item they replace and are marked
// as such.
TEST(CluMenuItemsTest, PinnedRowMenuListsItsItemsInOrder) {
  EXPECT_EQ(Labels(RowMenuItems(Input(Section::kPinned))),
            (std::vector<std::string>{
                "Copy Link", "Share", "-", "Change Icon…", "Rename…", "Mute",
                "Customize Page…", "Edit Pinned Page", "-", "Add Split View",
                "Duplicate", "Move to", "-", "Remove Pin"}));
  // Copy Link shows the copy key.
  EXPECT_EQ(Find(RowMenuItems(Input(Section::kPinned)), kMenuCopyLink)->keys,
            "edit.copyUrl");
  // Edit Pinned Page: replace with the current page (only after navigating), or edit.
  const std::vector<MenuItem> pinned = RowMenuItems(Input(Section::kPinned));
  const MenuItem* edit = Find(pinned, kMenuPinnedUrl);
  ASSERT_TRUE(edit);
  EXPECT_EQ(Labels(edit->submenu),
            (std::vector<std::string>{"Replace Pinned URL with Current",
                                      "Edit…"}));
  // Move to lists the spaces (the row's own checked and enabled), then New Space….
  const MenuItem* move = Find(pinned, kMenuMoveTo);
  ASSERT_TRUE(move);
  EXPECT_EQ(Labels(move->submenu),
            (std::vector<std::string>{"Home", "Work", "-", "New Space…"}));
  EXPECT_TRUE(move->submenu[0].enabled);
  EXPECT_TRUE(move->submenu[0].checked);
  EXPECT_FALSE(move->submenu[1].checked);
  EXPECT_EQ(move->submenu[1].id, "tabs.moveTo:work:pinned");
  EXPECT_FALSE(Find(pinned, kMenuFavorite));
}

TEST(CluMenuItemsTest, ShareListsTheServicesOrCopyLink) {
  RowMenuInput input = Input(Section::kToday);
  EXPECT_EQ(Labels(Find(RowMenuItems(input), kMenuShare)->submenu),
            (std::vector<std::string>{"Copy Link"}));
  input.share_services = {"Mail", "Messages"};
  const std::vector<MenuItem> items = RowMenuItems(input);
  const MenuItem* share = Find(items, kMenuShare);
  EXPECT_EQ(Labels(share->submenu),
            (std::vector<std::string>{"Mail", "Messages"}));
  EXPECT_EQ(share->submenu[1].id, "tabs.share:1");
}

TEST(CluMenuItemsTest, TodayRowMenuListsItsItemsInOrder) {
  EXPECT_EQ(Labels(RowMenuItems(Input(Section::kToday))),
            (std::vector<std::string>{
                "Copy Link", "Share", "-", "Change Icon…", "Rename…", "Mute",
                "Customize Page…", "-", "Open in Split View", "Duplicate",
                "Move to", "-", "Archive Tab", "Archive Other Tabs",
                "Archive Tabs Below", "Archive Tabs Above"}));
  const std::vector<MenuItem> items = RowMenuItems(Input(Section::kToday));
  EXPECT_FALSE(Find(items, kMenuArchive)->alternate);
  EXPECT_TRUE(Find(items, kMenuArchiveOthers)->alternate);
  EXPECT_FALSE(Find(items, kMenuArchiveBelow)->alternate);
  EXPECT_TRUE(Find(items, kMenuArchiveAbove)->alternate);
  // A Today row moves to another space's Today.
  EXPECT_EQ(Find(items, kMenuMoveTo)->submenu[1].id, "tabs.moveTo:work:today");
  // A Today row that came from a pinned page still has no Edit Pinned Page.
  RowMenuInput was_pinned = Input(Section::kToday);
  was_pinned.node.pinned_url = "https://a.test/";
  EXPECT_FALSE(Find(RowMenuItems(was_pinned), kMenuPinnedUrl));
}

TEST(CluMenuItemsTest, FavoriteMenuListsItsItemsInOrder) {
  // A favorite whose page shows no unread count has no Show Unread Badge.
  const std::vector<MenuItem> items = RowMenuItems(Input(Section::kFavorite));
  EXPECT_EQ(Labels(items),
            (std::vector<std::string>{
                "Copy Link", "Share", "-", "Change Icon…", "Rename…", "Mute",
                "Edit Pinned Page", "-", "Open in Split View", "Duplicate",
                "Move to", "-", "Remove Favorite"}));
  // Every space takes a favorite (into its Pinned), and none is checked.
  EXPECT_TRUE(Find(items, kMenuMoveTo)->submenu[0].enabled);
  EXPECT_FALSE(Find(items, kMenuMoveTo)->submenu[0].checked);
  // One with an unread count ("(3) Inbox") has it after Share, checked until it is turned off.
  RowMenuInput mail = Input(Section::kFavorite);
  mail.has_unread = true;
  const std::vector<MenuItem> mail_items = RowMenuItems(mail);
  EXPECT_EQ(Labels(mail_items),
            (std::vector<std::string>{
                "Copy Link", "Share", "-", "Show Unread Badge", "-",
                "Change Icon…", "Rename…", "Mute", "Edit Pinned Page", "-",
                "Open in Split View", "Duplicate", "Move to", "-",
                "Remove Favorite"}));
  EXPECT_TRUE(Find(mail_items, kMenuUnreadBadge)->checked);
  RowMenuInput off = mail;
  off.unread_badge = false;
  EXPECT_FALSE(Find(RowMenuItems(off), kMenuUnreadBadge)->checked);
  RowMenuInput pinned = Input(Section::kPinned);
  pinned.has_unread = true;
  EXPECT_FALSE(Find(RowMenuItems(pinned), kMenuUnreadBadge));
  RowMenuInput today = Input(Section::kToday);
  today.has_unread = true;
  EXPECT_FALSE(Find(RowMenuItems(today), kMenuUnreadBadge));
}

// Mute is off while the page is silent; a muted page can always be unmuted.
TEST(CluMenuItemsTest, MuteFollowsTheSound) {
  RowMenuInput input = Input(Section::kToday);
  EXPECT_FALSE(Find(RowMenuItems(input), kMenuMute)->enabled);
  input.audible = true;
  EXPECT_TRUE(Find(RowMenuItems(input), kMenuMute)->enabled);
  input.audible = false;
  input.node.muted = true;
  EXPECT_EQ(Find(RowMenuItems(input), kMenuMute)->label, "Unmute");
  EXPECT_TRUE(Find(RowMenuItems(input), kMenuMute)->enabled);
}

// Fix pass (docs/chromium/fixes.md, leftovers 5): a private window's row menu renames, copies, duplicates, mutes and
// closes; it has nothing that pins, moves to a space or archives.
TEST(CluMenuItemsTest, PrivateRowMenuStaysFlat) {
  const std::vector<MenuItem> items =
      PrivateRowMenuItems("https://a.test/", /*muted=*/true);
  for (const char* id :
       {kMenuCopyLink, kMenuRename, kMenuDuplicate, kMenuMute, kMenuClose}) {
    ASSERT_TRUE(Find(items, id)) << id;
    EXPECT_TRUE(Find(items, id)->enabled) << id;
  }
  EXPECT_EQ(Find(items, kMenuMute)->label, "Unmute");
  EXPECT_EQ(Find(items, kMenuRename)->label, "Rename…");
  for (const char* id : {kMenuPin, kMenuFavorite, kMenuMoveTo, kMenuToNewWindow,
                         kMenuArchive, kMenuSetIcon}) {
    EXPECT_FALSE(Find(items, id)) << id;
  }
  EXPECT_FALSE(
      Find(PrivateRowMenuItems("about:blank", false), kMenuCopyLink)->enabled);
  EXPECT_EQ(Find(PrivateRowMenuItems("", false), kMenuMute)->label, "Mute");
}

TEST(CluMenuItemsTest, BulkMoveToReachesPinnedTodayAndFoldersInTwoLevels) {
  const std::vector<MenuItem> items =
      MoveToItems(nullptr, Spaces(), /*favorites_full=*/false, kMenuBulkMoveTo);
  const MenuItem* home = Find(items, "tabs.bulk.moveTo:home");
  ASSERT_TRUE(home);
  ASSERT_FALSE(home->submenu.empty());
  EXPECT_EQ(home->submenu[0].label, "Pinned");
  EXPECT_TRUE(Find(home->submenu, "tabs.bulk.moveTo:home:folder:f1"));
  EXPECT_TRUE(Find(items, "tabs.bulk.moveTo:work:today")->enabled);
}

TEST(CluMenuItemsTest, MoveToIdsRoundTrip) {
  for (const MoveToPick& pick :
       {MoveToPick{.kind = MoveToPick::Kind::kFavorite},
        MoveToPick{.kind = MoveToPick::Kind::kPinned, .space_id = "s"},
        MoveToPick{.kind = MoveToPick::Kind::kToday, .space_id = "s"},
        MoveToPick{.kind = MoveToPick::Kind::kFolder,
                   .space_id = "s",
                   .folder_id = "f"}}) {
    const std::optional<MoveToPick> parsed =
        ParseMoveTo(MoveToId(pick, kMenuMoveTo), kMenuMoveTo);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->kind, pick.kind);
    EXPECT_EQ(parsed->space_id, pick.space_id);
    EXPECT_EQ(parsed->folder_id, pick.folder_id);
  }
  EXPECT_FALSE(ParseMoveTo("tabs.moveTo:s:other", kMenuMoveTo));
  EXPECT_FALSE(ParseMoveTo("tabs.bulk.moveTo:s:today", kMenuMoveTo));
}

TEST(CluMenuItemsTest, MultiSelectAndFolderAndEmptyMenus) {
  const std::vector<MenuItem> multi = MultiMenuItems(5, Spaces(), false, true);
  EXPECT_EQ(Find(multi, kMenuBulkArchive)->label, "Archive 5 Tabs");
  EXPECT_FALSE(Find(multi, kMenuBulkSplit)->enabled);
  EXPECT_TRUE(MultiMenuItems(3, Spaces(), false, true)[2].enabled);
  // Off when the selected tabs span spaces (clu_tab_menu.cc).
  EXPECT_FALSE(MultiMenuItems(3, Spaces(), false, false)[2].enabled);

  TabNode folder;
  folder.kind = NodeKind::kFolder;
  folder.section = Section::kPinned;
  folder.title_override = "Rio folder";
  EXPECT_TRUE(Find(FolderMenuItems(folder, true, true, Spaces()),
                   kMenuNewFolderInside));
  EXPECT_FALSE(Find(FolderMenuItems(folder, false, true, Spaces()),
                    kMenuNewFolderInside));
  // The folder menu, item by item.
  EXPECT_EQ(Labels(FolderMenuItems(folder, true, true, Spaces())),
            (std::vector<std::string>{
                "Share Folder…", "-", "New Nested Folder…",
                "Turn “Rio folder” into Space", "Copy All Links",
                "Copy All Links as Markdown", "-", "Change Icon…", "Rename…",
                "-", "Duplicate", "Move to", "-", "Delete"}));
  EXPECT_FALSE(Find(FolderMenuItems(folder, true, false, Spaces()),
                    kMenuFolderCopyLinks)
                   ->enabled);
  // Copy All Links as Markdown is the Option alternate of Copy All Links.
  EXPECT_TRUE(Find(FolderMenuItems(folder, true, true, Spaces()),
                   kMenuFolderCopyLinksMarkdown)
                  ->alternate);
  EXPECT_FALSE(
      Find(FolderMenuItems(folder, true, false, Spaces()), kMenuFolderShare)
          ->enabled);

  EXPECT_FALSE(Find(EmptyMenuItems(false), kMenuPasteAndGo)->enabled);
  EXPECT_EQ(Labels(NewMenuItems()),
            (std::vector<std::string>{"New Space", "New Folder", "-",
                                      "New Split", "New Tab"}));
  EXPECT_EQ(Find(NewMenuItems(), kMenuNewTab)->keys, "commandbar.newTab");
  EXPECT_EQ(Labels(MoreMenuItems()),
            (std::vector<std::string>{"Reload", "Toggle Sidebar"}));
  EXPECT_EQ(Find(MoreMenuItems(), kMenuToggleSidebar)->keys, "sidebar.toggle");
}

TEST(CluMenuItemsTest, OnlyEnabledLeavesRun) {
  const std::vector<std::string> leaves =
      EnabledLeaves(RowMenuItems(Input(Section::kToday)));
  EXPECT_TRUE(std::ranges::contains(leaves, std::string(kMenuArchive)));
  EXPECT_FALSE(std::ranges::contains(leaves, std::string(kMenuArchiveBelow)));
  EXPECT_FALSE(std::ranges::contains(leaves, std::string(kMenuMoveTo)));
  // The row's own space is checked and enabled; the menu runs it as a no-op.
  EXPECT_TRUE(std::ranges::contains(leaves, std::string("tabs.moveTo:home:today")));
}

}  // namespace
}  // namespace clu
