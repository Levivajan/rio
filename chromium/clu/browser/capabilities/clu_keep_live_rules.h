// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_KEEP_LIVE_RULES_H_
#define CLU_BROWSER_CAPABILITIES_CLU_KEEP_LIVE_RULES_H_

#include <optional>
#include <string_view>

// Clu's hibernation exemptions in Memory Saver terms (docs/PLAN.md 3.6 and 12.12, docs/chromium/m9.md). Chromium's
// discard policy already keeps visible, audible, capturing, picture-in-picture, DevTools, notification and form-edit
// pages live and waits after a tab was last seen; what it does not know is Clu's own windows and splits. Pure, so the
// rule is testable without a browser.
namespace clu {

struct KeepLiveFacts {
  // The tab's window is a normal Clu window. A Mini window, a popup or an app window shows one page and has no row
  // to restore it from.
  bool normal_window = true;
  // The tab is one pane of a split: a split comes back whole, never with one pane reloading.
  bool split = false;
};

// "window" or "split" when Clu keeps the tab live, nullopt when Chromium's own policy decides.
std::optional<std::string_view> KeepLiveReason(const KeepLiveFacts& facts);

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_KEEP_LIVE_RULES_H_
