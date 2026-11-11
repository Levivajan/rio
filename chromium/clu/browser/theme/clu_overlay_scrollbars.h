// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_THEME_CLU_OVERLAY_SCROLLBARS_H_
#define CLU_BROWSER_THEME_CLU_OVERLAY_SCROLLBARS_H_

#include <memory>

#include "ui/native_theme/os_settings_provider.h"

namespace clu {

// Every page, Chromium's own pages and Rio's WebUIs scroll under macOS overlay scrollbars (a thin rounded thumb that
// shows while scrolling, no track column), whether the Mac has a trackpad or a mouse and whatever
// "Show scroll bars" says. It answers macOS's scroller style question for the whole browser process: the
// NativeTheme web instance reads it, and every WebContents passes it to its renderer. Idempotent; lives for the
// process.
void InstallOverlayScrollbars();

// The same provider at `priority`, for tests: browser tests run under a kTesting provider that hides the production
// one, so they put this one over it.
std::unique_ptr<ui::OsSettingsProvider> CreateOverlayScrollbarsProviderForTesting(
    ui::OsSettingsProvider::PriorityLevel priority);

}  // namespace clu

#endif  // CLU_BROWSER_THEME_CLU_OVERLAY_SCROLLBARS_H_
