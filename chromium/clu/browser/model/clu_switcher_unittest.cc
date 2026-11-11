// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_switcher.h"

#include <string>
#include <vector>

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// archive-t4: the switcher state machine on mock time (src/main/archive/switcher.test.ts): a tap commits without
// showing, a hold shows at 180 ms, wrap, reverse, Esc and blur cancel, a key without Ctrl commits.
namespace clu {
namespace {

class CluSwitcherTest : public testing::Test, public CluSwitcher::Delegate {
 protected:
  // CluSwitcher::Delegate:
  std::vector<std::string> RecentTabs() override { return recent_; }
  std::string CurrentTabId() override { return active_; }
  void ShowSwitcher(const SwitcherView& view) override {
    shown_ = true;
    view_ = view;
  }
  void HideSwitcher() override { shown_ = false; }
  void CommitSwitch(const std::string& tab_id) override {
    committed_.push_back(tab_id);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::vector<std::string> recent_ = {"a", "b", "c", "d", "e", "f", "g"};
  std::string active_ = "a";
  bool shown_ = false;
  SwitcherView view_;
  std::vector<std::string> committed_;
  CluSwitcher switcher_{*this, task_environment_.GetMockTickClock()};
};

TEST_F(CluSwitcherTest, ATapCommitsTheSecondMostRecentWithoutShowing) {
  switcher_.Start(1);
  task_environment_.FastForwardBy(base::Milliseconds(100));
  switcher_.ControlReleased();
  EXPECT_FALSE(shown_);
  EXPECT_EQ(committed_, std::vector<std::string>{"b"});
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(shown_);
}

TEST_F(CluSwitcherTest, AHoldShowsFiveCardsAndStepsWithWrap) {
  switcher_.Start(1);
  task_environment_.FastForwardBy(kSwitcherHold);
  ASSERT_TRUE(shown_);
  EXPECT_EQ(view_.tab_ids.size(), kSwitcherMax);
  EXPECT_EQ(view_.selected, 1);
  switcher_.Start(1);
  EXPECT_EQ(view_.selected, 2);
  switcher_.Start(-1);
  switcher_.Start(-1);
  switcher_.Start(-1);
  EXPECT_EQ(view_.selected, 4);  // wrapped past the start
  switcher_.ControlReleased();
  EXPECT_FALSE(shown_);
  EXPECT_EQ(committed_, std::vector<std::string>{"e"});
}

TEST_F(CluSwitcherTest, ReverseStartsAtTheLeastRecentCard) {
  switcher_.Start(-1);
  switcher_.ControlReleased();
  EXPECT_EQ(committed_, std::vector<std::string>{"e"});
}

TEST_F(CluSwitcherTest, EscAndBlurCancel) {
  switcher_.Start(1);
  task_environment_.FastForwardBy(kSwitcherHold);
  switcher_.Cancel();
  EXPECT_FALSE(shown_);
  switcher_.Start(1);
  switcher_.Blur();
  switcher_.ControlReleased();
  EXPECT_TRUE(committed_.empty());
  task_environment_.FastForwardBy(kSwitcherHold);
  EXPECT_FALSE(shown_);
}

TEST_F(CluSwitcherTest, OneRecentTabDoesNothing) {
  recent_ = {"a"};
  switcher_.Start(1);
  EXPECT_FALSE(switcher_.active());
}

TEST_F(CluSwitcherTest, AClosedTabLeavesTheCards) {
  switcher_.Start(1);
  task_environment_.FastForwardBy(kSwitcherHold);
  switcher_.TabRemoved("b");
  EXPECT_EQ(view_.tab_ids, (std::vector<std::string>{"a", "c", "d", "e"}));
  EXPECT_EQ(view_.tab_ids[view_.selected], "c");
}

TEST_F(CluSwitcherTest, ACardClickCommitsThatCard) {
  switcher_.Start(1);
  task_environment_.FastForwardBy(kSwitcherHold);
  switcher_.Respond(true, std::string("d"));
  EXPECT_EQ(committed_, std::vector<std::string>{"d"});
  EXPECT_FALSE(switcher_.active());
}

}  // namespace
}  // namespace clu
