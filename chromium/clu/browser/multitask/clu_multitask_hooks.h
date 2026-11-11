// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MULTITASK_CLU_MULTITASK_HOOKS_H_
#define CLU_BROWSER_MULTITASK_CLU_MULTITASK_HOOKS_H_

#include <vector>

class GURL;
struct NavigateParams;

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

// The one-line hooks M6's upstream patches call (docs/chromium/m6.md section
// 3). This header has its own GN target with no dependencies, so the navigator,
// the throttle list and the app controller reach no Clu code; the
// implementations live in //clu/browser/ui/webui (clu_peek.cc,
// clu_mini_windows.cc). Each answers false, or adds nothing, outside a Clu
// window.
namespace clu {

// chrome::Navigate(), before it picks a window: a Shift-click, or a
// target=_blank or window.open from a pinned tab or a favorite to another host,
// opens in the source window's Peek (05 "Peek"); a new page asked for from a
// Mini window opens in another Mini window. True when Clu took the request;
// `params->navigated_or_inserted_contents` then names the WebContents that
// shows it.
bool InterceptNavigate(NavigateParams* params);

// CreateAndAddChromeThrottlesForNavigation(): a plain cross-host link click in
// a pinned tab or favorite is cancelled and opens in the window's Peek instead
// (CluPeekThrottle).
void MaybeAddPeekThrottle(content::NavigationThrottleRegistry& registry);

// AppController's openStartupTabsReplacingNTP: links from other apps (open-url,
// Handoff, a cold launch's queue) open in Mini windows (decision 27). True when
// Clu took every URL; false leaves all of them to Chromium.
bool OpenUrlsFromOtherApp(const std::vector<GURL>& urls);

}  // namespace clu

#endif  // CLU_BROWSER_MULTITASK_CLU_MULTITASK_HOOKS_H_
