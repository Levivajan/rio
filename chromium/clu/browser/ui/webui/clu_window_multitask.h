// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WINDOW_MULTITASK_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WINDOW_MULTITASK_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "clu/browser/multitask/clu_media_cards.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "components/split_tabs/split_tab_id.h"

class GURL;
struct SplitTabChange;
class TabStripModelChange;
struct TabStripSelectionChange;

namespace base {
class TickClock;
}  // namespace base

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace clu {

class CluPeek;
class CluWindowState;

// One window's M6 parts (docs/chromium/m6.md): split view over Chromium's
// two-pane split tabs, the drop preview of a sidebar drag, the sidebar media
// card and picture-in-picture state, the Peek, and the Mini window's own state
// when the window is one. CluWindowState owns it and calls in from its strip,
// snapshot and command paths.
//
// Split view: the profile's model holds the splits (CluModelService::splits());
// a window shows a split as a Chromium split of the two rows' pages
// (TabStripModel::AddToNewSplit), which MultiContentsView lays out with its own
// resize handle, swap, close button, drop target and focus outline. Changes Clu
// makes go model first, then strip; changes the user makes in Chromium's split
// UI come back through OnSplitTabChanged and go to the model.
class CluMediaActionWaiter;

class CluWindowMultitask {
 public:
  explicit CluWindowMultitask(CluWindowState& window);
  CluWindowMultitask(const CluWindowMultitask&) = delete;
  CluWindowMultitask& operator=(const CluWindowMultitask&) = delete;
  ~CluWindowMultitask();

  // ---- split view ---------------------------------------------------------------------------------------------------

  // split.create: a split of two tab rows of the window's space, shown with the
  // first focused. Three or four answer "split-full" with the toast for
  // now.
  base::expected<std::string, std::string> CreateSplit(
      const std::vector<std::string>& tab_ids,
      SplitDir dir,
      std::string_view focused_tab_id);
  // split.dropTab: `tab_id` lands on `edge` of the pane `target_tab_id` (05
  // "Drag a tab to a page edge"): a new split with a single tab, a new order or
  // direction inside the target's split, "split-full" for a third pane.
  base::expected<std::string, std::string> DropTab(
      const std::string& tab_id,
      const std::string& target_tab_id,
      SplitEdge edge);
  // split.swap: the two panes trade places.
  base::expected<void, std::string> Swap(const std::string& tab_id,
                                         const std::string& target_tab_id);
  // split.resize and split.equalize: the first pane's share, kept at 240 DIP
  // per pane while the room allows.
  base::expected<void, std::string> Resize(const std::string& split_id,
                                           base::span<const int32_t> path,
                                           base::span<const double> sizes);
  base::expected<void, std::string> Equalize(const std::string& split_id,
                                             base::span<const int32_t> path);
  // split.removePane: "separate" ends the split and shows the other pane;
  // "close" closes the pane's row (Today rows archive) and keeps the other on
  // screen.
  base::expected<void, std::string> RemovePane(const std::string& tab_id,
                                               bool close);
  // split.focusPane and Ctrl+Shift+1..4, ] and [.
  bool FocusPaneTab(const std::string& tab_id);
  bool FocusPaneIndex(int index);
  bool FocusPaneStep(int step);
  // split.dragState: the drop preview over the page area (windows[id].splitDrop
  // and the "split-drop" surface).
  enum class DragPhase { kStart, kHover, kEnd, kCancel };
  void DragState(DragPhase phase,
                 const std::string& tab_id,
                 const std::optional<std::string>& target_tab_id,
                 std::optional<SplitEdge> edge);
  // Add Split View's bar (split-add) and Option+Enter: `url` opens as a Today
  // row beside the page on screen.
  base::expected<void, std::string> OpenUrlInSplit(const GURL& url);
  // A tab row joins the page on screen in a split (a tab result picked in
  // split-add).
  base::expected<void, std::string> OpenRowInSplit(const std::string& row_id);
  // The window shows `row_id`: when the row is in a split, both pages come to
  // this window (loaded if they have none) and become a Chromium split in the
  // model's order, direction and share.
  void ShowSplitOf(const std::string& row_id);
  // The split the window shows, or null.
  const SplitRow* ShownSplit();
  // A page the strip does not have yet (a Peek, a Mini window's page,
  // window.open from a Peek) becomes a Today row of this window and shows,
  // without a reload: right after `after_row` when that is a Today row, else at
  // the top of the space's Today. Returns the row id; nullopt (and the page
  // closes) in a window without rows.
  std::optional<std::string> AdoptContentsAsRow(
      std::unique_ptr<content::WebContents> contents,
      const std::optional<std::string>& after_row);

  // ---- media card and picture-in-picture ----------------------------------------------------------------------------

  // media.toggle: pause the card's tab, or play what the card paused
  // (content::MediaSession, kUI).
  bool ToggleMedia(const std::string& tab_id);
  // media.previous and media.next: the tab's media session skips a track (a
  // no-op when the page offers none). False when the tab has no session.
  bool SkipMedia(const std::string& tab_id, bool next);
  // media.focus: bring the tab back.
  bool FocusMedia(const std::string& tab_id);
  // Media events of the window's pages (TabMediaObserver).
  void OnMediaChanged();
  CluMediaCards& media_cards() { return media_; }

  // ---- Peek ---------------------------------------------------------------------------------------------------------

  CluPeek& peek();
  bool has_peek();

  // ---- Mini window (docs/plan/05-multitask.md "Mini window") --------------------------------------------------------

  bool is_mini() const;
  // mini.moveToSpace: the page moves into `space_id` (default: the active space
  // of the last focused main window) as the top Today tab without a reload when
  // the space is on this profile; another profile asks first in the confirm
  // strip, and `confirm_profile_change` opens the URL fresh there. The Mini
  // window closes once the page left.
  base::expected<void, std::string> MoveToSpace(
      const std::optional<std::string>& space_id,
      bool confirm_profile_change);
  // mini.pickSpace: the native menu of spaces; a pick moves the page there.
  void PickSpace();
  void CancelMove();
  bool MiniBack();
  // The Mini window opened as the empty prompt: it grows from its
  // top-right corner to the page size once its page leaves about:blank.
  void SetMiniPrompt();
  // The Mini window closes: its page goes to the archive with reason mini
  // (decision 54), unless it moved.
  void OnMiniPageClosing(content::WebContents* contents);
  // Tests: the idle clock, and one idle check now.
  void SetMiniTickClockForTesting(const base::TickClock* clock);
  void CheckMiniIdleForTesting() { CheckMiniIdle(); }

  // ---- CluWindowState's hooks ---------------------------------------------------------------------------------------

  // A table command of M6, or a page command for an open Peek or a Mini window.
  // Nullopt: not one of them, the window goes on. `run` false only answers
  // whether it would run.
  std::optional<bool> Dispatch(std::string_view id, bool run);
  void OnTabStripModelChanged(const TabStripModelChange& change,
                              const TabStripSelectionChange& selection);
  void OnSplitTabChanged(const SplitTabChange& change);
  void OnShellChanged();
  void FillSnapshot(WindowSnapshot& s);
  void AddSurfaces(std::vector<OverlaySurfaceSnapshot>& surfaces);
  // The top overlay must show for the split drop preview, without taking focus
  // from the drag.
  bool WantsTopOverlay() const { return split_drop_.has_value(); }

 private:
  class TabMediaObserver;

  // The Chromium split holding both rows' pages in this window, if any.
  std::optional<split_tabs::SplitTabId> ChromiumSplitOf(
      const std::vector<std::string>& tab_ids);
  // Makes the strip show `split` (both pages present).
  void ApplySplitToStrip(const SplitRow& split);
  // A split of 3 or 4 on screen: the shell hosts the panes beside the active
  // page. Anything else clears them.
  void UpdateExtraPanes(const SplitRow& split);
  void OnExtraPaneTabLeaving(tabs::TabInterface* tab);
  void ClearExtraPanes();
  // The strip's split of `row_id`'s page, ended without touching the model.
  void RemoveStripSplitOf(const std::string& row_id);
  // A split Chromium ended that Clu did not end: the model's split goes too
  // (posted, after the strip settled).
  void OnStripSplitRemoved(std::vector<std::string> row_ids);
  void OnStripSplitAdded(std::vector<std::string> row_ids,
                         SplitDir dir,
                         double ratio,
                         split_tabs::SplitTabId chromium);
  gfx::Rect PaneRectOf(const std::string& tab_id);
  int SplitRoom(const SplitRow& split);
  void Watch(content::WebContents* contents);
  void Unwatch(content::WebContents* contents);
  std::vector<std::string> ShownRows();
  void InitMini();
  void UpdateMiniTarget();
  void CheckMiniIdle();
  void NoteMiniUse();
  void GrowMiniPrompt();

  const raw_ref<CluWindowState> window_;
  std::unique_ptr<CluPeek> peek_;
  // Clu is changing the strip's splits itself: their notifications are not the
  // user's.
  bool applying_ = false;
  // WillDeactivate of the active page and WillDetach of every pane while the
  // shell hosts panes of a split of 3 or 4.
  std::vector<base::CallbackListSubscription> extra_pane_subscriptions_;
  std::optional<SplitDropSnapshot> split_drop_;
  std::string split_drop_instance_;

  CluMediaCards media_;
  std::map<content::WebContents*, std::unique_ptr<TabMediaObserver>>
      media_observers_;

  // Mini window.
  struct Mini {
    Mini();
    ~Mini();

    std::string target_space_id;
    bool confirming = false;
    bool moved = false;
    bool prompt = false;
    base::TimeTicks last_used;
    raw_ptr<const base::TickClock> tick_clock = nullptr;
    base::RepeatingTimer idle_timer;
  };
  std::unique_ptr<Mini> mini_;

  // The media card's previous or next press waiting for the page to enable that action (SkipMedia).
  std::unique_ptr<CluMediaActionWaiter> media_action_;
  int media_action_seq_ = 0;
  base::WeakPtrFactory<CluWindowMultitask> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WINDOW_MULTITASK_H_
