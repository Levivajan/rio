// Copyright 2026 The Rio Authors. All rights reserved.

#import <Foundation/Foundation.h>

#include "clu/browser/settings/clu_settings_hooks.h"

namespace clu {

bool QuitCameFromAppleEvent() {
  // AppKit's quit handler calls -[NSApp terminate:] while it handles the event, so the event is still current.
  return NSAppleEventManager.sharedAppleEventManager.currentAppleEvent != nil;
}

}  // namespace clu
