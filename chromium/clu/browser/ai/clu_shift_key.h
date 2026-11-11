// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_AI_CLU_SHIFT_KEY_H_
#define CLU_BROWSER_AI_CLU_SHIFT_KEY_H_

namespace clu {

// Whether Shift is held right now (hover previews open while it is). False off macOS.
bool ShiftKeyDown();
// Tests: 1 holds Shift down, 0 up, -1 reads the keyboard again.
void SetShiftKeyDownForTesting(int down);

}  // namespace clu

#endif  // CLU_BROWSER_AI_CLU_SHIFT_KEY_H_
