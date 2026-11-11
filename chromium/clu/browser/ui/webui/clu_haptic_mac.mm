// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_haptic.h"

#import <AppKit/AppKit.h>

#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"

namespace clu {

namespace {
int g_reorder_haptics = 0;
}  // namespace

void PerformAlignmentHaptic() {
  [NSHapticFeedbackManager.defaultPerformer
      performFeedbackPattern:NSHapticFeedbackPatternAlignment
             performanceTime:NSHapticFeedbackPerformanceTimeNow];
}

bool PerformReorderHaptic() {
  CluAppSettings* settings = CluAppSettings::Get();
  if (!settings || !settings->GetBool(kReorderHapticKey)) {
    return false;
  }
  ++g_reorder_haptics;
  PerformAlignmentHaptic();
  return true;
}

int ReorderHapticsForTesting() {
  return g_reorder_haptics;
}

}  // namespace clu
