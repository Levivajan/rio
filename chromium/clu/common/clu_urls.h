// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_COMMON_CLU_URLS_H_
#define CLU_COMMON_CLU_URLS_H_

// The hosts and URLs of Clu's own WebUIs (docs/chromium/m2-ui-bridge.md 2.1). Users see them as rio://frame and
// rio://overlay; they never load in a tab.
namespace clu {

inline constexpr char kCluFrameHost[] = "clu-frame";
inline constexpr char kCluOverlayHost[] = "clu-overlay";
inline constexpr char kCluFrameUrl[] = "chrome://clu-frame/";
// The frame of a Mini window: its toolbar over the one page
// (src/renderer/ui/multitask/MiniApp.tsx).
inline constexpr char kCluMiniFrameUrl[] = "chrome://clu-frame/?mode=mini";
// The Peek layer's overlay under the Peek page (docs/chromium/m6.md).
inline constexpr char kCluUnderPeekOverlayUrl[] =
    "chrome://clu-overlay/?layer=under-peek";
inline constexpr char kCluTopOverlayUrl[] = "chrome://clu-overlay/?layer=top";
// M8: Clu's settings page, a WebUI in a tab (users see rio://settings).
inline constexpr char kCluSettingsHost[] = "clu-settings";
inline constexpr char kCluSettingsUrl[] = "chrome://clu-settings/";
// Help ▸ Contact the Team: CONTACT_URL in src/shared/command/rank.ts.
inline constexpr char kCluContactUrl[] =
    "https://github.com/Levivajan/rio-releases/issues/new";
// Followed by the slot, 0 to 3.
inline constexpr char kCluPassiveOverlayUrlPrefix[] =
    "chrome://clu-overlay/?layer=passive&slot=";

}  // namespace clu

#endif  // CLU_COMMON_CLU_URLS_H_
