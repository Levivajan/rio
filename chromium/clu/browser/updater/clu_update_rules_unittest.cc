// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/updater/clu_update_rules.h"

#include <string>

#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

// base64 of 32 bytes, the shape of SUPublicEDKey.
constexpr char kKey[] = "7i3naQJSgCQEUycqa7pCGgNYG6Fp9205t++5GgJ851A=";

TEST(CluUpdateRulesTest, FeedsAreHttpsOrThisMac) {
  EXPECT_TRUE(IsAllowedFeedUrl(GURL("https://updates.example/clu/appcast.xml")));
  EXPECT_TRUE(IsAllowedFeedUrl(GURL("http://127.0.0.1:47811/appcast.xml")));
  EXPECT_TRUE(IsAllowedFeedUrl(GURL("http://localhost:8000/appcast.xml")));
  EXPECT_TRUE(IsAllowedFeedUrl(GURL("http://[::1]:8000/appcast.xml")));

  EXPECT_FALSE(IsAllowedFeedUrl(GURL("http://updates.example/appcast.xml")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("http://127.0.0.2/appcast.xml")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("http://foo.localhost/appcast.xml")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("ftp://updates.example/appcast.xml")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("file:///tmp/appcast.xml")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("javascript:alert(1)")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("https://user:pw@updates.example/a.xml")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("https://updates.example/a.xml#x")));
  EXPECT_FALSE(IsAllowedFeedUrl(GURL("not a url")));
  EXPECT_FALSE(IsAllowedFeedUrl(
      GURL("https://updates.example/" + std::string(kMaxFeedUrlLength, 'a'))));
}

TEST(CluUpdateRulesTest, ConfigNeedsAGoodFeedAndA32ByteKey) {
  base::expected<UpdateConfig, std::string> config =
      ParseUpdateConfig("https://updates.example/appcast.xml", kKey);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(GURL("https://updates.example/appcast.xml"), config->feed_url);
  EXPECT_EQ(kKey, config->public_key);
  // Whitespace around the key, as a plist edit may leave it.
  EXPECT_TRUE(ParseUpdateConfig("https://updates.example/appcast.xml",
                                std::string(" ") + kKey + "\n")
                  .has_value());

  EXPECT_FALSE(ParseUpdateConfig("", kKey).has_value());
  EXPECT_FALSE(
      ParseUpdateConfig("http://updates.example/appcast.xml", kKey).has_value());
  EXPECT_FALSE(
      ParseUpdateConfig("https://updates.example/appcast.xml", "").has_value());
  EXPECT_FALSE(ParseUpdateConfig("https://updates.example/appcast.xml",
                                 "not base64!")
                   .has_value());
  // 31 and 34 bytes.
  EXPECT_FALSE(ParseUpdateConfig("https://updates.example/appcast.xml",
                                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==")
                   .has_value());
  EXPECT_FALSE(ParseUpdateConfig("https://updates.example/appcast.xml",
                                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==")
                   .has_value());
  EXPECT_FALSE(ParseUpdateConfig(std::string(kMaxFeedUrlLength + 1, 'h'), kKey)
                   .has_value());
}

TEST(CluUpdateRulesTest, VersionsFromTheFeedAreCleanedOrDropped) {
  EXPECT_EQ("158.0.8127.19", CleanUpdateVersion("158.0.8127.19"));
  EXPECT_EQ("159.0.1 beta", CleanUpdateVersion("  159.0.1 beta \n"));
  EXPECT_EQ("2.0+build_7-rc", CleanUpdateVersion("2.0+build_7-rc"));
  EXPECT_FALSE(CleanUpdateVersion(""));
  EXPECT_FALSE(CleanUpdateVersion("   "));
  EXPECT_FALSE(CleanUpdateVersion("<script>"));
  EXPECT_FALSE(CleanUpdateVersion("1.0\x07"));
  EXPECT_FALSE(CleanUpdateVersion("1.0\n2.0"));
  EXPECT_FALSE(CleanUpdateVersion("\xE2\x80\xAE" "1.0"));
  EXPECT_FALSE(CleanUpdateVersion(std::string(kMaxUpdateVersionLength + 1, '1')));
  EXPECT_EQ(std::string(kMaxUpdateVersionLength, '1'),
            CleanUpdateVersion(std::string(kMaxUpdateVersionLength, '1')));
}

TEST(CluUpdateRulesTest, NamesMatchTheTsTypes) {
  EXPECT_EQ("unavailable", UpdateStatusName(UpdateStatus::kUnavailable));
  EXPECT_EQ("idle", UpdateStatusName(UpdateStatus::kIdle));
  EXPECT_EQ("checking", UpdateStatusName(UpdateStatus::kChecking));
  EXPECT_EQ("downloading", UpdateStatusName(UpdateStatus::kDownloading));
  EXPECT_EQ("ready", UpdateStatusName(UpdateStatus::kReady));
  EXPECT_EQ("up-to-date", UpdateStatusName(UpdateStatus::kUpToDate));
  EXPECT_EQ("error", UpdateStatusName(UpdateStatus::kError));
  EXPECT_EQ("checking", UpdateCheckOutcomeName(UpdateCheckOutcome::kChecking));
  EXPECT_EQ("busy", UpdateCheckOutcomeName(UpdateCheckOutcome::kBusy));
  EXPECT_EQ("ready", UpdateCheckOutcomeName(UpdateCheckOutcome::kReady));
  EXPECT_EQ("unavailable",
            UpdateCheckOutcomeName(UpdateCheckOutcome::kUnavailable));
}

class CluUpdateModelTest : public testing::Test {
 protected:
  CluUpdateModelTest() {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'800'000'000'000));
  }

  base::SimpleTestClock clock_;
  CluUpdateModel model_{&clock_};
};

TEST_F(CluUpdateModelTest, UnavailableUntilTheUpdaterStartsAndIgnoresEvents) {
  EXPECT_EQ(UpdateStatus::kUnavailable, model_.Snapshot().status);
  EXPECT_EQ(UpdateCheckOutcome::kUnavailable, model_.BeginCheck(true));
  EXPECT_FALSE(model_.OnFound("2.0"));
  EXPECT_FALSE(model_.OnReady("2.0"));
  EXPECT_FALSE(model_.OnError());
  EXPECT_EQ(UpdateStatus::kUnavailable, model_.Snapshot().status);
  EXPECT_FALSE(model_.Snapshot().checked_at);

  model_.SetAvailable();
  EXPECT_EQ(UpdateStatus::kIdle, model_.Snapshot().status);
}

TEST_F(CluUpdateModelTest, AUserCheckWithNothingNewToastsUpToDateOnTheClock) {
  model_.SetAvailable();
  EXPECT_EQ(UpdateCheckOutcome::kChecking, model_.BeginCheck(true));
  EXPECT_EQ(UpdateStatus::kChecking, model_.Snapshot().status);
  clock_.Advance(base::Seconds(3));
  EXPECT_EQ("Rio is up to date", model_.OnNotFound());
  const UpdateSnapshot snapshot = model_.Snapshot();
  EXPECT_EQ(UpdateStatus::kUpToDate, snapshot.status);
  EXPECT_FALSE(snapshot.version);
  EXPECT_EQ(1'800'000'003'000.0, snapshot.checked_at);
  // The cycle's end changes nothing more.
  model_.OnCycleFinished();
  EXPECT_EQ(UpdateStatus::kUpToDate, model_.Snapshot().status);
}

TEST_F(CluUpdateModelTest, ABackgroundCheckNeverToasts) {
  model_.SetAvailable();
  EXPECT_EQ(UpdateCheckOutcome::kChecking, model_.BeginCheck(false));
  EXPECT_FALSE(model_.OnNotFound());
  EXPECT_EQ(UpdateCheckOutcome::kChecking, model_.BeginCheck(false));
  EXPECT_FALSE(model_.OnFound("158.0.8127.19"));
  EXPECT_EQ(UpdateStatus::kDownloading, model_.Snapshot().status);
  EXPECT_FALSE(model_.OnReady("158.0.8127.19"));
  EXPECT_EQ(UpdateStatus::kReady, model_.Snapshot().status);
  EXPECT_EQ("158.0.8127.19", model_.Snapshot().version);
}

TEST_F(CluUpdateModelTest, AFoundUpdateDownloadsThenStaysReady) {
  model_.SetAvailable();
  model_.BeginCheck(true);
  EXPECT_EQ("Downloading Rio 158.0.8127.19…", model_.OnFound("158.0.8127.19"));
  EXPECT_EQ(UpdateStatus::kDownloading, model_.Snapshot().status);
  EXPECT_EQ("Rio 158.0.8127.19 is ready",
            model_.OnReady("158.0.8127.19"));
  EXPECT_TRUE(model_.ready());

  // Nothing takes the Relaunch button away: no check starts, a late error or "not found" changes nothing.
  EXPECT_EQ(UpdateCheckOutcome::kReady, model_.BeginCheck(true));
  EXPECT_EQ(UpdateCheckOutcome::kReady, model_.BeginCheck(false));
  EXPECT_FALSE(model_.OnError());
  EXPECT_FALSE(model_.OnNotFound());
  EXPECT_FALSE(model_.OnFound("9.9"));
  model_.OnCycleFinished();
  EXPECT_EQ(UpdateStatus::kReady, model_.Snapshot().status);
  EXPECT_EQ("158.0.8127.19", model_.Snapshot().version);
}

TEST_F(CluUpdateModelTest, AJunkVersionIsNeverShown) {
  model_.SetAvailable();
  model_.BeginCheck(true);
  EXPECT_EQ("Downloading the update…", model_.OnFound("<b>9</b>"));
  EXPECT_FALSE(model_.Snapshot().version);
  EXPECT_EQ("The update is ready",
            model_.OnReady(std::string(500, '9')));
  EXPECT_FALSE(model_.Snapshot().version);
}

// The update card's close button hides the card for the ready version only.
TEST_F(CluUpdateModelTest, DismissHidesTheCardForThisVersionOnly) {
  EXPECT_FALSE(model_.Dismiss());
  model_.SetAvailable();
  EXPECT_FALSE(model_.Dismiss());
  EXPECT_FALSE(model_.dismissed_version());
  model_.BeginCheck(false);
  model_.OnReady("158.0.8127.19");
  EXPECT_FALSE(model_.Snapshot().dismissed);

  EXPECT_TRUE(model_.Dismiss());
  EXPECT_EQ("158.0.8127.19", model_.dismissed_version());
  EXPECT_TRUE(model_.Snapshot().dismissed);
  // Still ready: Sparkle installs it on quit.
  EXPECT_EQ(UpdateStatus::kReady, model_.Snapshot().status);
  EXPECT_TRUE(model_.ready());

  // A newer version that becomes ready shows the card again.
  model_.OnReady("158.0.8127.20");
  EXPECT_FALSE(model_.Snapshot().dismissed);
  EXPECT_EQ("158.0.8127.20", model_.Snapshot().version);
}

TEST_F(CluUpdateModelTest, AStoredDismissalHidesTheSameVersionAfterRelaunch) {
  model_.LoadDismissedVersion("158.0.8127.19");
  model_.SetAvailable();
  // Not ready yet: nothing to hide.
  EXPECT_FALSE(model_.Snapshot().dismissed);
  model_.BeginCheck(false);
  model_.OnReady("158.0.8127.19");
  EXPECT_TRUE(model_.Snapshot().dismissed);
  // Loaded once: a later load does not replace it.
  model_.LoadDismissedVersion("1.0");
  EXPECT_TRUE(model_.Snapshot().dismissed);
}

TEST_F(CluUpdateModelTest, AJunkOrEmptyStoredDismissalHidesNothing) {
  model_.LoadDismissedVersion("");
  EXPECT_FALSE(model_.dismissed_version());
  model_.LoadDismissedVersion("<script>");
  EXPECT_FALSE(model_.dismissed_version());
  model_.SetAvailable();
  model_.BeginCheck(false);
  model_.OnReady("158.0.8127.19");
  EXPECT_FALSE(model_.Snapshot().dismissed);
}

TEST_F(CluUpdateModelTest, AnUpdateWithNoVersionCanBeDismissedForThisRun) {
  model_.SetAvailable();
  model_.BeginCheck(false);
  model_.OnReady(std::string(500, '9'));
  EXPECT_TRUE(model_.Dismiss());
  EXPECT_EQ("", model_.dismissed_version());
  EXPECT_TRUE(model_.Snapshot().dismissed);
  model_.OnReady("158.0.8127.20");
  EXPECT_FALSE(model_.Snapshot().dismissed);
}

TEST_F(CluUpdateModelTest, ErrorsToastOnlyForTheUserAndOnlyOnce) {
  model_.SetAvailable();
  model_.BeginCheck(false);
  EXPECT_FALSE(model_.OnError());
  EXPECT_EQ(UpdateStatus::kIdle, model_.Snapshot().status);

  model_.BeginCheck(true);
  EXPECT_EQ("Couldn't check for updates", model_.OnError());
  EXPECT_EQ(UpdateStatus::kError, model_.Snapshot().status);
  // The abort that follows a failed download is the same failure.
  EXPECT_FALSE(model_.OnError());
  EXPECT_EQ(UpdateStatus::kError, model_.Snapshot().status);
  // A new check after an error runs.
  EXPECT_EQ(UpdateCheckOutcome::kChecking, model_.BeginCheck(true));
}

TEST_F(CluUpdateModelTest, AUserCheckWhileOneRunsJoinsIt) {
  model_.SetAvailable();
  EXPECT_EQ(UpdateCheckOutcome::kChecking, model_.BeginCheck(false));
  EXPECT_EQ(UpdateCheckOutcome::kBusy, model_.BeginCheck(true));
  EXPECT_EQ("Rio is up to date", model_.OnNotFound());

  model_.BeginCheck(false);
  model_.OnFound("2.0");
  EXPECT_EQ(UpdateCheckOutcome::kBusy, model_.BeginCheck(true));
  EXPECT_EQ("Rio 2.0 is ready", model_.OnReady("2.0"));
}

TEST_F(CluUpdateModelTest, ACycleThatEndsWithoutAResultGoesIdle) {
  model_.SetAvailable();
  model_.BeginCheck(true);
  model_.OnFound("2.0");
  // Sparkle dismissed the update (an update that needs the user, closed).
  model_.OnCycleFinished();
  EXPECT_EQ(UpdateStatus::kIdle, model_.Snapshot().status);
  EXPECT_TRUE(model_.Snapshot().checked_at);
  // The next background result does not toast for the old user check.
  model_.BeginCheck(false);
  EXPECT_FALSE(model_.OnNotFound());
}

TEST_F(CluUpdateModelTest, OutcomeToasts) {
  EXPECT_FALSE(ToastForCheckOutcome(UpdateCheckOutcome::kChecking));
  EXPECT_EQ("Rio is already checking for updates",
            ToastForCheckOutcome(UpdateCheckOutcome::kBusy));
  EXPECT_EQ("Relaunch Rio to install the update",
            ToastForCheckOutcome(UpdateCheckOutcome::kReady));
  EXPECT_EQ("Updates are off in this copy of Rio",
            ToastForCheckOutcome(UpdateCheckOutcome::kUnavailable));
}

}  // namespace
}  // namespace clu
