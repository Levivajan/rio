// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_settings_window.h"

#import <Cocoa/Cocoa.h>

#include "base/apple/foundation_util.h"

#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

namespace clu {

namespace {
NSString* const kTitleId = @"rio.settings.title";
}  // namespace

void StyleSettingsWindow(views::Widget* widget, std::string_view mode) {
  if (!widget) {
    return;
  }
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!window) {
    return;
  }
  // The title bar shows the window's color, the page's toolbar color, so title and tab row read as one toolbar
  // (#ffffff in light mode, #25282a in dark).
  window.titlebarAppearsTransparent = YES;
  window.backgroundColor = [NSColor
      colorWithName:@"CluSettingsToolbar"
      dynamicProvider:^NSColor*(NSAppearance* appearance) {
        const bool dark =
            [[appearance bestMatchFromAppearancesWithNames:@[
              NSAppearanceNameAqua, NSAppearanceNameDarkAqua
            ]] isEqualToString:NSAppearanceNameDarkAqua];
        return dark ? [NSColor colorWithSRGBRed:0x25 / 255.0
                                          green:0x28 / 255.0
                                           blue:0x2a / 255.0
                                          alpha:1]
                    : [NSColor whiteColor];
      }];
  // Rio's appearance, as its own windows take it (Match system follows macOS).
  if (mode == "light") {
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
  } else if (mode == "dark") {
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  } else {
    window.appearance = nil;
  }
  [window standardWindowButton:NSWindowMiniaturizeButton].enabled = NO;
  [window standardWindowButton:NSWindowZoomButton].enabled = NO;
  // The title sits centered in the title bar, as in a preference-style window. macOS 26
  // draws a plain window's title after the traffic lights, even with the preference toolbar style, so the window hides
  // its own title and shows it in a centered label (docs/chromium/variants.md).
  NSView* titlebar = [window standardWindowButton:NSWindowCloseButton].superview;
  if (!titlebar) {
    return;
  }
  window.titleVisibility = NSWindowTitleHidden;
  NSTextField* label = nil;
  for (NSView* view in titlebar.subviews) {
    if ([view.identifier isEqualToString:kTitleId]) {
      label = base::apple::ObjCCast<NSTextField>(view);
    }
  }
  if (!label) {
    label = [NSTextField labelWithString:window.title ?: @""];
    label.identifier = kTitleId;
    label.font = [NSFont titleBarFontOfSize:0];
    label.textColor = NSColor.labelColor;
    label.alignment = NSTextAlignmentCenter;
    label.lineBreakMode = NSLineBreakByTruncatingTail;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [titlebar addSubview:label];
    [NSLayoutConstraint activateConstraints:@[
      [label.centerXAnchor constraintEqualToAnchor:titlebar.centerXAnchor],
      [label.centerYAnchor constraintEqualToAnchor:titlebar.centerYAnchor],
      [label.widthAnchor constraintLessThanOrEqualToAnchor:titlebar.widthAnchor
                                                 constant:-200],
    ]];
  }
  label.stringValue = window.title ?: @"";
}

void SetSettingsWindowTitle(views::Widget* widget) {
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  NSView* titlebar = [window standardWindowButton:NSWindowCloseButton].superview;
  for (NSView* view in titlebar.subviews) {
    if ([view.identifier isEqualToString:kTitleId]) {
      base::apple::ObjCCast<NSTextField>(view).stringValue = window.title ?: @"";
    }
  }
}

}  // namespace clu
