// Copyright 2026 The Rio Authors. All rights reserved.

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/apple/owned_objc.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/commands/command_service.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#import "chrome/browser/ui/cocoa/chrome_command_dispatcher_delegate.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/common/extension.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/accelerators/accelerator.h"
#import "ui/base/accelerators/platform_accelerator_cocoa.h"
#include "ui/base/base_window.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/clipboard_test_util.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#import "ui/base/cocoa/nsmenu_additions.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "testing/gtest_mac.h"
#include "url/gurl.h"

// The CluKeys* checks of the M5 "Done when" row (docs/chromium/PLAN.md section 6): every docs/PLAN.md 8 chord from a
// focused page, with real NSEvents on the Mac key path. Router chords run in ChromeCommandDispatcherDelegate's
// prePerformKeyEquivalent and never reach the page; menu chords reach the page first and run once it leaves them.
namespace clu {
namespace {

// The chord as the page's keylog writes it (spaces/keylog.html): Meta, Control, Alt, Shift, then the key's code.
std::string KeylogChord(const CommandAccelerator& a, NSEvent* event) {
  std::string chord;
  auto add = [&](std::string_view part) {
    if (!chord.empty()) {
      chord += "+";
    }
    chord += part;
  };
  if (a.modifiers & ui::EF_COMMAND_DOWN) add("Meta");
  if (a.modifiers & ui::EF_CONTROL_DOWN) add("Control");
  if (a.modifiers & ui::EF_ALT_DOWN) add("Alt");
  if (a.modifiers & ui::EF_SHIFT_DOWN) add("Shift");
  add(ui::KeycodeConverter::DomCodeToCodeString(
      ui::KeycodeConverter::NativeKeycodeToDomCode(event.keyCode)));
  return chord;
}

// The first item of `menu` or its submenus with `action`.
NSMenuItem* ItemWithAction(NSMenu* menu, SEL action) {
  for (NSMenuItem* item in menu.itemArray) {
    if (item.action == action) {
      return item;
    }
    if (item.submenu) {
      if (NSMenuItem* found = ItemWithAction(item.submenu, action)) {
        return found;
      }
    }
  }
  return nil;
}

NSEventModifierFlags FlagsOf(int modifiers) {
  NSEventModifierFlags flags = 0;
  if (modifiers & ui::EF_COMMAND_DOWN) flags |= NSEventModifierFlagCommand;
  if (modifiers & ui::EF_CONTROL_DOWN) flags |= NSEventModifierFlagControl;
  if (modifiers & ui::EF_ALT_DOWN) flags |= NSEventModifierFlagOption;
  if (modifiers & ui::EF_SHIFT_DOWN) flags |= NSEventModifierFlagShift;
  return flags;
}

// A key down of `accelerator` in `window`, with the characters the keyboard sends. A real Backspace key sends
// NSDeleteCharacter, which Chromium's menu key lookup matches to a menu item's NSBackspaceCharacter;
// SynthesizeKeyEvent() gives NSBackspaceCharacter itself, which no Backspace key ever sends.
NSEvent* KeyDown(NSWindow* window, const CommandAccelerator& accelerator) {
  if (accelerator.key == ui::VKEY_BACK) {
    const unichar del = NSDeleteCharacter;
    NSString* characters = [NSString stringWithCharacters:&del length:1];
    return [NSEvent keyEventWithType:NSEventTypeKeyDown
                            location:NSZeroPoint
                       modifierFlags:FlagsOf(accelerator.modifiers)
                           timestamp:0
                        windowNumber:window.windowNumber
                             context:nil
                          characters:characters
         charactersIgnoringModifiers:characters
                           isARepeat:NO
                             keyCode:kVK_Delete];
  }
  return cocoa_test_event_utils::SynthesizeKeyEvent(
      window, /*keyDown=*/true, accelerator.key,
      FlagsOf(accelerator.modifiers));
}

// chrome/browser/ui/accelerator_table.cc kReservedCommandIds (Mac, 158).
bool ChromeReserves(int idc) {
  constexpr int kReserved[] = {
      IDC_CLOSE_TAB,         IDC_CLOSE_WINDOW,      IDC_NEW_INCOGNITO_WINDOW,
      IDC_NEW_ISOLATED_WINDOW, IDC_NEW_TAB,         IDC_NEW_WINDOW,
      IDC_RESTORE_TAB,       IDC_SELECT_NEXT_TAB,   IDC_SELECT_PREVIOUS_TAB,
      IDC_CYCLE_TO_NEXT_TAB, IDC_CYCLE_TO_PREV_TAB, IDC_EXIT,
  };
  return std::ranges::contains(kReserved, idc);
}

struct Chord {
  raw_ptr<const CommandSpec> spec;
  CommandAccelerator accelerator;
  bool reserved;
};

// Every chord of the main variant the key table binds (Cmd+Left and Cmd+Right stay Chromium's page-only Back and
// Forward; CluKeysTheRoleItemsKeepTheirKeys checks them).
std::vector<Chord> MainChords() {
  std::vector<Chord> out;
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.variant == CommandVariant::kMini) {
      continue;
    }
    if (spec.has_accelerator) {
      out.push_back({&spec, spec.accelerator, IsReservedEntry(spec)});
    }
    for (const CommandAlt& alt : spec.alts) {
      if (!IsPageOnlyAlt(spec, alt)) {
        out.push_back({&spec, alt.accelerator,
                       alt.route == CommandRoute::kRouter ||
                           IsReservedEntry(spec)});
      }
    }
  }
  return out;
}

class CluKeysBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    ui::TestClipboard::CreateForCurrentThread();
    shell().RecordKeysForTesting();
    // The window is key, as it is under the user's hands, so the main menu validates against it. InProcessBrowserTest
    // already fakes window focus on Mac (ScopedFakeNSWindowFocus); a second fake would swap its swizzles back.
    [ns_window() makeKeyWindow];
  }

  void TearDownOnMainThread() override {
    shell().SetRecordOnlyForTesting(false);
    ui::Clipboard::DestroyClipboardForCurrentThread();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  NSWindow* ns_window() {
    return browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }
  views::FocusManager* focus_manager() {
    return views::Widget::GetWidgetForNativeWindow(
               browser()->GetWindow()->GetNativeWindow())
        ->GetFocusManager();
  }

  // A keylog page (spaces/keylog.html) shown and focused; `prevent` makes the page claim that chord. Its "+" is
  // escaped, since URLSearchParams reads a bare "+" as a space.
  content::WebContents* FocusedKeylog(const std::string& prevent = "") {
    const GURL url = embedded_test_server()->GetURL(
        prevent.empty()
            ? "/spaces/keylog.html"
            : base::StrCat({"/spaces/keylog.html?prevent=",
                            base::EscapeQueryParamValue(prevent,
                                                        /*use_plus=*/false)}));
    base::expected<std::string, std::string> row =
        window().CreateRow(url, std::nullopt, /*activate=*/true);
    EXPECT_TRUE(row.has_value());
    content::WebContents* page = window().FindTab(row.value())->GetContents();
    EXPECT_TRUE(content::WaitForLoadStop(page));
    // Chromium holds a new page's input until its first frame (paint holding): a key before it comes back unhandled
    // at once and runs the browser's accelerator, as it would for a user typing before the page shows.
    content::WaitForCopyableViewInWebContents(page);
    page->Focus();
    return page;
  }

  NSEvent* EventFor(const CommandAccelerator& accelerator) {
    return KeyDown(ns_window(), accelerator);
  }

  // The key event as the focused page's view hands it to the page. Its window is one that is not key, so a key the
  // page leaves alone comes back through BrowserView::HandleKeyboardEvent() to the window's accelerators, the
  // non-AppKit half of Mac's redispatch (CommandDispatcher redispatches only into the key window, and AppKit's own
  // routing of that is for clu_interactive_ui_tests).
  void ForwardToPage(content::WebContents* page,
                     const CommandAccelerator& accelerator) {
    if (!other_window_) {
      other_window_ =
          [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 10, 10)
                                      styleMask:NSWindowStyleMaskBorderless
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
      other_window_.releasedWhenClosed = NO;
    }
    NSEvent* event = KeyDown(other_window_, accelerator);
    page->GetPrimaryMainFrame()->GetRenderWidgetHost()->ForwardKeyboardEvent(
        input::NativeWebKeyboardEvent(base::apple::OwnedNSEvent(event)));
  }

  std::string Keylog(content::WebContents* page) {
    return content::EvalJs(page, "window.__keys.join(',')").ExtractString();
  }

  // The page saw `chord`, or any chord starting with it.
  bool PageSaw(content::WebContents* page, const std::string& chord) {
    for (const std::string& seen : base::SplitString(
             Keylog(page), ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
      if (seen.starts_with(chord)) {
        return true;
      }
    }
    return false;
  }

  bool ShellRan(const std::string& id) {
    return std::ranges::contains(shell().key_commands_for_testing(), id);
  }

  NSWindow* __strong other_window_;
};

// The generated table, the main menu and the reservations agree: each chord is a main menu key equivalent with its
// command's tag (Chromium's key lookup finds it), router chords are reserved, and every chord is an accelerator of
// the window at the priority its route says.
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest,
                       CluKeysEveryChordResolvesThroughTheMainMenu) {
  FocusedKeylog();
  chrome::BrowserCommandController* controller =
      chrome::BrowserCommandController::From(browser());
  const input::NativeWebKeyboardEvent dummy(
      blink::WebInputEvent::Type::kRawKeyDown, 0, base::TimeTicks());
  for (const Chord& chord : MainChords()) {
    const CommandSpec& spec = *chord.spec;
    const int tag = MenuTagFor(spec);
    const ui::Accelerator accelerator(chord.accelerator.key,
                                      chord.accelerator.modifiers);
    // The window's accelerator at the right priority.
    EXPECT_TRUE(focus_manager()->IsAcceleratorRegistered(accelerator))
        << spec.id;
    EXPECT_EQ(focus_manager()->HasPriorityHandler(accelerator), chord.reserved)
        << spec.id;
    // The reservation Chromium asks for (IsReservedCommandOrKey()). It is per command, so it follows the entry's
    // route: a router alternate of a menu entry (Ctrl+Cmd+S beside Cmd+S) is reserved by its high-priority
    // accelerator above, which Chromium runs before it asks.
    EXPECT_EQ(controller->IsReservedCommandOrKey(tag, dummy),
              IsReservedEntry(spec) || ChromeReserves(tag))
        << spec.id;
    // The main menu holds the chord with the command's tag while the command can run; a disabled item (a command
    // of a later milestone, Back without history) is skipped by the lookup.
    CommandForKeyEventResult result =
        CommandForKeyEvent(EventFor(chord.accelerator));
    if (chrome::IsCommandEnabled(browser(), tag)) {
      EXPECT_TRUE(result.found()) << spec.id;
      EXPECT_TRUE(result.from_main_menu) << spec.id;
      EXPECT_EQ(result.chrome_command, tag) << spec.id;
    } else {
      EXPECT_FALSE(result.from_main_menu && result.chrome_command == tag)
          << spec.id;
    }
  }
}

// Router chords: ChromeCommandDispatcherDelegate runs them in prePerformKeyEquivalent, before the focused page's
// view gets the event, so the page never sees them.
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest,
                       CluKeysRouterChordsNeverReachThePage) {
  content::WebContents* page = FocusedKeylog();
  // Every chord records its command instead of running it, so the walk has no side effects.
  shell().SetRecordOnlyForTesting(true);
  ChromeCommandDispatcherDelegate* delegate =
      [[ChromeCommandDispatcherDelegate alloc] init];
  for (const Chord& chord : MainChords()) {
    if (!chord.reserved) {
      continue;
    }
    shell().ClearKeyCommandsForTesting();
    const ui::PerformKeyEquivalentResult result =
        [delegate prePerformKeyEquivalent:EventFor(chord.accelerator)
                                   window:ns_window()];
    EXPECT_EQ(result, ui::PerformKeyEquivalentResult::kHandled)
        << chord.spec->id;
    EXPECT_EQ(shell().key_commands_for_testing(),
              std::vector<std::string>{chord.spec->id})
        << chord.spec->id;
  }
  EXPECT_EQ(Keylog(page), "");
}

// Menu chords: prePerformKeyEquivalent leaves them alone, the page gets the key first, and only a key the page did
// not claim runs the command.
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest,
                       CluKeysMenuChordsReachThePageFirst) {
  content::WebContents* page = FocusedKeylog();
  shell().SetRecordOnlyForTesting(true);
  ChromeCommandDispatcherDelegate* delegate =
      [[ChromeCommandDispatcherDelegate alloc] init];
  int checked = 0;
  std::set<std::string> skipped;
  for (const Chord& chord : MainChords()) {
    if (chord.reserved) {
      continue;
    }
    const std::string id = chord.spec->id;
    // A command with nothing to do now (a later milestone's, Back without history) has a disabled menu item, so
    // Chromium's key lookup skips it and the window's accelerator declines it before the page gets the key: the
    // page still gets it, nothing ran. CluKeysEveryChordResolvesThroughTheMainMenu covers those chords.
    if (!chrome::IsCommandEnabled(browser(), MenuTagFor(*chord.spec))) {
      skipped.insert(id);
      continue;
    }
    ++checked;
    shell().ClearKeyCommandsForTesting();
    NSEvent* event = EventFor(chord.accelerator);
    EXPECT_EQ([delegate prePerformKeyEquivalent:event window:ns_window()],
              ui::PerformKeyEquivalentResult::kUnhandled)
        << id;
    EXPECT_TRUE(shell().key_commands_for_testing().empty()) << id;
    ForwardToPage(page, chord.accelerator);
    const std::string seen = KeylogChord(chord.accelerator, event);
    ASSERT_TRUE(base::test::RunUntil([&] { return PageSaw(page, seen); }))
        << id << " " << seen << ", the page saw: " << Keylog(page);
    // The page left it alone: the command runs after it (the window's normal-priority accelerator).
    EXPECT_TRUE(base::test::RunUntil([&] { return ShellRan(id); })) << id;
    page->Focus();
  }
  // Every menu chord ran but the split and Peek chords with no split or Peek to act on, and the four with nothing to do
  // on a page just loaded: no history to go back or forward through, nothing loading, the default zoom.
  EXPECT_EQ(skipped,
            (std::set<std::string>{"nav.back", "nav.forward", "nav.stop",
                                   "zoom.reset", "split.separate",
                                   "split.focus1", "split.focus2",
                                   "split.focus3", "split.focus4", "split.next",
                                   "split.prev", "peek.expand"}));
  EXPECT_EQ(checked, 31);
}

IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest,
                       CluKeysAPageThatClaimsAMenuChordKeepsIt) {
  // The page claims Cmd+S: the sidebar stays. Ctrl+Cmd+S, the router alternate, always toggles (12.3 box 2).
  content::WebContents* page = FocusedKeylog("Meta+KeyS");
  const bool hidden = shell().chrome_state().sidebar_hidden;
  ForwardToPage(page, {ui::VKEY_S, ui::EF_COMMAND_DOWN});
  ASSERT_TRUE(base::test::RunUntil([&] { return PageSaw(page, "Meta+KeyS"); }))
      << "the page saw: " << Keylog(page);
  // One more round trip through the renderer, so an unhandled key would have come back by now.
  EXPECT_TRUE(content::ExecJs(page, "true"));
  EXPECT_FALSE(ShellRan("sidebar.toggle"));
  EXPECT_EQ(shell().chrome_state().sidebar_hidden, hidden);

  ChromeCommandDispatcherDelegate* delegate =
      [[ChromeCommandDispatcherDelegate alloc] init];
  EXPECT_EQ([delegate prePerformKeyEquivalent:EventFor({ui::VKEY_S,
                                                        ui::EF_COMMAND_DOWN |
                                                            ui::EF_CONTROL_DOWN})
                                       window:ns_window()],
            ui::PerformKeyEquivalentResult::kHandled);
  EXPECT_NE(shell().chrome_state().sidebar_hidden, hidden);
}

// An extension shortcut assigned to a chord of Clu's key table (chrome://extensions/shortcuts, here Cmd+1) never
// becomes a second high-priority handler, which DCHECKs: the chord stays Clu's.
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest, CluKeysAnExtensionShortcutNeverTakesAKeyTableChord) {
  base::ScopedAllowBlockingForTesting allow;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(dir.GetPath().AppendASCII("manifest.json"), R"({
      "name": "Shortcut probe", "version": "1", "manifest_version": 3,
      "background": {"service_worker": "worker.js"},
      "commands": {"probe": {"description": "Probe",
                             "suggested_key": {"default": "Ctrl+Shift+Y"}}}})"));
  ASSERT_TRUE(base::WriteFile(dir.GetPath().AppendASCII("worker.js"), ""));
  extensions::ChromeTestExtensionLoader loader(browser()->GetProfile());
  scoped_refptr<const extensions::Extension> extension =
      loader.LoadExtension(dir.GetPath());
  ASSERT_TRUE(extension);
  // "Ctrl" is Command on Mac: Cmd+1, the first row (tabs.goto1, a router chord).
  extensions::CommandService::Get(browser()->GetProfile())
      ->UpdateKeybindingPrefs(extension->id(), "probe", "Ctrl+1");
  content::RunAllTasksUntilIdle();

  const ui::Accelerator cmd_1(ui::VKEY_1, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(focus_manager()->HasPriorityHandler(cmd_1));
  shell().RecordKeysForTesting();
  shell().ClearKeyCommandsForTesting();
  EXPECT_TRUE(focus_manager()->ProcessAccelerator(cmd_1));
  EXPECT_EQ(shell().key_commands_for_testing(),
            std::vector<std::string>{"tabs.goto1"});
}

// The chords run their commands for real, from the focused page: a sample across the lanes (CluTabs*, CluSpaces* and
// CluCommand* check each command's effect in depth).
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest, CluKeysChordsRunTheirCommands) {
  content::WebContents* page = FocusedKeylog();
  ChromeCommandDispatcherDelegate* delegate =
      [[ChromeCommandDispatcherDelegate alloc] init];
  auto press = [&](ui::KeyboardCode key, int modifiers) {
    return [delegate prePerformKeyEquivalent:EventFor({key, modifiers})
                                      window:ns_window()];
  };
  // Cmd+T opens the command bar, Cmd+T again closes it.
  EXPECT_EQ(press(ui::VKEY_T, ui::EF_COMMAND_DOWN),
            ui::PerformKeyEquivalentResult::kHandled);
  ASSERT_TRUE(window().command_session());
  EXPECT_EQ(press(ui::VKEY_T, ui::EF_COMMAND_DOWN),
            ui::PerformKeyEquivalentResult::kHandled);
  EXPECT_FALSE(window().command_session());
  // A held Cmd+T does not toggle the bar on every repeat.
  const ui::Accelerator repeat(ui::VKEY_T, ui::EF_COMMAND_DOWN | ui::EF_IS_REPEAT);
  EXPECT_TRUE(focus_manager()->ProcessAccelerator(repeat));
  EXPECT_FALSE(window().command_session());
  // A held menu chord is swallowed on repeat too: unconsumed, the repeat would go on to the main menu item with the
  // same chord and duplicate the tab every 33 ms.
  const int strip_count = browser()->GetTabStripModel()->count();
  EXPECT_TRUE(focus_manager()->ProcessAccelerator(ui::Accelerator(
      ui::VKEY_D,
      ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN | ui::EF_IS_REPEAT)));
  EXPECT_EQ(browser()->GetTabStripModel()->count(), strip_count);

  // Cmd+Shift+C copies the page URL; it is a menu chord, run here as the page leaves it.
  EXPECT_TRUE(focus_manager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_C, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)));
  const std::u16string copied = ui::clipboard_test_util::ReadText(
      ui::Clipboard::GetForCurrentThread(), ui::ClipboardBuffer::kCopyPaste,
      nullptr);
  EXPECT_EQ(base::UTF16ToUTF8(copied),
            embedded_test_server()->GetURL("/spaces/keylog.html").spec());

  // Cmd+1 shows the first row; Ctrl+2 switches to the second space.
  FocusedKeylog();
  EXPECT_EQ(press(ui::VKEY_1, ui::EF_COMMAND_DOWN),
            ui::PerformKeyEquivalentResult::kHandled);
  EXPECT_FALSE(window().ActiveRowId().empty());
  base::expected<std::string, SpacesError> two = space_actions::CreateSpace(
      &window(), std::string("Two"), std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(two.has_value());
  const std::string one = CluSpacesService::Get()->list().spaces()[0].id;
  ASSERT_TRUE(window().ShowSpace(one));
  EXPECT_EQ(press(ui::VKEY_2, ui::EF_CONTROL_DOWN),
            ui::PerformKeyEquivalentResult::kHandled);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return window().space_id() == two.value(); }));

  // Cmd+F is Chromium's own find bar, from Clu's key table.
  ASSERT_TRUE(window().ShowSpace(one));
  page->Focus();
  EXPECT_TRUE(focus_manager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_F, ui::EF_COMMAND_DOWN)));
  EXPECT_TRUE(base::test::RunUntil([&] {
    FindBarController* find = FindBarController::From(browser());
    return find && find->find_bar() && find->find_bar()->IsFindBarVisible();
  }));
}

// Rio's main menu carries View ▸ Appearance ▸ Automatic, Light, Dark and Expand Current Split, and
// Tabs ▸ New Folder…, Reveal Tab in Sidebar and Reset all tabs in this Space, each a commandDispatch: item of its
// command that the window validates and runs.
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest, CluKeysArcMenuItemsAreInTheMenuBar) {
  NSMenu* main_menu = [NSApp mainMenu];
  ASSERT_TRUE(main_menu);
  const std::pair<const char*, NSString*> items[] = {
      {"theme.appearanceAuto", @"Automatic"},
      {"theme.appearanceLight", @"Light"},
      {"theme.appearanceDark", @"Dark"},
      {"split.expand", @"Expand Current Split"},
      {"tabs.newFolder", @"New Folder…"},
      {"tabs.reveal", @"Reveal Tab in Sidebar"},
      {"tabs.resetAll", @"Reset all tabs in this Space"},
  };
  for (const auto& [id, title] : items) {
    const CommandSpec* spec = FindCommand(id);
    ASSERT_TRUE(spec) << id;
    NSMenuItem* found = nil;
    std::vector<NSMenu*> menus = {main_menu};
    while (!menus.empty() && !found) {
      NSMenu* menu = menus.back();
      menus.pop_back();
      for (NSMenuItem* item in menu.itemArray) {
        if (item.submenu) {
          menus.push_back(item.submenu);
        } else if (!item.hidden && item.tag == MenuTagFor(*spec)) {
          found = item;
        }
      }
    }
    ASSERT_TRUE(found) << id;
    EXPECT_NSEQ(found.title, title) << id;
    EXPECT_EQ(found.action, @selector(commandDispatch:)) << id;
  }
  // The window runs them: Appearance ▸ Dark checks Dark, Automatic puts it back; Reveal shows the row.
  ASSERT_TRUE(window().RunCommand("theme.appearanceDark"));
  EXPECT_EQ(window().CommandChecked("theme.appearanceDark"), true);
  ASSERT_TRUE(window().RunCommand("theme.appearanceAuto"));
  EXPECT_EQ(window().CommandChecked("theme.appearanceDark"), false);
  if (window().CanRunCommand("tabs.reveal")) {
    ASSERT_TRUE(window().RunCommand("tabs.reveal"));
    EXPECT_EQ(window().sidebar_request_for_testing()->kind, "reveal");
  }
}

// The roles of PLAN 8's last rows keep Chromium's items and keys, Cmd+Left and Cmd+Right stay Back and Forward for a
// page that leaves them, View Source keeps its key, and the Spaces menu shows the space names.
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest, CluKeysTheRoleItemsKeepTheirKeys) {
  FocusedKeylog();
  NSMenu* main_menu = [NSApp mainMenu];
  ASSERT_TRUE(main_menu);
  struct Role {
    SEL action;
    ui::KeyboardCode key;
    int modifiers;
  };
  const Role roles[] = {
      {@selector(performMiniaturize:), ui::VKEY_M, ui::EF_COMMAND_DOWN},
      {@selector(hide:), ui::VKEY_H, ui::EF_COMMAND_DOWN},
      {@selector(hideOtherApplications:), ui::VKEY_H,
       ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN},
      {@selector(terminate:), ui::VKEY_Q, ui::EF_COMMAND_DOWN},
      {@selector(undo:), ui::VKEY_Z, ui::EF_COMMAND_DOWN},
      {@selector(redo:), ui::VKEY_Z, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN},
      {@selector(cut:), ui::VKEY_X, ui::EF_COMMAND_DOWN},
      {@selector(copy:), ui::VKEY_C, ui::EF_COMMAND_DOWN},
      {@selector(paste:), ui::VKEY_V, ui::EF_COMMAND_DOWN},
      {@selector(selectAll:), ui::VKEY_A, ui::EF_COMMAND_DOWN},
  };
  for (const Role& role : roles) {
    const std::string name =
        base::SysNSStringToUTF8(NSStringFromSelector(role.action));
    NSMenuItem* item = ItemWithAction(main_menu, role.action);
    ASSERT_TRUE(item) << name;
    KeyEquivalentAndModifierMask* chord =
        ui::GetKeyEquivalentAndModifierMaskFromAccelerator(
            ui::Accelerator(role.key, role.modifiers));
    EXPECT_NSEQ(item.keyEquivalent, chord.keyEquivalent) << name;
    EXPECT_EQ(item.keyEquivalentModifierMask, chord.modifierMask) << name;
    // The lookup finds the item whenever AppKit enables it. Hide is enabled only for a regular app, and a browser
    // test runs as a background one.
    NSMenuItem* found = [main_menu
        cr_menuItemForKeyEquivalentEvent:EventFor({role.key, role.modifiers})];
    if (item.enabled) {
      EXPECT_EQ(found, item) << name;
    } else {
      EXPECT_FALSE(found) << name;
    }
  }
  // Ctrl+Cmd+F is Chromium's full screen item: in 158 an AppKit toggleFullScreen: item (the alternate of the one
  // tagged IDC_FULLSCREEN, which carries Fn+Ctrl+Cmd+F), so AppKit runs it and CommandForKeyEvent() does not list it.
  NSMenuItem* fullscreen = [main_menu
      cr_menuItemForKeyEquivalentEvent:EventFor({ui::VKEY_F,
                                                 ui::EF_COMMAND_DOWN |
                                                     ui::EF_CONTROL_DOWN})];
  ASSERT_TRUE(fullscreen);
  EXPECT_EQ(fullscreen.action, @selector(toggleFullScreen:));

  // Cmd+Left and Cmd+Right: no menu item, Chromium's page-only Back and Forward.
  NSEvent* left = EventFor({ui::VKEY_LEFT, ui::EF_COMMAND_DOWN});
  NSEvent* right = EventFor({ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN});
  EXPECT_FALSE(CommandForKeyEvent(left).found());
  EXPECT_EQ(DelayedWebContentsCommandForKeyEvent(left), IDC_BACK);
  EXPECT_EQ(DelayedWebContentsCommandForKeyEvent(right), IDC_FORWARD);
  // View Source keeps Cmd+Option+U.
  EXPECT_EQ(CommandForKeyEvent(EventFor({ui::VKEY_U,
                                         ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN}))
                .chrome_command,
            IDC_VIEW_SOURCE);
  // Bookmarks is hidden; Tabs and Spaces are there.
  EXPECT_TRUE([main_menu itemWithTag:IDC_BOOKMARKS_MENU].hidden);
  EXPECT_NSEQ([main_menu itemWithTag:IDC_TAB_MENU].title, @"Tabs");
  NSMenu* spaces = [main_menu itemWithTitle:@"Spaces"].submenu;
  ASSERT_TRUE(spaces);
  // Ctrl+1 reads as the first space's name.
  ASSERT_TRUE(CluSpacesService::Get()->list().spaces().size() >= 1);
  const std::string name = CluSpacesService::Get()->list().spaces()[0].name;
  [spaces.delegate menuNeedsUpdate:spaces];
  EXPECT_NSEQ([spaces itemWithTag:FindCommand("spaces.goto1")->idc].title,
              base::SysUTF8ToNSString(name));
  // One row per space: Ctrl+9's row shows only when a ninth space exists.
  const bool ninth = CluSpacesService::Get()->list().spaces().size() >= 9;
  EXPECT_EQ([spaces itemWithTag:FindCommand("spaces.goto9")->idc].hidden,
            !ninth);
}

// Settings, Shortcuts (docs/chromium/parity-settings.md section 4): a custom shortcut runs its command
// from the key table, the shipped key is free, the menu shows the new key, and clearing the map brings the table back.
IN_PROC_BROWSER_TEST_F(CluKeysBrowserTest, CluKeysCustomShortcutsTakeEffectEverywhere) {
  auto has = [](const ui::Accelerator& accelerator, const std::string& id) {
    for (const CluShell::KeyBinding& binding : CluShell::KeyBindings()) {
      if (binding.accelerator == accelerator && binding.id == id) {
        return true;
      }
    }
    return false;
  };
  const ui::Accelerator cmd_t(ui::VKEY_T, ui::EF_COMMAND_DOWN);
  const ui::Accelerator custom(ui::VKEY_E,
                               ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(has(cmd_t, "commandbar.newTab"));

  CluSettingsHandler settings(window().GetWeakPtr());
  base::test::TestFuture<mojom::CallResultPtr> set;
  settings.Set(mojom::SettingsSetInput::New(
                   "shortcuts.custom", "global",
                   R"({"commandbar.newTab":"Cmd+Ctrl+E","history.open":null})"),
               set.GetCallback());
  ASSERT_TRUE(set.Get()->is_value_json());
  EXPECT_TRUE(has(custom, "commandbar.newTab"));
  EXPECT_FALSE(has(cmd_t, "commandbar.newTab"));

  // The window registered the new key: it runs the command; the old one runs nothing of Rio's.
  shell().SetRecordOnlyForTesting(true);
  shell().ClearKeyCommandsForTesting();
  EXPECT_TRUE(focus_manager()->ProcessAccelerator(custom));
  focus_manager()->ProcessAccelerator(cmd_t);
  EXPECT_EQ(shell().key_commands_for_testing(),
            std::vector<std::string>{"commandbar.newTab"});
  shell().SetRecordOnlyForTesting(false);

  // The main menu carries it: New Tab shows ⌃⌘E and Show History has no key.
  NSMenuItem* new_tab = nil;
  NSMenuItem* history = nil;
  std::vector<NSMenu*> menus = {NSApp.mainMenu};
  while (!menus.empty()) {
    NSMenu* menu = menus.back();
    menus.pop_back();
    for (NSMenuItem* item in menu.itemArray) {
      if (item.submenu) {
        menus.push_back(item.submenu);
      } else if (!item.hidden && item.tag == IDC_NEW_TAB) {
        new_tab = item;
      } else if (!item.hidden && item.tag == IDC_SHOW_HISTORY) {
        history = item;
      }
    }
  }
  if (new_tab) {
    EXPECT_NSEQ(new_tab.keyEquivalent, @"e");
    EXPECT_EQ(new_tab.keyEquivalentModifierMask &
                  NSEventModifierFlagDeviceIndependentFlagsMask,
              NSEventModifierFlagCommand | NSEventModifierFlagControl);
  }
  if (history) {
    EXPECT_EQ(history.keyEquivalent.length, 0u);
  }

  // A key macOS keeps is refused and changes nothing.
  base::test::TestFuture<mojom::CallResultPtr> refused;
  settings.Set(mojom::SettingsSetInput::New("shortcuts.custom", "global",
                                            R"({"tabs.close":"Cmd+Q"})"),
               refused.GetCallback());
  EXPECT_FALSE(refused.Get()->is_value_json());
  EXPECT_TRUE(has(custom, "commandbar.newTab"));

  base::test::TestFuture<mojom::CallResultPtr> reset;
  settings.Set(mojom::SettingsSetInput::New("shortcuts.custom", "global", "{}"),
               reset.GetCallback());
  ASSERT_TRUE(reset.Get()->is_value_json());
  EXPECT_TRUE(has(cmd_t, "commandbar.newTab"));
  EXPECT_FALSE(has(custom, "commandbar.newTab"));
}

}  // namespace
}  // namespace clu
