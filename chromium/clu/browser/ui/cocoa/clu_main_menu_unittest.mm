// Copyright 2026 The Rio Authors. All rights reserved.

#import "clu/browser/ui/cocoa/clu_main_menu.h"

#import <Cocoa/Cocoa.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/clu_shortcuts.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/updater/clu_update_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"
#include "ui/base/accelerators/accelerator.h"
#import "ui/base/accelerators/platform_accelerator_cocoa.h"

// A menu target that enables only the tags in `enabled`, as a browser window's validation does.
@interface CluMenuValidatorForTesting : NSObject
@property(nonatomic) NSSet<NSNumber*>* enabled;
- (void)commandDispatch:(id)sender;
@end

@implementation CluMenuValidatorForTesting
@synthesize enabled = _enabled;
- (void)commandDispatch:(id)sender {
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
  return [_enabled containsObject:@(item.tag)];
}
@end

namespace clu {
namespace {

KeyEquivalentAndModifierMask* Equivalent(ui::KeyboardCode key, int modifiers) {
  return ui::GetKeyEquivalentAndModifierMaskFromAccelerator(
      ui::Accelerator(key, modifiers));
}

// An item the way Chromium's MenuItemBuilder makes one: the chord from the accelerator, commandDispatch: for a tag.
NSMenuItem* Item(NSString* title,
                 int tag,
                 ui::KeyboardCode key = ui::VKEY_UNKNOWN,
                 int modifiers = 0,
                 SEL action = @selector(commandDispatch:)) {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                action:action
                                         keyEquivalent:@""];
  item.tag = tag;
  if (key != ui::VKEY_UNKNOWN) {
    KeyEquivalentAndModifierMask* equivalent = Equivalent(key, modifiers);
    item.keyEquivalent = equivalent.keyEquivalent;
    item.keyEquivalentModifierMask = equivalent.modifierMask;
  }
  return item;
}

NSMenuItem* Menu(NSString* title, int tag, NSArray<NSMenuItem*>* items) {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                action:nil
                                         keyEquivalent:@""];
  item.tag = tag;
  NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
  for (NSMenuItem* child in items) {
    [menu addItem:child];
  }
  item.submenu = menu;
  return item;
}

// Chromium's Ctrl+Cmd+F alternate of Enter Full Screen (untagged).
NSMenuItem* FullScreenAlternate() {
  NSMenuItem* item = Item(@"Enter Full Screen", 0, ui::VKEY_F,
                          ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN,
                          @selector(toggleFullScreen:));
  item.alternate = YES;
  return item;
}

// A small copy of Chromium's 158 main menu (main_menu_builder.mm), with the items Clu's rules touch.
NSMenu* ChromiumLikeMenu() {
  constexpr int kCmd = ui::EF_COMMAND_DOWN;
  constexpr int kShift = ui::EF_SHIFT_DOWN;
  constexpr int kAlt = ui::EF_ALT_DOWN;
  NSMenu* main = [[NSMenu alloc] initWithTitle:@""];
  [main addItem:Menu(@"Rio", IDC_CHROME_MENU, @[
          Item(@"About Rio", IDC_ABOUT, ui::VKEY_UNKNOWN, 0,
               @selector(orderFrontStandardAboutPanel:)),
          Item(@"Settings…", IDC_OPTIONS, ui::VKEY_OEM_COMMA, kCmd,
               @selector(showPreferences:)),
          Item(@"Delete Browsing Data…", IDC_CLEAR_BROWSING_DATA,
               ui::VKEY_BACK, kCmd | kShift),
          Item(@"Import Bookmarks and Settings…", IDC_IMPORT_SETTINGS),
          Item(@"Quit Rio", IDC_EXIT, ui::VKEY_Q, kCmd, @selector(terminate:)),
        ])];
  [main addItem:Menu(@"File", IDC_FILE_MENU, @[
          Item(@"New Tab", IDC_NEW_TAB, ui::VKEY_T, kCmd),
          Item(@"New Incognito Window", IDC_NEW_INCOGNITO_WINDOW, ui::VKEY_N,
               kCmd | kShift),
          Item(@"Close Window", IDC_CLOSE_WINDOW, ui::VKEY_W, kCmd | kShift,
               @selector(performClose:)),
          Item(@"Close Tab", IDC_CLOSE_TAB, ui::VKEY_W, kCmd),
          Item(@"Save Page As…", IDC_SAVE_PAGE, ui::VKEY_S, kCmd),
        ])];
  [main addItem:Menu(@"Edit", IDC_EDIT_MENU, @[
          Item(@"Copy", IDC_CONTENT_CONTEXT_COPY, ui::VKEY_C, kCmd,
               @selector(copy:)),
          Menu(@"Find", IDC_FIND_MENU, @[
            Item(@"Search the Web…", IDC_FOCUS_SEARCH, ui::VKEY_F,
                 kCmd | kAlt),
            Item(@"Find…", IDC_FIND, ui::VKEY_F, kCmd),
          ]),
        ])];
  [main addItem:Menu(@"View", IDC_VIEW_MENU, @[
          Item(@"Always Show Bookmarks Bar", IDC_SHOW_BOOKMARK_BAR, ui::VKEY_B,
               kCmd | kShift),
          Item(@"Always Show Toolbar in Full Screen",
               IDC_TOGGLE_FULLSCREEN_TOOLBAR, ui::VKEY_F, kCmd | kShift),
          [NSMenuItem separatorItem],
          Item(@"Reload This Page", IDC_RELOAD, ui::VKEY_R, kCmd),
          [NSMenuItem separatorItem],
          Item(@"Enter Full Screen", IDC_FULLSCREEN, ui::VKEY_UNKNOWN, 0,
               @selector(toggleFullScreen:)),
          FullScreenAlternate(),
          Item(@"Actual Size", IDC_ZOOM_NORMAL, ui::VKEY_0, kCmd),
          Menu(@"Developer", IDC_DEVELOPER_MENU, @[
            Item(@"View Source", IDC_VIEW_SOURCE, ui::VKEY_U, kCmd | kAlt),
          ]),
        ])];
  [main addItem:Menu(@"History", IDC_HISTORY_MENU, @[
          Item(@"Back", IDC_BACK, ui::VKEY_OEM_4, kCmd),
        ])];
  [main addItem:Menu(@"Bookmarks", IDC_BOOKMARKS_MENU, @[
          Item(@"Bookmark This Tab…", IDC_BOOKMARK_THIS_TAB, ui::VKEY_D, kCmd),
        ])];
  [main addItem:Menu(@"Profiles", IDC_PROFILE_MAIN_MENU, @[])];
  [main addItem:Menu(@"Tab", IDC_TAB_MENU, @[
          Item(@"Select Next Tab", IDC_CYCLE_TO_NEXT_TAB, ui::VKEY_TAB,
               ui::EF_CONTROL_DOWN),
          Item(@"Pin Tab", IDC_WINDOW_PIN_TAB),
        ])];
  [main addItem:Menu(@"Window", IDC_WINDOW_MENU, @[
          Item(@"Minimize", IDC_MINIMIZE_WINDOW, ui::VKEY_M, kCmd,
               @selector(performMiniaturize:)),
          Item(@"Downloads", IDC_SHOW_DOWNLOADS, ui::VKEY_J, kCmd | kShift),
          Item(@"Extensions", IDC_MANAGE_EXTENSIONS),
          Item(@"Task Manager", IDC_TASK_MANAGER_MAIN_MENU),
        ])];
  [main addItem:Menu(@"Help", 0, @[
          Item(@"Rio Help", IDC_HELP_PAGE_VIA_MENU),
        ])];
  return main;
}

void Collect(NSMenu* menu, std::vector<NSMenuItem*>& out) {
  for (NSMenuItem* item in menu.itemArray) {
    out.push_back(item);
    if (item.submenu) {
      Collect(item.submenu, out);
    }
  }
}

std::vector<NSMenuItem*> AllItems(NSMenu* menu) {
  std::vector<NSMenuItem*> out;
  Collect(menu, out);
  return out;
}

std::string ChordText(NSMenuItem* item) {
  return base::SysNSStringToUTF8(item.keyEquivalent) + "/" +
         std::to_string(item.keyEquivalentModifierMask &
                        NSEventModifierFlagDeviceIndependentFlagsMask);
}

std::string ChordText(const CommandAccelerator& accelerator) {
  KeyEquivalentAndModifierMask* e =
      Equivalent(accelerator.key, accelerator.modifiers);
  return base::SysNSStringToUTF8(e.keyEquivalent) + "/" +
         std::to_string(e.modifierMask);
}

NSMenuItem* Top(NSMenu* main, NSString* title) {
  return [main itemWithTitle:title];
}

class CluMainMenuTest : public testing::Test {
 protected:
  void SetUp() override {
    menu_ = ChromiumLikeMenu();
    ApplyMainMenu(menu_);
  }
  void TearDown() override { ResetShortcutsForTesting(); }

  NSMenu* __strong menu_;
};

// The menu bar's order, with Chromium's History menu as Archive and Profiles before Window.
TEST_F(CluMainMenuTest, MenusAreFileEditViewSpacesTabsArchiveExtensionsWindowHelp) {
  std::vector<std::string> titles;
  for (NSMenuItem* item in menu_.itemArray) {
    if (!item.hidden) {
      titles.push_back(base::SysNSStringToUTF8(item.title));
    }
  }
  EXPECT_EQ(titles, (std::vector<std::string>{
                        "Rio", "File", "Edit", "View", "Spaces", "Tabs",
                        "Archive", "Extensions", "Profiles", "Window",
                        "Help"}));
  EXPECT_NSEQ([menu_ itemWithTag:IDC_HISTORY_MENU].title, @"Archive");
  // Archive keeps only HistoryMenuBridge's items, and Delete Browsing Data… is a hidden key item at the app menu's
  // end; Window takes Open Library…; Extensions takes Manage Extensions….
  NSMenu* archive = [menu_ itemWithTag:IDC_HISTORY_MENU].submenu;
  EXPECT_TRUE([Top(menu_, @"Window").submenu
      itemWithTag:FindCommand("archive.open")->idc]);
  EXPECT_FALSE([archive itemWithTag:IDC_CLEAR_BROWSING_DATA]);
  NSMenuItem* clear = [menu_.itemArray.firstObject.submenu
      itemWithTag:IDC_CLEAR_BROWSING_DATA];
  ASSERT_TRUE(clear);
  EXPECT_TRUE(clear.hidden);
  EXPECT_TRUE(clear.allowsKeyEquivalentWhenHidden);
  EXPECT_NSEQ(clear, menu_.itemArray.firstObject.submenu.itemArray.lastObject);
  NSMenuItem* manage = [Top(menu_, @"Extensions").submenu
      itemWithTag:IDC_MANAGE_EXTENSIONS];
  ASSERT_TRUE(manage);
  EXPECT_NSEQ(manage.title, @"Manage Extensions…");
  EXPECT_FALSE([Top(menu_, @"Window").submenu
      itemWithTag:IDC_MANAGE_EXTENSIONS]);
  // Edit: Copy URL and Copy URL as Markdown follow Copy; Search the Web… is gone.
  NSMenu* edit = [menu_ itemWithTag:IDC_EDIT_MENU].submenu;
  const NSInteger copy = [edit indexOfItemWithTag:IDC_CONTENT_CONTEXT_COPY];
  EXPECT_EQ([edit indexOfItemWithTag:FindCommand("edit.copyUrl")->idc],
            copy + 1);
  EXPECT_EQ(
      [edit indexOfItemWithTag:FindCommand("edit.copyUrlMarkdown")->idc],
      copy + 2);
  for (NSMenuItem* item : AllItems(menu_)) {
    EXPECT_NE(item.tag, IDC_FOCUS_SEARCH);
  }
  // Bookmarks is hidden, not removed: AppController rebuilds it by tag.
  EXPECT_TRUE([menu_ itemWithTag:IDC_BOOKMARKS_MENU].hidden);
  // The Tab menu kept its tag (TabMenuBridge and AppController find it by tag) and lost Chromium's tab strip items.
  NSMenu* tabs = [menu_ itemWithTag:IDC_TAB_MENU].submenu;
  ASSERT_TRUE(tabs);
  EXPECT_FALSE([tabs itemWithTag:IDC_CYCLE_TO_NEXT_TAB]);
  EXPECT_FALSE([tabs itemWithTag:IDC_WINDOW_PIN_TAB]);
  // The toolbar and bookmark bar toggles are gone, and View starts with an item, not a separator.
  EXPECT_FALSE([[menu_ itemWithTag:IDC_VIEW_MENU].submenu
      itemWithTag:IDC_SHOW_BOOKMARK_BAR]);
  EXPECT_FALSE([[menu_ itemWithTag:IDC_VIEW_MENU].submenu
      itemWithTag:IDC_TOGGLE_FULLSCREEN_TOOLBAR]);
  EXPECT_FALSE([menu_ itemWithTag:IDC_VIEW_MENU]
                   .submenu.itemArray.firstObject.separatorItem);
}

TEST_F(CluMainMenuTest, EveryTableCommandHasOneItemWithItsChord) {
  const std::vector<NSMenuItem*> all = AllItems(menu_);
  for (const CommandSpec& spec : CommandTable()) {
    // Check for Updates… shows only in a build that updates, through its own target
    // (CheckForUpdatesRunsWithNoWindowAndHidesWithoutAnUpdater).
    if (spec.variant == CommandVariant::kMini ||
        std::string_view(spec.id) == "platform.checkForUpdates") {
      continue;
    }
    const int tag = MenuTagFor(spec);
    // The go-to and recent-tab chords are keys only: one hidden item holds the chord.
    const std::string_view id = spec.id;
    const bool key_only = id.starts_with("tabs.goto") ||
                          id == "archive.mruNext" || id == "archive.mruPrev";
    // Delete Browsing Data… shows in no menu and keeps its menu chord on a hidden item.
    const bool hidden_keys = id == "browsingData.open";
    int visible = 0;
    NSMenuItem* found = nil;
    for (NSMenuItem* item : all) {
      if (item.tag == tag && item.hidden == (key_only || hidden_keys) &&
          !item.submenu &&
          item.allowsKeyEquivalentWhenHidden == hidden_keys) {
        ++visible;
        found = item;
      }
    }
    EXPECT_EQ(visible, 1) << spec.id;
    if (!found) {
      continue;
    }
    if (spec.has_accelerator) {
      // A shifted punctuation or digit chord shows its unshifted key with ⇧, and a hidden alternate fires it.
      bool fires = ChordText(found) == ChordText(spec.accelerator);
      for (NSMenuItem* item : all) {
        fires |= item.tag == tag && item.hidden &&
                 ChordText(item) == ChordText(spec.accelerator);
      }
      EXPECT_TRUE(fires) << spec.id;
    }
    if (tag == spec.idc) {
      EXPECT_EQ(found.action, @selector(commandDispatch:)) << spec.id;
    }
  }
}

TEST_F(CluMainMenuTest, EveryChordHasOneMeaning) {
  std::map<std::string, int> owners;
  for (NSMenuItem* item : AllItems(menu_)) {
    if (item.keyEquivalent.length) {
      ++owners[ChordText(item)];
    }
  }
  for (const auto& [chord, count] : owners) {
    EXPECT_EQ(count, 1) << chord;
  }
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.variant == CommandVariant::kMini) {
      continue;
    }
    if (spec.has_accelerator) {
      EXPECT_EQ(owners[ChordText(spec.accelerator)], 1) << spec.id;
    }
    for (const CommandAlt& alt : spec.alts) {
      // Cmd+Left and Cmd+Right stay with Chromium's page-only Back and Forward.
      EXPECT_EQ(owners[ChordText(alt.accelerator)],
                IsPageOnlyAlt(spec, alt) ? 0 : 1)
          << spec.id << " alt";
    }
  }
}

TEST_F(CluMainMenuTest, ChromiumItemsKeepTheirPlaceAndLoseClashingChords) {
  NSMenu* file = [menu_ itemWithTag:IDC_FILE_MENU].submenu;
  // Save Page As moved from Cmd+S (now the sidebar key) to Cmd+Shift+S.
  NSMenuItem* save = [file itemWithTag:IDC_SAVE_PAGE];
  ASSERT_TRUE(save);
  EXPECT_EQ(ChordText(save),
            ChordText(CommandAccelerator{
                ui::VKEY_S, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN}));
  // Cmd+Shift+N keeps Clu's title (docs/chromium/PLAN.md section 5).
  EXPECT_NSEQ([file itemWithTag:IDC_NEW_INCOGNITO_WINDOW].title,
              @"New Private Window");
  // History > Back reads "Go Back".
  NSMenu* history = [menu_ itemWithTag:IDC_HISTORY_MENU].submenu;
  EXPECT_NSEQ([history itemWithTag:IDC_BACK].title, @"Go Back");
  // Close Window keeps performClose:, which CommandForKeyEvent maps to IDC_CLOSE_WINDOW.
  EXPECT_EQ([file itemWithTag:IDC_CLOSE_WINDOW].action,
            @selector(performClose:));
  // The app menu keeps its titles; Settings… becomes a commandDispatch: item so Chromium's key lookup finds Cmd+,.
  NSMenu* app = menu_.itemArray.firstObject.submenu;
  EXPECT_NSEQ([app itemWithTag:IDC_OPTIONS].title, @"Settings…");
  EXPECT_EQ([app itemWithTag:IDC_OPTIONS].action, @selector(commandDispatch:));
  EXPECT_FALSE([app itemWithTag:IDC_OPTIONS].target);
  // About and Quit keep theirs.
  EXPECT_EQ([app itemWithTag:IDC_EXIT].action, @selector(terminate:));
  EXPECT_EQ([app itemWithTag:IDC_ABOUT].action,
            @selector(orderFrontStandardAboutPanel:));
  // Edit keeps its roles; Copy URL joins it.
  NSMenu* edit = [menu_ itemWithTag:IDC_EDIT_MENU].submenu;
  EXPECT_EQ([edit itemWithTag:IDC_CONTENT_CONTEXT_COPY].action,
            @selector(copy:));
  EXPECT_TRUE([edit itemWithTag:FindCommand("edit.copyUrl")->idc]);
  // Edit ▸ Find ▸ Find… (the command bar calls it Find in Page).
  EXPECT_NSEQ([[edit itemWithTag:IDC_FIND_MENU].submenu itemWithTag:IDC_FIND]
                  .title,
              @"Find…");
  // Bookmark This Tab lost Cmd+D to Pin; View Source keeps Cmd+Option+U.
  EXPECT_EQ([[menu_ itemWithTag:IDC_BOOKMARKS_MENU].submenu
                itemWithTag:IDC_BOOKMARK_THIS_TAB]
                .keyEquivalent.length,
            0u);
  NSMenuItem* source = nil;
  for (NSMenuItem* item : AllItems(menu_)) {
    if (item.tag == IDC_VIEW_SOURCE) {
      source = item;
    }
  }
  ASSERT_TRUE(source);
  EXPECT_NSEQ(source.keyEquivalent, @"u");
  // Help: Essential Keyboard Shortcuts, Contact the Team, Rio Help, then Troubleshooting ▸ with Open Task Manager
  // (out of the Window menu), Reveal Rio Data and Copy Rio Info.
  NSMenu* help = Top(menu_, @"Help").submenu;
  EXPECT_EQ([help indexOfItemWithTag:FindCommand("platform.shortcuts")->idc],
            0);
  NSMenuItem* contact =
      [help itemWithTag:FindCommand("platform.contactTeam")->idc];
  EXPECT_EQ([help indexOfItem:contact], 1);
  EXPECT_NSEQ(contact.title, @"Contact the Team");
  EXPECT_EQ([help indexOfItemWithTag:IDC_HELP_PAGE_VIA_MENU], 2);
  NSMenu* troubleshooting = [help itemWithTitle:@"Troubleshooting"].submenu;
  ASSERT_TRUE(troubleshooting);
  ASSERT_EQ(troubleshooting.numberOfItems, 3);
  EXPECT_EQ([troubleshooting itemAtIndex:0].tag, IDC_TASK_MANAGER_MAIN_MENU);
  EXPECT_NSEQ([troubleshooting itemAtIndex:0].title, @"Open Task Manager");
  EXPECT_NSEQ([troubleshooting itemAtIndex:1].title, @"Reveal Rio Data");
  EXPECT_NSEQ([troubleshooting itemAtIndex:2].title, @"Copy Rio Info");
  EXPECT_FALSE([Top(menu_, @"Window").submenu
      itemWithTag:IDC_TASK_MANAGER_MAIN_MENU]);
}

// The visible table commands of `menu`, in order.
std::vector<std::string> VisibleIds(NSMenu* menu) {
  std::vector<std::string> ids;
  for (NSMenuItem* item in menu.itemArray) {
    if (item.hidden || item.separatorItem || !item.tag) {
      continue;
    }
    for (const CommandSpec& spec : CommandTable()) {
      if (spec.variant != CommandVariant::kMini &&
          MenuTagFor(spec) == item.tag) {
        ids.push_back(spec.id);
        break;
      }
    }
  }
  return ids;
}

std::optional<bool> DarkMode(std::string_view id) {
  if (!id.starts_with("theme.appearance")) {
    return std::nullopt;
  }
  return id == "theme.appearanceDark";
}

std::optional<std::u16string> PinnedRow(std::string_view id) {
  if (id == "tabs.togglePin") {
    return u"Unpin Tab";
  }
  return std::nullopt;
}

std::optional<std::u16string> HiddenSidebar(std::string_view id) {
  if (id == "sidebar.toggle") {
    return u"Show Sidebar";
  }
  return std::nullopt;
}

// Add Split View reads ⌃⇧= (the key "=" with Ctrl and Shift), and its hidden alternate carries the ⌃+ AppKit
// and Chromium's key lookup match.
TEST_F(CluMainMenuTest, ShiftedSplitChordsShowTheUnshiftedKey) {
  NSMenu* view = [menu_ itemWithTag:IDC_VIEW_MENU].submenu;
  for (std::string_view id : {"split.add", "split.separate", "split.focus1",
                              "split.next"}) {
    const CommandSpec* spec = FindCommand(id);
    ASSERT_TRUE(spec) << id;
    const NSInteger at = [view indexOfItemWithTag:spec->idc];
    ASSERT_GE(at, 0) << id;
    NSMenuItem* shown = [view itemAtIndex:at];
    NSMenuItem* fires = [view itemAtIndex:at + 1];
    EXPECT_FALSE(shown.hidden) << id;
    EXPECT_EQ(shown.keyEquivalentModifierMask &
                  NSEventModifierFlagDeviceIndependentFlagsMask,
              NSEventModifierFlagControl | NSEventModifierFlagShift)
        << id;
    EXPECT_TRUE(fires.hidden && fires.allowsKeyEquivalentWhenHidden) << id;
    EXPECT_EQ(fires.tag, spec->idc) << id;
    EXPECT_EQ(ChordText(fires), ChordText(spec->accelerator)) << id;
  }
  EXPECT_NSEQ(
      [view itemWithTag:FindCommand("split.add")->idc].keyEquivalent, @"=");
  EXPECT_NSEQ(
      [view itemWithTag:FindCommand("split.separate")->idc].keyEquivalent,
      @"-");
  EXPECT_NSEQ(
      [view itemWithTag:FindCommand("split.next")->idc].keyEquivalent, @"]");
}

// The View labels, Hide Sidebar or Show Sidebar from the window, and Enter Full Screen last.
TEST_F(CluMainMenuTest, TheViewMenuUsesItsOwnLabels) {
  NSMenu* view = [menu_ itemWithTag:IDC_VIEW_MENU].submenu;
  EXPECT_NSEQ([view itemWithTag:IDC_RELOAD].title, @"Refresh the Page");
  EXPECT_NSEQ([view itemWithTag:FindCommand("tabs.collapsePinned")->idc].title,
              @"Collapse Pinned Tabs");
  EXPECT_NSEQ([view itemWithTag:IDC_ZOOM_NORMAL].title, @"Zoom to Actual Size");
  EXPECT_NSEQ([view itemWithTag:FindCommand("split.separate")->idc].title,
              @"Close this Split Pane");
  const int sidebar = FindCommand("sidebar.toggle")->idc;
  EXPECT_NSEQ([view itemWithTag:sidebar].title, @"Hide Sidebar");
  ASSERT_TRUE(view.delegate);
  SetMenuTitleProvider(&HiddenSidebar);
  [view.delegate menuNeedsUpdate:view];
  EXPECT_NSEQ([view itemWithTag:sidebar].title, @"Show Sidebar");
  SetMenuTitleProvider(nullptr);
  [view.delegate menuNeedsUpdate:view];
  EXPECT_NSEQ([view itemWithTag:sidebar].title, @"Hide Sidebar");
  // The sidebar's Ctrl+Cmd+S alternate stays hidden through an update.
  for (NSMenuItem* item in view.itemArray) {
    if (item.tag == sidebar && item.allowsKeyEquivalentWhenHidden) {
      EXPECT_TRUE(item.hidden);
    }
  }
  // The order: Appearance ▸, the sidebar rows, refreshing, split view, zoom.
  EXPECT_NSEQ([view itemAtIndex:0].title, @"Appearance");
  const std::vector<std::string> ids = VisibleIds(view);
  ASSERT_GE(ids.size(), 10u);
  EXPECT_EQ(std::vector<std::string>(ids.begin(), ids.begin() + 10),
            (std::vector<std::string>{
                "sidebar.toggle", "tabs.collapsePinned", "nav.stop",
                "nav.reload", "nav.hardReload", "page.clearCookiesAndRefresh",
                "page.clearCacheAndRefresh", "split.add", "split.separate",
                "split.expand"}));
  EXPECT_NSEQ([view itemWithTag:FindCommand("split.expand")->idc].title,
              @"Expand Current Split");
  const NSInteger last = view.numberOfItems - 1;
  EXPECT_TRUE([view itemAtIndex:last].alternate);
  EXPECT_EQ([view itemAtIndex:last - 1].tag, IDC_FULLSCREEN);
}

// Tabs opens with Pin Tab, Next and Previous Tab, the row actions and Clear Today; Go to Tab 1-9 and the
// recent-tab steps are keys only.
TEST_F(CluMainMenuTest, TheTabsMenuListsItsItemsInOrder) {
  NSMenu* tabs = [menu_ itemWithTag:IDC_TAB_MENU].submenu;
  const std::vector<std::string> ids = VisibleIds(tabs);
  ASSERT_GE(ids.size(), 13u);
  EXPECT_EQ(std::vector<std::string>(ids.begin(), ids.begin() + 13),
            (std::vector<std::string>{
                "tabs.togglePin", "tabs.newFolder", "tabs.next", "tabs.prev",
                "tabs.reveal", "tabs.duplicate", "tabs.toggleMute",
                "split.addToSplit", "split.openInSplit", "peek.expand",
                "archive.clearToday", "archive.undoClear", "tabs.resetAll"}));
  EXPECT_NSEQ([tabs itemWithTag:FindCommand("tabs.newFolder")->idc].title,
              @"New Folder…");
  EXPECT_NSEQ([tabs itemWithTag:FindCommand("tabs.reveal")->idc].title,
              @"Reveal Tab in Sidebar");
  EXPECT_NSEQ([tabs itemWithTag:FindCommand("tabs.resetAll")->idc].title,
              @"Reset all tabs in this Space");
  // The three have no key.
  for (const char* id : {"tabs.newFolder", "tabs.reveal", "tabs.resetAll"}) {
    EXPECT_NSEQ([tabs itemWithTag:FindCommand(id)->idc].keyEquivalent, @"")
        << id;
  }
  for (const std::string& id : ids) {
    EXPECT_FALSE(id.starts_with("tabs.goto")) << id;
    EXPECT_FALSE(id.starts_with("archive.mru")) << id;
  }
  EXPECT_NSEQ([tabs itemWithTag:FindCommand("tabs.togglePin")->idc].title,
              @"Pin Tab");
  EXPECT_NSEQ([tabs itemWithTag:FindCommand("archive.clearToday")->idc].title,
              @"Clear Today");
  // The window names Unpin Tab for a pinned row; the go-to rows stay hidden through an update.
  ASSERT_TRUE(tabs.delegate);
  SetMenuTitleProvider(&PinnedRow);
  [tabs.delegate menuNeedsUpdate:tabs];
  EXPECT_NSEQ([tabs itemWithTag:FindCommand("tabs.togglePin")->idc].title,
              @"Unpin Tab");
  EXPECT_TRUE([tabs itemWithTag:MenuTagFor(*FindCommand("tabs.goto1"))].hidden);
  SetMenuTitleProvider(nullptr);
}

// View ▸ Appearance ▸ Automatic, Light and Dark under a header, the mode in use checked.
TEST_F(CluMainMenuTest, TheAppearanceSubmenuChecksTheModeInUse) {
  NSMenu* view = [menu_ itemWithTag:IDC_VIEW_MENU].submenu;
  NSMenu* appearance = [view itemAtIndex:0].submenu;
  ASSERT_TRUE(appearance);
  ASSERT_EQ(appearance.numberOfItems, 4);
  EXPECT_NSEQ([appearance itemAtIndex:0].title, @"Rio and websites will use:");
  EXPECT_FALSE([appearance itemAtIndex:0].action);
  EXPECT_EQ(VisibleIds(appearance),
            (std::vector<std::string>{"theme.appearanceAuto",
                                      "theme.appearanceLight",
                                      "theme.appearanceDark"}));
  EXPECT_NSEQ([appearance itemAtIndex:1].title, @"Automatic");
  EXPECT_NSEQ([appearance itemAtIndex:2].title, @"Light");
  EXPECT_NSEQ([appearance itemAtIndex:3].title, @"Dark");
  ASSERT_TRUE(appearance.delegate);
  SetMenuCheckProvider(&DarkMode);
  [appearance.delegate menuNeedsUpdate:appearance];
  EXPECT_EQ([appearance itemAtIndex:1].state, NSControlStateValueOff);
  EXPECT_EQ([appearance itemAtIndex:2].state, NSControlStateValueOff);
  EXPECT_EQ([appearance itemAtIndex:3].state, NSControlStateValueOn);
  SetMenuCheckProvider(nullptr);
}

// File opens with New Tab…, New Window, New Private Window, New Mini Window, Restore Last Closed Tab, Open
// Command Bar and New Profile….
TEST_F(CluMainMenuTest, TheFileMenuListsItsItemsInOrder) {
  NSMenu* file = [menu_ itemWithTag:IDC_FILE_MENU].submenu;
  const std::vector<std::string> ids = VisibleIds(file);
  ASSERT_GE(ids.size(), 7u);
  EXPECT_EQ(std::vector<std::string>(ids.begin(), ids.begin() + 7),
            (std::vector<std::string>{"commandbar.newTab", "window.new",
                                      "window.newPrivate", "mini.new",
                                      "archive.reopenClosed",
                                      "commandbar.editUrl",
                                      "profiles.newProfile"}));
  EXPECT_NSEQ([file itemWithTag:IDC_NEW_TAB].title, @"New Tab…");
  EXPECT_NSEQ([file itemWithTag:IDC_RESTORE_TAB].title,
              @"Restore Last Closed Tab");
  EXPECT_NSEQ([file itemWithTag:IDC_FOCUS_LOCATION].title,
              @"Open Command Bar");
}

// Window opens with Stay On Top, then Minimize Window; Downloads reads View Downloads….
TEST_F(CluMainMenuTest, TheWindowMenuStartsWithStayOnTop) {
  NSMenu* window = [menu_ itemWithTag:IDC_WINDOW_MENU].submenu;
  EXPECT_EQ([window itemAtIndex:0].tag, FindCommand("window.stayOnTop")->idc);
  EXPECT_NSEQ([window itemAtIndex:0].title, @"Stay On Top");
  EXPECT_TRUE([window itemAtIndex:1].separatorItem);
  EXPECT_NSEQ([window itemWithTag:IDC_MINIMIZE_WINDOW].title,
              @"Minimize Window");
  EXPECT_NSEQ([window itemWithTag:IDC_SHOW_DOWNLOADS].title,
              @"View Downloads…");
  ASSERT_TRUE(window.delegate);
  [window.delegate menuNeedsUpdate:window];
  EXPECT_EQ([window itemAtIndex:0].state, NSControlStateValueOff);
}

TEST_F(CluMainMenuTest, AlternateChordsAreHiddenItemsBesideTheirCommand) {
  const CommandSpec* next = FindCommand("tabs.next");
  ASSERT_TRUE(next);
  NSMenu* tabs = [menu_ itemWithTag:IDC_TAB_MENU].submenu;
  const NSInteger at = [tabs indexOfItemWithTag:next->idc];
  ASSERT_GE(at, 0);
  NSMenuItem* main = [tabs itemAtIndex:at];
  NSMenuItem* alternate = [tabs itemAtIndex:at + 1];
  EXPECT_FALSE(main.hidden);
  EXPECT_TRUE(alternate.hidden);
  EXPECT_EQ(alternate.tag, next->idc);
  EXPECT_EQ(alternate.action, main.action);
  EXPECT_EQ(ChordText(alternate), ChordText(next->alts[0].accelerator));
}

// docs/chromium/parity-settings.md section 4: the user's keys reach the menu, its alternates follow, and a reset brings
// the table's back.
TEST_F(CluMainMenuTest, CustomShortcutsMoveTheMenuKeysAndTheirAlternates) {
  const CommandSpec* next = FindCommand("tabs.next");
  const CommandSpec* history = FindCommand("history.open");
  ASSERT_TRUE(next && history);
  NSMenu* tabs = [menu_ itemWithTag:IDC_TAB_MENU].submenu;
  auto alternates = [&](int tag) {
    int n = 0;
    for (NSMenuItem* item in tabs.itemArray) {
      n += item.tag == tag && item.hidden ? 1 : 0;
    }
    return n;
  };
  ASSERT_EQ(alternates(next->idc), 1);

  base::DictValue custom;
  custom.Set("tabs.next", "Cmd+Ctrl+N");
  custom.Set("history.open", base::Value());
  SetCustomShortcuts(custom);
  ApplyShortcuts(menu_);
  NSMenuItem* next_item = [tabs itemWithTag:next->idc];
  EXPECT_EQ(ChordText(next_item),
            ChordText({ui::VKEY_N, ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN}));
  EXPECT_EQ(alternates(next->idc), 0);
  NSMenuItem* history_item = nil;
  for (NSMenuItem* item : AllItems(menu_)) {
    if (item.tag == MenuTagFor(*history) && !item.hidden) {
      history_item = item;
    }
  }
  ASSERT_TRUE(history_item);
  EXPECT_EQ(history_item.keyEquivalent.length, 0u);

  SetCustomShortcuts(base::DictValue());
  ApplyShortcuts(menu_);
  EXPECT_EQ(ChordText([tabs itemWithTag:next->idc]),
            ChordText(next->accelerator));
  EXPECT_EQ(alternates(next->idc), 1);
  EXPECT_EQ(ChordText(history_item), ChordText(history->accelerator));
}

std::optional<std::u16string> SpaceName(std::string_view id) {
  if (id == "spaces.goto1") {
    return u"Work";
  }
  // Space 2 has no name; there is no third space.
  if (id == "spaces.goto2") {
    return std::nullopt;
  }
  return std::u16string();
}

TEST_F(CluMainMenuTest, TheSpacesMenuShowsSpaceNames) {
  NSMenu* spaces = Top(menu_, @"Spaces").submenu;
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(spaces.delegate);
  const int one = FindCommand("spaces.goto1")->idc;
  const int two = FindCommand("spaces.goto2")->idc;
  const int three = FindCommand("spaces.goto3")->idc;
  SetMenuTitleProvider(&SpaceName);
  [spaces.delegate menuNeedsUpdate:spaces];
  EXPECT_NSEQ([spaces itemWithTag:one].title, @"Work");
  EXPECT_NSEQ([spaces itemWithTag:two].title, @"Space 2");
  // One row per real space: the third is hidden, and its chord stays with the window's key table.
  EXPECT_FALSE([spaces itemWithTag:two].hidden);
  EXPECT_TRUE([spaces itemWithTag:three].hidden);
  SetMenuTitleProvider(nullptr);
  [spaces.delegate menuNeedsUpdate:spaces];
  EXPECT_NSEQ([spaces itemWithTag:one].title, @"Space 1");
  EXPECT_FALSE([spaces itemWithTag:three].hidden);
}

// A private window in front: every Spaces row gets an empty title (CluWindowState::CommandTitle).
std::optional<std::u16string> PrivateWindow(std::string_view id) {
  if (id.starts_with("spaces.") || id == "theme.editTheme") {
    return std::u16string();
  }
  return std::nullopt;
}

// In a private window the Spaces rows are hidden, not greyed, and the Spaces menu with them; a regular window
// in front brings them back.
TEST_F(CluMainMenuTest, APrivateWindowHidesTheSpacesMenu) {
  NSMenuItem* entry = Top(menu_, @"Spaces");
  ASSERT_TRUE(entry);
  NSMenu* spaces = entry.submenu;
  SetMenuTitleProvider(&PrivateWindow);
  UpdateSpacesMenu(menu_);
  for (std::string_view id : {"spaces.newSpace", "theme.editTheme",
                              "spaces.editSpace", "spaces.next", "spaces.prev",
                              "spaces.goto1"}) {
    EXPECT_TRUE([spaces itemWithTag:FindCommand(id)->idc].hidden) << id;
  }
  EXPECT_TRUE(entry.hidden);
  SetMenuTitleProvider(nullptr);
  UpdateSpacesMenu(menu_);
  EXPECT_FALSE(entry.hidden);
  EXPECT_FALSE([spaces itemWithTag:FindCommand("spaces.newSpace")->idc].hidden);
  EXPECT_FALSE([spaces itemWithTag:FindCommand("theme.editTheme")->idc].hidden);
}

// The Spaces menu's order: New Space…, Edit Theme…, Edit Space…, Next and Previous Space, then the spaces. New
// Profile… is in File.
TEST_F(CluMainMenuTest, TheSpacesMenuListsItsItemsInOrder) {
  NSMenu* spaces = Top(menu_, @"Spaces").submenu;
  std::vector<std::string> ids;
  for (NSMenuItem* item in spaces.itemArray) {
    if (const CommandSpec* spec = FindCommandByIdc(item.tag);
        spec && !item.hidden) {
      ids.push_back(spec->id);
    }
  }
  ASSERT_GE(ids.size(), 6u);
  EXPECT_EQ(std::vector<std::string>(ids.begin(), ids.begin() + 6),
            (std::vector<std::string>{"spaces.newSpace", "theme.editTheme",
                                      "spaces.editSpace", "spaces.next",
                                      "spaces.prev", "spaces.goto1"}));
  EXPECT_FALSE([spaces itemWithTag:FindCommand("profiles.newProfile")->idc]);
  EXPECT_FALSE([[menu_ itemWithTag:IDC_VIEW_MENU].submenu
      itemWithTag:FindCommand("theme.editTheme")->idc]);
}

// M12: Check for Updates… sits in the app menu after Import…, not in Help.
TEST_F(CluMainMenuTest, CheckForUpdatesFollowsImportInTheAppMenu) {
  const CommandSpec* spec = FindCommand("platform.checkForUpdates");
  ASSERT_TRUE(spec);
  NSMenu* app = menu_.itemArray.firstObject.submenu;
  EXPECT_EQ([app indexOfItemWithTag:IDC_OPTIONS], 1);
  const NSInteger import = [app indexOfItemWithTag:IDC_IMPORT_SETTINGS];
  ASSERT_GE(import, 0);
  NSMenuItem* item = [app itemAtIndex:import + 1];
  EXPECT_EQ(item.tag, spec->idc);
  EXPECT_NSEQ(item.title, @"Check for Updates…");
  EXPECT_FALSE([menu_.itemArray.lastObject.submenu itemWithTag:spec->idc]);
}

// The app menu has Set as Default Browser right after Settings…; Help doesn't keep it. File has
// Capture… ⇧⌘2, Chromium's region capture.
TEST_F(CluMainMenuTest, SetAsDefaultBrowserFollowsSettingsAndFileHasCapture) {
  NSMenu* app = menu_.itemArray.firstObject.submenu;
  const NSInteger settings = [app indexOfItemWithTag:IDC_OPTIONS];
  ASSERT_GE(settings, 0);
  const CommandSpec* spec = FindCommand("platform.setDefaultBrowser");
  ASSERT_TRUE(spec);
  NSMenuItem* item = [app itemAtIndex:settings + 1];
  EXPECT_EQ(item.tag, spec->idc);
  EXPECT_NSEQ(item.title, @"Set as Default Browser");
  EXPECT_FALSE([Top(menu_, @"Help").submenu itemWithTag:spec->idc]);
  NSMenuItem* capture = [[menu_ itemWithTag:IDC_FILE_MENU].submenu
      itemWithTag:IDC_SHARING_HUB_SCREENSHOT];
  ASSERT_TRUE(capture);
  EXPECT_NSEQ(capture.title, @"Capture…");
  EXPECT_NSEQ(capture.keyEquivalent, @"2");
  EXPECT_EQ(capture.keyEquivalentModifierMask &
                NSEventModifierFlagDeviceIndependentFlagsMask,
            NSEventModifierFlagCommand | NSEventModifierFlagShift);
  EXPECT_EQ(capture.action, @selector(commandDispatch:));
}

// Stands in for Sparkle: counts the checks it was asked for.
class CountingUpdateDriver : public CluUpdateDriver {
 public:
  explicit CountingUpdateDriver(int* checks) : checks_(checks) {}
  bool Start(Client* client) override { return true; }
  void CheckNow() override { ++*checks_; }
  bool InstallNow() override { return false; }

 private:
  raw_ptr<int> checks_;
};

// Check for Updates… has its own target, so it runs with no window open (AppController disables a commandDispatch:
// item it doesn't know), and it is listed only while the build can update.
TEST_F(CluMainMenuTest, CheckForUpdatesRunsWithNoWindowAndHidesWithoutAnUpdater) {
  base::test::TaskEnvironment task_environment;
  CluUpdateService::Get().ResetForTesting();
  NSMenu* app = menu_.itemArray.firstObject.submenu;
  NSMenuItem* item =
      [app itemWithTag:FindCommand("platform.checkForUpdates")->idc];
  ASSERT_TRUE(item);
  EXPECT_TRUE(item.hidden);
  ASSERT_TRUE(item.target);
  EXPECT_NE(item.action, @selector(commandDispatch:));
  EXPECT_FALSE([item.target validateMenuItem:item]);

  int checks = 0;
  CluUpdateService::Get().SetDriverForTesting(
      std::make_unique<CountingUpdateDriver>(&checks));
  EXPECT_FALSE(item.hidden);
  EXPECT_TRUE([item.target validateMenuItem:item]);
  // No window answers commandDispatch: here, so the service checks itself.
  [NSApplication sharedApplication];
  [NSApp sendAction:item.action to:item.target from:item];
  EXPECT_EQ(checks, 1);
  CluUpdateService::Get().ResetForTesting();
}

// -[NSMenu update] on the main menu validates only its own items; UpdateMenuItems validates every submenu's, so
// Chromium's key lookup sees what can run now.
TEST(CluMainMenuUpdateTest, EverySubmenuItemIsValidatedAgain) {
  // Without an NSApplication, -[NSMenu update] disables every item without asking its target.
  [NSApplication sharedApplication];
  CluMenuValidatorForTesting* validator = [[CluMenuValidatorForTesting alloc] init];
  NSMenuItem* runs = Item(@"Runs", 1, ui::VKEY_R, ui::EF_COMMAND_DOWN);
  NSMenuItem* idle = Item(@"Idle", 2, ui::VKEY_I, ui::EF_COMMAND_DOWN);
  NSMenuItem* nested = Item(@"Nested", 3, ui::VKEY_N, ui::EF_COMMAND_DOWN);
  for (NSMenuItem* item in @[ runs, idle, nested ]) {
    item.target = validator;
  }
  NSMenu* main = [[NSMenu alloc] initWithTitle:@""];
  NSMenuItem* file = Menu(@"File", 0, @[ runs, idle ]);
  [file.submenu addItem:Menu(@"More", 0, @[ nested ])];
  [main addItem:file];

  validator.enabled = [NSSet setWithArray:@[ @1, @3 ]];
  [main update];
  // What Chromium's lookup saw before: nothing validated, so every item still enabled.
  EXPECT_TRUE(idle.enabled);
  UpdateMenuItems(main);
  EXPECT_TRUE(runs.enabled);
  EXPECT_FALSE(idle.enabled);
  EXPECT_TRUE(nested.enabled);

  validator.enabled = [NSSet setWithArray:@[ @2 ]];
  UpdateMenuItems(main);
  EXPECT_FALSE(runs.enabled);
  EXPECT_TRUE(idle.enabled);
  EXPECT_FALSE(nested.enabled);
}

TEST(CluMainMenuJunkTest, AnEmptyOrMissingMenuIsLeftAlone) {
  ApplyMainMenu(nil);
  NSMenu* empty = [[NSMenu alloc] initWithTitle:@""];
  ApplyMainMenu(empty);
  EXPECT_EQ(empty.numberOfItems, 0);
  UpdateMenuItems(nil);
  UpdateMenuItems(empty);
}

}  // namespace
}  // namespace clu
