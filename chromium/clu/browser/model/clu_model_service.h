// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_MODEL_SERVICE_H_
#define CLU_BROWSER_MODEL_CLU_MODEL_SERVICE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "clu/browser/model/clu_archive_rules.h"
#include "clu/browser/model/clu_store.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "components/keyed_service/core/keyed_service.h"

namespace base {
class Clock;
class TickClock;
}  // namespace base

namespace content {
class WebContents;
}  // namespace content

class Profile;

// One profile's Clu model (docs/chromium/PLAN.md 1.3 "Model and storage"): the tab tree with its spaces, the archive
// and the closed stack, and the profile's Clu settings. It ports the domain logic of src/main/tabs and
// src/main/archive. Rows persist to <profile>/Clu/clu.db through CluStore on a background sequence; structural changes
// are written at once in one transaction, title and URL changes 1 s later. Off-the-record profiles have no model, so
// private tabs never reach tab_nodes or the archive.
//
// Live pages stay in each window's TabStripModel. A window registers itself as a WindowLink, which gives the model the
// pages it cannot reach on its own: the model decides what happens to a row, the window does it to the page.
namespace clu {

class CluModelService : public KeyedService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // The tree, the archive, a pending Clear or a setting changed: windows republish their state.
    virtual void OnModelChanged() = 0;
    // The profile is going away (KeyedService::Shutdown()): drop every pointer to the model.
    virtual void OnModelShuttingDown(CluModelService* model) {}
  };

  // A space's rows on their way to another profile's model (spaces.setProfile), or a subtree's (a cross-profile
  // move).
  struct MovedRows {
    MovedRows();
    MovedRows(const MovedRows&) = delete;
    MovedRows& operator=(const MovedRows&) = delete;
    MovedRows(MovedRows&&);
    MovedRows& operator=(MovedRows&&);
    ~MovedRows();

    std::vector<TabNode> nodes;
    SpaceTabState state;
  };

  // A window of this profile (CluWindowState).
  class WindowLink {
   public:
    virtual ~WindowLink() = default;
    // The window's state id (WindowState.id): async replies find their window again by it.
    virtual const std::string& LinkWindowId() = 0;
    // The space the window shows.
    virtual std::string LinkSpaceId() = 0;
    // The row the window shows, or empty.
    virtual std::string LinkActiveNodeId() = 0;
    // The window's recent tabs, most recent first.
    virtual std::vector<std::string> LinkMru() = 0;
    // The window's live page for `node_id`, or null.
    virtual content::WebContents* LinkContents(std::string_view node_id) = 0;
    // Calls `each` with every live page of the window and its row id, in the order LinkContents searches them.
    virtual void LinkEachContents(
        base::FunctionRef<void(std::string_view node_id,
                               content::WebContents* contents)> each) = 0;
    // Shows `node_id` (loading it when it has no page), or the empty state for nullopt.
    virtual void LinkActivate(const std::optional<std::string>& node_id) = 0;
    // Closes the row's page, if this window has it, without the close rules: the row already changed.
    virtual void LinkDropPage(const std::string& node_id) = 0;
    // Rows left the tree: they leave the window's recent list and its switcher.
    virtual void LinkRowsRemoved(const std::vector<std::string>& ids) = 0;
    // A toast in this window ("Favorites are full", Clear's Undo).
    virtual void LinkToast(const std::string& text,
                           const std::optional<std::string>& undo_command) = 0;
    // Drops this window's toasts whose button runs `command`: an Undo that no longer applies.
    virtual void LinkDropToasts(const std::string& command) = 0;
  };

  // A closed page's last URL, title and history, read before the page went.
  struct PageCapture {
    PageCapture();
    PageCapture(const PageCapture&);
    PageCapture& operator=(const PageCapture&);
    ~PageCapture();

    std::string url;
    std::string title;
    std::optional<std::string> nav_state;
  };
  // Reads `contents` for its row.
  static PageCapture Capture(content::WebContents* contents);

  // Clear with Undo (src/main/archive/clear.ts): the Today tabs of a space leave the sidebar at once and stay alive off
  // the list for 5 s; Undo puts them back, the end of the toast archives them as one batch.
  static constexpr base::TimeDelta kClearUndo = base::Seconds(5);
  static constexpr base::TimeDelta kWriteDebounce = base::Seconds(1);

  // `store_path` empty keeps the rows in memory.
  CluModelService(base::FilePath store_path,
                  const base::Clock* clock,
                  const base::TickClock* tick_clock);
  CluModelService(const CluModelService&) = delete;
  CluModelService& operator=(const CluModelService&) = delete;
  ~CluModelService() override;

  // The model of `profile`, or null for an off-the-record or guest profile (defined with the factory).
  static CluModelService* Get(Profile* profile);

  // KeyedService:
  void Shutdown() override;

  bool loaded() const { return loaded_; }
  // The row `space_id` showed when the app last quit, once per run: the first window shows it, and only its page
  // loads (PLAN M3).
  std::string TakeLaunchRow(const std::string& space_id);
  // Runs `callback` once the rows are read (at once when they are).
  void WhenLoaded(base::OnceClosure callback);

  const std::string& profile_id() const { return profile_id_; }
  // The profile's Clu id as CluSpacesService knows it (it wins over the one clu.db made).
  void SetProfileId(std::string profile_id);
  bool persistent() const { return persistent_; }
  // app.dataHealth.recovered: "backup" or "fresh" when clu.db was unreadable at launch.
  const std::optional<std::string>& recovered() const { return recovered_; }
  // This profile's spaces in switcher order. Before CluSpacesService sets them, the ones clu.db mirrors.
  const std::vector<SpaceRow>& spaces() const { return spaces_; }
  // CluSpacesService: the profile's spaces now. They are mirrored into clu.db. Tab rows of a space that is no longer
  // here move to the end of the first space's Today (docs/plan/03-spaces.md "Load-time repair"), folders of it go;
  // with no space at all the rows wait.
  void SetSpaces(std::vector<SpaceRow> spaces);
  const SpaceRow* FindSpace(std::string_view space_id) const;
  // The first space in sidebar order.
  const std::string& default_space_id() const;

  const CluTabTree& tree() const { return tree_; }
  // Structural and field changes go through the tree, then Commit() or CommitSoon().
  CluTabTree& mutable_tree() { return tree_; }
  // Writes what changed now (structural changes), and tells the windows.
  void Commit();
  // Writes what changed within 1 s (title, URL, favicon), and tells the windows now.
  void CommitSoon();

  int64_t NowMs() const;
  int archive_revision() const { return archive_revision_; }
  int archive_after_hours() const { return archive_after_hours_; }
  // tabs.archiveAfterHours: 12, 24, 168 or 720. False for anything else. Runs a sweep.
  bool SetArchiveAfterHours(double hours);
  // A favorite's Show Unread Badge: on unless its menu turned it off. False for a row that is not a favorite.
  bool UnreadBadgeShown(const std::string& tab_id) const;
  // Turns it on or off for a favorite and stores the ids that are off; false for anything else.
  bool SetUnreadBadgeShown(const std::string& tab_id, bool shown);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);
  void AddWindow(WindowLink* window);
  void RemoveWindow(WindowLink* window);

  // ---- tab rows and their pages ----------------------------------------------------------------------------------

  // The live page of `node_id` in any window, or null.
  content::WebContents* FindContents(std::string_view node_id);

  // While one is alive, FindContents answers from one pass over every window's pages instead of searching each
  // window's tab strip per call: a state snapshot looks up every row of the profile. Only for code that changes no
  // tab strip while it runs.
  class ContentsIndexScope {
   public:
    explicit ContentsIndexScope(CluModelService& model);
    ContentsIndexScope(const ContentsIndexScope&) = delete;
    ContentsIndexScope& operator=(const ContentsIndexScope&) = delete;
    ~ContentsIndexScope();

   private:
    const raw_ref<CluModelService> model_;
    std::map<std::string, raw_ptr<content::WebContents>, std::less<>> index_;
    raw_ptr<const std::map<std::string, raw_ptr<content::WebContents>,
                           std::less<>>>
        previous_;
  };
  // The window that shows `node_id` as its active row, other than `except`.
  WindowLink* WindowShowing(std::string_view node_id, WindowLink* except);

  // F9 close: a Today row archives with reason close and leaves; an anchored row keeps its place, resets to its pinned
  // URL and goes on the closed stack with its last URL and history. The page closes and every window that showed the
  // row activates the F9 next tab. `closed_page` is what a page that already closed left (the user closed it through
  // the strip, the page or an extension did); without it the row's live page, if any, is read and closed.
  void CloseRow(const std::string& node_id,
                std::optional<PageCapture> closed_page);
  // Archives rows and removes them: Today rows with `reason`, anchored rows with reason delete (F9.6). One closed-stack
  // entry per reason. Windows that showed one fall back.
  void ArchiveRows(const std::vector<std::string>& node_ids,
                   ArchiveReason reason);
  // A page that has no row (a Mini window's, decision 54) goes to the archive
  // of `space_id` with `reason`, on the closed stack, so the archive panel and
  // Cmd+Shift+T reach it.
  void ArchiveDetachedPage(const PageCapture& page,
                           const std::string& space_id,
                           ArchiveReason reason);
  // Next active after rows leave (F9), for one window: the row that takes the place of its active row, the one above,
  // the window's most recent tab in its space, else none.
  std::optional<std::string> NextActiveFor(WindowLink* window,
                                           const RemovedRow& removed,
                                           const std::set<std::string>& gone);
  // True when `node_id` is a tab row a window on `space_id` may show: in that space, or a favorite.
  bool IsInSpace(std::string_view node_id, std::string_view space_id) const;

  // ---- spaces --------------------------------------------------------------------------------------------------------

  // spaces.delete: every tab row of the space goes to the archive with reason delete (its pinned rows too, so nothing
  // is lost silently), its folders and its state go. Windows have already left the space.
  void DeleteSpaceRows(const std::string& space_id);
  // spaces.setProfile: the space's rows and state leave this model, their pages close in every window.
  MovedRows TakeSpaceRows(const std::string& space_id);
  // ...and join the other profile's model. Pages come back in this profile when shown, at the row's URL without
  // history (F8): back and forward history stay behind with the old profile.
  void AddSpaceRows(const std::string& space_id, MovedRows rows);
  // ...or go back into this model as they were, history included, when the move failed.
  void ReturnSpaceRows(const std::string& space_id, MovedRows rows);
  // A cross-profile move: the node (a tab, or a folder with everything under it) leaves this model, its pages close.
  MovedRows TakeSubtree(const std::string& node_id);
  // The same rows without taking them (a cross-profile move checks the target first).
  MovedRows CopySubtree(const std::string& node_id) const;
  // ...and lands at `target` in this one, without history. False (and nothing added) when the target refuses it: an
  // unknown space or folder, a folder too deep, a full favorites row.
  base::expected<void, TreeError> CanAddSubtree(const MovedRows& rows,
                                                const DropTarget& target) const;
  base::expected<void, TreeError> AddSubtree(MovedRows rows,
                                             const DropTarget& target);

  // ---- Clear, reopen and the archive ------------------------------------------------------------------------------

  // Clears `space_id`'s Today tabs into a pending batch with an Undo toast in `window`. Returns how many left.
  int ClearToday(const std::string& space_id, WindowLink* window);
  // Puts the pending batch back in place. False when nothing is pending.
  bool UndoClear();
  // Archives the pending batch now (the toast ended, a newer Clear, shutdown).
  void CommitClear();
  // Rows of a pending Clear: off the Today list until it commits or is undone.
  const std::set<std::string>& pending_clear() const { return pending_ids_; }

  // Cmd+Shift+T: pops closed-stack entries until one restores, then falls back to the newest reopenable archive row
  // or batch. Restored rows open at the top of Today in `window`, the last one active. `done(true)` when something
  // came back.
  void ReopenClosed(WindowLink* window, base::OnceCallback<void(bool)> done);
  // A page Chromium brought back (session restore, Recently Closed) is the Today row `row_id` now. When that row was
  // archived (a closed Today row's archive row keeps its row id), the archive row goes and the row moves to the top of
  // Today in the archive row's space while that space exists. `done(true)` when an archive row was taken.
  void ReclaimArchived(const std::string& row_id,
                       base::OnceCallback<void(bool)> done);
  // archive.restore: the row back at the top of Today in its space (or `window`'s space when that is gone), active,
  // with its history; the archive row goes. A URL the policy refuses keeps the row and answers "not-restorable".
  void RestoreArchive(const std::string& archive_id,
                      WindowLink* window,
                      base::OnceCallback<void(std::optional<std::string>)> done);
  void DeleteArchive(const std::string& archive_id,
                     base::OnceCallback<void(bool)> done);
  void ListArchive(const std::string& query,
                   const std::string& cursor,
                   int limit,
                   base::OnceCallback<void(ArchivePage)> done);
  // Clear browsing data with "Archived tabs": rows archived in [begin, end), epoch ms. `done` gets how
  // many went.
  void ClearArchiveBetween(int64_t begin,
                           int64_t end,
                           base::OnceCallback<void(int)> done);
  const ClosedStack& closed_stack() const { return closed_; }
  // The last URL and history an anchored row reopens with (Cmd+Shift+T after closing it), taken once by the window
  // that loads the row's page.
  std::optional<std::pair<std::string, std::optional<std::string>>>
  TakeReopenOverride(std::string_view node_id);

  // ---- import (M8) ---------------------------------------------------------------------------------------------

  // Imported rows (pinned trees, favorites) join the tree as they are, folders before their children, after the rows
  // already in each list, and are written in one transaction. Returns the ids added.
  std::vector<std::string> AddImportedRows(std::vector<TabNode> nodes);
  // The import map in clu.db: the Clu id each source key was imported as, for keys that have one.
  void GetImportMap(std::string source,
                    std::vector<std::string> keys,
                    base::OnceCallback<void(std::map<std::string, std::string>)>
                        done);
  void PutImportMap(std::string source,
                    std::vector<std::pair<std::string, std::string>> rows);

  // ---- auto-archive -----------------------------------------------------------------------------------------------

  // One sweep (F6): idle Today rows archive with reason auto, then the archive is pruned. `launch` also protects each
  // space's restored active tab. Returns the archived row ids.
  std::vector<std::string> Sweep(bool launch);

  // ---- split view (docs/chromium/m6.md) -----------------------------------------------------------------------------

  // A split holds two tab rows of one space in pane order
  // (clu/browser/multitask/clu_split_rules.h).
  const std::map<std::string, SplitRow, std::less<>>& splits() const {
    return splits_;
  }
  const SplitRow* FindSplit(std::string_view split_id) const;
  // The split that holds tab row `tab_id`, or null.
  const SplitRow* SplitOf(std::string_view tab_id) const;
  // A split of `tab_ids` in that pane order: two tab rows of one space, never
  // favorites. Each leaves the split it was in. The second row moves right
  // after the first in the first's list, so the sidebar shows the split where
  // the first row was (05 "Sidebar item"). Returns the split id, or the reason:
  // "not-found", "favorite", "other-space", "split-full".
  base::expected<std::string, std::string> CreateSplit(
      const std::vector<std::string>& tab_ids,
      SplitDir dir,
      std::string_view focused_tab_id);
  // The same panes in another order or direction (a swap, a drop on the other
  // pane's edge). False for an unknown split, or tabs it does not hold.
  bool ArrangeSplit(std::string_view split_id,
                    const std::vector<std::string>& tab_ids,
                    SplitDir dir);
  // The first pane's share. Published at once, written 500 ms after the last
  // change (05 "Dividers").
  bool SetSplitRatio(std::string_view split_id, double ratio);
  // Each pane's share in a split of 3 or 4, written as
  // SetSplitRatio writes. Shares that are no valid split of the panes are
  // refused.
  bool SetSplitSizes(std::string_view split_id,
                     const std::vector<double>& sizes);
  void SetSplitFocus(std::string_view split_id, std::string_view tab_id);
  // The split ends; its tabs stay where they are as normal tabs.
  void DissolveSplit(std::string_view split_id);
  static constexpr base::TimeDelta kSplitResizeWrite = base::Milliseconds(500);

  base::WeakPtr<CluModelService> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  // Tests: a clock to sweep against, and a callback once every write posted so far has landed.
  void SetClockForTesting(const base::Clock* clock);
  void FlushForTesting(base::OnceClosure done);
  // Tests: the store's writes fail as on a full disk.
  void SetStoreFailWritesForTesting(bool fail);

 private:
  struct PendingClear {
    PendingClear();
    PendingClear(const PendingClear&);
    PendingClear& operator=(const PendingClear&);
    ~PendingClear();

    std::string space_id;
    raw_ptr<WindowLink> window = nullptr;
    std::vector<std::string> ids;
    // Windows that showed a cleared tab, and which one.
    std::vector<std::pair<raw_ptr<WindowLink>, std::string>> active;
  };

  void OnLoaded(StoreLoad load);
  // Once per run, in the first window: clu.db was unreadable and came back from the backup or started fresh.
  void ShowRecoveryToast();
  // Pages of `ids` close in every window and the rows leave the windows' recent lists.
  void DropPages(const std::vector<std::string>& ids);
  void ArchiveRowsWith(const std::vector<std::string>& node_ids,
                       ArchiveReason reason,
                       const std::optional<PageCapture>& closed_page);
  void Write();
  // A write that failed rolled back: its changes, splits and archive rows go into the next write, which is retried.
  void OnWritten(TreeChanges changes,
                 std::set<std::string> splits,
                 std::vector<StoreWrite::ArchiveBatch> archive,
                 bool ok);
  void NotifyChanged();
  void BumpArchive();
  ArchiveSnapshot SnapshotOf(const TabNode& node,
                             const std::optional<PageCapture>& closed_page);
  // Rows left: windows drop their pages and activate the F9 next tab.
  void FallBack(const std::vector<RemovedRow>& removed);
  // Inserts a restored row at the top of Today and shows it in `window`.
  std::optional<std::string> InsertRestored(const ArchiveRow& row,
                                            WindowLink* window);
  void RestoreRows(std::vector<ArchiveRow> rows,
                   WindowLink* window,
                   base::OnceCallback<void(bool)> done);
  void ReopenNext(WindowLink* window, base::OnceCallback<void(bool)> done);
  void OnReopenRows(const std::string& window_id,
                    bool from_stack,
                    base::OnceCallback<void(bool)> done,
                    std::vector<ArchiveRow> rows);
  // `window` while it is still registered, else the first window, else null.
  WindowLink* LiveWindow(WindowLink* window);
  // The id of a registered window, else empty.
  std::string IdOf(WindowLink* window);
  // The registered window with `window_id`, else the first window, else null.
  WindowLink* WindowById(const std::string& window_id);
  void SweepTick();
  bool HasWindow(WindowLink* window) const;
  // Splits whose panes left the tree, left the split's space or became
  // favorites dissolve (clu_model_splits.cc).
  void ValidateSplits();
  // The splits that changed since the last write go into `write`.
  void TakeSplitWrites(StoreWrite& write);
  void TouchSplit(const std::string& split_id);

  raw_ptr<const base::Clock> clock_;
  base::SequenceBound<CluStore> store_;

  bool loaded_ = false;
  std::set<std::string> launch_rows_taken_;
  std::vector<base::OnceClosure> on_loaded_;
  std::string profile_id_;
  bool persistent_ = false;
  std::optional<std::string> recovered_;
  // The recovery toast waits for the first window.
  bool recovery_toast_pending_ = false;
  std::vector<SpaceRow> spaces_;
  CluTabTree tree_;
  int archive_after_hours_ = kDefaultArchiveAfterHours;
  std::set<std::string> unread_badge_off_;
  int archive_revision_ = 0;
  ClosedStack closed_;
  std::map<std::string, int64_t> sweep_first_seen_;

  std::map<std::string,
           std::pair<std::string, std::optional<std::string>>,
           std::less<>>
      reopen_overrides_;
  std::optional<PendingClear> pending_;
  std::set<std::string> pending_ids_;
  base::OneShotTimer clear_timer_;

  base::OneShotTimer write_timer_;
  // Archive rows of tabs that left the tree, written with the tree's next write (one transaction).
  std::vector<StoreWrite::ArchiveBatch> pending_archive_;
  // Writes failed in a row; the retry waits longer each time.
  int write_failures_ = 0;
  base::RepeatingTimer sweep_timer_;
  bool shut_down_ = false;

  std::map<std::string, SplitRow, std::less<>> splits_;
  std::set<std::string> dirty_splits_;
  base::OneShotTimer split_write_timer_;

  base::ObserverList<Observer> observers_;
  std::vector<raw_ptr<WindowLink>> windows_;
  // Set while a ContentsIndexScope is alive.
  raw_ptr<const std::map<std::string, raw_ptr<content::WebContents>,
                         std::less<>>>
      contents_index_ = nullptr;
  base::WeakPtrFactory<CluModelService> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_MODEL_SERVICE_H_
