// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_service.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// CluSiteService on a temp store and a test clock: the blocking switch and every tweak rule (save keeps zaps and
// the switch, JS changes are reported for a reload, zaps append, undo, remove and clear, an empty tweak drops out
// of the list), persistence across a restart, and nothing written before the rows are read.
namespace clu {
namespace {

class Recorder : public CluSiteService::Observer {
 public:
  void OnSiteDataLoaded() override { ++loaded; }
  void OnBlockingChanged(const std::string& site) override {
    blocking.push_back(site);
  }
  void OnTweakChanged(const std::string& site) override {
    tweaks.push_back(site);
  }

  int loaded = 0;
  std::vector<std::string> blocking;
  std::vector<std::string> tweaks;
};

class CluSiteServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'800'000'000'000));
  }

  std::unique_ptr<CluSiteService> Open() {
    auto service = std::make_unique<CluSiteService>(
        dir_.GetPath().AppendASCII("Clu").AppendASCII("site.db"), &clock_);
    service->AddObserver(&recorder_);
    base::RunLoop run;
    service->WhenLoaded(run.QuitClosure());
    run.Run();
    return service;
  }

  void Flush(CluSiteService& service) {
    base::RunLoop run;
    service.FlushForTesting(run.QuitClosure());
    run.Run();
  }

  void Close(std::unique_ptr<CluSiteService> service) {
    Flush(*service);
    service->RemoveObserver(&recorder_);
    service.reset();
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  base::SimpleTestClock clock_;
  Recorder recorder_;
};

TEST_F(CluSiteServiceTest, BlockingIsOnUntilASiteTurnsItOffAndItPersists) {
  std::unique_ptr<CluSiteService> service = Open();
  EXPECT_EQ(recorder_.loaded, 1);
  EXPECT_TRUE(service->persistent());
  EXPECT_TRUE(service->IsBlockingEnabled("a.test"));
  EXPECT_TRUE(service->SetBlockingEnabled("a.test", false));
  EXPECT_FALSE(service->SetBlockingEnabled("a.test", false));
  EXPECT_FALSE(service->SetBlockingEnabled("www.a.test", false));
  EXPECT_FALSE(service->IsBlockingEnabled("a.test"));
  EXPECT_EQ(recorder_.blocking, (std::vector<std::string>{"a.test"}));
  Close(std::move(service));

  service = Open();
  EXPECT_FALSE(service->IsBlockingEnabled("a.test"));
  EXPECT_TRUE(service->SetBlockingEnabled("a.test", true));
  EXPECT_TRUE(service->exceptions().empty());
  Close(std::move(service));
  EXPECT_TRUE(Open()->IsBlockingEnabled("a.test"));
}

TEST_F(CluSiteServiceTest, SaveKeepsZapsAndTheSwitchAndReportsTheOldJs) {
  std::unique_ptr<CluSiteService> service = Open();
  EXPECT_EQ(service->AddZap("a.test", Zap{.selector = "#promo"}),
            AppendZapResult::kOk);
  const SiteTweak* t = service->Tweak("a.test");
  ASSERT_TRUE(t);
  EXPECT_TRUE(t->enabled);
  // The service's clock stamps a zap without a time, and the tweak.
  EXPECT_EQ(t->zaps[0].created_at, 1'800'000'000'000);
  EXPECT_EQ(service->SetTweakEnabled("a.test", false), false);

  clock_.Advance(base::Seconds(1));
  CluSiteService::SaveResult r =
      service->SaveTweak("a.test", "h1{color:red}", "x()", std::nullopt);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.old_js, "");
  EXPECT_FALSE(r.enabled);
  t = service->Tweak("a.test");
  EXPECT_EQ(t->zaps.size(), 1u);
  EXPECT_FALSE(t->enabled);
  EXPECT_EQ(t->updated_at, 1'800'000'001'000);

  r = service->SaveTweak("a.test", "", "y()", TweakControls());
  EXPECT_EQ(r.old_js, "x()");
  // Controls that change nothing store as none.
  EXPECT_EQ(service->Tweak("a.test")->controls, std::nullopt);
  // Turning a tweak with JS on or off says its pages reload; nothing changed says nothing.
  EXPECT_EQ(service->SetTweakEnabled("a.test", true), true);
  EXPECT_EQ(service->SetTweakEnabled("a.test", true), std::nullopt);
  EXPECT_EQ(service->SetTweakEnabled("none.test", false), std::nullopt);
}

TEST_F(CluSiteServiceTest, CodeOverTheCapIsRefusedAndNothingChanges) {
  std::unique_ptr<CluSiteService> service = Open();
  const std::string big(kMaxTweakCodeBytes + 1, 'a');
  EXPECT_FALSE(service->SaveTweak("a.test", big, "", std::nullopt).ok);
  EXPECT_FALSE(service->SaveTweak("a.test", "", big, std::nullopt).ok);
  EXPECT_FALSE(service->SaveTweak("not a site", "", "", std::nullopt).ok);
  EXPECT_EQ(service->Tweak("a.test"), nullptr);
  EXPECT_TRUE(recorder_.tweaks.empty());
}

TEST_F(CluSiteServiceTest, ZapsUndoRemoveAndClear) {
  std::unique_ptr<CluSiteService> service = Open();
  EXPECT_EQ(service->UndoZap("a.test"), std::nullopt);
  EXPECT_EQ(service->AddZap("a.test", Zap{.selector = "a{"}),
            AppendZapResult::kUnsafe);
  for (const char* s : {"#a", "#b", "#c"}) {
    ASSERT_EQ(service->AddZap("a.test", Zap{.selector = s, .label = s}),
              AppendZapResult::kOk);
  }
  EXPECT_EQ(service->UndoZap("a.test")->selector, "#c");
  EXPECT_FALSE(service->RemoveZap("a.test", 5));
  EXPECT_TRUE(service->RemoveZap("a.test", 0));
  EXPECT_EQ(service->Tweak("a.test")->zaps[0].selector, "#b");
  EXPECT_TRUE(service->ClearZaps("a.test"));
  EXPECT_FALSE(service->ClearZaps("a.test"));
  // A tweak with nothing left is not listed.
  EXPECT_TRUE(service->ListTweaks().empty());
  EXPECT_EQ(recorder_.tweaks.size(), 6u);
}

TEST_F(CluSiteServiceTest, TheListHoldsEverySiteWithSomethingAndDeleteDropsIt) {
  std::unique_ptr<CluSiteService> service = Open();
  service->SaveTweak("b.test", "p{}", "", std::nullopt);
  service->SaveTweak("a.test", "", "x()", std::nullopt);
  service->SaveTweak("c.test", " ", "", std::nullopt);
  std::vector<SiteTweak> list = service->ListTweaks();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].site, "a.test");
  EXPECT_EQ(list[1].site, "b.test");
  const std::optional<SiteTweak> old = service->DeleteTweak("a.test");
  ASSERT_TRUE(old);
  EXPECT_EQ(old->js, "x()");
  EXPECT_EQ(service->DeleteTweak("a.test"), std::nullopt);
  Close(std::move(service));
  service = Open();
  list = service->ListTweaks();
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].site, "b.test");
}

TEST_F(CluSiteServiceTest, NothingIsWrittenBeforeTheRowsAreRead) {
  CluSiteService service(dir_.GetPath().AppendASCII("site.db"), &clock_);
  EXPECT_FALSE(service.loaded());
  EXPECT_FALSE(service.SetBlockingEnabled("a.test", false));
  EXPECT_FALSE(service.SaveTweak("a.test", "p{}", "", std::nullopt).ok);
  EXPECT_EQ(service.AddZap("a.test", Zap{.selector = "#a"}),
            AppendZapResult::kUnsafe);
  base::RunLoop run;
  service.WhenLoaded(run.QuitClosure());
  run.Run();
  EXPECT_TRUE(service.IsBlockingEnabled("a.test"));
  EXPECT_EQ(service.Tweak("a.test"), nullptr);
}

TEST_F(CluSiteServiceTest, TheBlockerVersionIsStoredWithTheRows) {
  std::unique_ptr<CluSiteService> service = Open();
  EXPECT_EQ(service->blocker_version(), "");
  service->SetBlockerVersion("0123456789abcdef");
  Close(std::move(service));
  EXPECT_EQ(Open()->blocker_version(), "0123456789abcdef");
}

}  // namespace
}  // namespace clu
