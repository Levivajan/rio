// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_INSTANT_LINK_H_
#define CLU_BROWSER_SITE_CLU_INSTANT_LINK_H_

#include <optional>

#include "url/gurl.h"

// Instant Links (Shift+Return in the command bar) open Google's I'm Feeling Lucky search (`btnI=1`,
// src/shared/command/ask.ts). Google answers it with a redirect to its own `/url?q=<top result>` page, which stops on a
// "Redirect Notice" instead of going on. The page's throttle reads the top result out of that redirect and loads it
// directly. Pure, so clu_unittests covers it.
namespace clu {

// True for a Google search URL that asks for the top result (`btnI` with a value).
bool IsInstantLinkSearch(const GURL& url);

// The top result when `start` is an Instant Links search and `redirect` is Google's `/url` page naming an http or
// https address in `q` (or `url`); nullopt for anything else, so every other redirect goes on as usual.
std::optional<GURL> InstantLinkTarget(const GURL& start, const GURL& redirect);

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_INSTANT_LINK_H_
