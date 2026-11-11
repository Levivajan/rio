// Copyright 2026 The Rio Authors. All rights reserved.

#import "clu/browser/ui/cocoa/clu_main_menu.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/clu_shortcuts.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/updater/clu_update_service.h"
#include "ui/base/accelerators/accelerator.h"
#import "ui/base/accelerators/platform_accelerator_cocoa.h"

namespace {

clu::MenuTitleProvider g_title_provider = nullptr;
clu::MenuCheckProvider g_check_provider = nullptr;

// Chromium items that drive the toolbar, the bookmark bar or the tab strip, which the Clu layout hides.
constexpr int kRemovedTags[] = {
    IDC_SHOW_BOOKMARK_BAR,          IDC_TOGGLE_FULLSCREEN_TOOLBAR,
    IDC_SHOW_FULL_URLS,             IDC_SHOW_GOOGLE_LENS_SHORTCUT,
    IDC_SHOW_AI_MODE_OMNIBOX_BUTTON, IDC_SHOW_SEARCH_TOOLS,
    IDC_TOGGLE_VERTICAL_TABS,       IDC_TOGGLE_VERTICAL_TABS_COLLAPSE,
    // Search the Web… focuses the hidden omnibox; Cmd+Option+F is Find and Replace.
    IDC_FOCUS_SEARCH,
};

// Main menu labels that differ from the command bar's.
constexpr std::pair<std::string_view, const char*> kMenuTitles[] = {
    {"nav.reload", "Refresh the Page"},
    {"nav.hardReload", "Force Refresh the Page"},
    {"zoom.reset", "Zoom to Actual Size"},
    {"find.open", "Find…"},
    {"split.separate", "Close this Split Pane"},
    {"sidebar.toggle", "Hide Sidebar"},
    {"tabs.togglePin", "Pin Tab"},
    {"archive.clearToday", "Clear Today"},
    {"commandbar.newTab", "New Tab…"},
    {"archive.reopenClosed", "Restore Last Closed Tab"},
    {"commandbar.editUrl", "Open Command Bar"},
    {"downloads.open", "View Downloads…"},
    {"theme.appearanceAuto", "Automatic"},
    {"theme.appearanceLight", "Light"},
    {"theme.appearanceDark", "Dark"},
};

// Commands the menu bar doesn't list: their chords stay with the window's key table.
bool IsKeyOnly(std::string_view id) {
  return id.starts_with("tabs.goto") || id == "archive.mruNext" ||
         id == "archive.mruPrev";
}

NSString* MenuTitle(const clu::CommandSpec& spec) {
  for (const auto& [id, title] : kMenuTitles) {
    if (id == spec.id) {
      return base::SysUTF8ToNSString(title);
    }
  }
  return base::SysUTF8ToNSString(spec.title);
}

// The unshifted key of a shifted punctuation or digit chord. AppKit draws Ctrl+Shift+= as ⌃+; the menu shows ⌃⇧=.
NSString* UnshiftedKey(ui::KeyboardCode key) {
  switch (key) {
    case ui::VKEY_OEM_PLUS:
      return @"=";
    case ui::VKEY_OEM_MINUS:
      return @"-";
    case ui::VKEY_OEM_4:
      return @"[";
    case ui::VKEY_OEM_6:
      return @"]";
    default:
      break;
  }
  if (key >= ui::VKEY_0 && key <= ui::VKEY_9) {
    return [NSString stringWithFormat:@"%d", key - ui::VKEY_0];
  }
  return nil;
}

struct Chord {
  NSString* __strong key;
  NSUInteger mask;
};

Chord ChordOf(const clu::CommandAccelerator& accelerator) {
  KeyEquivalentAndModifierMask* equivalent =
      ui::GetKeyEquivalentAndModifierMaskFromAccelerator(
          ui::Accelerator(accelerator.key, accelerator.modifiers));
  return {equivalent.keyEquivalent, equivalent.modifierMask};
}

bool SameChord(NSMenuItem* item, const Chord& chord) {
  return item.keyEquivalent.length > 0 &&
         [item.keyEquivalent isEqualToString:chord.key] &&
         (item.keyEquivalentModifierMask &
          NSEventModifierFlagDeviceIndependentFlagsMask) == chord.mask;
}

void SetChord(NSMenuItem* item, const Chord& chord) {
  item.keyEquivalent = chord.key;
  item.keyEquivalentModifierMask = chord.mask;
}

void ClearChord(NSMenuItem* item) {
  item.keyEquivalent = @"";
  item.keyEquivalentModifierMask = 0;
}

template <typename Fn>
void ForEachItem(NSMenu* menu, Fn fn) {
  for (NSMenuItem* item in [menu.itemArray copy]) {
    fn(item);
    if (item.submenu && item.submenu != NSApp.servicesMenu) {
      ForEachItem(item.submenu, fn);
    }
  }
}

NSMenuItem* ItemWithTag(NSMenu* menu, int tag) {
  for (NSMenuItem* item in menu.itemArray) {
    if (item.tag == tag) {
      return item;
    }
    if (item.submenu) {
      if (NSMenuItem* found = ItemWithTag(item.submenu, tag)) {
        return found;
      }
    }
  }
  return nil;
}

// Leading, trailing and doubled untagged separators go (tagged ones are anchors of Chromium's menu bridges).
void TidySeparators(NSMenu* menu) {
  bool last_was_separator = true;
  for (NSMenuItem* item in [menu.itemArray copy]) {
    if (!item.separatorItem || item.tag != 0) {
      last_was_separator = item.separatorItem;
      continue;
    }
    if (last_was_separator) {
      [menu removeItem:item];
    }
    last_was_separator = true;
  }
  NSMenuItem* last = menu.itemArray.lastObject;
  if (last && last.separatorItem && last.tag == 0) {
    [menu removeItem:last];
  }
}

// Moves the table items `ids` (each with its hidden alternates) into `menu` from index `at` on, in that order; an empty
// id puts a separator there. Returns the index after the last one placed.
NSInteger Place(NSMenu* main_menu,
                NSMenu* menu,
                NSInteger at,
                std::initializer_list<std::string_view> ids) {
  for (std::string_view id : ids) {
    if (id.empty()) {
      [menu insertItem:[NSMenuItem separatorItem] atIndex:at++];
      continue;
    }
    const clu::CommandSpec* spec = clu::FindCommand(id);
    if (!spec) {
      continue;
    }
    const int tag = clu::MenuTagFor(*spec);
    std::vector<NSMenuItem*> items;
    ForEachItem(main_menu, [&items, tag](NSMenuItem* item) {
      if (item.tag == tag && !item.separatorItem && !item.submenu) {
        items.push_back(item);
      }
    });
    for (NSMenuItem* item : items) {
      NSMenu* from = item.menu;
      if (from == menu && [from indexOfItem:item] < at) {
        --at;
      }
      [from removeItem:item];
      [menu insertItem:item atIndex:at++];
    }
  }
  return at;
}

// Moves the top-level `item` right before `before` (the end when nil).
void MoveTop(NSMenu* main_menu, NSMenuItem* item, NSMenuItem* before) {
  if (!item || item == before) {
    return;
  }
  [main_menu removeItem:item];
  const NSInteger at = before ? [main_menu indexOfItem:before] : -1;
  [main_menu insertItem:item atIndex:at >= 0 ? at : main_menu.numberOfItems];
}

NSMenuItem* NewItem(const clu::CommandSpec& spec, int tag) {
  NSMenuItem* item =
      [[NSMenuItem alloc] initWithTitle:MenuTitle(spec)
                                 action:@selector(commandDispatch:)
                          keyEquivalent:@""];
  item.tag = tag;
  return item;
}

// `item`'s chord and its alternates: hidden items right after it, with the same action and tag. Alternates a previous
// pass added go first. A shifted punctuation or digit chord shows its unshifted key with ⇧, and a hidden alternate
// carries the chord AppKit and Chromium's key lookup match (they compare the shifted character).
void SetKeys(NSMenuItem* item,
             const clu::CommandSpec& spec,
             const clu::CommandKeys& keys) {
  NSMenu* menu = item.menu;
  for (NSMenuItem* other in [menu.itemArray copy]) {
    if (other != item && other.tag == item.tag && other.hidden &&
        other.allowsKeyEquivalentWhenHidden) {
      [menu removeItem:other];
    }
  }
  std::vector<Chord> hidden;
  if (keys.primary) {
    const Chord chord = ChordOf(*keys.primary);
    NSString* unshifted = UnshiftedKey(keys.primary->key);
    if (unshifted && (keys.primary->modifiers & ui::EF_SHIFT_DOWN) &&
        !(chord.mask & NSEventModifierFlagShift)) {
      SetChord(item, {unshifted, chord.mask | NSEventModifierFlagShift});
      hidden.push_back(chord);
    } else {
      SetChord(item, chord);
    }
  } else {
    ClearChord(item);
  }
  for (const clu::CommandAlt& alt : keys.alts) {
    if (!clu::IsPageOnlyAlt(spec, alt)) {
      hidden.push_back(ChordOf(alt.accelerator));
    }
  }
  NSInteger at = [menu indexOfItem:item];
  for (const Chord& chord : hidden) {
    NSMenuItem* alternate = [[NSMenuItem alloc] initWithTitle:item.title
                                                       action:item.action
                                                keyEquivalent:@""];
    alternate.target = item.target;
    alternate.tag = item.tag;
    alternate.hidden = YES;
    // AppKit fires a hidden item's chord only when allowed, for windows without a shell (DevTools, popups).
    alternate.allowsKeyEquivalentWhenHidden = YES;
    SetChord(alternate, chord);
    [menu insertItem:alternate atIndex:++at];
  }
}

// The visible item of a table command (alternates are hidden items with its tag).
NSMenuItem* PrimaryItem(NSMenu* menu, int tag) {
  NSMenuItem* found = nil;
  ForEachItem(menu, [&found, tag](NSMenuItem* item) {
    if (!found && item.tag == tag && !item.separatorItem &&
        !(item.hidden && item.allowsKeyEquivalentWhenHidden)) {
      found = item;
    }
  });
  return found;
}

// Chromium items ApplyShortcuts() took a chord from, with that chord, so a later change can give it back.
NSMapTable<NSMenuItem*, NSArray*>* ClearedByShortcuts() {
  static NSMapTable<NSMenuItem*, NSArray*>* cleared =
      [NSMapTable weakToStrongObjectsMapTable];
  return cleared;
}

base::CallbackListSubscription& ShortcutsSubscription() {
  static base::NoDestructor<base::CallbackListSubscription> subscription;
  return *subscription;
}

// Keeps Check for Updates… listed only while the build can update.
base::CallbackListSubscription& UpdateItemSubscription() {
  static base::NoDestructor<base::CallbackListSubscription> subscription;
  return *subscription;
}

}  // namespace

// Check for Updates… (docs/chromium/m12.md) works with no window open, where AppController would disable a
// commandDispatch: item it doesn't know. With a Rio window in front it runs as that window's command, so the window
// toasts the outcome.
@interface CluUpdateMenuTarget : NSObject
+ (instancetype)shared;
- (void)checkForUpdates:(id)sender;
@end

@implementation CluUpdateMenuTarget
+ (instancetype)shared {
  static CluUpdateMenuTarget* shared = [[CluUpdateMenuTarget alloc] init];
  return shared;
}

- (void)checkForUpdates:(id)sender {
  id window = [NSApp targetForAction:@selector(commandDispatch:)
                                  to:nil
                                from:sender];
  if (window && window != NSApp.delegate) {
    [NSApp sendAction:@selector(commandDispatch:) to:window from:sender];
    return;
  }
  clu::CluUpdateService::Get().EnsureStarted();
  clu::CluUpdateService::Get().Check(base::DoNothing());
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
  return clu::CluUpdateService::Get().available();
}
@end

// The Spaces menu shows the space names of the window in front (docs/PLAN.md 8 `commands.setTitle`), one row per space:
// a space the provider says isn't there (an empty title) hides its row. The View and Tabs menus use it too, for Hide or
// Show Sidebar and Pin or Unpin Tab.
@interface CluSpacesMenuDelegate : NSObject <NSMenuDelegate>
@end

@implementation CluSpacesMenuDelegate

- (void)menuNeedsUpdate:(NSMenu*)menu {
  for (NSMenuItem* item in menu.itemArray) {
    const clu::CommandSpec* spec = clu::FindCommandByIdc(item.tag);
    // Hidden alternates stay hidden.
    if (!spec || (item.hidden && item.allowsKeyEquivalentWhenHidden)) {
      continue;
    }
    std::optional<std::u16string> title =
        g_title_provider ? g_title_provider(spec->id) : std::nullopt;
    item.hidden = (title && title->empty()) || IsKeyOnly(spec->id);
    // Stay On Top is checked while the window in front floats.
    if (std::string_view(spec->id) == "window.stayOnTop") {
      item.state = NSApp.mainWindow.level > NSNormalWindowLevel
                       ? NSControlStateValueOn
                       : NSControlStateValueOff;
    }
    item.title = title && !title->empty() ? base::SysUTF16ToNSString(*title)
                                           : MenuTitle(*spec);
    // View ▸ Appearance checks the mode in use.
    if (std::optional<bool> checked =
            g_check_provider ? g_check_provider(spec->id) : std::nullopt) {
      item.state = *checked ? NSControlStateValueOn : NSControlStateValueOff;
    }
  }
}

@end

namespace clu {

void SetMenuTitleProvider(MenuTitleProvider provider) {
  g_title_provider = provider;
}

void SetMenuCheckProvider(MenuCheckProvider provider) {
  g_check_provider = provider;
}

void ApplyMainMenu(NSMenu* main_menu) {
  if (!main_menu || main_menu.numberOfItems == 0) {
    return;
  }
  NSMenu* app_menu = main_menu.itemArray.firstObject.submenu;

  // The chords of the table, with the tag each belongs to.
  struct Bound {
    Chord chord;
    int tag;
  };
  std::vector<Bound> bound;
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.variant == CommandVariant::kMini) {
      continue;
    }
    const int tag = MenuTagFor(spec);
    const CommandKeys keys = KeysFor(spec);
    if (keys.primary) {
      bound.push_back({ChordOf(*keys.primary), tag});
    }
    for (const CommandAlt& alt : keys.alts) {
      if (!IsPageOnlyAlt(spec, alt)) {
        bound.push_back({ChordOf(alt.accelerator), tag});
      }
    }
  }

  // 1. What Clu's layout has no use for.
  for (int tag : kRemovedTags) {
    while (NSMenuItem* item = ItemWithTag(main_menu, tag)) {
      [item.menu removeItem:item];
    }
  }
  if (NSMenuItem* bookmarks = [main_menu itemWithTag:IDC_BOOKMARKS_MENU]) {
    bookmarks.hidden = YES;
  }

  // 2. A Chromium item whose chord the table gives another command loses it, so a key has one meaning.
  ForEachItem(main_menu, [&bound](NSMenuItem* item) {
    for (const Bound& b : bound) {
      if (SameChord(item, b.chord) && item.tag != b.tag) {
        ClearChord(item);
        return;
      }
    }
  });

  // 3. The menus the table names. The Tab menu becomes Clu's Tabs menu; Spaces follows it.
  NSMenuItem* tab_item = [main_menu itemWithTag:IDC_TAB_MENU];
  NSMenu* tabs = tab_item.submenu;
  if (tabs) {
    [tabs removeAllItems];
    tab_item.title = @"Tabs";
    tabs.title = @"Tabs";
  }
  NSMenuItem* spaces_item = [[NSMenuItem alloc] initWithTitle:@"Spaces"
                                                       action:nil
                                                keyEquivalent:@""];
  NSMenu* spaces = [[NSMenu alloc] initWithTitle:@"Spaces"];
  static CluSpacesMenuDelegate* delegate = [[CluSpacesMenuDelegate alloc] init];
  spaces.delegate = delegate;
  spaces_item.submenu = spaces;
  // Spaces comes before Tabs.
  const NSInteger tab_index = tab_item ? [main_menu indexOfItem:tab_item] : -1;
  [main_menu insertItem:spaces_item
                atIndex:tab_index >= 0 ? tab_index
                                       : main_menu.numberOfItems - 1];

  auto menu_for = [&](CommandMenu menu) -> NSMenu* {
    switch (menu) {
      case CommandMenu::kFile:
        return [main_menu itemWithTag:IDC_FILE_MENU].submenu;
      case CommandMenu::kEdit:
        return [main_menu itemWithTag:IDC_EDIT_MENU].submenu;
      case CommandMenu::kView:
        return [main_menu itemWithTag:IDC_VIEW_MENU].submenu;
      case CommandMenu::kHistory:
        return [main_menu itemWithTag:IDC_HISTORY_MENU].submenu;
      case CommandMenu::kTabs:
        return tabs;
      case CommandMenu::kSpaces:
        return spaces;
      case CommandMenu::kWindow:
        return [main_menu itemWithTag:IDC_WINDOW_MENU].submenu;
      case CommandMenu::kHelp:
        // Chromium's Help menu is the last one and has no tag.
        return main_menu.itemArray.lastObject.submenu;
    }
    return nil;
  };

  // Menus that got a first Clu item: it starts a group of its own.
  std::vector<NSMenu*> grouped = {tabs, spaces};
  auto append = [&](NSMenu* menu, NSMenuItem* item) {
    if (std::ranges::find(grouped, menu) == grouped.end()) {
      grouped.push_back(menu);
      if (menu.numberOfItems && !menu.itemArray.lastObject.separatorItem) {
        [menu addItem:[NSMenuItem separatorItem]];
      }
    }
    [menu addItem:item];
  };

  for (const CommandSpec& spec : CommandTable()) {
    if (spec.variant == CommandVariant::kMini) {
      continue;
    }
    const int tag = MenuTagFor(spec);
    NSMenuItem* item = tag != spec.idc ? ItemWithTag(main_menu, tag) : nil;
    // A Chromium item outside the Tabs menu Clu rebuilt: it keeps its place and validation.
    if (item && item.menu != tabs) {
      if (item.menu != app_menu) {
        item.title = MenuTitle(spec);
      }
      // Chromium's key lookup (CommandForKeyEvent) sees only commandDispatch: items and Close Window. Settings…
      // (showPreferences:) becomes one, so Cmd+, is a menu key the page gets first; AppController runs
      // commandDispatch: IDC_OPTIONS when no window is open.
      if ((spec.has_accelerator || KeysFor(spec).primary) &&
          item.action != @selector(commandDispatch:) &&
          item.action != @selector(performClose:)) {
        item.action = @selector(commandDispatch:);
        item.target = nil;
      }
    } else if (std::string_view(spec.id) == "platform.checkForUpdates" &&
               app_menu) {
      // Mac apps keep Check for Updates… in the app menu, right after About (docs/chromium/m12.md).
      item = NewItem(spec, tag);
      item.target = [CluUpdateMenuTarget shared];
      item.action = @selector(checkForUpdates:);
      item.hidden = !CluUpdateService::Get().available();
      NSMenuItem* __weak weak_item = item;
      UpdateItemSubscription() =
          CluUpdateService::Get().AddObserver(base::BindRepeating(^{
            weak_item.hidden = !CluUpdateService::Get().available();
          }));
      // The app menu: About, Settings…, Import…, Check for Updates….
      NSMenuItem* anchor = [app_menu itemWithTag:IDC_IMPORT_SETTINGS];
      anchor = anchor ? anchor : [app_menu itemWithTag:IDC_OPTIONS];
      anchor = anchor ? anchor : [app_menu itemWithTag:IDC_ABOUT];
      [app_menu insertItem:item
                   atIndex:anchor ? [app_menu indexOfItem:anchor] + 1 : 0];
    } else {
      item = NewItem(spec, tag);
      NSMenu* menu = menu_for(spec.menu);
      if (!menu) {
        continue;
      }
      append(menu, item);
    }
    SetKeys(item, spec, KeysFor(spec));
    if (IsKeyOnly(spec.id)) {
      item.hidden = YES;
    }
  }

  // The menu bar's order: Rio, File, Edit, View, Spaces, Tabs, Archive, Extensions, then
  // Profiles, Window and Help. Archive is Chromium's History menu (it keeps its tag for HistoryMenuBridge, which takes
  // no item of another tag: ShouldMenuItemBeVisible() hits NOTREACHED). Extensions takes Manage Extensions… out of the
  // Window menu. Profiles stays: ProfileMenuController shows and hides it by itself.
  NSMenuItem* window_item = [main_menu itemWithTag:IDC_WINDOW_MENU];
  NSMenuItem* history_item = [main_menu itemWithTag:IDC_HISTORY_MENU];
  if (NSMenu* archive = history_item.submenu) {
    history_item.title = @"Archive";
    archive.title = @"Archive";
    MoveTop(main_menu, history_item, window_item);
  }
  // Delete Browsing Data… shows in no menu (Settings > Profiles has it): it leaves the app menu's list and stays as a
  // hidden item at its end, so Cmd+Shift+Delete still runs it.
  if (NSMenuItem* clear = [app_menu itemWithTag:IDC_CLEAR_BROWSING_DATA]) {
    [app_menu removeItem:clear];
    TidySeparators(app_menu);
    clear.hidden = YES;
    clear.allowsKeyEquivalentWhenHidden = YES;
    [app_menu addItem:clear];
  }
  if (NSMenuItem* manage = ItemWithTag(main_menu, IDC_MANAGE_EXTENSIONS)) {
    NSMenu* from = manage.menu;
    [from removeItem:manage];
    TidySeparators(from);
    manage.title = @"Manage Extensions…";
    NSMenu* extensions = [[NSMenu alloc] initWithTitle:@"Extensions"];
    [extensions addItem:manage];
    NSMenuItem* extensions_item =
        [[NSMenuItem alloc] initWithTitle:@"Extensions"
                                   action:nil
                            keyEquivalent:@""];
    extensions_item.submenu = extensions;
    [main_menu insertItem:extensions_item atIndex:0];
    MoveTop(main_menu, extensions_item, window_item);
  }
  MoveTop(main_menu, [main_menu itemWithTag:IDC_PROFILE_MAIN_MENU],
          window_item);

  // Edit: Copy URL and Copy URL as Markdown follow Copy.
  if (NSMenu* edit = menu_for(CommandMenu::kEdit)) {
    if (NSMenuItem* copy = [edit itemWithTag:IDC_CONTENT_CONTEXT_COPY]) {
      Place(main_menu, edit, [edit indexOfItem:copy] + 1,
            {"edit.copyUrl", "edit.copyUrlMarkdown"});
      TidySeparators(edit);
    }
  }

  // File's order: New Tab…, New Window, New Private Window, New Mini Window, Restore Last
  // Closed Tab, Open Command Bar, New Profile…, then Chromium's items from Open File… on, with Capture… ⇧⌘2 above
  // Print.
  if (NSMenu* file = menu_for(CommandMenu::kFile)) {
    Place(main_menu, file, 0,
          {"commandbar.newTab", "window.new", "window.newPrivate", "mini.new",
           "archive.reopenClosed", "commandbar.editUrl", "profiles.newProfile",
           ""});
    const NSInteger print = [file indexOfItemWithTag:IDC_PRINT];
    if (print >= 0) {
      Place(main_menu, file, print, {"page.capture", ""});
    }
    TidySeparators(file);
  }

  // Tabs' order: Pin Tab, New Folder…, Next and Previous Tab, Reveal Tab in Sidebar, the row actions, Clear
  // Today, Reset all tabs in this Space, then the blocker and Site tweaks rows. The go-to and recent-tab chords stay
  // keys only.
  if (tabs) {
    Place(main_menu, tabs, 0,
          {"tabs.togglePin", "tabs.newFolder", "", "tabs.next", "tabs.prev",
           "tabs.reveal", "", "tabs.duplicate", "tabs.toggleMute",
           "split.addToSplit", "split.openInSplit", "peek.expand", "",
           "archive.clearToday", "archive.undoClear", "tabs.resetAll", ""});
    TidySeparators(tabs);
    tabs.delegate = delegate;
  }

  // Spaces' order: New Space…, Edit Theme…, Edit Space…, Next and Previous Space, then the
  // spaces. New Profile… sits in File.
  Place(main_menu, spaces, 0,
        {"spaces.newSpace", "theme.editTheme", "spaces.editSpace", "",
         "spaces.next", "spaces.prev", ""});
  TidySeparators(spaces);

  // The app menu: About, Settings…, Set as Default Browser, Import…, Check for Updates….
  if (NSMenuItem* settings = [app_menu itemWithTag:IDC_OPTIONS]) {
    Place(main_menu, app_menu, [app_menu indexOfItem:settings] + 1,
          {"platform.setDefaultBrowser"});
  }

  // Help: Essential Keyboard Shortcuts and Contact the Team first, then Rio Help, then
  // Troubleshooting ▸ with Open Task Manager (out of the Window menu), Reveal Rio Data and Copy Rio Info.
  if (NSMenu* help = menu_for(CommandMenu::kHelp)) {
    Place(main_menu, help, 0, {"platform.shortcuts", "platform.contactTeam"});
    NSMenu* troubleshooting =
        [[NSMenu alloc] initWithTitle:@"Troubleshooting"];
    if (NSMenuItem* item =
            ItemWithTag(main_menu, IDC_TASK_MANAGER_MAIN_MENU)) {
      NSMenu* window_menu = item.menu;
      [window_menu removeItem:item];
      TidySeparators(window_menu);
      item.title = @"Open Task Manager";
      [troubleshooting addItem:item];
    }
    for (std::string_view id :
         {"platform.showLogs", "platform.copyDiagnostics"}) {
      const CommandSpec* spec = FindCommand(id);
      if (NSMenuItem* item =
              spec ? [help itemWithTag:MenuTagFor(*spec)] : nil) {
        [help removeItem:item];
        [troubleshooting addItem:item];
      }
    }
    if (troubleshooting.numberOfItems) {
      NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:@"Troubleshooting"
                                                     action:nil
                                              keyEquivalent:@""];
      entry.submenu = troubleshooting;
      [help addItem:[NSMenuItem separatorItem]];
      [help addItem:entry];
    }
    TidySeparators(help);
  }

  // Chromium's tab list follows the Tabs items (TabMenuBridge adds it after the last static item).
  if (tabs) {
    [tabs addItem:[NSMenuItem separatorItem]];
  }
  if (NSMenu* view = [main_menu itemWithTag:IDC_VIEW_MENU].submenu) {
    // View's order: Appearance ▸, the sidebar rows, loading and refreshing, split view, zoom, then
    // the rest, Cast and Developer ▸.
    Place(main_menu, view, 0,
          {"sidebar.toggle", "tabs.collapsePinned", "", "nav.stop",
           "nav.reload", "nav.hardReload", "page.clearCookiesAndRefresh",
           "page.clearCacheAndRefresh", "", "split.add", "split.separate",
           "split.expand", "", "zoom.reset", "zoom.in", "zoom.out", ""});
    // Appearance ▸ Automatic, Light, Dark, checked on the mode in use.
    NSMenu* appearance = [[NSMenu alloc] initWithTitle:@"Appearance"];
    [appearance addItem:[[NSMenuItem alloc]
                            initWithTitle:@"Rio and websites will use:"
                                   action:nil
                            keyEquivalent:@""]];
    Place(main_menu, appearance, 1,
          {"theme.appearanceAuto", "theme.appearanceLight",
           "theme.appearanceDark"});
    if (appearance.numberOfItems > 1) {
      appearance.delegate = delegate;
      NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:@"Appearance"
                                                     action:nil
                                              keyEquivalent:@""];
      entry.submenu = appearance;
      [view insertItem:entry atIndex:0];
    }
    // Enter Full Screen (and its Ctrl+Cmd+F alternate right after it) goes last.
    if (NSMenuItem* fullscreen = [view itemWithTag:IDC_FULLSCREEN]) {
      const NSInteger at = [view indexOfItem:fullscreen];
      NSMenuItem* next =
          at + 1 < view.numberOfItems ? [view itemAtIndex:at + 1] : nil;
      [view removeItem:fullscreen];
      [view addItem:[NSMenuItem separatorItem]];
      [view addItem:fullscreen];
      if (next.alternate) {
        [view removeItem:next];
        [view addItem:next];
      }
    }
    view.delegate = delegate;
    TidySeparators(view);
  }
  // Window: Stay On Top first, checked by the delegate.
  if (NSMenu* window_menu = window_item.submenu) {
    Place(main_menu, window_menu, 0, {"window.stayOnTop", ""});
    [window_menu itemWithTag:IDC_MINIMIZE_WINDOW].title = @"Minimize Window";
    TidySeparators(window_menu);
    window_menu.delegate = delegate;
  }

  // Settings, Shortcuts: the menu takes the user's keys whenever they change.
  ShortcutsSubscription() = AddShortcutsChangedCallback(base::BindRepeating(^{
    ApplyShortcuts(NSApp.mainMenu);
  }));

  // The Spaces menu follows the window in front, so a private window hides it. Chromium marks the browser active
  // during the same activation; the update reads it once that has run.
  auto observe = [](__strong id& observer, NSNotificationName name) {
    if (observer) {
      [NSNotificationCenter.defaultCenter removeObserver:observer];
    }
    observer = [NSNotificationCenter.defaultCenter
        addObserverForName:name
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* notification) {
                  dispatch_async(dispatch_get_main_queue(), ^{
                    UpdateSpacesMenu(NSApp.mainMenu);
                  });
                }];
  };
  static id became_main_observer = nil;
  static id resigned_main_observer = nil;
  observe(became_main_observer, NSWindowDidBecomeMainNotification);
  observe(resigned_main_observer, NSWindowDidResignMainNotification);
}

void ApplyShortcuts(NSMenu* main_menu) {
  if (!main_menu) {
    return;
  }
  // Chords a previous pass took from Chromium's items go back first; the pass below takes again what is still used.
  NSMapTable<NSMenuItem*, NSArray*>* cleared = ClearedByShortcuts();
  for (NSMenuItem* item in [cleared keyEnumerator].allObjects) {
    NSArray* chord = [cleared objectForKey:item];
    if (item.keyEquivalent.length == 0 && chord.count == 2) {
      item.keyEquivalent = chord[0];
      item.keyEquivalentModifierMask = [chord[1] unsignedIntegerValue];
    }
  }
  [cleared removeAllObjects];
  std::vector<std::pair<Chord, int>> bound;
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.variant == CommandVariant::kMini) {
      continue;
    }
    const int tag = MenuTagFor(spec);
    NSMenuItem* item = PrimaryItem(main_menu, tag);
    if (!item) {
      continue;
    }
    const CommandKeys keys = KeysFor(spec);
    SetKeys(item, spec, keys);
    if (keys.primary) {
      bound.emplace_back(ChordOf(*keys.primary), tag);
    }
  }
  // A Chromium item with a chord the user gave a table command loses it, so a key has one meaning.
  ForEachItem(main_menu, [&bound, cleared](NSMenuItem* item) {
    for (const auto& [chord, tag] : bound) {
      if (item.tag != tag && SameChord(item, chord)) {
        if (!clu::FindCommandByIdc(item.tag)) {
          [cleared setObject:@[ item.keyEquivalent, @(item.keyEquivalentModifierMask) ]
                      forKey:item];
        }
        ClearChord(item);
        return;
      }
    }
  });
}

void UpdateSpacesMenu(NSMenu* main_menu) {
  NSMenuItem* entry = nil;
  for (NSMenuItem* item in main_menu.itemArray) {
    if ([item.submenu.title isEqualToString:@"Spaces"]) {
      entry = item;
      break;
    }
  }
  NSMenu* spaces = entry.submenu;
  if (!spaces) {
    return;
  }
  [spaces.delegate menuNeedsUpdate:spaces];
  BOOL any = NO;
  for (NSMenuItem* item in spaces.itemArray) {
    if (!item.hidden && !item.separatorItem) {
      any = YES;
      break;
    }
  }
  entry.hidden = !any;
}

void UpdateMenuItems(NSMenu* main_menu) {
  ForEachItem(main_menu, [](NSMenuItem* item) {
    if (item.submenu && item.submenu != NSApp.servicesMenu) {
      [item.submenu update];
    }
  });
}

}  // namespace clu
