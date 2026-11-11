// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_URL_POLICY_H_
#define CLU_BROWSER_UI_WEBUI_CLU_URL_POLICY_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class GURL;

// Which URLs the bridge opens, and how state shows them. Rio adopts Chromium's rules for typed URLs; the
// browser checks every URL again, whatever the WebUI classified.
namespace clu {

// A URL typed or picked in the command bar, or passed to tabs.create: web pages, files, Chromium's internal pages
// and view-source of those. Script, data and blob URLs never load from the bar, and Clu's own frame and overlay
// WebUIs never load in a tab.
bool IsTypedUrlAllowed(const GURL& url);

// A URL from a history row: web pages only.
bool IsWebUrl(const GURL& url);

// The chrome://favicon2 URL the frame WebUI shows for `page_url` (its CSP allows only that origin for icons), or
// nullopt for a page without one. A non-empty `icon_version` goes into the URL as `cluIcon`, a key favicon2 ignores:
// the WebUI caches each favicon2 answer by URL, so a live page's URL changes once Chromium has its icon.
std::optional<std::string> FaviconUrlFor(const GURL& page_url,
                                         std::string_view icon_version = {});

// The icons a page's HTML declares in <link rel="icon" | "apple-touch-icon"> tags, best first: the largest
// declared size up to 256 px, then larger ones. SVG and non-web hrefs are skipped (the image decoder takes neither).
// Only the head is read, at most the first 512 KB; junk markup yields fewer or no URLs, never a crash.
std::vector<GURL> DeclaredIconUrls(std::string_view html, const GURL& base_url);

// `url` as state carries it: the spec in its display form (DisplayUrl), or only the scheme for a URL longer than
// MAX_URL (a huge data: URL).
std::string StateUrl(const GURL& url);

// The rio:// display alias (docs/chromium/m8.md, PLAN 5 "History"): Clu's own pages show as rio:// URLs, the way
// Brave shows brave://. rio://settings, rio://history and rio://extensions are Chromium's settings, history and
// extensions pages, and rio://about is the About page of its settings (docs/chromium/parity-settings.md section 5).
// Rio's app settings are a window with no address. Every other URL is unchanged. Clu has no omnibox, so this is all a display alias needs: state shows the
// alias, and what the command bar or the sidebar sends back maps to the real URL here.
std::string DisplayUrl(std::string_view spec);
// The URL to load for a typed or sent one: a rio:// alias becomes its chrome:// page (an unknown one stays as it was,
// which IsTypedUrlAllowed() refuses), anything else is returned as it is.
GURL ResolveCluUrl(const GURL& url);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_URL_POLICY_H_
