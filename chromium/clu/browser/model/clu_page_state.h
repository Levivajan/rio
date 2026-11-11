// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_PAGE_STATE_H_
#define CLU_BROWSER_MODEL_CLU_PAGE_STATE_H_

#include <optional>
#include <string>

namespace content {
class WebContents;
}  // namespace content

// A live page's history as a tab row stores it (clu_nav_state.h), and the way back: Chromium's own session
// serialization (sessions::ContentSerializedNavigationBuilder) carries URL, title and page state per entry.
namespace clu {

// The page's back and forward entries, capped, as nav_state JSON; nullopt for a page with nothing restorable.
std::optional<std::string> CaptureNavState(content::WebContents* contents);

// Restores `json` into a fresh WebContents that has not navigated yet. The selected entry loads when the page is
// shown. False (and nothing restored) for junk JSON or entries the policy refuses.
bool RestoreNavState(content::WebContents* contents, const std::string& json);

// The URL a tab row records for a live page: the last committed URL, or the pending one before the first commit.
std::string PageUrl(content::WebContents* contents);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_PAGE_STATE_H_
