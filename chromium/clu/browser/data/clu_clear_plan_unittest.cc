// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/data/clu_clear_plan.h"

#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "content/public/browser/browsing_data_remover.h"
#include "testing/gtest/include/gtest/gtest.h"

// The clear plan against Chromium's remover: each box maps to its data types, the range starts where the dialog
// says, and nothing outside the boxes is cleared.
namespace clu {
namespace {

using content::BrowsingDataRemover;
namespace cbdr = chrome_browsing_data_remover;

constexpr base::Time kNow =
    base::Time::FromMillisecondsSinceUnixEpoch(int64_t{1'800'000'000'000});

TEST(CluClearPlanTest, EachRangeStartsWhereTheDialogSays) {
  const ClearTypes history{.history = true};
  EXPECT_EQ(PlanClear(ClearRange::kHour, history, kNow).begin, kNow - base::Hours(1));
  EXPECT_EQ(PlanClear(ClearRange::kDay, history, kNow).begin, kNow - base::Hours(24));
  EXPECT_EQ(PlanClear(ClearRange::kWeek, history, kNow).begin, kNow - base::Days(7));
  EXPECT_EQ(PlanClear(ClearRange::kMonth, history, kNow).begin, kNow - base::Days(28));
  EXPECT_EQ(PlanClear(ClearRange::kAll, history, kNow).begin, base::Time());
  EXPECT_EQ(PlanClear(ClearRange::kHour, history, kNow).end, base::Time::Max());
}

TEST(CluClearPlanTest, EachBoxMapsToItsTypesAndNothingElse) {
  const ClearPlan history = PlanClear(ClearRange::kHour, {.history = true}, kNow);
  EXPECT_EQ(history.remove_mask, cbdr::DATA_TYPE_HISTORY);
  EXPECT_FALSE(history.archive);
  const ClearPlan cookies = PlanClear(ClearRange::kHour, {.cookies = true}, kNow);
  EXPECT_EQ(cookies.remove_mask, cbdr::DATA_TYPE_SITE_DATA);
  EXPECT_TRUE(cookies.remove_mask & BrowsingDataRemover::DATA_TYPE_COOKIES);
  EXPECT_TRUE(cookies.remove_mask & BrowsingDataRemover::DATA_TYPE_LOCAL_STORAGE);
  EXPECT_FALSE(cookies.remove_mask & cbdr::DATA_TYPE_HISTORY);
  EXPECT_EQ(PlanClear(ClearRange::kHour, {.cache = true}, kNow).remove_mask,
            BrowsingDataRemover::DATA_TYPE_CACHE);
  EXPECT_EQ(PlanClear(ClearRange::kHour, {.downloads = true}, kNow).remove_mask,
            BrowsingDataRemover::DATA_TYPE_DOWNLOADS);
  EXPECT_EQ(PlanClear(ClearRange::kHour, {.permissions = true}, kNow).remove_mask,
            cbdr::DATA_TYPE_CONTENT_SETTINGS);
  EXPECT_EQ(history.origin_mask, BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB);
}

TEST(CluClearPlanTest, TheArchiveBoxClearsArchiveRowsInTheSameRange) {
  const ClearPlan plan = PlanClear(ClearRange::kDay, {.archive = true}, kNow);
  EXPECT_EQ(plan.remove_mask, 0u);
  EXPECT_EQ(plan.origin_mask, 0u);
  EXPECT_TRUE(plan.archive);
  EXPECT_FALSE(plan.empty());
  EXPECT_EQ(plan.archive_begin_ms, (kNow - base::Hours(24)).InMillisecondsSinceUnixEpoch());
  EXPECT_EQ(PlanClear(ClearRange::kAll, {.archive = true}, kNow).archive_begin_ms, 0);
}

TEST(CluClearPlanTest, NoBoxTickedClearsNothing) {
  EXPECT_TRUE(PlanClear(ClearRange::kAll, {}, kNow).empty());
}

}  // namespace
}  // namespace clu
