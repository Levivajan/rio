// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_DATA_CLU_BROWSING_DATA_H_
#define CLU_BROWSER_DATA_CLU_BROWSING_DATA_H_

#include <cstdint>

#include "base/functional/callback_forward.h"
#include "clu/browser/data/clu_clear_plan.h"

class GURL;
class Profile;

// Runs a ClearPlan on one profile (browsingData.clear): Chromium's BrowsingDataRemover of that profile
// clears the data types, the profile's CluModelService the archive rows. `done` runs once both have finished. Each
// Clu profile is a Chromium profile, so "the right partition" is the profile the payload names.
namespace clu {

void RunClearPlan(Profile* profile, const ClearPlan& plan, base::OnceClosure done);

// View ▸ Clear Cookies and Refresh, Clear Cache and Refresh: clears the `remove_mask` data types of `url`'s site
// (its registrable domain, else its host) on `profile`, then runs `done`.
void ClearSiteData(Profile* profile,
                   const GURL& url,
                   uint64_t remove_mask,
                   base::OnceClosure done);

}  // namespace clu

#endif  // CLU_BROWSER_DATA_CLU_BROWSING_DATA_H_
