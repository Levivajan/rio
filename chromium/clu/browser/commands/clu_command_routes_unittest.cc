// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/commands/clu_command_routes.h"

#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "clu/browser/commands/command_table.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

// chrome/browser/ui/accelerator_table.cc kReservedCommandIds (Mac, 158): Chromium reserves these on its own.
constexpr int kChromeReserved[] = {
    IDC_CLOSE_TAB,         IDC_CLOSE_WINDOW,        IDC_NEW_INCOGNITO_WINDOW,
    IDC_NEW_ISOLATED_WINDOW, IDC_NEW_TAB,           IDC_NEW_WINDOW,
    IDC_RESTORE_TAB,       IDC_SELECT_NEXT_TAB,     IDC_SELECT_PREVIOUS_TAB,
    IDC_CYCLE_TO_NEXT_TAB, IDC_CYCLE_TO_PREV_TAB,   IDC_EXIT,
};

bool ChromeReserves(int idc) {
  return std::ranges::find(kChromeReserved, idc) != std::end(kChromeReserved);
}

// A chord as the key table compares it: key code plus exact modifiers.
std::string ChordKey(const CommandAccelerator& a) {
  return base::StrCat({base::NumberToString(static_cast<int>(a.key)), ":",
                       base::NumberToString(a.modifiers)});
}

bool InVariant(const CommandSpec& spec, CommandVariant variant) {
  return spec.variant == variant || spec.variant == CommandVariant::kBoth;
}

TEST(CluCommandRoutesTest, IdsAreContiguousAndFindable) {
  const base::span<const CommandSpec> table = CommandTable();
  ASSERT_FALSE(table.empty());
  EXPECT_EQ(table.front().idc, IDC_CLU_FIRST);
  EXPECT_EQ(table.back().idc, IDC_CLU_LAST);
  std::set<std::string> ids;
  for (size_t i = 0; i < table.size(); ++i) {
    const CommandSpec& spec = table[i];
    EXPECT_EQ(spec.idc, IDC_CLU_FIRST + static_cast<int>(i));
    EXPECT_TRUE(ids.insert(spec.id).second) << spec.id;
    EXPECT_EQ(FindCommand(spec.id), &spec);
    EXPECT_EQ(FindCommandByIdc(spec.idc), &spec);
  }
  EXPECT_EQ(FindCommand("no.such"), nullptr);
  EXPECT_EQ(FindCommand(""), nullptr);
  EXPECT_EQ(FindCommandByIdc(IDC_CLU_FIRST - 1), nullptr);
  EXPECT_EQ(FindCommandByIdc(IDC_CLU_LAST + 1), nullptr);
  EXPECT_EQ(FindCommandByIdc(IDC_NEW_TAB), nullptr);
  // Chromium's own ids stay below Clu's range.
  EXPECT_LT(IDC_BOOKMARKS_MENU, IDC_CLU_FIRST);
}

// commands.test.ts checks the TS table; this checks the generated C++ copy the browser dispatches from.
TEST(CluCommandRoutesTest, EveryChordIsUniqueWithinItsVariant) {
  for (CommandVariant variant : {CommandVariant::kMain, CommandVariant::kMini}) {
    std::set<std::string> seen;
    for (const CommandSpec& spec : CommandTable()) {
      if (!InVariant(spec, variant)) {
        continue;
      }
      if (spec.has_accelerator) {
        EXPECT_TRUE(seen.insert(ChordKey(spec.accelerator)).second) << spec.id;
      }
      for (const CommandAlt& alt : spec.alts) {
        EXPECT_TRUE(seen.insert(ChordKey(alt.accelerator)).second)
            << spec.id << " alt";
      }
    }
  }
}

TEST(CluCommandRoutesTest, NoCommandBouncesBetweenCluAndChromium) {
  for (const CommandSpec& spec : CommandTable()) {
    const int native = ChromeCommandFor(spec.id);
    if (native) {
      // A native command must not be one Clu replaces, or RunCommand() and HandleBrowserCommand() would loop.
      EXPECT_TRUE(CluCommandForChromeCommand(native).empty()) << spec.id;
    }
    const int tag = MenuTagFor(spec);
    const std::string_view replaced = CluCommandForChromeCommand(tag);
    // A menu item either runs its own Clu command, a Chromium command Clu leaves alone, or its IDC_CLU_* id.
    EXPECT_TRUE(replaced.empty() || replaced == spec.id) << spec.id;
    if (tag != spec.idc && replaced.empty()) {
      EXPECT_LT(tag, IDC_CLU_FIRST) << spec.id;
    }
  }
  // Every replaced command names a table entry.
  for (int idc : {IDC_NEW_TAB, IDC_FOCUS_LOCATION, IDC_CLOSE_TAB,
                  IDC_RESTORE_TAB, IDC_SELECT_TAB_0, IDC_SELECT_LAST_TAB,
                  IDC_CYCLE_TO_NEXT_TAB, IDC_FOCUS_NEXT_PANE,
                  IDC_DUPLICATE_TAB, IDC_NEW_INCOGNITO_WINDOW,
                  IDC_SHOW_DOWNLOADS}) {
    EXPECT_NE(FindCommand(CluCommandForChromeCommand(idc)), nullptr) << idc;
  }
  // Bookmarking and the avatar menu stay Chromium's.
  EXPECT_TRUE(CluCommandForChromeCommand(IDC_BOOKMARK_THIS_TAB).empty());
  EXPECT_TRUE(CluCommandForChromeCommand(IDC_SHOW_AVATAR_MENU).empty());
  // Cmd+Shift+S is Save Page As again: Cmd+S is the sidebar key, never IDC_SAVE_PAGE (docs/chromium/m2-shell.md).
  EXPECT_TRUE(CluCommandForChromeCommand(IDC_SAVE_PAGE).empty());
  EXPECT_EQ(ChromeCommandFor("page.savePage"), IDC_SAVE_PAGE);
  // File ▸ Capture… is Chromium's region capture.
  EXPECT_EQ(ChromeCommandFor("page.capture"), IDC_SHARING_HUB_SCREENSHOT);
  EXPECT_EQ(ChromeCommandFor("commandbar.newTab"), 0);
  EXPECT_EQ(ChromeCommandFor("junk"), 0);
  // M9: Cmd+Shift+J opens Chromium's downloads bubble through Clu (Chromium's command opens only the page), and
  // Passwords is Chromium's password manager.
  EXPECT_EQ(CluCommandForChromeCommand(IDC_SHOW_DOWNLOADS), "downloads.open");
  EXPECT_EQ(ChromeCommandFor("downloads.open"), 0);
  EXPECT_EQ(MenuTagFor(*FindCommand("downloads.open")), IDC_SHOW_DOWNLOADS);
  EXPECT_EQ(ChromeCommandFor("passwords.open"), IDC_SHOW_PASSWORD_MANAGER);
  // M8: History, Settings and Clear Browsing Data are Clu's own, on Chromium's menu items.
  EXPECT_EQ(CluCommandForChromeCommand(IDC_SHOW_HISTORY), "history.open");
  EXPECT_EQ(CluCommandForChromeCommand(IDC_OPTIONS), "settings.open");
  EXPECT_EQ(CluCommandForChromeCommand(IDC_CLEAR_BROWSING_DATA), "browsingData.open");
  EXPECT_EQ(ChromeCommandFor("history.open"), 0);
  EXPECT_EQ(MenuTagFor(*FindCommand("settings.open")), IDC_OPTIONS);
}

// A focused Peek takes Chromium's page commands (docs/chromium/m6.md): each is a table command whose menu item is
// that Chromium command, so the menu, its key equivalent and Clu's own paths name the same id.
TEST(CluCommandRoutesTest, PageCommandsMapChromiumMenuItemsToTableCommands) {
  constexpr std::pair<int, std::string_view> kExpected[] = {
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
  for (const auto& [idc, id] : kExpected) {
    EXPECT_EQ(PageCommandForChromeCommand(idc), id) << idc;
    const CommandSpec* spec = FindCommand(id);
    ASSERT_NE(spec, nullptr) << id;
    EXPECT_EQ(MenuTagFor(*spec), idc) << id;
    // Never a command Clu replaces, so the Peek path cannot bounce back into HandleBrowserCommand().
    EXPECT_TRUE(CluCommandForChromeCommand(idc).empty()) << idc;
  }
  // Window and tab commands stay with the window.
  for (int idc : {IDC_NEW_TAB, IDC_CLOSE_TAB, IDC_PRINT, IDC_SAVE_PAGE,
                  IDC_DEV_TOOLS, IDC_SHOW_DOWNLOADS, IDC_CLU_FIRST, 0, -1}) {
    EXPECT_TRUE(PageCommandForChromeCommand(idc).empty()) << idc;
  }
}

// PLAN 8: router chords never reach the page, menu chords reach it first. Each menu item's tag answers
// IsReservedCommandOrKey() the same way its route says.
TEST(CluCommandRoutesTest, MenuTagsAreReservedExactlyForRouterChords) {
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.variant == CommandVariant::kMini) {
      continue;
    }
    const int tag = MenuTagFor(spec);
    if (IsReservedEntry(spec)) {
      EXPECT_TRUE(IsReservedChromeCommand(tag) || ChromeReserves(tag))
          << spec.id;
    } else {
      EXPECT_FALSE(IsReservedChromeCommand(tag)) << spec.id;
      EXPECT_FALSE(ChromeReserves(tag)) << spec.id;
    }
  }
  EXPECT_TRUE(IsReservedEntry(*FindCommand("spaces.next")));
  EXPECT_TRUE(IsReservedEntry(*FindCommand("commandbar.editUrl")));
  EXPECT_FALSE(IsReservedEntry(*FindCommand("sidebar.toggle")));
  // Cmd+1..9 are router keys Chromium leaves to the page.
  EXPECT_TRUE(IsReservedChromeCommand(IDC_SELECT_TAB_0));
  EXPECT_TRUE(IsReservedChromeCommand(IDC_SELECT_LAST_TAB));
  EXPECT_TRUE(IsReservedChromeCommand(IDC_FOCUS_LOCATION));
  // Cmd+Option+Down is a menu key: the page gets it first.
  EXPECT_FALSE(IsReservedChromeCommand(IDC_FOCUS_NEXT_PANE));
  EXPECT_FALSE(IsReservedChromeCommand(IDC_BACK));
  EXPECT_FALSE(IsReservedChromeCommand(IDC_FIND));
  EXPECT_FALSE(IsReservedChromeCommand(0));
}

TEST(CluCommandRoutesTest, RouterEntriesHaveMenusAndRouterAltsKeepTheirRoute) {
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.route == CommandRoute::kRouter) {
      EXPECT_TRUE(spec.has_accelerator) << spec.id;
    }
  }
  // Cmd+Left and Cmd+Right stay Chromium's page-only Back and Forward; no other alt is.
  int page_only = 0;
  for (const CommandSpec& spec : CommandTable()) {
    for (const CommandAlt& alt : spec.alts) {
      if (IsPageOnlyAlt(spec, alt)) {
        ++page_only;
        EXPECT_TRUE(std::string_view(spec.id) == "nav.back" ||
                    std::string_view(spec.id) == "nav.forward");
      }
    }
  }
  EXPECT_EQ(page_only, 2);
  // Ctrl+Cmd+S always toggles the sidebar, even in a page that claims Cmd+S.
  const CommandSpec* sidebar = FindCommand("sidebar.toggle");
  ASSERT_NE(sidebar, nullptr);
  ASSERT_EQ(sidebar->alts.size(), 1u);
  EXPECT_EQ(sidebar->alts[0].route, CommandRoute::kRouter);
  EXPECT_EQ(sidebar->alts[0].accelerator.key, ui::VKEY_S);
  EXPECT_EQ(sidebar->alts[0].accelerator.modifiers,
            ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN);
}

}  // namespace
}  // namespace clu
