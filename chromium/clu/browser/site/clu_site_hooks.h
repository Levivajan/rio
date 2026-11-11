// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_SITE_HOOKS_H_
#define CLU_BROWSER_SITE_CLU_SITE_HOOKS_H_

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

// The hook chrome_content_browser_client_navigation_throttles.cc calls for every navigation (header only, no deps;
// //clu/browser/site:site implements it). See docs/chromium/m7.md.
namespace clu {

// For a page of a profile Clu serves: attaches the page helper (hiding CSS, Site tweaks, zap mode) to its WebContents,
// and holds a main-frame web navigation until the profile's blocker has its rules in place, at most 5 seconds, so the
// first page after launch is already blocked.
void AddCluNavigationThrottles(content::NavigationThrottleRegistry& registry);

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_SITE_HOOKS_H_
