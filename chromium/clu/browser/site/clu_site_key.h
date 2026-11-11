// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_SITE_KEY_H_
#define CLU_BROWSER_SITE_CLU_SITE_KEY_H_

#include <optional>
#include <string>
#include <string_view>

class GURL;

// The site a blocking exception or a Site tweak belongs to (src/shared/site.ts, docs/plan/08-extensions.md "Site
// key"): the lowercase host of an http or https URL with one leading "www." removed. Everything else (about:blank,
// chrome:, chrome-extension:, file:, junk) has no site, and neither exceptions nor tweaks apply to it.
namespace clu {

// URLs longer than this have no site (siteKey refuses strings over 32 KiB).
inline constexpr size_t kMaxSiteUrlLength = 32 * 1024;

std::optional<std::string> SiteKey(const GURL& url);

// True for a string SiteKey() can return: what a stored row or a payload names must survive the round trip.
bool IsSiteKey(std::string_view site);

// True when `host` is `domain` or one of its subdomains.
bool HostMatchesDomain(std::string_view host, std::string_view domain);

// Developer Mode: local state, a dictionary of site key to on or off for the sites the user switched.
inline constexpr char kCluDevModeSitesPref[] = "clu.dev_mode_sites";

// A site without a stored choice is in Developer Mode when it is local: localhost, *.localhost, 127.0.0.1, [::1].
bool DevModeDefault(std::string_view site);

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_SITE_KEY_H_
