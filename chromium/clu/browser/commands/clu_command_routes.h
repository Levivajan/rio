// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_COMMANDS_CLU_COMMAND_ROUTES_H_
#define CLU_BROWSER_COMMANDS_CLU_COMMAND_ROUTES_H_

#include <string_view>

#include "clu/browser/commands/command_table.h"

// How the docs/PLAN.md 8 key table meets Chromium's commands (docs/chromium/m5.md section 1). Three maps, each keyed
// the other way round from the next, kept apart so no command can bounce between Clu and Chromium:
//
// - CluCommandForChromeCommand(): Chromium commands a Clu window replaces with a Clu command. Cmd+T is Chromium's
//   IDC_NEW_TAB on every path Chromium owns (the Dock menu, AppleScript, an extension's chrome.tabs keys), so in a
//   Clu window IDC_NEW_TAB opens the command bar.
// - ChromeCommandFor(): table commands Clu runs as a Chromium command (Find, Zoom, Print, Settings and the other
//   native features of docs/chromium/PLAN.md section 4).
// - MenuTagFor(): the tag of a table command's item in the Mac main menu. A Chromium command keeps its Chromium tag,
//   so its existing item, its no-window handling in AppController and its validation keep working.
namespace clu {

// The table entry with the TS id `id`, or null.
const CommandSpec* FindCommand(std::string_view id);
// The table entry with IDC_CLU_* id `idc`, or null.
const CommandSpec* FindCommandByIdc(int idc);

// The Clu command a Chromium command id runs in a Clu window, or empty when Chromium runs its own.
std::string_view CluCommandForChromeCommand(int command_id);

// The Chromium command a table command runs as, or 0 when Clu runs it (or a later milestone will).
int ChromeCommandFor(std::string_view id);

// The table command a Chromium page command id is (Back, Forward, Reload, Stop, Find, Zoom), or empty. A focused Peek
// takes these from Chromium's menu items and their key equivalents instead of the tab under it (docs/chromium/m6.md).
std::string_view PageCommandForChromeCommand(int command_id);

// The tag of `spec`'s main menu item: its Chromium command, the Chromium command it replaces, else its IDC_CLU_* id.
int MenuTagFor(const CommandSpec& spec);

// A chord the page never sees: route kRouter, plus Next and Previous Space (docs/chromium/m4.md section 5:
// Chromium already reserved Cmd+Option+Left and Right, and 12.7 says the page never sees the space keys).
bool IsReservedEntry(const CommandSpec& spec);

// An alternate chord Chromium already runs as the same command, and only once a page has left the key alone:
// Cmd+Left and Cmd+Right for Back and Forward (DelayedWebContentsCommandForKeyEvent() in
// global_keyboard_shortcuts_mac.mm). Clu binds neither a key nor a menu item for them, so text fields, the find bar
// included, keep Cmd+Left and Cmd+Right (docs/PLAN.md 8).
bool IsPageOnlyAlt(const CommandSpec& spec, const CommandAlt& alt);

// BrowserCommandController::IsReservedCommandOrKey() for a Clu window: the menu tag or replaced Chromium command of
// a reserved entry. Chromium's own reserved commands (kReservedCommandIds) stay reserved on their own.
bool IsReservedChromeCommand(int command_id);

}  // namespace clu

#endif  // CLU_BROWSER_COMMANDS_CLU_COMMAND_ROUTES_H_
