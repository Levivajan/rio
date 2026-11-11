// Copyright 2026 The Rio Authors. All rights reserved.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "clu/browser/ui/views/frame/clu_look.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

// AppKit's private reveal amount of a titlebar accessory, 0 (hidden) to 1 (shown), as
// components/remote_cocoa/app_shim/immersive_mode_controller_cocoa.mm declares it. AppKit sets it on every step of the
// full-screen titlebar's slide.
@interface NSTitlebarAccessoryViewController ()
- (double)revealAmount;
- (void)setRevealAmount:(double)revealAmount;
@end

// An empty accessory in the window's titlebar. In full screen the titlebar lives in its own window above the
// screen's top, and AppKit slides that window down with the menu bar when the pointer reaches the top; the accessory
// moves with it and hears each step.
@interface CluTitlebarRevealController : NSTitlebarAccessoryViewController
- (instancetype)initWithWindow:(NSWindow*)window
                      callback:(base::RepeatingCallback<void(int)>)callback;
- (void)stop;
@end

@implementation CluTitlebarRevealController {
  NSWindow* __weak _browserWindow;
  base::RepeatingCallback<void(int)> _callback;
  int _covered;
}

- (instancetype)initWithWindow:(NSWindow*)window
                      callback:(base::RepeatingCallback<void(int)>)callback {
  if ((self = [super init])) {
    _browserWindow = window;
    _callback = std::move(callback);
    self.view = [[NSView alloc] initWithFrame:NSZeroRect];
    self.layoutAttribute = NSLayoutAttributeBottom;
    self.fullScreenMinHeight = 0;
  }
  return self;
}

- (void)stop {
  _callback.Reset();
}

- (void)setRevealAmount:(double)revealAmount {
  [super setRevealAmount:revealAmount];
  // What the slid-down bar covers: its window's height (the menu bar and the titlebar) times how far it is out.
  // Outside full screen the accessory sits in the browser window itself, which covers nothing.
  NSWindow* bar = self.view.window;
  int covered = 0;
  if (bar && bar != _browserWindow && std::isfinite(revealAmount)) {
    covered = static_cast<int>(std::lround(
        std::clamp(revealAmount, 0.0, 1.0) * NSHeight(bar.frame)));
    covered = std::clamp(covered, 0, 200);
  }
  if (covered == _covered) {
    return;
  }
  _covered = covered;
  if (_callback) {
    _callback.Run(covered);
  }
}

@end

namespace clu {

namespace {

class TitlebarRevealWatchMac : public TitlebarRevealWatch {
 public:
  TitlebarRevealWatchMac(NSWindow* window,
                         base::RepeatingCallback<void(int)> on_reveal)
      : window_(window),
        controller_([[CluTitlebarRevealController alloc]
            initWithWindow:window
                  callback:std::move(on_reveal)]) {
    [window addTitlebarAccessoryViewController:controller_];
  }
  TitlebarRevealWatchMac(const TitlebarRevealWatchMac&) = delete;
  TitlebarRevealWatchMac& operator=(const TitlebarRevealWatchMac&) = delete;
  ~TitlebarRevealWatchMac() override {
    [controller_ stop];
    NSWindow* window = window_;
    if (!window) {
      return;
    }
    const NSUInteger index =
        [window.titlebarAccessoryViewControllers indexOfObject:controller_];
    if (index != NSNotFound) {
      [window removeTitlebarAccessoryViewControllerAtIndex:index];
    }
  }

 private:
  NSWindow* __weak window_;
  CluTitlebarRevealController* __strong controller_;
};

}  // namespace

std::unique_ptr<TitlebarRevealWatch> WatchTitlebarReveal(
    views::Widget* widget,
    base::RepeatingCallback<void(int)> on_reveal) {
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return nullptr;
  }
  return std::make_unique<TitlebarRevealWatchMac>(window, std::move(on_reveal));
}

void SetWindowButtonsVisible(views::Widget* widget, bool visible) {
  if (!widget) {
    return;
  }
  // A normal browser window lives in the browser process, so its NSWindow is reachable here (no app shim).
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!window) {
    return;
  }
  for (NSWindowButton kind :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [window standardWindowButton:kind];
    if (button.hidden == visible) {
      button.hidden = !visible;
    }
  }
}

bool IsSecondaryClickEvent() {
  NSEvent* event = [NSApp currentEvent];
  if (!event) {
    return false;
  }
  switch (event.type) {
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeRightMouseDragged:
      return true;
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
      return (event.modifierFlags & NSEventModifierFlagControl) != 0;
    default:
      return false;
  }
}

void SetWidgetIgnoresMouse(views::Widget* widget, bool ignore) {
  NSWindow* window = widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (window && window.ignoresMouseEvents != ignore) {
    window.ignoresMouseEvents = ignore;
  }
}

}  // namespace clu
