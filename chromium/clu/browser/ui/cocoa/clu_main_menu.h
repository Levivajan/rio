// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_COCOA_CLU_MAIN_MENU_H_
#define CLU_BROWSER_UI_COCOA_CLU_MAIN_MENU_H_

#include <optional>
#include <string>
#include <string_view>

#if defined(__OBJC__)
@class NSMenu;
#endif

// Clu's Mac main menu (docs/chromium/PLAN.md 1.3 "Keys", docs/chromium/m5.md section 1). Chromium builds its menu
// (chrome/browser/ui/cocoa/main_menu_builder.mm) and hands it here before it becomes NSApp.mainMenu, so Chromium's
// own wiring stays: the app menu, Edit, the History menu bridge, the Window list, Services, AppController's
// no-window commands.
namespace clu {

#if defined(__OBJC__)
// Gives `main_menu` the docs/PLAN.md 8 key table:
// - every table command of the main variant gets an item in its menu (File, Edit, View, History, Tabs, Spaces,
//   Window, Help) carrying its chord: Chromium's item when the command is a Chromium command (its title becomes
//   Clu's outside the app menu), else a new commandDispatch: item tagged with its IDC_CLU_* id;
// - a Chromium item whose chord the table uses for another command loses its chord;
// - the Tab menu becomes Clu's Tabs menu, a Spaces menu follows it, and the Bookmarks menu and the toolbar and tab
//   strip toggles Clu's layout has no use for are hidden or removed.
// Alternate chords are hidden items, so Chromium's key lookup (CommandForKeyEvent) finds them.
void ApplyMainMenu(NSMenu* main_menu);

// The user's custom shortcuts (Settings, Shortcuts, clu/browser/commands/clu_shortcuts.h) on the table's items: each
// item's chord and hidden alternates again, and a Chromium item that has a chord the user gave a table command loses
// it. ApplyMainMenu() subscribes it to every change.
void ApplyShortcuts(NSMenu* main_menu);

// Runs the Spaces menu's rows against the window in front (titles, hidden rows) and hides the Spaces menu itself
// when no row is left, as in a private window. ApplyMainMenu() calls it whenever the main window changes.
void UpdateSpacesMenu(NSMenu* main_menu);

// Validates every item of `main_menu`'s submenus. -[NSMenu update], which Chromium's key lookup
// (cr_menuItemForKeyEquivalentEvent) calls on the main menu, validates only the main menu's own items, so a submenu
// item kept whatever state it had when its menu was last open: a command that cannot run now still matched its chord
// and one that can was skipped. AppController's pre-search block calls this before each lookup.
void UpdateMenuItems(NSMenu* main_menu);
#endif

// The title the Spaces menu shows for `command_id` now (the space name for spaces.gotoN), or nullopt for the table's
// title. //clu/browser/ui/webui sets it, since only it knows the windows' spaces.
using MenuTitleProvider =
    std::optional<std::u16string> (*)(std::string_view command_id);
void SetMenuTitleProvider(MenuTitleProvider provider);

// Whether the menu checks `command_id` now (View ▸ Appearance ▸ the mode in use), or nullopt for no check.
// //clu/browser/ui/webui sets it, since only it reads the app settings.
using MenuCheckProvider = std::optional<bool> (*)(std::string_view command_id);
void SetMenuCheckProvider(MenuCheckProvider provider);

}  // namespace clu

#endif  // CLU_BROWSER_UI_COCOA_CLU_MAIN_MENU_H_
