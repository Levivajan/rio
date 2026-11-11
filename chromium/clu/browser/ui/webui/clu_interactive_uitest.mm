// Copyright 2026 The Rio Authors. All rights reserved.

#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/apple/scoped_objc_class_swizzler.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/overlay/back_to_tab_button.h"
#include "chrome/browser/ui/views/overlay/video_overlay_window_views.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_window_site.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/common/result_codes.h"
#include "content/public/test/media_start_stop_observer.h"
#include "media/base/picture_in_picture_events_info.h"
#include "net/base/filename_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/view.h"
#include "url/gurl.h"

// The CluInteractive* tests (docs/chromium/interactive.md): real keyboard and mouse input from the OS. Every key and
// click is a CGEvent posted at the HID event tap, so it reaches Clu through the WindowServer and AppKit exactly as a
// hand's does: the key window, prePerformKeyEquivalent, the main menu's key equivalents and its redispatch, the
// flagsChanged event of a modifier's release, the WindowServer's choice of the window under the pointer and the
// native menu's own tracking loop. The window must sit on a display other than the main one (the runner passes
// --window-position), and a key goes out only while Clu is the active app, so no event can reach another app.
namespace clu {
namespace {

// Input ----------------------------------------------------------------------------------------------------------

CGEventSourceRef Source() {
  // A private source: the events carry exactly the flags set on them, never a modifier held on the real keyboard.
  static CGEventSourceRef source =
      CGEventSourceCreate(kCGEventSourceStatePrivate);
  return source;
}

CGEventFlags FlagsOf(int modifiers) {
  CGEventFlags flags = 0;
  if (modifiers & ui::EF_COMMAND_DOWN) flags |= kCGEventFlagMaskCommand;
  if (modifiers & ui::EF_CONTROL_DOWN) flags |= kCGEventFlagMaskControl;
  if (modifiers & ui::EF_ALT_DOWN) flags |= kCGEventFlagMaskAlternate;
  if (modifiers & ui::EF_SHIFT_DOWN) flags |= kCGEventFlagMaskShift;
  return flags;
}

struct Modifier {
  int flag;
  CGKeyCode key;
};
constexpr Modifier kModifiers[] = {
    {ui::EF_COMMAND_DOWN, kVK_Command},
    {ui::EF_CONTROL_DOWN, kVK_Control},
    {ui::EF_ALT_DOWN, kVK_Option},
    {ui::EF_SHIFT_DOWN, kVK_Shift},
};

// Keys only while this process is the active app: the WindowServer sends keyboard events to the active app, so a key
// can never land in another app's window.
bool CluIsActive() {
  return NSRunningApplication.currentApplication.active;
}

void PostKeyCode(CGKeyCode code, bool down, int held) {
  base::apple::ScopedCFTypeRef<CGEventRef> event(
      CGEventCreateKeyboardEvent(Source(), code, down));
  CGEventSetFlags(event.get(), FlagsOf(held));
  CGEventPost(kCGHIDEventTap, event.get());
}

CGKeyCode MacKey(ui::KeyboardCode key) {
  const int code = ui::MacKeyCodeForWindowsKeyCode(key, 0, nullptr, nullptr);
  CHECK_GE(code, 0) << key;
  return static_cast<CGKeyCode>(code);
}

// The modifiers of `modifiers` going down (or up) one by one, each event with the flags held after it.
void Modifiers(int modifiers, bool down, int* held) {
  for (const Modifier& m : kModifiers) {
    if (modifiers & m.flag) {
      *held = down ? (*held | m.flag) : (*held & ~m.flag);
      PostKeyCode(m.key, down, *held);
    }
  }
}

[[nodiscard]] bool Press(ui::KeyboardCode key, int modifiers = 0) {
  if (!CluIsActive()) {
    return false;
  }
  int held = 0;
  Modifiers(modifiers, true, &held);
  PostKeyCode(MacKey(key), true, held);
  PostKeyCode(MacKey(key), false, held);
  Modifiers(modifiers, false, &held);
  return true;
}

// For a chord held across steps (Ctrl+Tab): the modifiers stay down until Release().
[[nodiscard]] bool Hold(int modifiers) {
  if (!CluIsActive()) {
    return false;
  }
  int held = 0;
  Modifiers(modifiers, true, &held);
  return true;
}
[[nodiscard]] bool PressHeld(ui::KeyboardCode key, int held) {
  if (!CluIsActive()) {
    return false;
  }
  PostKeyCode(MacKey(key), true, held);
  PostKeyCode(MacKey(key), false, held);
  return true;
}
void Release(int modifiers) {
  int held = modifiers;
  Modifiers(modifiers, false, &held);
}

// Every modifier up, so a failed test never leaves one held for the next or for the Mac.
void ReleaseAllModifiers() {
  for (const Modifier& m : kModifiers) {
    PostKeyCode(m.key, false, 0);
  }
}

// The window the WindowServer sends a click at `point` to (screen points, top left origin, as gfx::Rect is on Mac)
// belongs to this process: the frontmost on-screen window there. A click is never sent anywhere else.
bool OursAt(const gfx::Point& point) {
  // Never into the Dock's bar or the menu bar: when the Dock sits on the tests display its bar lives in the Dock's
  // full-display window skipped below, and a click there opens whatever app is under it. The visible frame leaves
  // both out (AppKit's coordinates have a bottom left origin on the main display's bottom edge).
  if (!NSScreen.screens.count) {
    return false;
  }
  const NSPoint ns_point = NSMakePoint(
      point.x(), NSMaxY(NSScreen.screens[0].frame) - point.y());
  bool visible = false;
  for (NSScreen* screen in NSScreen.screens) {
    if (NSPointInRect(ns_point, screen.frame)) {
      visible = NSPointInRect(ns_point, screen.visibleFrame);
      break;
    }
  }
  if (!visible) {
    return false;
  }
  base::apple::ScopedCFTypeRef<CFArrayRef> windows(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
      kCGNullWindowID));
  if (!windows) {
    return false;
  }
  for (CFIndex i = 0; i < CFArrayGetCount(windows.get()); ++i) {
    NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(
        windows.get(), i);
    CGRect bounds;
    if (!CGRectMakeWithDictionaryRepresentation(
            (__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds],
            &bounds) ||
        [info[(__bridge NSString*)kCGWindowAlpha] doubleValue] <= 0 ||
        !CGRectContainsPoint(bounds, CGPointMake(point.x(), point.y()))) {
      continue;
    }
    // The Dock keeps a transparent window over each whole display (for Mission Control and its own reveal), which
    // clicks pass through; the Dock's own bar is smaller and still counts.
    if ([info[(__bridge NSString*)kCGWindowOwnerName] isEqual:@"Dock"] &&
        NSScreen.screens.count &&
        [NSScreen.screens indexOfObjectPassingTest:^BOOL(NSScreen* screen,
                                                         NSUInteger, BOOL*) {
          return screen.frame.size.width == bounds.size.width &&
                 screen.frame.size.height == bounds.size.height;
        }] != NSNotFound) {
      continue;
    }
    return [info[(__bridge NSString*)kCGWindowOwnerPID] intValue] == getpid();
  }
  return false;
}

void PostMouse(CGEventType type,
               const gfx::Point& point,
               CGMouseButton button,
               int modifiers,
               int click_state = 1) {
  base::apple::ScopedCFTypeRef<CGEventRef> event(CGEventCreateMouseEvent(
      Source(), type, CGPointMake(point.x(), point.y()), button));
  CGEventSetFlags(event.get(), FlagsOf(modifiers));
  if (type != kCGEventMouseMoved) {
    CGEventSetIntegerValueField(event.get(), kCGMouseEventClickState,
                                click_state);
  }
  CGEventPost(kCGHIDEventTap, event.get());
}

[[nodiscard]] bool MoveTo(const gfx::Point& point, int modifiers = 0) {
  if (!OursAt(point)) {
    return false;
  }
  PostMouse(kCGEventMouseMoved, point, kCGMouseButtonLeft, modifiers);
  return true;
}

[[nodiscard]] bool Click(const gfx::Point& point, int modifiers = 0) {
  // Checked again after the move: the Dock may have slid in under the pointer.
  if (!MoveTo(point, modifiers) || !OursAt(point)) {
    return false;
  }
  PostMouse(kCGEventLeftMouseDown, point, kCGMouseButtonLeft, modifiers);
  PostMouse(kCGEventLeftMouseUp, point, kCGMouseButtonLeft, modifiers);
  return true;
}

// Two clicks the WindowServer counts as a double-click (click states 1 and 2).
[[nodiscard]] bool DoubleClick(const gfx::Point& point) {
  if (!MoveTo(point)) {
    return false;
  }
  for (int click = 1; click <= 2; ++click) {
    PostMouse(kCGEventLeftMouseDown, point, kCGMouseButtonLeft, 0, click);
    PostMouse(kCGEventLeftMouseUp, point, kCGMouseButtonLeft, 0, click);
  }
  return true;
}

// Lets the browser handle the events posted so far, as the time between a hand's events would.
void Pace() {
  const base::TimeTicks until = base::TimeTicks::Now() + base::Milliseconds(20);
  CHECK(base::test::RunUntil([&] { return base::TimeTicks::Now() >= until; }));
}

// A press at `from`, the pointer dragged to `to` in steps with the button held, and the release there. Both ends must
// be on Clu's own window. The steps are paced: Chromium reads the held buttons from the Mac's current state
// ([NSEvent pressedMouseButtons]), so a burst that already holds the release turns the moves into button-less moves,
// which end the page's pointer capture before the release.
[[nodiscard]] bool Drag(const gfx::Point& from, const gfx::Point& to) {
  if (!MoveTo(from) || !OursAt(to)) {
    return false;
  }
  PostMouse(kCGEventLeftMouseDown, from, kCGMouseButtonLeft, 0);
  Pace();
  constexpr int kSteps = 16;
  for (int i = 1; i <= kSteps; ++i) {
    PostMouse(kCGEventLeftMouseDragged,
              gfx::Point(from.x() + (to.x() - from.x()) * i / kSteps,
                         from.y() + (to.y() - from.y()) * i / kSteps),
              kCGMouseButtonLeft, 0);
    Pace();
  }
  PostMouse(kCGEventLeftMouseUp, to, kCGMouseButtonLeft, 0);
  return true;
}

[[nodiscard]] bool RightClick(const gfx::Point& point) {
  if (!MoveTo(point)) {
    return false;
  }
  PostMouse(kCGEventRightMouseDown, point, kCGMouseButtonRight, 0);
  PostMouse(kCGEventRightMouseUp, point, kCGMouseButtonRight, 0);
  return true;
}

// Front-to-back position of `window` among the WindowServer's on-screen windows, or -1.
int ScreenOrderOf(NSWindow* window) {
  base::apple::ScopedCFTypeRef<CFArrayRef> windows(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
  for (CFIndex i = 0; windows && i < CFArrayGetCount(windows.get()); ++i) {
    NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(
        windows.get(), i);
    if ([info[(__bridge NSString*)kCGWindowNumber] integerValue] ==
        window.windowNumber) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace
}  // namespace clu

// Picks an item of the native menu that opens next with the pointer, the way a hand does: once the menu tracks, the
// pointer moves down through it a few points at a time until AppKit highlights the item titled `title`, then clicks.
// It runs on an NSTimer in the common modes, so it steps inside the menu's own tracking loop.
@interface CluMenuPicker : NSObject
@property(nonatomic, readonly) BOOL opened;
@property(nonatomic, readonly) BOOL clicked;
@property(nonatomic, readonly) NSString* seen;
- (instancetype)initWithTitle:(NSString*)title from:(gfx::Point)from;
- (void)stop;
@end

@implementation CluMenuPicker {
  NSString* __strong _title;
  gfx::Point _at;
  int _steps;
  id __strong _observer;
  NSTimer* __strong _timer;
  NSMutableArray<NSString*>* __strong _seen;
}

@synthesize opened = _opened;
@synthesize clicked = _clicked;

- (instancetype)initWithTitle:(NSString*)title from:(gfx::Point)from {
  if ((self = [super init])) {
    _title = title;
    // Right of the press, where the menu opens.
    _at = gfx::Point(from.x() + 24, from.y());
    _seen = [NSMutableArray array];
    __weak CluMenuPicker* weak = self;
    _observer = [NSNotificationCenter.defaultCenter
        addObserverForName:NSMenuDidBeginTrackingNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* note) {
                  [weak menuOpened:base::apple::ObjCCast<NSMenu>(note.object)];
                }];
  }
  return self;
}

- (NSString*)seen {
  return [_seen componentsJoinedByString:@","];
}

- (void)menuOpened:(NSMenu*)menu {
  if (_opened || !menu) {
    return;
  }
  _opened = YES;
  __weak CluMenuPicker* weak = self;
  __weak NSMenu* weak_menu = menu;
  _timer = [NSTimer timerWithTimeInterval:0.03
                                  repeats:YES
                                    block:^(NSTimer*) {
                                      [weak stepIn:weak_menu];
                                    }];
  [NSRunLoop.currentRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stepIn:(NSMenu*)menu {
  NSString* highlighted = menu.highlightedItem.title;
  if (highlighted && ![_seen.lastObject isEqualToString:highlighted]) {
    [_seen addObject:highlighted];
  }
  if (highlighted && [highlighted isEqualToString:_title]) {
    [_timer invalidate];
    _clicked = clu::OursAt(_at);
    if (_clicked) {
      clu::PostMouse(kCGEventLeftMouseDown, _at, kCGMouseButtonLeft, 0);
      clu::PostMouse(kCGEventLeftMouseUp, _at, kCGMouseButtonLeft, 0);
    } else {
      [menu cancelTracking];
    }
    return;
  }
  // 600 points down without finding it: close the menu so the test can fail.
  if (++_steps > 200) {
    [_timer invalidate];
    [menu cancelTracking];
    return;
  }
  _at.Offset(0, 3);
  clu::PostMouse(kCGEventMouseMoved, _at, kCGMouseButtonLeft, 0);
}

- (void)stop {
  [_timer invalidate];
  if (_observer) {
    [NSNotificationCenter.defaultCenter removeObserver:_observer];
    _observer = nil;
  }
}
@end

namespace clu {
namespace {

constexpr char kRectOf[] = R"(
  (() => {
    const e = document.querySelector($1);
    if (!e) return [];
    const r = e.getBoundingClientRect();
    return [Math.round(r.x), Math.round(r.y), Math.round(r.width),
            Math.round(r.height)];
  })())";

// Resolves false after 10 s, so a missing element fails the test with a message instead of hanging it.
constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const end = Date.now() + 10000;
    const check = () => document.querySelector($1) ? resolve(true)
                        : Date.now() > end ? resolve(false)
                                           : setTimeout(check, 20);
    check();
  }))";

constexpr char kHovered[] = R"(
  Array.from(document.querySelectorAll(':hover')).map(e =>
      e.tagName + (e.dataset.tabId ? '#' + e.dataset.tabId : '')).join('>'))";

constexpr char kWaitForInputFocus[] = R"(
  new Promise(resolve => {
    const check = () => {
      const input = document.querySelector('[data-surface="command"] input');
      input && document.activeElement === input ? resolve(true)
                                                : setTimeout(check, 20);
    };
    check();
  }))";

constexpr int kToastSlot = 0;
constexpr int kSwitcherSlot = 1;
constexpr int kHoverSlot = 2;
constexpr int kPeekSlot = CluWindowSidebarPeek::kPeekSlot;

// Types into the command bar the way React sees typing: through the native value setter, then an input event.
constexpr char kTypeIntoCommandBar[] = R"(
  (() => {
    const input = document.querySelector('[data-surface="command"] input');
    const setter = Object.getOwnPropertyDescriptor(
        HTMLInputElement.prototype, 'value').set;
    setter.call(input, $1);
    input.dispatchEvent(new InputEvent('input', {bubbles: true}));
    return document.activeElement === input;
  })())";

class CluInteractiveTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // ui_controls (enabled by the interactive test runner) answers +[NSEvent mouseLocation] and pressedMouseButtons
    // from its own synthetic pointer. These tests move the real one, so AppKit's own answers come back for the test.
    if (Class donor = NSClassFromString(@"FakeNSEventTestingDonor")) {
      real_mouse_location_ =
          std::make_unique<base::apple::ScopedObjCClassSwizzler>(
              [NSEvent class], donor, @selector(mouseLocation));
      real_mouse_buttons_ =
          std::make_unique<base::apple::ScopedObjCClassSwizzler>(
              [NSEvent class], donor, @selector(pressedMouseButtons));
    }
    base::apple::ScopedCFTypeRef<CGEventRef> now(CGEventCreate(nullptr));
    cursor_ = CGEventGetLocation(now.get());
    // What AppKit delivered, for the failure messages: each key event as type:keyCode:flags.
    NSMutableArray<NSString*>* seen = [NSMutableArray array];
    seen_ = seen;
    key_monitor_ = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown |
                                             NSEventMaskKeyUp |
                                             NSEventMaskFlagsChanged
                                     handler:^NSEvent*(NSEvent* event) {
                                       [seen addObject:[NSString
                                           stringWithFormat:@"%@:%d:%lx",
                                               event.type == NSEventTypeKeyDown
                                                   ? @"down"
                                                   : event.type ==
                                                             NSEventTypeKeyUp
                                                         ? @"up"
                                                         : @"flags",
                                               event.keyCode,
                                               (unsigned long)(event.modifierFlags &
                                                   NSEventModifierFlagDeviceIndependentFlagsMask)]];
                                       return event;
                                     }];

    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    // Copy URL and Copy Link write here, never to the Mac's pasteboard.
    ui::TestClipboard::CreateForCurrentThread();
    shell().RecordKeysForTesting();

    // Real clicks land wherever the window is: it must be on a display of its own, never the main one.
    const gfx::Rect bounds = browser()->GetWindow()->GetBounds();
    const display::Display primary =
        display::Screen::Get()->GetPrimaryDisplay();
    ASSERT_FALSE(primary.bounds().Intersects(bounds))
        << "the window is on the main display at " << bounds.ToString()
        << "; pass --window-position on the tests display";
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(base::test::RunUntil([] { return CluIsActive(); }));
  }

  void TearDownOnMainThread() override {
    if (key_monitor_) {
      [NSEvent removeMonitor:key_monitor_];
      key_monitor_ = nil;
    }
    ReleaseAllModifiers();
    // The pointer goes back where it was (a warp sends no event to any app).
    CGWarpMouseCursorPosition(cursor_);
    real_mouse_buttons_.reset();
    real_mouse_location_.reset();
    ui::Clipboard::DestroyClipboardForCurrentThread();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  // The key events AppKit delivered so far and the commands the key table ran, for a failure message.
  std::string Seen() {
    std::string commands;
    for (const std::string& id : shell().key_commands_for_testing()) {
      commands += id + " ";
    }
    return base::StrCat({"keys: ",
                         base::SysNSStringToUTF8(
                             [seen_ componentsJoinedByString:@" "]),
                         "; commands: ", commands});
  }
  CluModelService& model() { return *window().model(); }
  const CluTabTree& tree() { return model().tree(); }
  std::string space() { return window().space_id(); }
  content::WebContents* frame() { return shell().frame_contents(); }
  content::WebContents* overlay() { return shell().overlay_contents(); }

  // A Today row on the input log, shown, loaded, painted and focused.
  std::string Open(const std::string& name = "page") {
    return OpenUrl(embedded_test_server()->GetURL(
        base::StrCat({"/interactive/inputlog.html?", name})));
  }

  // A Today row on `url`, shown, loaded, painted and focused.
  std::string OpenUrl(const GURL& url) {
    base::expected<std::string, std::string> id =
        window().CreateRow(url, std::nullopt, /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return std::string();
    }
    content::WebContents* page = Contents(id.value());
    EXPECT_TRUE(content::WaitForLoadStop(page));
    // Chromium holds a new page's input until its first frame (paint holding).
    content::WaitForCopyableViewInWebContents(page);
    FocusPage(id.value());
    return id.value();
  }

  content::WebContents* Contents(const std::string& row_id) {
    tabs::TabInterface* tab = window().FindTab(row_id);
    return tab ? tab->GetContents() : nullptr;
  }

  void FocusPage(const std::string& row_id) {
    content::WebContents* page = Contents(row_id);
    ASSERT_TRUE(page);
    page->Focus();
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return page->GetRenderWidgetHostView()->HasFocus(); }));
  }

  bool PageHasFocus(const std::string& row_id) {
    content::WebContents* page = Contents(row_id);
    return page && page->GetRenderWidgetHostView() &&
           page->GetRenderWidgetHostView()->HasFocus();
  }

  std::vector<std::string> Log(content::WebContents* page,
                               const std::string& list) {
    return base::SplitString(
        content::EvalJs(page, base::StrCat({"(window.", list, " ?? []).join(' ')"}))
            .ExtractString(),
        " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  }
  std::vector<std::string> Keys(const std::string& row_id) {
    return Log(Contents(row_id), "__keys");
  }
  std::vector<std::string> Clicks(const std::string& row_id) {
    return Log(Contents(row_id), "__clicks");
  }

  // A plain X typed into the focused page of `row_id` and seen by it: every key before it, and anything their
  // redispatch ran, is done.
  void Settle(const std::string& row_id) {
    const auto count = [&] { return std::ranges::count(Keys(row_id), "KeyX"); };
    const auto before = count();
    ASSERT_TRUE(Press(ui::VKEY_X));
    ASSERT_TRUE(base::test::RunUntil([&] { return count() > before; }))
        << "the page saw: " << base::JoinString(Keys(row_id), " ") << "; "
        << Seen() << "; focused: " << PageHasFocus(row_id);
  }

  // The screen rect of the element `selector` in `contents` (a WebUI or a page), from its client rect.
  gfx::Rect ScreenRectOf(content::WebContents* contents,
                         const std::string& selector) {
    EXPECT_EQ(true, content::EvalJs(contents, content::JsReplace(
                                                  kWaitForSelector, selector)));
    const base::ListValue r =
        content::EvalJs(contents, content::JsReplace(kRectOf, selector))
            .ExtractList()
            .Clone();
    if (r.size() != 4) {
      ADD_FAILURE() << "no " << selector;
      return gfx::Rect();
    }
    const gfx::Rect container = contents->GetContainerBounds();
    return gfx::Rect(container.x() + r[0].GetInt(),
                     container.y() + r[1].GetInt(), r[2].GetInt(),
                     r[3].GetInt());
  }

  // The Today rows the sidebar shows (a cleared row stays in the tree until its undo time ends).
  std::vector<std::string> ShownToday() {
    std::vector<std::string> ids;
    const base::DictValue state = window().CurrentState();
    if (const base::ListValue* today =
            state.FindDict("spaces")->FindDict(space())->FindList("today")) {
      for (const base::Value& id : *today) {
        ids.push_back(id.GetString());
      }
    }
    return ids;
  }

  // The on-screen windows under `point`, front to back, as owner:number, with Clu's own named.
  std::string WindowsAt(const gfx::Point& point) {
    std::string out;
    base::apple::ScopedCFTypeRef<CFArrayRef> windows(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
    for (CFIndex i = 0; windows && i < CFArrayGetCount(windows.get()); ++i) {
      NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(
          windows.get(), i);
      CGRect bounds;
      if (!CGRectMakeWithDictionaryRepresentation(
              (__bridge CFDictionaryRef)
                  info[(__bridge NSString*)kCGWindowBounds],
              &bounds) ||
          !CGRectContainsPoint(bounds, CGPointMake(point.x(), point.y()))) {
        continue;
      }
      const NSInteger number =
          [info[(__bridge NSString*)kCGWindowNumber] integerValue];
      std::string name = base::SysNSStringToUTF8(
          info[(__bridge NSString*)kCGWindowOwnerName]);
      NSWindow* browser_window =
          browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
      if (number == browser_window.windowNumber) {
        name = "browser";
      } else if (number == NativeWindowOf(overlay()).windowNumber) {
        name = "top-overlay";
      } else {
        for (int slot = 0; slot < CluShell::kPassiveSlots; ++slot) {
          if (number ==
              NativeWindowOf(shell().passive_contents(slot)).windowNumber) {
            name = base::StrCat({"passive", base::NumberToString(slot)});
          }
        }
      }
      out += base::StrCat({name, ":", base::NumberToString(number), " "});
    }
    return out;
  }

  std::string RowSelector(const std::string& id) {
    return base::StrCat({"nav.clu-sidebar [data-tab-id=\"", id, "\"]"});
  }
  gfx::Point RowCenter(const std::string& id) {
    return ScreenRectOf(frame(), RowSelector(id)).CenterPoint();
  }
  // A point in the page area, clear of the overlays' cards.
  gfx::Point PagePoint() {
    const gfx::Rect page = Contents(window().ActiveRowId())->GetContainerBounds();
    return gfx::Point(page.right() - 40, page.y() + 40);
  }

  // The NSWindow that shows `contents` (an overlay's own child window, not the browser window it hangs from).
  NSWindow* NativeWindowOf(content::WebContents* contents) {
    return contents->GetNativeView().GetNativeNSView().window;
  }

  // The command bar's session opened and its input focused.
  void WaitForBar() {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return window().command_session().has_value(); }));
    ASSERT_TRUE(shell().IsOverlayVisible());
    EXPECT_EQ(true, content::EvalJs(overlay(), kWaitForInputFocus));
  }

  // A screenshot of the window on the tests display, from the WindowServer (overlay windows included), to
  // $CLU_SHOTS_DIR when it is set.
  void Shot(const std::string& name) {
    std::unique_ptr<base::Environment> env = base::Environment::Create();
    std::optional<std::string> dir = env->GetVar("CLU_SHOTS_DIR");
    if (!dir) {
      return;
    }
    base::ScopedAllowBlockingForTesting allow;
    const base::FilePath path =
        base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(name + ".png");
    base::CreateDirectory(path.DirName());
    const gfx::Rect b = browser()->GetWindow()->GetBounds();
    base::CommandLine shot(base::FilePath("/usr/sbin/screencapture"));
    shot.AppendArg("-x");
    shot.AppendArg(base::StrCat(
        {"-R", base::NumberToString(b.x()), ",", base::NumberToString(b.y()),
         ",", base::NumberToString(b.width()), ",",
         base::NumberToString(b.height())}));
    shot.AppendArgPath(path);
    base::Process process = base::LaunchProcess(shot, base::LaunchOptions());
    int code = -1;
    process.WaitForExitWithTimeout(base::Seconds(10), &code);
  }

 private:
  std::unique_ptr<base::apple::ScopedObjCClassSwizzler> real_mouse_location_;
  std::unique_ptr<base::apple::ScopedObjCClassSwizzler> real_mouse_buttons_;
  CGPoint cursor_ = CGPointZero;
  id __strong key_monitor_;
  NSMutableArray<NSString*>* __strong seen_;
};

// M3: the row chords from a focused page --------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluInteractiveTest,
                       CluInteractiveRowChordsFromAFocusedPage) {
  const std::string startup = window().ActiveRowId();
  const std::string c = Open("c");
  const std::string b = Open("b");
  const std::string a = Open("a");
  // Today is [a, b, c, startup]; the page has focus. Cmd+2 is a router chord: the second row shows and the page
  // never saw the key.
  ASSERT_TRUE(Press(ui::VKEY_2, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().ActiveRowId() == b; }));
  ASSERT_TRUE(Press(ui::VKEY_4, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == startup; }));
  ASSERT_TRUE(Press(ui::VKEY_1, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().ActiveRowId() == a; }));
  // Cmd+9 is the ninth row: with four, nothing happens, and the page still never sees it. Cmd+A, a key the page
  // gets, follows it, so the page has processed every key before.
  ASSERT_TRUE(Press(ui::VKEY_9, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(Press(ui::VKEY_A, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return std::ranges::contains(Keys(a), "Meta+KeyA"); }));
  EXPECT_EQ(window().ActiveRowId(), a);
  for (const std::string& row : {a, b, startup}) {
    if (Contents(row)) {
      for (const std::string& key : Keys(row)) {
        EXPECT_FALSE(key.starts_with("Meta+Digit")) << key;
      }
    }
  }

  // Cmd+D is a menu chord: the page sees it first, then the row pins. Again, it unpins.
  // Each runs once: Settle() waits out the page's answer and the redispatch after it.
  ASSERT_TRUE(Press(ui::VKEY_D, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Get(a)->section == Section::kPinned; }));
  Settle(a);
  EXPECT_EQ(tree().Get(a)->section, Section::kPinned);
  EXPECT_TRUE(std::ranges::contains(Keys(a), "Meta+KeyD"));
  ASSERT_TRUE(Press(ui::VKEY_D, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Get(a)->section == Section::kToday; }));
  Settle(a);
  EXPECT_EQ(tree().Get(a)->section, Section::kToday);
  // Cmd+Shift+M mutes the page.
  ASSERT_TRUE(Press(ui::VKEY_M, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return Contents(a)->IsAudioMuted(); }));
  Settle(a);
  EXPECT_TRUE(Contents(a)->IsAudioMuted());
  EXPECT_TRUE(tree().Get(a)->muted);
  // Cmd+Shift+D duplicates it below and shows the copy.
  ASSERT_TRUE(Press(ui::VKEY_D, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window().ActiveRowId() != a && tree().Today(space()).size() == 5u;
  }));
  const std::string copy = window().ActiveRowId();
  ASSERT_TRUE(content::WaitForLoadStop(Contents(copy)));
  content::WaitForCopyableViewInWebContents(Contents(copy));
  FocusPage(copy);
  Settle(copy);
  const std::vector<std::string> rows = tree().Today(space());
  EXPECT_EQ(rows.size(), 5u);
  const auto source = std::ranges::find(rows, a);
  ASSERT_NE(source, rows.end());
  ASSERT_NE(source + 1, rows.end());
  EXPECT_EQ(*(source + 1), copy);
  EXPECT_EQ(tree().Get(copy)->url, tree().Get(a)->url);

  // Cmd+W closes the shown row (from its page) and the next one shows; Cmd+Shift+T brings it back with its page.
  ASSERT_TRUE(content::WaitForLoadStop(Contents(copy)));
  FocusPage(copy);
  ASSERT_TRUE(Press(ui::VKEY_W, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] { return !tree().Get(copy); }));
  EXPECT_TRUE(browser()->GetWindow()->IsVisible());
  EXPECT_FALSE(window().ActiveRowId().empty());
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tree().Today(space()).size() == 5u; }));
  const std::string back = window().ActiveRowId();
  EXPECT_EQ(tree().Today(space()).front(), back);
  EXPECT_EQ(tree().Get(back)->url, tree().Get(a)->url);
  // Both came through the key table, not through Chromium's own commands.
  EXPECT_TRUE(std::ranges::contains(shell().key_commands_for_testing(),
                                    std::string("tabs.close")));
  EXPECT_TRUE(std::ranges::contains(shell().key_commands_for_testing(),
                                    std::string("archive.reopenClosed")));
}

// M3: Ctrl+Tab, with the switcher committing on the Control key's release -----------------------------------------

IN_PROC_BROWSER_TEST_F(CluInteractiveTest,
                       CluInteractiveCtrlTabCommitsOnTheControlRelease) {
  const std::string first = Open("first");
  const std::string second = Open("second");
  const std::string third = Open("third");
  // A tap: Control down, Tab, Control up at once switches to the previous tab with no overlay.
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  Release(ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == second; }));
  EXPECT_FALSE(window().switcher().active());
  EXPECT_FALSE(shell().IsPassiveVisible(kSwitcherSlot));

  // A hold: the switcher shows after 180 ms, Tab steps, and the release commits the third most recent tab.
  FocusPage(second);
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kSwitcherSlot); }))
      << Seen() << "; switcher running: " << window().switcher().active();
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  EXPECT_EQ(window().ActiveRowId(), second);
  Shot("ctrl-tab-switcher");
  Release(ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == first; }));
  EXPECT_FALSE(window().switcher().active());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().IsPassiveVisible(kSwitcherSlot); }));
  // The switcher never took focus from the page; the shown page has it.
  EXPECT_TRUE(base::test::RunUntil([&] { return PageHasFocus(first); }));
  // Tab and the Control release never reached a page as keys.
  for (const std::string& row : {first, second, third}) {
    for (const std::string& key : Keys(row)) {
      EXPECT_NE(key, "Control+Tab") << row;
    }
  }

  // Ctrl+Shift+Tab goes the other way through the same list: the least recent of the shown cards.
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(
      PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kSwitcherSlot); }));
  Release(ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window().switcher().active() && window().ActiveRowId() != first;
  }));

  // Esc while Control is held cancels: the tab stays.
  const std::string before = window().ActiveRowId();
  FocusPage(before);
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kSwitcherSlot); }));
  ASSERT_TRUE(PressHeld(ui::VKEY_ESCAPE, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !window().switcher().active(); }));
  Release(ui::EF_CONTROL_DOWN);
  EXPECT_FALSE(shell().IsPassiveVisible(kSwitcherSlot));
  EXPECT_EQ(window().ActiveRowId(), before);
}

// A click on a card commits that card, with Control still held as a hand holds it.
IN_PROC_BROWSER_TEST_F(CluInteractiveTest, CluInteractiveSwitcherCardClick) {
  const std::string first = Open("first");
  const std::string second = Open("second");
  Open("third");
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kSwitcherSlot); }));
  // The selection is on the second card (`second`); the third card is `first`.
  const gfx::Point card =
      ScreenRectOf(shell().passive_contents(kSwitcherSlot),
                   base::StrCat({".clu-switcher-card[data-tab-id=\"", first,
                                 "\"]"}))
          .CenterPoint();
  ASSERT_TRUE(Click(card, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == first; }));
  EXPECT_FALSE(window().switcher().active());
  // The release after the click changes nothing more.
  Release(ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().IsPassiveVisible(kSwitcherSlot); }));
  EXPECT_EQ(window().ActiveRowId(), first);
  EXPECT_NE(window().ActiveRowId(), second);
}

// M4: the space chords --------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluInteractiveTest, CluInteractiveSpaceChords) {
  const std::string home = space();
  base::expected<std::string, SpacesError> two = space_actions::CreateSpace(
      &window(), std::string("Two"), std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(two.has_value());
  base::expected<std::string, SpacesError> three = space_actions::CreateSpace(
      &window(), std::string("Three"), std::nullopt, std::nullopt,
      std::nullopt);
  ASSERT_TRUE(three.has_value());
  ASSERT_TRUE(window().ShowSpace(home));
  const std::string row = Open("home");

  // Ctrl+2 and Ctrl+3 from the focused page; Ctrl+9 with three spaces does nothing, and the page never sees any.
  ASSERT_TRUE(Press(ui::VKEY_2, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return space() == two.value(); }));
  ASSERT_TRUE(Press(ui::VKEY_3, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return space() == three.value(); }));
  ASSERT_TRUE(Press(ui::VKEY_9, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(Press(ui::VKEY_1, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] { return space() == home; }));
  FocusPage(row);
  // Cmd+Option+Right and Left step through the spaces. They are high priority like the router chords (M4's exception,
  // docs/chromium/m5.md section 1), so the page never sees them either.
  ASSERT_TRUE(Press(ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return space() == two.value(); }));
  ASSERT_TRUE(Press(ui::VKEY_LEFT, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] { return space() == home; }));
  ASSERT_TRUE(Press(ui::VKEY_LEFT, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN));
  ASSERT_TRUE(Press(ui::VKEY_A, ui::EF_COMMAND_DOWN));
  // The first space has nothing before it.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Contents(row) && std::ranges::contains(Keys(row), "Meta+KeyA");
  }));
  EXPECT_EQ(space(), home);
  for (const std::string& key : Keys(row)) {
    EXPECT_FALSE(key.starts_with("Control+Digit")) << key;
    EXPECT_FALSE(key.starts_with("Meta+Alt+Arrow")) << key;
  }
}

// M5: the command bar chords and focus return ----------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluInteractiveTest, CluInteractiveCommandBarChords) {
  const std::string row = Open("bar");
  // Cmd+T opens the bar over the focused page; real typing reaches its input.
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  WaitForBar();
  EXPECT_EQ(window().command_session()->mode, "new-tab");
  ASSERT_TRUE(Press(ui::VKEY_Q));
  ASSERT_TRUE(Press(ui::VKEY_Z));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay(),
                           "document.querySelector('[data-surface=\"command\"] "
                           "input')?.value ?? ''")
               .ExtractString() == "qz";
  }));
  Shot("command-bar-typed");
  // Cmd+L inside the bar switches it to edit-url, with the page's URL in it.
  ASSERT_TRUE(Press(ui::VKEY_L, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window().command_session() &&
           window().command_session()->mode == "edit-url";
  }));
  // Esc closes it, and the page has the keys again.
  ASSERT_TRUE(Press(ui::VKEY_ESCAPE));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window().command_session().has_value(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return PageHasFocus(row); }));
  EXPECT_FALSE(shell().IsOverlayVisible());
  ASSERT_TRUE(Press(ui::VKEY_K));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return std::ranges::contains(Keys(row), "KeyK"); }));
  // The typed letters went to the bar, never to the page.
  EXPECT_FALSE(std::ranges::contains(Keys(row), "KeyQ"));

  // Cmd+L from the page opens edit-url; Cmd+L again closes it.
  ASSERT_TRUE(Press(ui::VKEY_L, ui::EF_COMMAND_DOWN));
  WaitForBar();
  EXPECT_EQ(window().command_session()->mode, "edit-url");
  ASSERT_TRUE(Press(ui::VKEY_L, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window().command_session().has_value(); }));
  // Cmd+T, then Cmd+T again, closes it the same way.
  ASSERT_TRUE(base::test::RunUntil([&] { return PageHasFocus(row); }));
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  WaitForBar();
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window().command_session().has_value(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return PageHasFocus(row); }));
  // A click on the scrim closes it as well.
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  WaitForBar();
  const gfx::Rect page = Contents(row)->GetContainerBounds();
  ASSERT_TRUE(Click(gfx::Point(page.x() + 30, page.bottom() - 30)));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window().command_session().has_value(); }));
  // The click closed the bar and went no further.
  EXPECT_TRUE(Clicks(row).empty());
}

// M3 and M5: Cmd+Shift+K clears Today with an Undo toast; a real click on Undo brings the rows back. Cmd+Shift+L
// (Window > Open Library…) shows the Library.
IN_PROC_BROWSER_TEST_F(CluInteractiveTest, CluInteractiveClearTodayAndUndo) {
  Open("one");
  Open("two");
  const std::string row = Open("three");
  const std::vector<std::string> today = ShownToday();
  ASSERT_GE(today.size(), 3u);
  ASSERT_TRUE(Press(ui::VKEY_K, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] { return ShownToday().empty(); }))
      << Seen();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kToastSlot); }));
  Shot("clear-today-toast");
  const gfx::Point undo =
      ScreenRectOf(shell().passive_contents(kToastSlot), ".clu-toast button")
          .CenterPoint();
  ASSERT_TRUE(Click(undo));
  ASSERT_TRUE(base::test::RunUntil([&] { return ShownToday() == today; }))
      << WindowsAt(undo);
  // The Undo toast gives way to a short "Restored" one.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().IsPassiveVisible(kToastSlot); }));
  // The toast never took focus: the window is still key and the click activated nothing else.
  EXPECT_TRUE(CluIsActive());
  EXPECT_TRUE(browser()->GetWindow()->IsActive());

  FocusPage(window().ActiveRowId());
  ASSERT_TRUE(Press(ui::VKEY_L, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().archive_panel(); }));
  EXPECT_FALSE(row.empty());
}

// M3: a mouse pick from the native row menu -------------------------------------------------------------------------

// A Today row's menu has no Pin item, so the pick is Archive Tab.
IN_PROC_BROWSER_TEST_F(CluInteractiveTest, CluInteractiveRowMenuPick) {
  const std::string row = Open("menu");
  ASSERT_EQ(tree().Get(row)->section, Section::kToday);
  const gfx::Point at = RowCenter(row);
  CluMenuPicker* picker = [[CluMenuPicker alloc] initWithTitle:@"Archive Tab"
                                                          from:at];
  ASSERT_TRUE(RightClick(at));
  const bool archived = base::test::RunUntil([&] {
    const auto* node = tree().Get(row);
    return !node || node->section != Section::kToday;
  });
  [picker stop];
  EXPECT_TRUE(picker.opened);
  EXPECT_TRUE(picker.clicked) << "highlighted: "
                              << base::SysNSStringToUTF8(picker.seen);
  ASSERT_TRUE(archived);
  EXPECT_EQ(std::ranges::count(ShownToday(), row), 0);
}

// M3: the hover card under a real pointer --------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluInteractiveTest, CluInteractiveHoverCard) {
  const std::string other = Open("other");
  const std::string row = Open("hover");
  ASSERT_TRUE(MoveTo(PagePoint()));
  const gfx::Point at = RowCenter(other);
  ASSERT_TRUE(MoveTo(at));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kHoverSlot); }))
      << "under the pointer: " << WindowsAt(at) << "; the frame's :hover: "
      << content::EvalJs(frame(), kHovered).ExtractString();
  Shot("hover-card");
  // The card is the title pill, hung 11 right of and 12 below the pointer.
  const gfx::Rect card =
      ScreenRectOf(shell().passive_contents(kHoverSlot), ".clu-hover-card");
  EXPECT_NEAR(card.x(), at.x() + 11, 2);
  EXPECT_NEAR(card.y(), at.y() + 12, 2);
  EXPECT_NEAR(card.height(), 28, 1);
  // A click on the row while its card shows lands on the row.
  ASSERT_TRUE(Click(RowCenter(other)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == other; }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().IsPassiveVisible(kHoverSlot); }));

  // The card shows again on the other row; the pointer moving onto the card leaves the row, so the card goes, and a
  // click there lands on the sidebar row under it (the card hangs into the next row, `other`).
  ASSERT_TRUE(MoveTo(PagePoint()));
  ASSERT_TRUE(MoveTo(RowCenter(row)));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kHoverSlot); }));
  // A point on the card over the row it hangs into, left of the close button a hovered row shows at its right.
  gfx::Rect on_other =
      ScreenRectOf(shell().passive_contents(kHoverSlot), ".clu-hover-card");
  on_other.Intersect(ScreenRectOf(frame(), RowSelector(other)));
  ASSERT_FALSE(on_other.IsEmpty())
      << "the card does not hang into the next row";
  const gfx::Point on_card(on_other.x() + 8, on_other.CenterPoint().y());
  ASSERT_TRUE(MoveTo(on_card));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().IsPassiveVisible(kHoverSlot); }));
  // The WindowServer takes the card's window off the screen a moment after AppKit hides it; a hand's click comes
  // later than that.
  const base::TimeTicks hidden = base::TimeTicks::Now();
  NSWindow* card_window = NativeWindowOf(shell().passive_contents(kHoverSlot));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return ScreenOrderOf(card_window) < 0; }));
  const base::TimeDelta gone = base::TimeTicks::Now() - hidden;
  EXPECT_LT(gone, base::Milliseconds(100));
  LOG(INFO) << "the hover card's window left the screen "
            << gone.InMilliseconds() << " ms after it hid";
  const std::string under = WindowsAt(on_card);
  ASSERT_TRUE(Click(RowCenter(row)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == row; }));
  // A hand reaches the spot, then clicks: the pointer rests there a moment first. A press posted in the same instant
  // as the move that ends a previous row's hover was lost every time (3 of 3); 150 ms later it lands every time.
  ASSERT_TRUE(MoveTo(on_card));
  const base::TimeTicks rest = base::TimeTicks::Now() + base::Milliseconds(150);
  ASSERT_TRUE(base::test::RunUntil([&] { return base::TimeTicks::Now() >= rest; }));
  ASSERT_TRUE(Click(on_card));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == other; }))
      << "under the pointer before the click: " << under << "; after: "
      << WindowsAt(on_card);
  EXPECT_FALSE(shell().IsPassiveVisible(kHoverSlot));
}

// docs/chromium/fixes.md (m7.md section 5): with the tweak editor open, a real click on the page beside it reaches the
// page, and the editor stays open --------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluInteractiveTest,
                       CluInteractiveThePageBesideTheTweakEditorTakesClicks) {
  const std::string row = Open("tweaked");
  ASSERT_TRUE(window().site().OpenTweakEditor(row).has_value());
  ASSERT_TRUE(base::test::RunUntil([&] { return shell().IsOverlayVisible(); }));
  const gfx::Rect editor = ScreenRectOf(overlay(), "[data-tweak-editor]");
  ASSERT_FALSE(editor.IsEmpty());
  const gfx::Rect page = Contents(row)->GetContainerBounds();
  const gfx::Point beside(page.x() + 40, page.CenterPoint().y());
  ASSERT_FALSE(editor.Contains(beside));
  const size_t before = Clicks(row).size();
  ASSERT_TRUE(Click(beside));
  ASSERT_TRUE(base::test::RunUntil([&] { return Clicks(row).size() > before; }))
      << WindowsAt(beside);
  EXPECT_TRUE(shell().IsOverlayVisible());
  Shot("tweak-editor-page-click");
}

// A page renderer crash: the crashed page's view refuses focus and views moves focus on as if Tab were
// pressed. The frame takes it with nothing focused: no ring on the sidebar toggle and no "Hide sidebar" tooltip.
IN_PROC_BROWSER_TEST_F(CluInteractiveTest,
                       CluInteractiveACrashedPageLeavesNoFocusRingInTheFrame) {
  const std::string row = Open("crashed");
  FocusPage(row);
  content::WebContents* page = Contents(row);
  content::RenderProcessHostWatcher gone(
      page->GetPrimaryMainFrame()->GetProcess(),
      content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  page->GetPrimaryMainFrame()->GetProcess()->Shutdown(content::RESULT_CODE_KILLED);
  gone.Wait();
  ASSERT_TRUE(base::test::RunUntil([&] { return page->IsCrashed(); }));
  // Past the tooltip's 450 ms delay.
  base::RunLoop wait;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, wait.QuitClosure(), base::Milliseconds(700));
  wait.Run();
  EXPECT_EQ(content::EvalJs(frame(),
                            "document.querySelectorAll(':focus-visible').length"),
            0);
  EXPECT_EQ(content::EvalJs(frame(),
                            "document.querySelectorAll('[role=\"tooltip\"]').length"),
            0);
  Shot("crashed-page-frame-focus");
}

// How the passive overlays stack with the command bar ---------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluInteractiveTest,
                       CluInteractiveOverlaysStackUnderTheCommandBar) {
  Open("one");
  const std::string row = Open("two");
  const std::vector<std::string> today = ShownToday();

  // A hover card goes when the bar opens.
  ASSERT_TRUE(MoveTo(PagePoint()));
  ASSERT_TRUE(MoveTo(RowCenter(row)));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kHoverSlot); }));
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  WaitForBar();
  EXPECT_FALSE(shell().IsPassiveVisible(kHoverSlot));
  ASSERT_TRUE(Press(ui::VKEY_ESCAPE));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window().command_session().has_value(); }));
  ASSERT_TRUE(MoveTo(PagePoint()));

  // The Undo toast stays under the bar (the top layer covers the passive ones): a click on Undo while the bar
  // is open closes the bar and undoes nothing.
  FocusPage(row);
  ASSERT_TRUE(Press(ui::VKEY_K, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kToastSlot); }));
  const gfx::Point undo =
      ScreenRectOf(shell().passive_contents(kToastSlot), ".clu-toast button")
          .CenterPoint();
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  WaitForBar();
  NSWindow* top = NativeWindowOf(overlay());
  NSWindow* toast = NativeWindowOf(shell().passive_contents(kToastSlot));
  ASSERT_NE(top, toast);
  ASSERT_GE(ScreenOrderOf(top), 0);
  ASSERT_GE(ScreenOrderOf(toast), 0);
  EXPECT_LT(ScreenOrderOf(top), ScreenOrderOf(toast));
  Shot("toast-under-command-bar");
  const std::string under = WindowsAt(undo);
  ASSERT_TRUE(Click(undo));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window().command_session().has_value(); }));
  EXPECT_TRUE(ShownToday().empty()) << under;
  // With the bar gone, the same click undoes. The bar's window leaves the screen a moment after it hides (the
  // WindowServer orders it out after AppKit), and a click posted before that goes to the leaving window.
  ASSERT_TRUE(shell().IsPassiveVisible(kToastSlot));
  ASSERT_TRUE(base::test::RunUntil([&] { return ScreenOrderOf(top) < 0; }));
  ASSERT_TRUE(Click(undo));
  ASSERT_TRUE(base::test::RunUntil([&] { return ShownToday() == today; }))
      << WindowsAt(undo);

  // Ctrl+Tab while the bar is open: the switcher runs under it and the Control release still commits.
  FocusPage(window().ActiveRowId());
  const std::string shown = window().ActiveRowId();
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  WaitForBar();
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kSwitcherSlot); }));
  const int switcher_order =
      ScreenOrderOf(NativeWindowOf(shell().passive_contents(kSwitcherSlot)));
  ASSERT_GE(switcher_order, 0);
  EXPECT_LT(ScreenOrderOf(top), switcher_order);
  Release(ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !window().switcher().active(); }));
  EXPECT_NE(window().ActiveRowId(), shown);
}


// 12.3 with the real pointer and keys (docs/chromium/sidebar-pip.md) ------------------------------------------------

class CluInteractiveSidebarTest : public CluInteractiveTest {
 protected:
  CluAppSettings& settings() { return *CluAppSettings::Get(); }
  int width() { return shell().chrome_state().sidebar_width; }
  // The resize handle's center (on the sidebar's edge: the band is 4 inside and 4 over the card), on screen, once the
  // frame draws it at the width the browser holds (the state patch trails a drag's last call).
  gfx::Point Grip() {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return content::EvalJs(
                 frame(),
                 "document.querySelector('[data-shell=\"resize-handle\"]')"
                 "?.getAttribute('aria-valuenow') ?? '-1'")
                 .ExtractString() == base::NumberToString(width());
    })) << width();
    // 2 inside the sidebar: the band's half over the card only takes the pointer in the card's gutters, where the
    // page view does not cover it.
    return ScreenRectOf(frame(), "[data-shell=\"resize-handle\"]")
               .CenterPoint() -
           gfx::Vector2d(2, 0);
  }
  // The window's client area on screen (the frame WebUI fills it).
  gfx::Rect Client() { return frame()->GetContainerBounds(); }
  void WaitForTween() {
    ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  }
  // Where the active page sits on screen, relative to the client area.
  int PageX() {
    return Contents(window().ActiveRowId())->GetContainerBounds().x() -
           Client().x();
  }
  // A second Clu window, made key; it must open on the tests display as the first did.
  BrowserWindowInterface* SecondWindow() {
    BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());
    const gfx::Rect bounds = second->GetWindow()->GetBounds();
    EXPECT_FALSE(display::Screen::Get()->GetPrimaryDisplay().bounds().Intersects(
        bounds))
        << "the second window opened on the main display at "
        << bounds.ToString();
    return second;
  }
  void Refocus() {
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(base::test::RunUntil([] { return CluIsActive(); }));
  }
};

// The sidebar's edge under a real drag: the width follows, clamps at 500 and 164, persists for a new window, and a
// double-click on the edge resets it to 214.
IN_PROC_BROWSER_TEST_F(CluInteractiveSidebarTest, CluInteractiveSidebarResize) {
  Open("resize");
  ASSERT_EQ(width(), kSidebarWidth);
  gfx::Point grip = Grip();
  ASSERT_TRUE(Drag(grip, grip + gfx::Vector2d(100, 0)));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return settings().last_sidebar_width() != kSidebarWidth; }))
      << "under the pointer: " << WindowsAt(grip);
  // 100 points right of the default width.
  EXPECT_LE(std::abs(width() - (kSidebarWidth + 100)), 1) << width();
  EXPECT_EQ(settings().last_sidebar_width(), width());
  EXPECT_TRUE(base::test::RunUntil([&] { return std::abs(PageX() - width()) <= 1; }))
      << PageX();

  // Far right: 500.
  grip = Grip();
  ASSERT_TRUE(Drag(grip, grip + gfx::Vector2d(400, 0)));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return settings().last_sidebar_width() == kSidebarMaxWidth; }))
      << width();
  EXPECT_EQ(width(), kSidebarMaxWidth);
  Shot("sidebar-500");

  // A new window starts at the width the drag left.
  BrowserWindowInterface* second = SecondWindow();
  ASSERT_TRUE(CluShell::From(second));
  EXPECT_EQ(CluShell::From(second)->chrome_state().sidebar_width,
            kSidebarMaxWidth);
  CloseBrowserSynchronously(second);
  Refocus();

  // Far left, still inside the window: 164.
  grip = Grip();
  ASSERT_TRUE(Drag(grip, gfx::Point(Client().x() + 40, grip.y())));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return settings().last_sidebar_width() == kSidebarMinWidth; }))
      << width();
  EXPECT_EQ(width(), kSidebarMinWidth);

  // A double-click on the edge: 214, for the next window too.
  ASSERT_TRUE(DoubleClick(Grip()));
  ASSERT_TRUE(base::test::RunUntil([&] { return width() == kSidebarWidth; }))
      << width();
  EXPECT_EQ(settings().last_sidebar_width(), kSidebarWidth);
}

// Cmd+S from a focused page hides and shows the sidebar; a page that claims Cmd+S keeps it, and Ctrl+Cmd+S toggles
// anyway, the page never seeing it.
IN_PROC_BROWSER_TEST_F(CluInteractiveSidebarTest,
                       CluInteractiveSidebarToggleKeys) {
  const std::string row = Open("toggle");
  ASSERT_FALSE(shell().chrome_state().sidebar_hidden);
  ASSERT_TRUE(Press(ui::VKEY_S, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().chrome_state().sidebar_hidden; }))
      << Seen();
  WaitForTween();
  // The page view takes its place with the layout pass after the tween's last frame.
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return std::abs(PageX() - kCardInset) <= 1; }))
      << PageX();
  // Persisted with the next state publish (16 ms coalesced); M11's hide takes one frame, so it can trail the hide.
  EXPECT_TRUE(base::test::RunUntil([&] { return settings().last_sidebar_hidden(); }));
  Shot("sidebar-hidden");
  ASSERT_TRUE(Press(ui::VKEY_S, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().chrome_state().sidebar_hidden; }))
      << Seen();
  WaitForTween();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return std::abs(PageX() - shell().layout().effective_width) <= 1;
  })) << PageX();

  const std::string claims = OpenUrl(
      embedded_test_server()->GetURL("/spaces/keylog.html?prevent=Meta%2BKeyS"));
  ASSERT_TRUE(Press(ui::VKEY_S, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return std::ranges::count(Keys(claims), "Meta+KeyS") == 1;
  })) << base::JoinString(Keys(claims), " ");
  Settle(claims);
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  ASSERT_TRUE(Press(ui::VKEY_S, ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().chrome_state().sidebar_hidden; }))
      << Seen();
  Settle(claims);
  for (const std::string& key : Keys(claims)) {
    EXPECT_FALSE(key.starts_with("Meta+Control+KeyS")) << key;
  }
  ASSERT_TRUE(Press(ui::VKEY_S, ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().chrome_state().sidebar_hidden; }));
}

// 12.3 third box: the real pointer resting in the left gutter opens the peek after the dwell; it covers the sidebar
// width plus 16 px and leaves the page where it is; its own hover card holds it; leaving closes it; a pointer resting
// on it outlasts the 5 s guard; blur and Cmd+S close it.
IN_PROC_BROWSER_TEST_F(CluInteractiveSidebarTest,
                       CluInteractiveHoverPeekFromTheGutter) {
  const std::string other = Open("other");
  const std::string row = Open("peek");
  ASSERT_TRUE(Press(ui::VKEY_S, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().chrome_state().sidebar_hidden; }));
  WaitForTween();
  CluWindowSidebarPeek& peek = window().sidebar_peek();
  const gfx::Rect client = Client();
  const gfx::Point gutter(client.x() + 3, client.y() + client.height() / 2);
  ASSERT_TRUE(MoveTo(PagePoint()));
  const base::TimeTicks start = base::TimeTicks::Now();
  ASSERT_TRUE(MoveTo(gutter));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek.is_open(); }))
      << "under the pointer: " << WindowsAt(gutter);
  EXPECT_GE(base::TimeTicks::Now() - start, kPeekDwell);
  ASSERT_TRUE(shell().IsPassiveVisible(kPeekSlot));
  content::WebContents* peek_ui = shell().passive_contents(kPeekSlot);
  ASSERT_EQ(true,
            content::EvalJs(peek_ui, content::JsReplace(
                                         kWaitForSelector,
                                         "[data-shell=\"sidebar-peek\"]")));
  Shot("hover-peek");
  // The peek's window as the WindowServer has it: the sidebar width plus the bleed, at the window's left edge.
  const int expected = shell().layout().effective_width + kPeekBleed;
  EXPECT_EQ(NSWidth(NativeWindowOf(peek_ui).frame), expected);
  EXPECT_EQ(peek_ui->GetContainerBounds().x(), client.x());
  EXPECT_LE(std::abs(PageX() - kCardInset), 1);

  // A row in the peek: its hover card shows beside it, and the peek stays.
  const std::string row_in_peek =
      base::StrCat({"[data-shell=\"sidebar-peek\"] [data-tab-id=\"", other,
                    "\"]"});
  const gfx::Point on_row = ScreenRectOf(peek_ui, row_in_peek).CenterPoint();
  ASSERT_TRUE(MoveTo(on_row));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().IsPassiveVisible(kHoverSlot); }))
      << "under the pointer: " << WindowsAt(on_row);
  EXPECT_TRUE(peek.is_open());
  EXPECT_EQ(peek.controller().children(), 1);
  Shot("hover-peek-card");

  // Leaving for the page: the card goes, then the peek after the grace.
  ASSERT_TRUE(MoveTo(PagePoint()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !peek.is_open() && !shell().IsPassiveVisible(kHoverSlot);
  }));
  NSWindow* peek_window = NativeWindowOf(peek_ui);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return ScreenOrderOf(peek_window) < 0; }));

  // A pointer that rests on the peek without moving keeps it past the 5 s guard.
  ASSERT_TRUE(MoveTo(gutter));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek.is_open(); }));
  const base::TimeTicks resting = base::TimeTicks::Now();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return base::TimeTicks::Now() - resting > kPeekGuard + base::Seconds(1);
  }));
  EXPECT_TRUE(peek.is_open());

  // Another window taking focus closes it.
  BrowserWindowInterface* second = SecondWindow();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(second));
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek.is_open(); }));
  CloseBrowserSynchronously(second);
  Refocus();

  // Cmd+S while it shows pins the sidebar.
  ASSERT_TRUE(MoveTo(PagePoint()));
  ASSERT_TRUE(MoveTo(gutter));
  ASSERT_TRUE(base::test::RunUntil([&] { return peek.is_open(); }));
  ASSERT_TRUE(Press(ui::VKEY_S, ui::EF_COMMAND_DOWN));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !shell().chrome_state().sidebar_hidden; }));
  EXPECT_FALSE(peek.is_open());
  EXPECT_FALSE(shell().IsPassiveVisible(kPeekSlot));
}

// Auto picture-in-picture (docs/PLAN.md 12.9 box 2) with real keys and a real click: a playing, heard clip enters PiP
// when a real Ctrl+Tab leaves its tab and leaves it on the way back; a real Cmd+T and Enter that open another tab
// enter it again; a real click on the PiP window's back-to-tab button shows the tab. The Mac's sound stays muted:
// Chromium's audibility check reads the stream, not the speakers.
IN_PROC_BROWSER_TEST_F(CluInteractiveSidebarTest,
                       CluInteractiveAutoPipFromRealKeysAndBackToTab) {
  const std::string other = Open("other");
  const std::string video = OpenUrl(net::FilePathToFileURL(
      base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
          .AppendASCII("clu/test/data/split/clip.html")));
  content::WebContents* contents = Contents(video);
  content::MediaStartStopObserver started(
      contents, content::MediaStartStopObserver::Type::kStart);
  ASSERT_TRUE(
      content::ExecJs(contents, "document.getElementById('play').click()"));
  started.Wait();
  const auto heard = [&] {
    const media::PictureInPictureEventsInfo::AutoPipInfo info =
        AutoPictureInPictureTabHelper::FromWebContents(contents)
            ->GetAutoPipInfo();
    return info.has_audio_focus && info.is_playing && info.was_recently_audible;
  };
  ASSERT_TRUE(base::test::RunUntil(heard));
  FocusPage(video);

  // A real Ctrl+Tab tap: the other tab shows and the clip enters PiP.
  content::MediaStartStopObserver entered(
      contents, content::MediaStartStopObserver::Type::kEnterPictureInPicture);
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  Release(ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == other; }))
      << Seen();
  entered.Wait();
  EXPECT_TRUE(contents->HasPictureInPictureVideo());
  Shot("auto-pip");

  // Ctrl+Tab back: PiP ends.
  content::MediaStartStopObserver left(
      contents, content::MediaStartStopObserver::Type::kExitPictureInPicture);
  ASSERT_TRUE(Hold(ui::EF_CONTROL_DOWN));
  ASSERT_TRUE(PressHeld(ui::VKEY_TAB, ui::EF_CONTROL_DOWN));
  Release(ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == video; }))
      << Seen();
  left.Wait();
  EXPECT_FALSE(contents->HasPictureInPictureVideo());

  // A real Cmd+T, a URL in the bar and a real Enter open a new tab: PiP again.
  ASSERT_TRUE(base::test::RunUntil(heard));
  content::MediaStartStopObserver again(
      contents, content::MediaStartStopObserver::Type::kEnterPictureInPicture);
  ASSERT_TRUE(Press(ui::VKEY_T, ui::EF_COMMAND_DOWN));
  WaitForBar();
  EXPECT_EQ(true, content::EvalJs(
                      overlay(),
                      content::JsReplace(
                          kTypeIntoCommandBar,
                          embedded_test_server()->GetURL("/tabs/a.html").spec())));
  ASSERT_TRUE(Press(ui::VKEY_RETURN));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::string active = window().ActiveRowId();
    return active != video && active != other;
  })) << Seen();
  again.Wait();

  // A real click on the PiP window's back-to-tab button: the clip's tab shows and PiP ends.
  auto* pip = static_cast<VideoOverlayWindowViews*>(
      content::PictureInPictureWindowController::
          GetOrCreateVideoPictureInPictureController(contents)
              ->GetWindowForTesting());
  ASSERT_TRUE(pip);
  ASSERT_TRUE(base::test::RunUntil([&] { return pip->IsVisible(); }));
  const gfx::Rect pip_bounds = pip->GetWindowBoundsInScreen();
  ASSERT_FALSE(display::Screen::Get()->GetPrimaryDisplay().bounds().Intersects(
      pip_bounds))
      << "the PiP window opened on the main display at "
      << pip_bounds.ToString();
  // The pointer over the PiP window shows its controls.
  ASSERT_TRUE(MoveTo(pip_bounds.CenterPoint()))
      << "under the pointer: " << WindowsAt(pip_bounds.CenterPoint());
  views::View* back = pip->back_to_tab_button_for_testing();
  ASSERT_TRUE(back);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return back->GetVisible() && back->IsDrawn() && back->GetEnabled();
  }));
  Shot("auto-pip-controls");
  const gfx::Point at = back->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(Click(at)) << "under the pointer: " << WindowsAt(at);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == video; }))
      << "under the pointer: " << WindowsAt(at);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !contents->HasPictureInPictureVideo(); }));
}

}  // namespace
}  // namespace clu
