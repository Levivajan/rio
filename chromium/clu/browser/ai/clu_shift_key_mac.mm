// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ai/clu_shift_key.h"

#import <AppKit/AppKit.h>

namespace clu {

namespace {
int g_shift_for_testing = -1;
}  // namespace

bool ShiftKeyDown() {
  if (g_shift_for_testing >= 0) {
    return g_shift_for_testing == 1;
  }
  return (NSEvent.modifierFlags & NSEventModifierFlagShift) != 0;
}

void SetShiftKeyDownForTesting(int down) {
  g_shift_for_testing = down;
}

}  // namespace clu
