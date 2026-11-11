// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/commands/clu_command_routes.h"

#include <algorithm>

#include "chrome/app/chrome_command_ids.h"

namespace clu {

namespace {

struct IdcAndId {
  int idc;
  std::string_view id;
};

// Chromium commands a Clu window runs as Clu commands. The first ones are the chords Chromium binds outside the
// main menu (global_keyboard_shortcuts_mac.mm) and the ids M2 and M3 already routed; Clu's own main menu and key
// table (CluShell) reach Clu first, so these matter for Chromium's other paths: the Dock menu, AppleScript, the
// hidden Ctrl+PageDown shortcuts, chrome::ExecuteCommand() callers.
constexpr IdcAndId kReplaced[] = {
    {IDC_NEW_TAB, "commandbar.newTab"},
    {IDC_FOCUS_LOCATION, "commandbar.editUrl"},
    {IDC_CLOSE_TAB, "tabs.close"},
    {IDC_RESTORE_TAB, "archive.reopenClosed"},
    {IDC_NEW_INCOGNITO_WINDOW, "window.newPrivate"},
    {IDC_SELECT_TAB_0, "tabs.goto1"},
    {IDC_SELECT_TAB_1, "tabs.goto2"},
    {IDC_SELECT_TAB_2, "tabs.goto3"},
    {IDC_SELECT_TAB_3, "tabs.goto4"},
    {IDC_SELECT_TAB_4, "tabs.goto5"},
    {IDC_SELECT_TAB_5, "tabs.goto6"},
    {IDC_SELECT_TAB_6, "tabs.goto7"},
    {IDC_SELECT_TAB_7, "tabs.goto8"},
    // Cmd+9 is the 9th row, never the last (02-tabs F12).
    {IDC_SELECT_LAST_TAB, "tabs.goto9"},
    // Ctrl+Tab: the recent-tab switcher (02-tabs F13).
    {IDC_CYCLE_TO_NEXT_TAB, "archive.mruNext"},
    {IDC_CYCLE_TO_PREV_TAB, "archive.mruPrev"},
    // Cmd+Option+Down and Up, Cmd+Shift+] and [, Ctrl+PageDown and PageUp.
    {IDC_FOCUS_NEXT_PANE, "tabs.next"},
    {IDC_FOCUS_PREVIOUS_PANE, "tabs.prev"},
    {IDC_SELECT_NEXT_TAB, "tabs.next"},
    {IDC_SELECT_PREVIOUS_TAB, "tabs.prev"},
    // Chromium's Duplicate Tab (Cmd+D, Cmd+Shift+D and Cmd+Shift+M are Clu's own menu items now, so bookmarking and
    // the avatar menu stay Chromium's).
    {IDC_DUPLICATE_TAB, "tabs.duplicate"},
    // Cmd+Shift+J and Window > Downloads: Chromium's downloads bubble on CluAnchorView, or chrome://downloads when
    // the bubble has nothing to list (docs/chromium/m9.md); Chromium's own command opens only the page.
    {IDC_SHOW_DOWNLOADS, "downloads.open"},
    // M8: History focuses or opens chrome://history in the window's space, Settings opens Clu's settings page, Clear
    // Browsing Data opens Clu's dialog, which also clears archive rows (docs/chromium/m8.md).
    {IDC_SHOW_HISTORY, "history.open"},
    {IDC_OPTIONS, "settings.open"},
    {IDC_CLEAR_BROWSING_DATA, "browsingData.open"},
};

// Table commands that are Chromium's own features (docs/chromium/PLAN.md section 4).
constexpr IdcAndId kNative[] = {
    {IDC_NEW_WINDOW, "window.new"},
    {IDC_CLOSE_WINDOW, "window.close"},
    {IDC_FIND, "find.open"},
    {IDC_FIND_NEXT, "find.next"},
    {IDC_FIND_PREVIOUS, "find.prev"},
    {IDC_ZOOM_PLUS, "zoom.in"},
    {IDC_ZOOM_MINUS, "zoom.out"},
    {IDC_ZOOM_NORMAL, "zoom.reset"},
    {IDC_PRINT, "page.print"},
    {IDC_SAVE_PAGE, "page.savePage"},
    {IDC_SHARING_HUB_SCREENSHOT, "page.capture"},
    {IDC_DEV_TOOLS, "page.devtools"},
    {IDC_DEV_TOOLS_CONSOLE, "page.console"},
    {IDC_SHOW_PASSWORD_MANAGER, "passwords.open"},
    {IDC_OPEN_FILE, "platform.openFile"},
    {IDC_MANAGE_EXTENSIONS, "extensions.open"},
    {IDC_ABOUT, "platform.aboutPage"},
};

// Clu commands whose menu item is the Chromium item that does the same: the replaced commands above, and the page
// commands, which Chromium's own History and View items already run on the active tab.
constexpr IdcAndId kMenuTags[] = {
    {IDC_NEW_TAB, "commandbar.newTab"},
    {IDC_FOCUS_LOCATION, "commandbar.editUrl"},
    {IDC_CLOSE_TAB, "tabs.close"},
    {IDC_RESTORE_TAB, "archive.reopenClosed"},
    {IDC_NEW_INCOGNITO_WINDOW, "window.newPrivate"},
    {IDC_BACK, "nav.back"},
    {IDC_FORWARD, "nav.forward"},
    {IDC_RELOAD, "nav.reload"},
    {IDC_RELOAD_BYPASSING_CACHE, "nav.hardReload"},
    {IDC_STOP, "nav.stop"},
    {IDC_SHOW_DOWNLOADS, "downloads.open"},
    {IDC_SHOW_HISTORY, "history.open"},
    {IDC_OPTIONS, "settings.open"},
    {IDC_CLEAR_BROWSING_DATA, "browsingData.open"},
};

// Chromium commands that act on the focused page: the History and View menus run them on the active tab, and a
// focused Peek takes them instead (docs/chromium/m6.md).
constexpr IdcAndId kPageCommands[] = {
    {IDC_BACK, "nav.back"},
    {IDC_FORWARD, "nav.forward"},
    {IDC_RELOAD, "nav.reload"},
    {IDC_RELOAD_BYPASSING_CACHE, "nav.hardReload"},
    {IDC_STOP, "nav.stop"},
    {IDC_FIND, "find.open"},
    {IDC_FIND_NEXT, "find.next"},
    {IDC_FIND_PREVIOUS, "find.prev"},
    {IDC_ZOOM_PLUS, "zoom.in"},
    {IDC_ZOOM_MINUS, "zoom.out"},
    {IDC_ZOOM_NORMAL, "zoom.reset"},
};

template <size_t N>
int IdcOf(const IdcAndId (&map)[N], std::string_view id) {
  const auto it = std::ranges::find(map, id, &IdcAndId::id);
  return it == std::end(map) ? 0 : it->idc;
}

}  // namespace

const CommandSpec* FindCommand(std::string_view id) {
  const base::span<const CommandSpec> table = CommandTable();
  const auto it = std::ranges::find_if(
      table, [id](const CommandSpec& spec) { return id == spec.id; });
  return it == table.end() ? nullptr : &*it;
}

const CommandSpec* FindCommandByIdc(int idc) {
  if (idc < IDC_CLU_FIRST || idc > IDC_CLU_LAST) {
    return nullptr;
  }
  const base::span<const CommandSpec> table = CommandTable();
  const auto it = std::ranges::find(table, idc, &CommandSpec::idc);
  return it == table.end() ? nullptr : &*it;
}

std::string_view CluCommandForChromeCommand(int command_id) {
  const auto it = std::ranges::find(kReplaced, command_id, &IdcAndId::idc);
  return it == std::end(kReplaced) ? std::string_view() : it->id;
}

std::string_view PageCommandForChromeCommand(int command_id) {
  const auto it = std::ranges::find(kPageCommands, command_id, &IdcAndId::idc);
  return it == std::end(kPageCommands) ? std::string_view() : it->id;
}

int ChromeCommandFor(std::string_view id) {
  return IdcOf(kNative, id);
}

int MenuTagFor(const CommandSpec& spec) {
  if (const int native = ChromeCommandFor(spec.id)) {
    return native;
  }
  if (const int same = IdcOf(kMenuTags, spec.id)) {
    return same;
  }
  return spec.idc;
}

bool IsReservedEntry(const CommandSpec& spec) {
  const std::string_view id(spec.id);
  return spec.route == CommandRoute::kRouter || id == "spaces.next" ||
         id == "spaces.prev";
}

bool IsPageOnlyAlt(const CommandSpec& spec, const CommandAlt& alt) {
  const std::string_view id(spec.id);
  return (id == "nav.back" || id == "nav.forward") &&
         alt.route == CommandRoute::kMenu &&
         alt.accelerator.modifiers == ui::EF_COMMAND_DOWN &&
         (alt.accelerator.key == ui::VKEY_LEFT ||
          alt.accelerator.key == ui::VKEY_RIGHT);
}

bool IsReservedChromeCommand(int command_id) {
  const std::string_view replaced = CluCommandForChromeCommand(command_id);
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.variant == CommandVariant::kMini || !IsReservedEntry(spec)) {
      continue;
    }
    if (MenuTagFor(spec) == command_id || replaced == spec.id) {
      return true;
    }
  }
  return false;
}

}  // namespace clu
