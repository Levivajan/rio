// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_SITE_PROFILE_H_
#define CLU_BROWSER_SITE_CLU_SITE_PROFILE_H_

namespace base {
class Clock;
}  // namespace base

namespace content {
class BrowserContext;
}  // namespace content

// The blocker and Site tweaks of a profile (docs/chromium/m7.md). They hang off the regular profile and serve its
// private windows too: a private window blocks with the profile's lists and exceptions and shows its tweaks, but
// changes nothing (the handlers refuse writes there, so nothing about a private visit is stored). Made on first use,
// which the navigation throttle makes the profile's first page. Guest and system profiles get none.
namespace clu {

class CluBlocker;
class CluSiteService;

// Null for a context Clu does not serve.
CluSiteService* GetCluSiteService(content::BrowserContext* context);
CluBlocker* GetCluBlocker(content::BrowserContext* context);

// Tests: the clock tweaks stamp updatedAt with, for profiles made from now on (null restores the default).
void SetCluSiteClockForTesting(const base::Clock* clock);

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_SITE_PROFILE_H_
