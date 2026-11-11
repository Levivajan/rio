// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_HAPTIC_H_
#define CLU_BROWSER_UI_WEBUI_CLU_HAPTIC_H_

namespace clu {

// The sidebar's width lock: the trackpad's alignment tick, as macOS gives when a dragged item snaps.
void PerformAlignmentHaptic();

// A sidebar drop that moved tabs: the same tick, unless Advanced's "Haptic feedback when reordering tabs"
// (tabs.reorderHaptic) is off. True when it ticked.
bool PerformReorderHaptic();

// How many times PerformReorderHaptic() ticked in this process.
int ReorderHapticsForTesting();

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_HAPTIC_H_
