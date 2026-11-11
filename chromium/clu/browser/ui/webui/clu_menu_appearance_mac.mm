// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_menu_appearance.h"

#import <Cocoa/Cocoa.h>

#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

namespace clu {

struct ScopedMenuAppearance::Saved {
  NSWindow* __weak window = nil;
  NSAppearance* appearance = nil;
};

namespace {

NSWindow* WindowOf(views::Widget* widget) {
  return widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
}

}  // namespace

ScopedMenuAppearance::ScopedMenuAppearance(views::Widget* widget,
                                           std::optional<bool> light) {
  NSWindow* window = WindowOf(widget);
  if (!window || !light) {
    return;
  }
  saved_ = std::make_unique<Saved>();
  saved_->window = window;
  saved_->appearance = window.appearance;
  window.appearance = [NSAppearance
      appearanceNamed:*light ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua];
}

ScopedMenuAppearance::~ScopedMenuAppearance() {
  if (saved_ && saved_->window) {
    saved_->window.appearance = saved_->appearance;
  }
}

// static
const char* ScopedMenuAppearance::WindowAppearanceForTesting(
    views::Widget* widget) {
  NSWindow* window = WindowOf(widget);
  if (!window || !window.appearance) {
    return "inherit";
  }
  return [window.appearance.name isEqualToString:NSAppearanceNameDarkAqua]
             ? "dark"
             : "light";
}

}  // namespace clu
