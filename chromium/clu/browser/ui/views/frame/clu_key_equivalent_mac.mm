// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_key_equivalent.h"

#import <AppKit/AppKit.h>

namespace clu {

bool IsKeyEquivalentInFlight() {
  return NSApp.currentEvent.type == NSEventTypeKeyDown;
}

}  // namespace clu
