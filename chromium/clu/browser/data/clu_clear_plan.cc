// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/data/clu_clear_plan.h"

#include <limits>

#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "content/public/browser/browsing_data_remover.h"

namespace clu {

namespace {

base::TimeDelta Span(ClearRange range) {
  switch (range) {
    case ClearRange::kHour:
      return base::Hours(1);
    case ClearRange::kDay:
      return base::Hours(24);
    case ClearRange::kWeek:
      return base::Days(7);
    case ClearRange::kMonth:
      return base::Days(28);
    case ClearRange::kAll:
      return base::TimeDelta::Max();
  }
}

}  // namespace

ClearPlan PlanClear(ClearRange range, const ClearTypes& types, base::Time now) {
  using content::BrowsingDataRemover;
  ClearPlan plan;
  plan.end = base::Time::Max();
  plan.begin = range == ClearRange::kAll ? base::Time() : now - Span(range);
  if (types.history) {
    plan.remove_mask |= chrome_browsing_data_remover::DATA_TYPE_HISTORY;
  }
  if (types.cookies) {
    plan.remove_mask |= chrome_browsing_data_remover::DATA_TYPE_SITE_DATA;
  }
  if (types.cache) {
    plan.remove_mask |= BrowsingDataRemover::DATA_TYPE_CACHE;
  }
  if (types.downloads) {
    plan.remove_mask |= BrowsingDataRemover::DATA_TYPE_DOWNLOADS;
  }
  if (types.permissions) {
    plan.remove_mask |= chrome_browsing_data_remover::DATA_TYPE_CONTENT_SETTINGS;
  }
  // Web origins only, as Chromium's own dialog without "hosted app data".
  plan.origin_mask =
      plan.remove_mask ? BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB : 0;
  plan.archive = types.archive;
  plan.archive_begin_ms =
      range == ClearRange::kAll
          ? 0
          : plan.begin.InMillisecondsSinceUnixEpoch();
  plan.archive_end_ms = std::numeric_limits<int64_t>::max();
  return plan;
}

}  // namespace clu
