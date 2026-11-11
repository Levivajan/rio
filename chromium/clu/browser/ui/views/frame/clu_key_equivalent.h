// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_KEY_EQUIVALENT_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_KEY_EQUIVALENT_H_

namespace clu {

// True while a main menu item runs because its key equivalent was pressed (the event being handled is a key down).
// With the command bar open the overlay window is key, so a Clu chord reaches its menu item instead of CluShell's
// accelerators; it is still the chord (a held Ctrl+Tab starts the switcher).
bool IsKeyEquivalentInFlight();

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_KEY_EQUIVALENT_H_
