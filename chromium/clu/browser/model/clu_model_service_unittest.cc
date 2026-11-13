// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_model_service.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/uuid.h"
#include "clu/browser/model/clu_store.h"
#include "sql/database.h"
#include "testing/gtest/include/gtest/gtest.h"

// The model service at its own layer (PLAN 12.1 "logic tests sit at the service layer"): F9 close and next-active,
// the closed stack and its archive fallback, Clear with Undo, the auto-archive sweep on an injected base::Clock, the
// archive restore that refuses a script URL, persistence across a reload. Windows are a fake WindowLink without
// pages; clu_browsertests runs the same paths with real pages.
namespace clu {
namespace {

constexpr base::TimeDelta kHour = base::Hours(1);

class FakeWindow : public CluModelService::WindowLink {
 public:
  explicit FakeWindow(std::string space_id) : space_id_(std::move(space_id)) {}

  // CluModelService::WindowLink:
  const std::string& LinkWindowId() override { return id_; }
  std::string LinkSpaceId() override { return space_id_; }
  std::string LinkActiveNodeId() override { return active_; }
  std::vector<std::string> LinkMru() override { return mru_; }
  content::WebContents* LinkContents(std::string_view node_id) override {
    return nullptr;
  }
  void LinkEachContents(
      base::FunctionRef<void(std::string_view, content::WebContents*)> each)
      override {}
  void LinkActivate(const std::optional<std::string>& node_id) override {
    active_ = node_id.value_or(std::string());
    activations_.push_back(active_);
  }
  void LinkDropPage(const std::string& node_id) override {
    dropped_.push_back(node_id);
  }
  void LinkRowsRemoved(const std::vector<std::string>& ids) override {}
  void LinkToast(const std::string& text,
                 const std::optional<std::string>& undo_command) override {
    toasts_.push_back(text);
    undo_ = undo_command;
  }
  void LinkDropToasts(const std::string& command) override {
    if (undo_ == command) {
      undo_.reset();
    }
  }

  std::string id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::string space_id_;
  std::string active_;
  std::vector<std::string> mru_;
  std::vector<std::string> activations_;
  std::vector<std::string> dropped_;
  std::vector<std::string> toasts_;
  std::optional<std::string> undo_;
};

class CluModelServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    path_ = dir_.GetPath().AppendASCII("Clu").AppendASCII("clu.db");
    clock_.SetNow(base::Time::Now());
    Start();
  }

  void TearDown() override { Stop(); }

  void Start() {
    model_ = std::make_unique<CluModelService>(
        path_, &clock_, task_environment_.GetMockTickClock());
    base::RunLoop loop;
    model_->WhenLoaded(loop.QuitClosure());
    loop.Run();
    window_ = std::make_unique<FakeWindow>(model_->default_space_id());
    model_->AddWindow(window_.get());
  }

  void Stop() {
    if (!model_) {
      return;
    }
    model_->RemoveWindow(window_.get());
    model_->Shutdown();
    model_.reset();
    task_environment_.RunUntilIdle();
  }

  void Restart() {
    Stop();
    Start();
  }

  const std::string& space() { return model_->default_space_id(); }

  std::string Today(const std::string& url) {
    NewTab tab;
    tab.url = url;
    auto id = model_->mutable_tree().InsertTab(tab, DropTarget::Today(space(), 0),
                                               model_->NowMs());
    EXPECT_TRUE(id.has_value());
    model_->Commit();
    return id.value_or("");
  }

  std::string Pinned(const std::string& url) {
    NewTab tab;
    tab.url = url;
    auto id = model_->mutable_tree().InsertTab(
        tab, DropTarget::Pinned(space(), "", 0), model_->NowMs());
    EXPECT_TRUE(id.has_value());
    model_->Commit();
    return id.value_or("");
  }

  ArchivePage List(const std::string& query = std::string()) {
    base::test::TestFuture<ArchivePage> page;
    model_->ListArchive(query, std::string(), 100, page.GetCallback());
    return page.Take();
  }

  bool Reopen() {
    base::test::TestFuture<bool> done;
    model_->ReopenClosed(window_.get(), done.GetCallback());
    return done.Get();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir dir_;
  base::FilePath path_;
  base::SimpleTestClock clock_;
  std::unique_ptr<CluModelService> model_;
  std::unique_ptr<FakeWindow> window_;
};

TEST_F(CluModelServiceTest, AFreshProfileHasItsHomeSpace) {
  EXPECT_TRUE(model_->loaded());
  EXPECT_TRUE(model_->persistent());
  ASSERT_EQ(model_->spaces().size(), 1u);
  EXPECT_EQ(model_->spaces()[0].name, kDefaultSpaceName);
  EXPECT_EQ(model_->profile_id().size(), 36u);
}

TEST_F(CluModelServiceTest, ClosingATodayRowArchivesItAndTheNextRowTakesItsPlace) {
  const std::string c = Today("https://c.test/");
  const std::string b = Today("https://b.test/");
  const std::string a = Today("https://a.test/");
  window_->active_ = b;
  model_->CloseRow(b, std::nullopt);
  EXPECT_FALSE(model_->tree().Get(b));
  // F9: the row that takes the closed row's place.
  EXPECT_EQ(window_->active_, c);
  EXPECT_EQ(window_->dropped_, std::vector<std::string>{b});
  const ArchivePage page = List();
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].url, "https://b.test/");
  EXPECT_EQ(page.rows[0].reason, ArchiveReason::kClose);
  EXPECT_EQ(model_->tree().Today(space()), (std::vector<std::string>{a, c}));
}

TEST_F(CluModelServiceTest, ClosingThePinnedRowResetsItAndCmdShiftTBringsTheLastUrlBack) {
  const std::string pinned = Pinned("https://p.test/home");
  model_->mutable_tree().Mutable(pinned)->url = "https://p.test/away";
  window_->active_ = pinned;
  CluModelService::PageCapture page;
  page.url = "https://p.test/away";
  page.title = "Away";
  model_->CloseRow(pinned, page);
  ASSERT_TRUE(model_->tree().Get(pinned));
  EXPECT_EQ(model_->tree().Get(pinned)->url, "https://p.test/home");
  // No other row: the empty state.
  EXPECT_EQ(window_->active_, "");
  EXPECT_TRUE(List().rows.empty());

  ASSERT_TRUE(Reopen());
  EXPECT_EQ(window_->active_, pinned);
  const auto reopen = model_->TakeReopenOverride(pinned);
  ASSERT_TRUE(reopen);
  EXPECT_EQ(reopen->first, "https://p.test/away");
}

TEST_F(CluModelServiceTest, ReopenRestoresTheNewestCloseAtTheTopOfToday) {
  const std::string gone = Today("https://gone.test/");
  const std::string kept = Today("https://kept.test/");
  model_->CloseRow(gone, std::nullopt);
  model_->CloseRow(kept, std::nullopt);
  ASSERT_TRUE(Reopen());
  ASSERT_EQ(model_->tree().Today(space()).size(), 1u);
  EXPECT_EQ(model_->tree().Get(model_->tree().Today(space())[0])->url,
            "https://kept.test/");
  // The archive row of the reopened tab went with it.
  task_environment_.RunUntilIdle();
  ASSERT_EQ(List().rows.size(), 1u);
}

// Two quick Cmd+Shift+T presses with the closed stack empty (after a relaunch) bring back the two newest closes, not
// the newest one twice.
TEST_F(CluModelServiceTest, TwoQuickReopensAfterARestartRestoreTwoDifferentRows) {
  const std::string older = Today("https://older.test/");
  const std::string newer = Today("https://newer.test/");
  model_->CloseRow(older, std::nullopt);
  clock_.Advance(base::Seconds(1));
  model_->CloseRow(newer, std::nullopt);
  Restart();
  base::test::TestFuture<bool> first;
  base::test::TestFuture<bool> second;
  model_->ReopenClosed(window_.get(), first.GetCallback());
  model_->ReopenClosed(window_.get(), second.GetCallback());
  EXPECT_TRUE(first.Get());
  EXPECT_TRUE(second.Get());
  std::vector<std::string> urls;
  for (const std::string& id : model_->tree().Today(space())) {
    urls.push_back(model_->tree().Get(id)->url);
  }
  EXPECT_EQ(urls, (std::vector<std::string>{"https://older.test/",
                                            "https://newer.test/"}));
  EXPECT_TRUE(List().rows.empty());
}

TEST_F(CluModelServiceTest, TheSweepArchivesIdleTodayRowsAtThirteenHours) {
  const std::string idle = Today("https://idle.test/");
  const std::string active = Today("https://active.test/");
  const std::string pinned = Pinned("https://pinned.test/");
  model_->mutable_tree().Mutable(idle)->last_active_at = model_->NowMs();
  model_->mutable_tree().Mutable(active)->last_active_at = model_->NowMs();
  model_->mutable_tree().Mutable(pinned)->last_active_at = model_->NowMs();
  window_->active_ = active;
  clock_.Advance(11 * kHour);
  EXPECT_TRUE(model_->Sweep(false).empty());
  clock_.Advance(2 * kHour);
  EXPECT_EQ(model_->Sweep(false), std::vector<std::string>{idle});
  EXPECT_FALSE(model_->tree().Get(idle));
  EXPECT_TRUE(model_->tree().Get(active));
  EXPECT_TRUE(model_->tree().Get(pinned));
  const ArchivePage page = List();
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].reason, ArchiveReason::kAuto);
  // Cmd+Shift+T never brings back what auto-archive took.
  EXPECT_FALSE(Reopen());
  // Released, the active row archives on the next sweep once idle.
  window_->active_.clear();
  EXPECT_EQ(model_->Sweep(false), std::vector<std::string>{active});
}

TEST_F(CluModelServiceTest, TheArchiveAfterSettingDecidesTheThreshold) {
  EXPECT_FALSE(model_->SetArchiveAfterHours(13));
  EXPECT_EQ(model_->archive_after_hours(), 12);
  ASSERT_TRUE(model_->SetArchiveAfterHours(168));
  const std::string tab = Today("https://a.test/");
  model_->mutable_tree().Mutable(tab)->last_active_at = model_->NowMs();
  clock_.Advance(13 * kHour);
  EXPECT_TRUE(model_->Sweep(false).empty());
  clock_.Advance(155 * kHour);
  EXPECT_EQ(model_->Sweep(false), std::vector<std::string>{tab});
  Restart();
  EXPECT_EQ(model_->archive_after_hours(), 168);
}

TEST_F(CluModelServiceTest, ClearOffersUndoThenArchivesAsOneBatch) {
  const std::string c = Today("https://c.test/");
  const std::string b = Today("https://b.test/");
  const std::string a = Today("https://a.test/");
  window_->active_ = b;
  EXPECT_EQ(model_->ClearToday(space(), window_.get()), 3);
  EXPECT_EQ(window_->undo_, "archive.undoClear");
  EXPECT_EQ(model_->pending_clear().size(), 3u);
  EXPECT_EQ(window_->active_, "");
  ASSERT_TRUE(model_->UndoClear());
  EXPECT_TRUE(model_->pending_clear().empty());
  // The Undo toast goes; "Restored 3 tabs" shows without a button.
  EXPECT_FALSE(window_->undo_);
  EXPECT_EQ(window_->toasts_.back(), "Restored 3 tabs");
  EXPECT_EQ(window_->active_, b);
  EXPECT_EQ(model_->tree().Today(space()), (std::vector<std::string>{a, b, c}));

  window_->active_.clear();
  model_->ClearToday(space(), window_.get());
  // The toast ends after 5 s: the batch archives with reason clear.
  task_environment_.FastForwardBy(CluModelService::kClearUndo);
  EXPECT_TRUE(model_->tree().Today(space()).empty());
  EXPECT_EQ(List().rows.size(), 3u);
  // Cmd+Shift+T brings all three back, in order.
  ASSERT_TRUE(Reopen());
  std::vector<std::string> urls;
  for (const std::string& id : model_->tree().Today(space())) {
    urls.push_back(model_->tree().Get(id)->url);
  }
  EXPECT_EQ(urls, (std::vector<std::string>{"https://a.test/", "https://b.test/",
                                            "https://c.test/"}));
}

TEST_F(CluModelServiceTest, ArchivingAnAnchoredRowUsesReasonDelete) {
  const std::string pinned = Pinned("https://p.test/");
  model_->ArchiveRows({pinned}, ArchiveReason::kClose);
  EXPECT_FALSE(model_->tree().Get(pinned));
  const ArchivePage page = List();
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].reason, ArchiveReason::kDelete);
}

TEST_F(CluModelServiceTest, ARestoredRowLandsAtTheTopOfTodayWithItsHistory) {
  const std::string old = Today("https://old.test/");
  const std::string tab = Today("https://a.test/");
  model_->mutable_tree().Mutable(tab)->nav_state =
      R"({"index":1,"entries":[{"url":"https://a.test/","title":"A"},)"
      R"({"url":"https://a.test/2","title":"A2"}]})";
  model_->ArchiveRows({tab}, ArchiveReason::kClose);
  const ArchivePage page = List();
  ASSERT_EQ(page.rows.size(), 1u);
  base::test::TestFuture<std::optional<std::string>> done;
  model_->RestoreArchive(page.rows[0].id, window_.get(), done.GetCallback());
  EXPECT_EQ(done.Get(), std::nullopt);
  const std::vector<std::string>& today = model_->tree().Today(space());
  ASSERT_EQ(today.size(), 2u);
  EXPECT_EQ(today[1], old);
  EXPECT_EQ(window_->active_, today[0]);
  EXPECT_TRUE(model_->tree().Get(today[0])->nav_state);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(List().rows.empty());
}

TEST_F(CluModelServiceTest, AScriptUrlRowIsNotRestoredAndStays) {
  Stop();
  {
    CluStore store(path_);
    store.Load(clock_.Now().InMillisecondsSinceUnixEpoch());
    ASSERT_TRUE(store.database_for_testing().Execute(
        "INSERT INTO archived_tabs (id, url, title, reason, archived_at) "
        "VALUES ('00000000-0000-4000-8000-00000000000a', 'javascript:alert(1)', "
        "'Bad', 'close', 5)"));
  }
  Start();
  base::test::TestFuture<std::optional<std::string>> done;
  model_->RestoreArchive("00000000-0000-4000-8000-00000000000a", window_.get(),
                         done.GetCallback());
  EXPECT_EQ(done.Get(), "not-restorable");
  EXPECT_EQ(window_->toasts_,
            std::vector<std::string>{"This page can't be restored"});
  EXPECT_TRUE(model_->tree().Today(space()).empty());
  EXPECT_EQ(List().rows.size(), 1u);
}

TEST_F(CluModelServiceTest, RowsAndTheActiveRowSurviveARestart) {
  const std::string pinned = Pinned("https://p.test/");
  const std::string today = Today("https://t.test/");
  model_->mutable_tree().MutableSpaceState(space()).active_tab_id = today;
  model_->Commit();
  Restart();
  EXPECT_TRUE(model_->tree().Get(pinned));
  EXPECT_TRUE(model_->tree().Get(today));
  EXPECT_EQ(model_->TakeLaunchRow(space()), today);
  // Only the first window of the run gets it.
  EXPECT_EQ(model_->TakeLaunchRow(space()), "");
}

// A write the store rolls back (a full disk) is not lost: its rows and the closed tab's archive row go out with the
// retried write.
TEST_F(CluModelServiceTest, AFailedWriteIsWrittenAgain) {
  model_->SetStoreFailWritesForTesting(true);
  const std::string kept = Today("https://kept.test/");
  const std::string closed = Today("https://closed.test/");
  model_->CloseRow(closed, std::nullopt);
  {
    base::RunLoop loop;
    model_->FlushForTesting(loop.QuitClosure());
    loop.Run();
  }
  model_->SetStoreFailWritesForTesting(false);
  task_environment_.FastForwardBy(base::Minutes(2));
  {
    base::RunLoop loop;
    model_->FlushForTesting(loop.QuitClosure());
    loop.Run();
  }
  ASSERT_EQ(List().rows.size(), 1u);
  Restart();
  EXPECT_TRUE(model_->tree().Get(kept));
  EXPECT_FALSE(model_->tree().Get(closed));
  const ArchivePage page = List();
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].url, "https://closed.test/");
}

TEST_F(CluModelServiceTest, TheLaunchSweepProtectsEachSpacesRestoredRow) {
  const std::string restored = Today("https://restored.test/");
  const std::string idle = Today("https://idle.test/");
  model_->mutable_tree().Mutable(restored)->last_active_at = model_->NowMs();
  model_->mutable_tree().Mutable(idle)->last_active_at = model_->NowMs();
  model_->mutable_tree().MutableSpaceState(space()).active_tab_id = restored;
  model_->Commit();
  Stop();
  clock_.Advance(13 * kHour);
  Start();
  EXPECT_TRUE(model_->tree().Get(restored));
  EXPECT_FALSE(model_->tree().Get(idle));
}

// M4: spaces (docs/chromium/m4.md) --------------------------------------------------------------------------------

constexpr char kWorkSpace[] = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa2";

SpaceRow SpaceOf(const std::string& id, const std::string& name) {
  SpaceRow row;
  row.id = id;
  row.name = name;
  return row;
}

TEST_F(CluModelServiceTest, SetSpacesKeepsRowsOfARemovedSpaceInTheFirstSpace) {
  const std::string home = space();
  model_->SetSpaces({model_->spaces()[0], SpaceOf(kWorkSpace, "Work")});
  NewTab tab;
  tab.url = "https://w.test/";
  const std::string row =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Pinned(kWorkSpace, "", 0), model_->NowMs())
          .value();
  const std::string folder =
      model_->mutable_tree()
          .CreateFolder(kWorkSpace, "", "F", std::nullopt, model_->NowMs())
          .value();
  tab.url = "https://f.test/";
  const std::string inner =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Pinned(kWorkSpace, folder, 0),
                     model_->NowMs())
          .value();
  model_->Commit();
  // Work leaves this profile without its rows being taken first (a repair): they move to the end of Home's Today.
  model_->SetSpaces({model_->spaces()[0]});
  EXPECT_EQ(model_->tree().Today(home), (std::vector<std::string>{row, inner}));
  EXPECT_EQ(model_->tree().Get(row)->section, Section::kToday);
  EXPECT_FALSE(model_->tree().Get(folder));
  // The mirror holds the spaces the service set, so a restart keeps them.
  Restart();
  ASSERT_EQ(model_->spaces().size(), 1u);
  EXPECT_EQ(model_->spaces()[0].id, home);
  EXPECT_EQ(model_->tree().Today(home).size(), 2u);
}

TEST_F(CluModelServiceTest, DeletingASpaceArchivesEveryRowOfIt) {
  model_->SetSpaces({model_->spaces()[0], SpaceOf(kWorkSpace, "Work")});
  NewTab tab;
  tab.url = "https://pinned.test/";
  ASSERT_TRUE(model_->mutable_tree()
                  .InsertTab(tab, DropTarget::Pinned(kWorkSpace, "", 0),
                             model_->NowMs())
                  .has_value());
  tab.url = "https://today.test/";
  ASSERT_TRUE(model_->mutable_tree()
                  .InsertTab(tab, DropTarget::Today(kWorkSpace, 0),
                             model_->NowMs())
                  .has_value());
  ASSERT_TRUE(model_->mutable_tree()
                  .CreateFolder(kWorkSpace, "", "F", std::nullopt,
                                model_->NowMs())
                  .has_value());
  model_->Commit();
  model_->DeleteSpaceRows(kWorkSpace);
  EXPECT_TRUE(model_->tree().Pinned(kWorkSpace).empty());
  EXPECT_TRUE(model_->tree().Today(kWorkSpace).empty());
  task_environment_.RunUntilIdle();
  // Nothing is lost silently: the pinned URL is in the archive too (reason delete).
  const ArchivePage page = List();
  ASSERT_EQ(page.rows.size(), 2u);
  for (const ArchiveRow& row : page.rows) {
    EXPECT_EQ(row.reason, ArchiveReason::kDelete);
  }
}

TEST_F(CluModelServiceTest, ASpaceMovesToAnotherProfilesModelWithoutHistory) {
  base::ScopedTempDir other_dir;
  ASSERT_TRUE(other_dir.CreateUniqueTempDir());
  auto other = std::make_unique<CluModelService>(
      other_dir.GetPath().AppendASCII("clu.db"), &clock_,
      task_environment_.GetMockTickClock());
  base::RunLoop loop;
  other->WhenLoaded(loop.QuitClosure());
  loop.Run();

  model_->SetSpaces({model_->spaces()[0], SpaceOf(kWorkSpace, "Work")});
  NewTab tab;
  tab.url = "https://today.test/";
  tab.nav_state = R"({"entries":[],"index":0})";
  const std::string today =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Today(kWorkSpace, 0), model_->NowMs())
          .value();
  tab.url = "https://pinned.test/";
  const std::string pinned =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Pinned(kWorkSpace, "", 0),
                     model_->NowMs())
          .value();
  model_->mutable_tree().MutableSpaceState(kWorkSpace).active_tab_id = today;
  model_->Commit();
  window_->active_ = today;

  CluModelService::MovedRows rows = model_->TakeSpaceRows(kWorkSpace);
  EXPECT_EQ(rows.nodes.size(), 2u);
  EXPECT_FALSE(model_->tree().Get(today));
  // The pages close in this profile's windows.
  EXPECT_TRUE(std::ranges::contains(window_->dropped_, today));
  model_->SetSpaces({model_->spaces()[0]});

  other->SetSpaces({other->spaces()[0], SpaceOf(kWorkSpace, "Work")});
  other->AddSpaceRows(kWorkSpace, std::move(rows));
  ASSERT_TRUE(other->tree().Get(today));
  EXPECT_EQ(other->tree().Get(today)->nav_state, std::nullopt);
  EXPECT_EQ(other->tree().Pinned(kWorkSpace), std::vector<std::string>{pinned});
  EXPECT_EQ(other->tree().SpaceState(kWorkSpace).active_tab_id, today);
  other->Shutdown();
  other.reset();
  task_environment_.RunUntilIdle();
}

// spaces.setProfile failed after the rows left: they come back to their own model as they were, history included.
TEST_F(CluModelServiceTest, AFailedProfileMoveReturnsTheRowsWithTheirHistory) {
  model_->SetSpaces({model_->spaces()[0], SpaceOf(kWorkSpace, "Work")});
  NewTab tab;
  tab.url = "https://today.test/";
  tab.nav_state = R"({"entries":[],"index":0})";
  const std::string today =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Today(kWorkSpace, 0), model_->NowMs())
          .value();
  model_->Commit();
  CluModelService::MovedRows rows = model_->TakeSpaceRows(kWorkSpace);
  model_->ReturnSpaceRows(kWorkSpace, std::move(rows));
  ASSERT_TRUE(model_->tree().Get(today));
  EXPECT_EQ(model_->tree().Get(today)->nav_state, tab.nav_state);
}

TEST_F(CluModelServiceTest, ASubtreeChecksTheTargetBeforeItMoves) {
  base::ScopedTempDir other_dir;
  ASSERT_TRUE(other_dir.CreateUniqueTempDir());
  auto other = std::make_unique<CluModelService>(
      other_dir.GetPath().AppendASCII("clu.db"), &clock_,
      task_environment_.GetMockTickClock());
  base::RunLoop loop;
  other->WhenLoaded(loop.QuitClosure());
  loop.Run();
  const std::string target_space = other->default_space_id();

  const std::string folder =
      model_->mutable_tree()
          .CreateFolder(space(), "", "Docs", std::nullopt, model_->NowMs())
          .value();
  NewTab tab;
  tab.url = "https://doc.test/";
  const std::string inner =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Pinned(space(), folder, 0),
                     model_->NowMs())
          .value();
  model_->Commit();

  const CluModelService::MovedRows preview = model_->CopySubtree(folder);
  ASSERT_EQ(preview.nodes.size(), 2u);
  // A folder never lands in Today, and an unknown space refuses.
  EXPECT_EQ(other->CanAddSubtree(preview, DropTarget::Today(target_space, 0))
                .error(),
            TreeError::kFolderNotAllowed);
  EXPECT_EQ(other->CanAddSubtree(preview, DropTarget::Pinned(kWorkSpace, "", 0))
                .error(),
            TreeError::kBadTarget);
  ASSERT_TRUE(
      other->CanAddSubtree(preview, DropTarget::Pinned(target_space, "", 0))
          .has_value());
  ASSERT_TRUE(other
                  ->AddSubtree(model_->TakeSubtree(folder),
                               DropTarget::Pinned(target_space, "", 0))
                  .has_value());
  EXPECT_FALSE(model_->tree().Get(folder));
  EXPECT_FALSE(model_->tree().Get(inner));
  EXPECT_EQ(other->tree().Children(folder), std::vector<std::string>{inner});
  EXPECT_EQ(other->tree().Get(inner)->pinned_url, "https://doc.test/");

  // A pinned row that wandered off its pin keeps the pin; a row with a URL no row may hold is refused before it moves.
  tab.url = "https://pin.test/";
  const std::string wandered =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Pinned(space(), "", 0), model_->NowMs())
          .value();
  model_->mutable_tree().Mutable(wandered)->url = "https://pin.test/away";
  model_->Commit();
  ASSERT_TRUE(other
                  ->AddSubtree(model_->TakeSubtree(wandered),
                               DropTarget::Pinned(target_space, "", 0))
                  .has_value());
  EXPECT_EQ(other->tree().Get(wandered)->pinned_url, "https://pin.test/");
  CluModelService::MovedRows junk = model_->CopySubtree(folder);
  junk.nodes.push_back(TabNode());
  junk.nodes.back().id = "junk-row";
  junk.nodes.back().url = "javascript:alert(1)";
  EXPECT_EQ(other->CanAddSubtree(junk, DropTarget::Today(target_space, 0))
                .error(),
            TreeError::kBadUrl);
  other->Shutdown();
  other.reset();
  task_environment_.RunUntilIdle();
}

// Split view (docs/chromium/m6.md): the model rules that hold for two panes.
TEST_F(CluModelServiceTest, ASplitJoinsTheSecondRowRightAfterTheFirst) {
  const std::string c = Today("https://a.test/c");
  const std::string b = Today("https://a.test/b");
  const std::string a = Today("https://a.test/a");
  // Today is a, b, c: c joins a's slot.
  base::expected<std::string, std::string> id =
      model_->CreateSplit({a, c}, SplitDir::kRow, c);
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(model_->tree().Today(space()), (std::vector<std::string>{a, c, b}));
  const SplitRow* split = model_->FindSplit(id.value());
  ASSERT_TRUE(split);
  EXPECT_EQ(split->tab_ids, (std::vector<std::string>{a, c}));
  EXPECT_EQ(split->focused_tab_id, c);
  EXPECT_EQ(model_->SplitOf(a), split);
  EXPECT_EQ(model_->tree().Get(c)->split_id, id.value());
}

TEST_F(CluModelServiceTest, ATodayTabJoiningAPinnedTabBecomesPinned) {
  const std::string pinned = Pinned("https://a.test/p");
  const std::string today = Today("https://a.test/t");
  ASSERT_TRUE(
      model_->CreateSplit({pinned, today}, SplitDir::kRow, pinned).has_value());
  EXPECT_EQ(model_->tree().Get(today)->section, Section::kPinned);
  EXPECT_EQ(model_->tree().Pinned(space()),
            (std::vector<std::string>{pinned, today}));
}

TEST_F(CluModelServiceTest, SplitsRefuseFavoritesUnknownTabsAndAFifthPane) {
  const std::string a = Today("https://a.test/a");
  const std::string b = Today("https://a.test/b");
  const std::string c = Today("https://a.test/c");
  const std::string d = Today("https://a.test/d");
  const std::string e = Today("https://a.test/e");
  NewTab fav;
  fav.url = "https://a.test/f";
  const std::string f =
      model_->mutable_tree()
          .InsertTab(fav, DropTarget::Favorite(0), model_->NowMs())
          .value();
  EXPECT_EQ(model_->CreateSplit({a, f}, SplitDir::kRow, a).error(), "favorite");
  EXPECT_EQ(model_->CreateSplit({a, "bogus"}, SplitDir::kRow, a).error(),
            "not-found");
  EXPECT_EQ(model_->CreateSplit({a, a}, SplitDir::kRow, a).error(),
            "not-found");
  EXPECT_EQ(model_->CreateSplit({a, b, c, d, e}, SplitDir::kRow, a).error(),
            "split-full");
  EXPECT_TRUE(model_->splits().empty());
}

TEST_F(CluModelServiceTest, ATabInAnotherSplitLeavesItFirst) {
  const std::string a = Today("https://a.test/a");
  const std::string b = Today("https://a.test/b");
  const std::string c = Today("https://a.test/c");
  const std::string first =
      model_->CreateSplit({a, b}, SplitDir::kRow, a).value_or("");
  const std::string second =
      model_->CreateSplit({c, b}, SplitDir::kCol, c).value_or("");
  EXPECT_FALSE(model_->FindSplit(first));
  EXPECT_FALSE(model_->tree().Get(a)->split_id);
  EXPECT_EQ(model_->SplitOf(b)->id, second);
}

TEST_F(CluModelServiceTest, ArchivingAPaneDissolvesTheSplit) {
  const std::string a = Today("https://a.test/a");
  const std::string b = Today("https://a.test/b");
  const std::string id =
      model_->CreateSplit({a, b}, SplitDir::kRow, a).value_or("");
  model_->ArchiveRows({b}, ArchiveReason::kClose);
  EXPECT_FALSE(model_->FindSplit(id));
  EXPECT_FALSE(model_->tree().Get(a)->split_id);
}

TEST_F(CluModelServiceTest, ASwapKeepsTheShareWithItsPaneAndPersists) {
  const std::string a = Today("https://a.test/a");
  const std::string b = Today("https://a.test/b");
  const std::string id =
      model_->CreateSplit({a, b}, SplitDir::kRow, a).value_or("");
  ASSERT_TRUE(model_->SetSplitRatio(id, 0.3));
  ASSERT_TRUE(model_->ArrangeSplit(id, {b, a}, SplitDir::kRow));
  EXPECT_NEAR(model_->FindSplit(id)->ratio, 0.7, 1e-9);
  EXPECT_FALSE(model_->ArrangeSplit(id, {a, "bogus"}, SplitDir::kRow));
  Restart();
  const SplitRow* split = model_->FindSplit(id);
  ASSERT_TRUE(split);
  EXPECT_EQ(split->tab_ids, (std::vector<std::string>{b, a}));
  EXPECT_NEAR(split->ratio, 0.7, 1e-9);
}

TEST_F(CluModelServiceTest, AResizeWritesHalfASecondAfterTheLastChange) {
  const std::string a = Today("https://a.test/a");
  const std::string b = Today("https://a.test/b");
  const std::string id =
      model_->CreateSplit({a, b}, SplitDir::kRow, a).value_or("");
  // Junk keeps the share; the margin keeps a pane from collapsing.
  ASSERT_TRUE(model_->SetSplitRatio(id, std::nan("")));
  EXPECT_EQ(model_->FindSplit(id)->ratio, 0.5);
  ASSERT_TRUE(model_->SetSplitRatio(id, 0.999));
  EXPECT_NEAR(model_->FindSplit(id)->ratio, 1 - kMinSplitRatio, 1e-9);
  ASSERT_TRUE(model_->SetSplitRatio(id, 0.4));
  task_environment_.FastForwardBy(CluModelService::kSplitResizeWrite);
  base::test::TestFuture<void> flushed;
  model_->FlushForTesting(flushed.GetCallback());
  ASSERT_TRUE(flushed.Wait());
  Restart();
  EXPECT_NEAR(model_->FindSplit(id)->ratio, 0.4, 1e-9);
}

TEST_F(CluModelServiceTest, AClosedSplitReopensAsASplit) {
  const std::string a = Today("https://a.test/a");
  const std::string b = Today("https://a.test/b");
  ASSERT_TRUE(model_->CreateSplit({a, b}, SplitDir::kRow, a).has_value());
  model_->ArchiveRows({a, b}, ArchiveReason::kClose);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(model_->splits().empty());
  // Cmd+Shift+T brings the batch back as the split it was (PLAN decision 55).
  EXPECT_TRUE(Reopen());
  ASSERT_EQ(model_->splits().size(), 1u);
  EXPECT_EQ(model_->splits().begin()->second.tab_ids.size(), 2u);
}

// Restore (docs/chromium/fixes.md) -----------------------------------------------------------------------------------

constexpr char kOtherSpace[] = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb3";

TEST_F(CluModelServiceTest, AClosedTodayRowIsArchivedAndReopenedUnderItsOwnId) {
  const std::string row = Today("https://closed.test/");
  model_->CloseRow(row, std::nullopt);
  ASSERT_FALSE(model_->tree().Get(row));
  // The archive row keeps the row id, which the page's restore files carry.
  const ArchivePage page = List();
  ASSERT_EQ(page.rows.size(), 1u);
  EXPECT_EQ(page.rows[0].id, row);
  // Cmd+Shift+T brings the row back under that id, so a copy Chromium restores later finds it.
  ASSERT_TRUE(Reopen());
  ASSERT_TRUE(model_->tree().Get(row));
  EXPECT_EQ(model_->tree().Today(space()).front(), row);
  EXPECT_TRUE(List().rows.empty());
}

TEST_F(CluModelServiceTest, ARestoredPageTakesItsArchivedRowBackToItsSpace) {
  model_->SetSpaces({model_->spaces()[0], SpaceOf(kOtherSpace, "Other")});
  NewTab tab;
  tab.url = "https://other.test/";
  const std::string row =
      model_->mutable_tree()
          .InsertTab(tab, DropTarget::Today(kOtherSpace, 0), model_->NowMs())
          .value();
  model_->Commit();
  model_->CloseRow(row, std::nullopt);
  ASSERT_EQ(List().rows.size(), 1u);

  // The page comes back in a window on the first space and becomes a Today row there, under its old id.
  tab.id = row;
  ASSERT_TRUE(model_->mutable_tree()
                  .InsertTab(tab, DropTarget::Today(space(), 0), model_->NowMs())
                  .has_value());
  model_->Commit();
  base::test::TestFuture<bool> reclaimed;
  model_->ReclaimArchived(row, reclaimed.GetCallback());
  ASSERT_TRUE(reclaimed.Get());
  // One row, in its own space; the archive row is gone, so Cmd+Shift+T has nothing to bring back twice.
  EXPECT_EQ(model_->tree().Get(row)->space_id, kOtherSpace);
  EXPECT_EQ(model_->tree().Today(kOtherSpace), std::vector<std::string>{row});
  EXPECT_TRUE(model_->tree().Today(space()).empty());
  EXPECT_TRUE(List().rows.empty());
  EXPECT_FALSE(Reopen());

  // A page with no archive row, or junk for an id, takes nothing.
  base::test::TestFuture<bool> none;
  model_->ReclaimArchived(base::Uuid::GenerateRandomV4().AsLowercaseString(),
                          none.GetCallback());
  EXPECT_FALSE(none.Get());
  base::test::TestFuture<bool> junk;
  model_->ReclaimArchived("not-a-row", junk.GetCallback());
  EXPECT_FALSE(junk.Get());
}

}  // namespace
}  // namespace clu
