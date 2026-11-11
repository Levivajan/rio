// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SETTINGS_CLU_SETTINGS_HOOKS_H_
#define CLU_BROWSER_SETTINGS_CLU_SETTINGS_HOOKS_H_

class Profile;

// The one-line hooks the app controller patch calls (docs/chromium/parity-settings.md). This header has its own GN
// target with no dependencies; the implementations live in //clu/browser/ui/webui (clu_settings_window.cc,
// clu_quit_mac.mm).
namespace clu {

// AppController's Settings… (Cmd+, with no Rio window in front, or with the settings window itself in front): Rio's
// app settings window opens for `profile`'s Rio window in front, else a new one. False leaves it to Chromium.
bool ShowAppSettingsForProfile(Profile* profile);

// Whether the quit underway comes from an Apple event (the Dock's Quit, AppleScript's quit, a logout or a
// restart). AppController's "Hold ⌘Q to quit" panel then stays away and Rio quits at once.
bool QuitCameFromAppleEvent();

}  // namespace clu

#endif  // CLU_BROWSER_SETTINGS_CLU_SETTINGS_HOOKS_H_
