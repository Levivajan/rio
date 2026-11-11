// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_DATA_CLU_CLEAR_PLAN_H_
#define CLU_BROWSER_DATA_CLU_CLEAR_PLAN_H_

#include <cstdint>

#include "base/time/time.h"

// Clear browsing data (docs/plan/07-data.md, docs/PLAN.md 12.13 fourth box): which of Chromium's
// BrowsingDataRemover types and which time range a browsingData.clear payload means, plus whether Clu's archive rows
// go too. Pure, so clu_unittests checks each box and range. Chromium clears cookies and cache by time.
namespace clu {

enum class ClearRange { kHour, kDay, kWeek, kMonth, kAll };

struct ClearTypes {
  bool history = false;
  bool cookies = false;
  bool cache = false;
  bool downloads = false;
  bool permissions = false;
  bool archive = false;
};

struct ClearPlan {
  // [begin, end) for BrowsingDataRemover; begin is base::Time() for all time.
  base::Time begin;
  base::Time end;
  // content::BrowsingDataRemover and chrome_browsing_data_remover DATA_TYPE_* bits; 0 when only the archive goes.
  uint64_t remove_mask = 0;
  uint64_t origin_mask = 0;
  bool archive = false;
  // The same range in epoch ms for the archive rows.
  int64_t archive_begin_ms = 0;
  int64_t archive_end_ms = 0;

  bool empty() const { return remove_mask == 0 && !archive; }
};

// Last hour, last 24 hours, last 7 days, last 4 weeks (Chromium's ranges) or all time, ending at `now`. Cookies are
// "Cookies and site data" (Chromium's DATA_TYPE_SITE_DATA: cookies, storage, media licenses), cache is the HTTP and
// code caches, permissions are site settings (DATA_TYPE_CONTENT_SETTINGS), downloads the download history.
ClearPlan PlanClear(ClearRange range, const ClearTypes& types, base::Time now);

}  // namespace clu

#endif  // CLU_BROWSER_DATA_CLU_CLEAR_PLAN_H_
