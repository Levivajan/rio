// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/updater/clu_update_service.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

// Stands in for Sparkle: records what the service asked for and lets the test send Sparkle's events.
class FakeDriver : public CluUpdateDriver {
 public:
  struct Log {
    int starts = 0;
    int checks = 0;
    int installs = 0;
    // Every SetAutomatic() value, in order.
    std::vector<bool> automatic;
    // Every SetDownloads() value, in order.
    std::vector<bool> downloads;
    raw_ptr<Client> client = nullptr;
  };

  FakeDriver(Log* log, bool starts) : log_(log), starts_(starts) {}

  bool Start(Client* client) override {
    ++log_->starts;
    log_->client = starts_ ? client : nullptr;
    return starts_;
  }
  void CheckNow() override { ++log_->checks; }
  bool InstallNow() override {
    ++log_->installs;
    return true;
  }
  void SetAutomatic(bool automatic) override {
    log_->automatic.push_back(automatic);
  }
  void SetDownloads(bool downloads) override {
    log_->downloads.push_back(downloads);
  }

 private:
  raw_ptr<Log> log_;
  const bool starts_;
};

class CluUpdateServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'800'000'000'000));
    service().SetClockForTesting(&clock_);
    subscription_ = service().AddObserver(
        base::BindRepeating([](int* n) { ++*n; }, &changes_));
  }

  void TearDown() override {
    subscription_ = {};
    service().ResetForTesting();
    service().SetAutomatic(true);
    service().SetClockForTesting(nullptr);
  }

  CluUpdateService& service() { return CluUpdateService::Get(); }

  void UseDriver(bool starts) {
    service().SetDriverForTesting(std::make_unique<FakeDriver>(&log_, starts));
  }

  CluUpdateService::ToastCallback Toasts() {
    return base::BindRepeating(
        [](std::vector<std::string>* toasts, const std::string& text) {
          toasts->push_back(text);
        },
        &toasts_);
  }

  base::test::TaskEnvironment task_environment_;
  base::SimpleTestClock clock_;
  FakeDriver::Log log_;
  std::vector<std::string> toasts_;
  int changes_ = 0;
  base::CallbackListSubscription subscription_;
};

// General, "Automatically update Rio" (general.autoUpdate): a value set before the updater starts reaches it at the
// start, later changes reach it at once, and a build that cannot update is never asked.
TEST_F(CluUpdateServiceTest, AutomaticUpdatesFollowTheSettingBeforeAndAfterStart) {
  EXPECT_TRUE(service().automatic());
  service().SetAutomatic(false);
  UseDriver(/*starts=*/false);
  EXPECT_TRUE(log_.automatic.empty());

  UseDriver(/*starts=*/true);
  EXPECT_EQ(std::vector<bool>({false}), log_.automatic);
  service().SetAutomatic(true);
  service().SetAutomatic(false);
  EXPECT_EQ(std::vector<bool>({false, true, false}), log_.automatic);
  EXPECT_FALSE(service().automatic());
  // A check the user asks for still runs with automatic updates off.
  EXPECT_EQ(UpdateCheckOutcome::kChecking, service().Check(Toasts()));
  EXPECT_EQ(1, log_.checks);
}

// With automatic updates off, "Check for Updates…" still downloads what it finds, so the cycle ends with Rio's
// Relaunch card instead of the updater's own alert; the next cycle follows the setting again. About starts no check
// then, since its check would download and install on quit.
TEST_F(CluUpdateServiceTest, AUserCheckWithAutomaticOffDownloadsForThatCycle) {
  service().SetAutomatic(false);
  UseDriver(/*starts=*/true);
  EXPECT_EQ(UpdateCheckOutcome::kChecking, service().Check(Toasts()));
  EXPECT_EQ(1, log_.checks);
  EXPECT_EQ(std::vector<bool>({true}), log_.downloads);

  log_.client->OnCheckStarted();
  log_.client->OnUpdateFound("158.0.8127.19");
  log_.client->OnUpdateReady("158.0.8127.19");
  log_.client->OnCycleFinished();
  EXPECT_EQ(std::vector<bool>({true, false}), log_.downloads);
  EXPECT_EQ(UpdateStatus::kReady, service().Snapshot().status);
  EXPECT_NE(std::ranges::find(toasts_, "Downloading Rio 158.0.8127.19…"),
            toasts_.end());
  EXPECT_TRUE(service().Install());
  EXPECT_EQ(std::vector<bool>({false}), log_.automatic);

  // About with automatic updates off: no check, no download, and the state known so far.
  service().ResetForTesting();
  log_ = {};
  UseDriver(/*starts=*/true);
  std::vector<std::string> states;
  auto report = base::BindRepeating(
      [](std::vector<std::string>* out, const std::string& state,
         const std::string&) { out->push_back(state); },
      &states);
  ASSERT_TRUE(service().CheckFromAbout(report));
  EXPECT_EQ(0, log_.checks);
  EXPECT_TRUE(log_.downloads.empty());
  EXPECT_EQ(UpdateStatus::kIdle, service().Snapshot().status);
  EXPECT_EQ(states, (std::vector<std::string>{"disabled"}));

  // With automatic updates on, About checks and downloads through the setting itself.
  service().SetAutomatic(true);
  states.clear();
  ASSERT_TRUE(service().CheckFromAbout(report));
  EXPECT_EQ(1, log_.checks);
  EXPECT_TRUE(log_.downloads.empty());
  log_.client->OnUpdateNotFound();
  log_.client->OnCycleFinished();
  EXPECT_EQ(states.back(), "updated");
  EXPECT_EQ(UpdateCheckOutcome::kChecking, service().Check(Toasts()));
  log_.client->OnUpdateNotFound();
  log_.client->OnCycleFinished();
  EXPECT_TRUE(log_.downloads.empty());
}

TEST_F(CluUpdateServiceTest, ABuildThatCannotUpdateStaysUnavailable) {
  UseDriver(/*starts=*/false);
  EXPECT_EQ(1, log_.starts);
  EXPECT_FALSE(service().available());
  EXPECT_EQ(UpdateStatus::kUnavailable, service().Snapshot().status);

  EXPECT_EQ(UpdateCheckOutcome::kUnavailable, service().Check(Toasts()));
  EXPECT_EQ(0, log_.checks);
  EXPECT_EQ(std::vector<std::string>{"Updates are off in this copy of Rio"},
            toasts_);
  EXPECT_FALSE(service().Install());
  EXPECT_EQ(0, log_.installs);
}

TEST_F(CluUpdateServiceTest, AUserCheckRunsOnceAndToastsItsResult) {
  UseDriver(/*starts=*/true);
  ASSERT_TRUE(service().available());
  changes_ = 0;

  EXPECT_EQ(UpdateCheckOutcome::kChecking, service().Check(Toasts()));
  EXPECT_EQ(1, log_.checks);
  EXPECT_EQ(UpdateStatus::kChecking, service().Snapshot().status);
  EXPECT_GE(changes_, 1);
  // Sparkle reports the check it was asked for, and a second click joins it.
  log_.client->OnCheckStarted();
  EXPECT_EQ(UpdateCheckOutcome::kBusy, service().Check(Toasts()));
  EXPECT_EQ(1, log_.checks);

  clock_.Advance(base::Seconds(2));
  log_.client->OnUpdateNotFound();
  log_.client->OnCycleFinished();
  EXPECT_EQ((std::vector<std::string>{"Rio is already checking for updates",
                                      "Rio is up to date"}),
            toasts_);
  EXPECT_EQ(UpdateStatus::kUpToDate, service().Snapshot().status);
  EXPECT_EQ(1'800'000'002'000.0, service().Snapshot().checked_at);
}

TEST_F(CluUpdateServiceTest, AScheduledCheckFindsAnUpdateQuietlyAndRelaunchInstallsIt) {
  UseDriver(/*starts=*/true);
  EXPECT_FALSE(service().Install());
  EXPECT_EQ(0, log_.installs);

  log_.client->OnCheckStarted();
  log_.client->OnUpdateFound("158.0.8127.19");
  EXPECT_EQ(UpdateStatus::kDownloading, service().Snapshot().status);
  log_.client->OnUpdateReady("158.0.8127.19");
  log_.client->OnCycleFinished();
  EXPECT_TRUE(toasts_.empty());
  EXPECT_EQ(UpdateStatus::kReady, service().Snapshot().status);
  EXPECT_EQ("158.0.8127.19", service().Snapshot().version);

  // A check while one is ready starts nothing and says why.
  EXPECT_EQ(UpdateCheckOutcome::kReady, service().Check(Toasts()));
  EXPECT_EQ(0, log_.checks);
  EXPECT_EQ(std::vector<std::string>{
                "Relaunch Rio to install the update"},
            toasts_);

  EXPECT_TRUE(service().Install());
  EXPECT_EQ(1, log_.installs);
}

// Closing the update card publishes app.update.dismissed and leaves the update to install.
TEST_F(CluUpdateServiceTest, DismissPublishesAndTheUpdateStillInstalls) {
  UseDriver(/*starts=*/true);
  EXPECT_FALSE(service().Dismiss());
  log_.client->OnCheckStarted();
  log_.client->OnUpdateReady("158.0.8127.19");
  const int before = changes_;
  EXPECT_TRUE(service().Dismiss());
  EXPECT_EQ(before + 1, changes_);
  EXPECT_TRUE(service().Snapshot().dismissed);
  EXPECT_EQ("158.0.8127.19", service().dismissed_version());
  EXPECT_TRUE(service().Install());
  EXPECT_EQ(1, log_.installs);

  log_.client->OnUpdateReady("158.0.8127.20");
  EXPECT_FALSE(service().Snapshot().dismissed);
}

TEST_F(CluUpdateServiceTest, AStoredDismissalLoadsAndPublishesOnce) {
  UseDriver(/*starts=*/true);
  log_.client->OnCheckStarted();
  log_.client->OnUpdateReady("158.0.8127.19");
  const int before = changes_;
  service().LoadDismissedVersion("158.0.8127.19");
  EXPECT_TRUE(service().Snapshot().dismissed);
  EXPECT_EQ(before + 1, changes_);
  service().LoadDismissedVersion("158.0.8127.19");
  EXPECT_EQ(before + 1, changes_);
}

TEST_F(CluUpdateServiceTest, ErrorsReachOnlyTheUserWhoAsked) {
  UseDriver(/*starts=*/true);
  log_.client->OnCheckStarted();
  log_.client->OnUpdateError("feed unreachable");
  EXPECT_TRUE(toasts_.empty());
  EXPECT_EQ(UpdateStatus::kIdle, service().Snapshot().status);

  service().Check(Toasts());
  log_.client->OnUpdateError("feed unreachable");
  log_.client->OnUpdateError("the same failure, reported again");
  EXPECT_EQ(std::vector<std::string>{
                "Couldn't check for updates"},
            toasts_);
  EXPECT_EQ(UpdateStatus::kError, service().Snapshot().status);
}

// About Rio checks quietly and shows what the check found; a feed it cannot reach (not published yet) shows
// nothing rather than an error.
TEST_F(CluUpdateServiceTest, AboutChecksQuietlyAndShowsNoErrorForAnUnreachableFeed) {
  std::vector<std::string> states;
  auto report = base::BindRepeating(
      [](std::vector<std::string>* out, const std::string& state,
         const std::string& version) {
        out->push_back(version.empty() ? state : state + " " + version);
      },
      &states);
  UseDriver(/*starts=*/false);
  EXPECT_FALSE(service().CheckFromAbout(report));

  UseDriver(/*starts=*/true);
  ASSERT_TRUE(CheckForUpdatesFromAbout(report));
  EXPECT_EQ(log_.checks, 1);
  log_.client->OnCheckStarted();
  log_.client->OnUpdateError("the feed answered 404");
  log_.client->OnCycleFinished();
  EXPECT_EQ(states, (std::vector<std::string>{"checking", "checking", "disabled"}));
  EXPECT_TRUE(toasts_.empty());

  // Settled: later events reach About no more. A new visit checks again and finds nothing new.
  states.clear();
  ASSERT_TRUE(service().CheckFromAbout(report));
  EXPECT_EQ(log_.checks, 2);
  log_.client->OnUpdateNotFound();
  log_.client->OnCycleFinished();
  EXPECT_EQ(states.back(), "updated");
  const size_t seen = states.size();
  log_.client->OnCheckStarted();
  EXPECT_EQ(states.size(), seen);

  // An update that is ready shows Relaunch, and a visit while it is ready starts no check.
  states.clear();
  log_.client->OnUpdateFound("158.0.8127.19");
  log_.client->OnUpdateReady("158.0.8127.19");
  ASSERT_TRUE(service().CheckFromAbout(report));
  EXPECT_EQ(log_.checks, 2);
  EXPECT_EQ(states, (std::vector<std::string>{"nearly-updated 158.0.8127.19"}));
  EXPECT_TRUE(toasts_.empty());
}

TEST_F(CluUpdateServiceTest, StartsOnceAndResetsForTheNextTest) {
  UseDriver(/*starts=*/true);
  service().EnsureStarted();
  EXPECT_EQ(1, log_.starts);
  service().ResetForTesting();
  EXPECT_FALSE(service().available());
}

}  // namespace
}  // namespace clu
