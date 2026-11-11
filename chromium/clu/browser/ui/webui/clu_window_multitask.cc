// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_window_multitask.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/containers/flat_map.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/time/default_tick_clock.h"
#include "base/uuid.h"
#include "chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_page_state.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/multitask/clu_mini_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_mini_windows.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/media_player_id.h"
#include "content/public/browser/media_session.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/base_window.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace clu {

// Sends the media card's previous or next to the page once its media session has the action enabled. The page's
// setActionHandler() reaches the browser asynchronously, and the session routes an action to the page's client
// without checking it is bound yet (a DCHECK), so an action is sent only once the session lists it: by then the
// client is bound. A press the page never enables is dropped after a second, as Chromium's own controls never offer it.
class CluMediaActionWaiter : public media_session::mojom::MediaSessionObserver {
 public:
  CluMediaActionWaiter(content::MediaSession* session,
                       media_session::mojom::MediaSessionAction action,
                       base::OnceClosure done)
      : session_(session), action_(action), done_(std::move(done)) {
    session->AddObserver(receiver_.BindNewPipeAndPassRemote());
    receiver_.set_disconnect_handler(
        base::BindOnce(&CluMediaActionWaiter::Finish, base::Unretained(this)));
    timeout_.Start(FROM_HERE, base::Seconds(1),
                   base::BindOnce(&CluMediaActionWaiter::Finish,
                                  base::Unretained(this)));
  }

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr session_info) override {}
  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override {}
  void MediaSessionActionsChanged(
      const std::vector<media_session::mojom::MediaSessionAction>& actions)
      override {
    if (!done_ || !std::ranges::contains(actions, action_)) {
      return;
    }
    if (action_ == media_session::mojom::MediaSessionAction::kNextTrack) {
      session_->NextTrack();
    } else {
      session_->PreviousTrack();
    }
    Finish();
  }
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override {}
  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override {}

 private:
  // The owner frees this waiter later, never inside its own call.
  void Finish() {
    timeout_.Stop();
    if (done_) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                               std::move(done_));
    }
  }

  raw_ptr<content::MediaSession> session_;
  const media_session::mojom::MediaSessionAction action_;
  base::OnceClosure done_;
  base::OneShotTimer timeout_;
  mojo::Receiver<media_session::mojom::MediaSessionObserver> receiver_{this};
};


namespace {

constexpr base::TimeDelta kSplitToast = base::Seconds(3);

void Post(base::WeakPtr<CluWindowMultitask> self,
          base::OnceCallback<void(CluWindowMultitask*)> task) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<CluWindowMultitask> self,
                        base::OnceCallback<void(CluWindowMultitask*)> task) {
                       if (self) {
                         std::move(task).Run(self.get());
                       }
                     },
                     std::move(self), std::move(task)));
}

split_tabs::SplitTabLayout LayoutOf(SplitDir dir) {
  return dir == SplitDir::kRow ? split_tabs::SplitTabLayout::kSideBySide
                               : split_tabs::SplitTabLayout::kStacked;
}

SplitDir DirOf(split_tabs::SplitTabLayout layout) {
  return layout == split_tabs::SplitTabLayout::kStacked ? SplitDir::kCol
                                                        : SplitDir::kRow;
}

std::string_view EdgeName(SplitEdge edge) {
  switch (edge) {
    case SplitEdge::kLeft:
      return "left";
    case SplitEdge::kRight:
      return "right";
    case SplitEdge::kTop:
      return "top";
    case SplitEdge::kBottom:
      return "bottom";
  }
  return "right";
}

// Half of the target pane toward the edge (05 "The overlay draws a translucent
// half-pane preview").
gfx::Rect HalfToward(const gfx::Rect& pane, SplitEdge edge) {
  switch (edge) {
    case SplitEdge::kLeft:
      return gfx::Rect(pane.x(), pane.y(), pane.width() / 2, pane.height());
    case SplitEdge::kRight:
      return gfx::Rect(pane.x() + pane.width() - pane.width() / 2, pane.y(),
                       pane.width() / 2, pane.height());
    case SplitEdge::kTop:
      return gfx::Rect(pane.x(), pane.y(), pane.width(), pane.height() / 2);
    case SplitEdge::kBottom:
      return gfx::Rect(pane.x(), pane.y() + pane.height() - pane.height() / 2,
                       pane.width(), pane.height() / 2);
  }
  return pane;
}

std::vector<std::string> RowIdsOf(
    const std::vector<std::pair<tabs::TabInterface*, int>>& tabs) {
  std::vector<std::pair<int, std::string>> ordered;
  for (const auto& [tab, index] : tabs) {
    if (tab && tab->GetContents() && CluTabId::Has(tab->GetContents())) {
      ordered.emplace_back(index, CluTabId::Get(tab->GetContents()));
    }
  }
  std::ranges::sort(ordered);
  std::vector<std::string> ids;
  for (auto& [index, id] : ordered) {
    ids.push_back(std::move(id));
  }
  return ids;
}

}  // namespace

// One page's media events for the window's media card.
class CluWindowMultitask::TabMediaObserver
    : public content::WebContentsObserver {
 public:
  TabMediaObserver(CluWindowMultitask& owner, content::WebContents* contents)
      : content::WebContentsObserver(contents), owner_(owner) {}

  // content::WebContentsObserver:
  void MediaStartedPlaying(const MediaPlayerInfo& info,
                           const content::MediaPlayerId& id) override {
    players_.insert(id);
    owner_->media_cards().Started(Row());
    owner_->OnMediaChanged();
  }
  void MediaStoppedPlaying(const MediaPlayerInfo& info,
                           const content::MediaPlayerId& id,
                           MediaStoppedReason reason) override {
    players_.erase(id);
    if (players_.empty()) {
      owner_->media_cards().Paused(Row());
    }
    owner_->OnMediaChanged();
  }
  void OnAudioStateChanged(bool audible) override {
    owner_->media_cards().Audible(Row(), audible);
    owner_->OnMediaChanged();
  }
  void MediaPictureInPictureChanged(bool is_picture_in_picture) override {
    owner_->OnMediaChanged();
  }
  void PrimaryPageChanged(content::Page& page) override { Reset(); }
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override {
    Reset();
  }

 private:
  std::string Row() { return CluTabId::Get(web_contents()); }
  void Reset() {
    players_.clear();
    owner_->media_cards().Reset(Row());
    owner_->OnMediaChanged();
  }

  const raw_ref<CluWindowMultitask> owner_;
  std::set<content::MediaPlayerId> players_;
};

CluWindowMultitask::Mini::Mini() = default;
CluWindowMultitask::Mini::~Mini() = default;

CluWindowMultitask::CluWindowMultitask(CluWindowState& window)
    : window_(window) {
  TabStripModel* strip = window_->browser()->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    Watch(strip->GetWebContentsAt(i));
  }
  if (window_->shell().is_mini()) {
    InitMini();
  }
}

CluWindowMultitask::~CluWindowMultitask() {
  media_observers_.clear();
  peek_.reset();
}

// Split view ----------------------------------------------------------------------------------------------------------

base::expected<std::string, std::string> CluWindowMultitask::CreateSplit(
    const std::vector<std::string>& tab_ids,
    SplitDir dir,
    std::string_view focused_tab_id) {
  CluModelService* m = window_->model();
  if (!m || is_mini()) {
    return base::unexpected("no-rows");
  }
  if (tab_ids.size() > kMaxSplitPanes) {
    window_->ShowToast(kSplitFullToast, std::nullopt, kSplitToast);
    return base::unexpected("split-full");
  }
  for (const std::string& id : tab_ids) {
    const TabNode* node = m->tree().Get(id);
    // Duplicate ids and ids from another space are refused (05 "Edge cases").
    if (node && node->section != Section::kFavorite &&
        node->space_id != window_->space_id()) {
      return base::unexpected("other-space");
    }
  }
  base::expected<std::string, std::string> id =
      m->CreateSplit(tab_ids, dir, focused_tab_id);
  if (!id.has_value()) {
    return id;
  }
  const SplitRow* split = m->FindSplit(id.value());
  ShowSplitOf(split ? split->focused_tab_id : tab_ids.front());
  return id;
}

base::expected<std::string, std::string> CluWindowMultitask::DropTab(
    const std::string& tab_id,
    const std::string& target_tab_id,
    SplitEdge edge) {
  CluModelService* m = window_->model();
  if (!m) {
    return base::unexpected("no-rows");
  }
  if (tab_id == target_tab_id) {
    return base::unexpected("same-tab");
  }
  const DropPlacement place = PlaceDrop(edge);
  const std::vector<std::string> order =
      place.dropped_first ? std::vector<std::string>{tab_id, target_tab_id}
                          : std::vector<std::string>{target_tab_id, tab_id};
  if (const SplitRow* into = m->SplitOf(target_tab_id)) {
    // A move inside one split: the same two panes in a new order or direction;
    // in a split of 3 or 4 the pane goes beside the target.
    if (std::ranges::contains(into->tab_ids, tab_id)) {
      const std::string id = into->id;
      if (into->tab_ids.size() > 2) {
        std::vector<std::string> moved;
        for (const std::string& pane : into->tab_ids) {
          if (pane != tab_id) {
            moved.push_back(pane);
          }
        }
        auto at = std::ranges::find(moved, target_tab_id);
        if (!place.dropped_first) {
          ++at;
        }
        moved.insert(at, tab_id);
        m->ArrangeSplit(id, moved, into->dir);
      } else {
        m->ArrangeSplit(id, order, place.dir);
      }
      ShowSplitOf(tab_id);
      return base::ok(id);
    }
    // A third or fourth pane joins beside the target, in the split's direction
    //; a full split refuses it with the toast.
    if (into->tab_ids.size() >= kMaxSplitPanes) {
      window_->ShowToast(kSplitFullToast, std::nullopt, kSplitToast);
      return base::unexpected("split-full");
    }
    std::vector<std::string> grown = into->tab_ids;
    auto at = std::ranges::find(grown, target_tab_id);
    if (!place.dropped_first) {
      ++at;
    }
    grown.insert(at, tab_id);
    return CreateSplit(grown, into->dir, tab_id);
  }
  // The dropped tab takes the focus.
  return CreateSplit(order, place.dir, tab_id);
}

base::expected<void, std::string> CluWindowMultitask::Swap(
    const std::string& tab_id,
    const std::string& target_tab_id) {
  CluModelService* m = window_->model();
  const SplitRow* split = m ? m->SplitOf(tab_id) : nullptr;
  if (!split || !std::ranges::contains(split->tab_ids, target_tab_id) ||
      tab_id == target_tab_id) {
    return base::unexpected("not-found");
  }
  // The two panes trade places (with two panes that reverses the split).
  std::vector<std::string> order = split->tab_ids;
  std::iter_swap(std::ranges::find(order, tab_id),
                 std::ranges::find(order, target_tab_id));
  const std::string id = split->id;
  m->ArrangeSplit(id, order, split->dir);
  ShowSplitOf(window_->ActiveRowId());
  return base::ok();
}

int CluWindowMultitask::SplitRoom(const SplitRow& split) {
  const gfx::Rect& page = window_->shell().layout().page_rect;
  return split.dir == SplitDir::kRow ? page.width() : page.height();
}

base::expected<void, std::string> CluWindowMultitask::Resize(
    const std::string& split_id,
    base::span<const int32_t> path,
    base::span<const double> sizes) {
  CluModelService* m = window_->model();
  const SplitRow* split = m ? m->FindSplit(split_id) : nullptr;
  if (!split) {
    return base::unexpected("not-found");
  }
  // One group at the root, one size per pane (the TS refinement: sizes sum to
  // 1).
  if (!path.empty() || sizes.size() != split->tab_ids.size() ||
      !SizesSumToOne(sizes)) {
    return base::unexpected("stale");
  }
  if (split->tab_ids.size() > 2) {
    // A split of 3 or 4: every pane keeps its minimum while the
    // room allows; a drag past it keeps the shares as they were.
    const std::vector<double> wanted(sizes.begin(), sizes.end());
    if (PaneShares(wanted, wanted.size()) != wanted) {
      return base::unexpected("stale");
    }
    const int min_pane =
        split->dir == SplitDir::kRow ? kMinPaneWidth : kMinPaneHeight;
    const int room = window_->shell().ExtraPaneRoom(
        window_->shell().layout().page_rect);
    if (room >= min_pane * static_cast<int>(wanted.size())) {
      for (double share : wanted) {
        if (share * room < min_pane) {
          return base::ok();
        }
      }
    }
    const std::string id = split->id;
    m->SetSplitSizes(id, wanted);
    if (const SplitRow* shown = ShownSplit(); shown && shown->id == id) {
      window_->shell().SetExtraShares(wanted);
    }
    return base::ok();
  }
  const int min_pane =
      split->dir == SplitDir::kRow ? kMinPaneWidth : kMinPaneHeight;
  const double ratio =
      ClampSplitRatio(sizes[0], SplitRoom(*split), min_pane, split->ratio);
  m->SetSplitRatio(split_id, ratio);
  if (std::optional<split_tabs::SplitTabId> chromium =
          ChromiumSplitOf(split->tab_ids)) {
    base::AutoReset<bool> applying(&applying_, true);
    window_->browser()->GetTabStripModel()->UpdateSplitRatio(*chromium, ratio);
  }
  return base::ok();
}

base::expected<void, std::string> CluWindowMultitask::Equalize(
    const std::string& split_id,
    base::span<const int32_t> path) {
  CluModelService* m = window_->model();
  const SplitRow* split = m ? m->FindSplit(split_id) : nullptr;
  if (split && split->tab_ids.size() > 2) {
    const std::vector<double> even =
        PaneShares(base::span<const double>(), split->tab_ids.size());
    return Resize(split_id, path, even);
  }
  const double even[] = {0.5, 0.5};
  return Resize(split_id, path, even);
}

base::expected<void, std::string> CluWindowMultitask::RemovePane(
    const std::string& tab_id,
    bool close) {
  CluModelService* m = window_->model();
  const SplitRow* split = m ? m->SplitOf(tab_id) : nullptr;
  if (!split) {
    return base::unexpected("not-found");
  }
  std::string other;
  for (const std::string& id : split->tab_ids) {
    if (id != tab_id) {
      other = id;
    }
  }
  const bool shown = window_->ActiveRowId() == tab_id ||
                     std::ranges::contains(ShownRows(), tab_id);
  if (split->tab_ids.size() > 2) {
    // The other panes stay a split; the focus moves to the pane
    // after the removed one, or the one before it at the end.
    std::vector<std::string> rest;
    std::string focus = split->focused_tab_id;
    for (size_t i = 0; i < split->tab_ids.size(); ++i) {
      if (split->tab_ids[i] != tab_id) {
        rest.push_back(split->tab_ids[i]);
      } else if (focus == tab_id) {
        focus = i + 1 < split->tab_ids.size() ? split->tab_ids[i + 1]
                                              : split->tab_ids[i - 1];
      }
    }
    const SplitDir dir = split->dir;
    ClearExtraPanes();
    base::expected<std::string, std::string> kept =
        m->CreateSplit(rest, dir, focus);
    if (!kept.has_value()) {
      return base::unexpected(kept.error());
    }
    // CreateSplit dissolved the old split; the removed row is a normal tab.
    if (shown) {
      window_->ActivateRow(focus);
    }
    if (close) {
      window_->CloseRowCommand(tab_id);
    }
    return base::ok();
  }
  RemoveStripSplitOf(tab_id);
  m->DissolveSplit(split->id);
  // The rest of the split stays on screen.
  if (shown && !other.empty()) {
    window_->ActivateRow(other);
  }
  if (close) {
    window_->CloseRowCommand(tab_id);
  }
  return base::ok();
}

bool CluWindowMultitask::FocusPaneTab(const std::string& tab_id) {
  const SplitRow* split = ShownSplit();
  if (!split || !std::ranges::contains(split->tab_ids, tab_id)) {
    return false;
  }
  tabs::TabInterface* tab = window_->FindTab(tab_id);
  if (!tab) {
    return false;
  }
  window_->ActivateTab(tab);
  // A split of 3 or 4 hosts its other panes again right away.
  ShowSplitOf(tab_id);
  window_->FocusActivePage();
  return true;
}

bool CluWindowMultitask::FocusPaneIndex(int index) {
  const SplitRow* split = ShownSplit();
  if (!split) {
    return false;
  }
  const std::optional<std::string> tab = PaneAt(split->tab_ids, index);
  return tab && FocusPaneTab(*tab);
}

bool CluWindowMultitask::FocusPaneStep(int step) {
  const SplitRow* split = ShownSplit();
  if (!split) {
    return false;
  }
  const std::optional<std::string> tab =
      StepPane(split->tab_ids, window_->ActiveRowId(), step);
  return tab && FocusPaneTab(*tab);
}

void CluWindowMultitask::DragState(
    DragPhase phase,
    const std::string& tab_id,
    const std::optional<std::string>& target_tab_id,
    std::optional<SplitEdge> edge) {
  switch (phase) {
    case DragPhase::kStart:
      split_drop_ = SplitDropSnapshot();
      split_drop_->tab_id = tab_id;
      split_drop_instance_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
      break;
    case DragPhase::kHover: {
      if (!split_drop_ || split_drop_->tab_id != tab_id) {
        return;
      }
      split_drop_->target_tab_id = target_tab_id;
      split_drop_->edge.reset();
      split_drop_->zone.reset();
      // Only a pane on screen is a target.
      const gfx::Rect pane =
          target_tab_id ? PaneRectOf(*target_tab_id) : gfx::Rect();
      if (target_tab_id && !pane.IsEmpty()) {
        if (edge) {
          split_drop_->edge = std::string(EdgeName(*edge));
          split_drop_->zone = HalfToward(pane, *edge);
        } else {
          // A pane bar over another pane: a swap shows the whole pane.
          split_drop_->zone = pane;
        }
      }
      break;
    }
    case DragPhase::kEnd:
    case DragPhase::kCancel:
      if (!split_drop_ || split_drop_->tab_id != tab_id) {
        return;
      }
      split_drop_.reset();
      break;
  }
  window_->MarkDirty();
  window_->Flush();
  window_->UpdateTopOverlay();
}

base::expected<void, std::string> CluWindowMultitask::OpenUrlInSplit(
    const GURL& url) {
  const std::string shown = window_->ActiveRowId();
  if (!window_->model() || shown.empty()) {
    // Nothing on screen to split with: the page opens as a tab.
    base::expected<std::string, std::string> row =
        window_->CreateRow(url, std::nullopt, /*activate=*/true);
    if (!row.has_value()) {
      return base::unexpected(row.error());
    }
    return base::ok();
  }
  if (const SplitRow* on_screen = window_->model()->SplitOf(shown)) {
    // The page joins the split on screen as its next pane, up to 4.
    if (on_screen->tab_ids.size() >= kMaxSplitPanes) {
      window_->ShowToast(kSplitFullToast, std::nullopt, kSplitToast);
      return base::unexpected("split-full");
    }
    base::expected<std::string, std::string> row =
        window_->CreateRow(url, std::nullopt, /*activate=*/false);
    if (!row.has_value()) {
      return base::unexpected(row.error());
    }
    return OpenRowInSplit(row.value());
  }
  base::expected<std::string, std::string> row =
      window_->CreateRow(url, std::nullopt, /*activate=*/false);
  if (!row.has_value()) {
    return base::unexpected(row.error());
  }
  // A pane to the right of the focused pane, or below it when that pane is
  // narrower than two minimum panes (05 "Shortcuts").
  const SplitDir dir =
      window_->shell().layout().page_rect.width() < 2 * kMinPaneWidth
          ? SplitDir::kCol
          : SplitDir::kRow;
  base::expected<std::string, std::string> split =
      CreateSplit({shown, row.value()}, dir, row.value());
  if (!split.has_value()) {
    return base::unexpected(split.error());
  }
  return base::ok();
}

base::expected<void, std::string> CluWindowMultitask::OpenRowInSplit(
    const std::string& row_id) {
  const std::string shown = window_->ActiveRowId();
  if (shown.empty() || shown == row_id) {
    return window_->ActivateRow(row_id)
               ? base::expected<void, std::string>(base::ok())
               : base::unexpected("not-found");
  }
  // On a split already on screen the row joins it as the next pane, up to 4
  // panes.
  std::vector<std::string> panes = {shown, row_id};
  SplitDir dir = SplitDir::kRow;
  if (const SplitRow* on_screen = ShownSplit()) {
    if (std::ranges::contains(on_screen->tab_ids, row_id)) {
      return FocusPaneTab(row_id) ? base::expected<void, std::string>(base::ok())
                                  : base::unexpected("not-found");
    }
    panes = on_screen->tab_ids;
    panes.push_back(row_id);
    dir = on_screen->dir;
  }
  base::expected<std::string, std::string> split =
      CreateSplit(panes, dir, row_id);
  if (!split.has_value()) {
    return base::unexpected(split.error());
  }
  return base::ok();
}

const SplitRow* CluWindowMultitask::ShownSplit() {
  CluModelService* m = window_->model();
  const std::string shown = window_->ActiveRowId();
  return m && !shown.empty() ? m->SplitOf(shown) : nullptr;
}

std::optional<split_tabs::SplitTabId> CluWindowMultitask::ChromiumSplitOf(
    const std::vector<std::string>& tab_ids) {
  std::optional<split_tabs::SplitTabId> id;
  for (const std::string& row : tab_ids) {
    tabs::TabInterface* tab = window_->FindTab(row);
    std::optional<split_tabs::SplitTabId> split =
        tab ? tab->GetSplit() : std::nullopt;
    if (!split || (id && *id != *split)) {
      return std::nullopt;
    }
    id = split;
  }
  return id;
}

void CluWindowMultitask::ShowSplitOf(const std::string& row_id) {
  CluModelService* m = window_->model();
  if (!m || applying_ || row_id.empty()) {
    return;
  }
  const SplitRow* found = m->SplitOf(row_id);
  if (!found) {
    // A row that is no pane: a Chromium split it still sits in (the user made
    // it natively, then the model let go) ends. Hosted panes of a split of 3
    // or 4 go.
    ClearExtraPanes();
    return;
  }
  const SplitRow split = *found;
  // Both pages in this window: loaded where they have none, taken from the
  // window that has them.
  for (const std::string& id : split.tab_ids) {
    if (window_->FindTab(id)) {
      continue;
    }
    const TabNode* node = m->tree().Get(id);
    if (!node) {
      return;
    }
    base::AutoReset<bool> applying(&applying_, true);
    if (content::WebContents* contents = m->FindContents(id)) {
      if (!window_->TakePageFrom(contents)) {
        return;
      }
    } else {
      window_->LoadRowPage(*node);
    }
  }
  ApplySplitToStrip(split);
  // The shown row is the focused pane.
  if (tabs::TabInterface* tab = window_->FindTab(row_id)) {
    if (window_->ActiveTab() != tab) {
      base::AutoReset<bool> applying(&applying_, true);
      window_->ActivateTab(tab);
    }
  }
  m->SetSplitFocus(split.id, row_id);
  UpdateExtraPanes(split);
  window_->UpdatePageShown();
  window_->MarkDirty();
}

void CluWindowMultitask::UpdateExtraPanes(const SplitRow& split) {
  ClearExtraPanes();
  tabs::TabInterface* active = window_->ActiveTab();
  if (split.tab_ids.size() < 3 || !active) {
    return;
  }
  std::vector<content::WebContents*> panes;
  std::optional<size_t> active_index;
  for (const std::string& id : split.tab_ids) {
    tabs::TabInterface* tab = window_->FindTab(id);
    if (!tab || !tab->GetContents()) {
      return;
    }
    if (tab == active) {
      active_index = panes.size();
    }
    panes.push_back(tab->GetContents());
  }
  if (!active_index) {
    return;
  }
  // Before Chromium shows another page in MultiContentsView, or a pane's page
  // leaves the window, every page leaves the hosted views: a page never sits in
  // two views at once.
  extra_pane_subscriptions_.push_back(active->RegisterWillDeactivate(
      base::BindRepeating(&CluWindowMultitask::OnExtraPaneTabLeaving,
                          weak_ptr_factory_.GetWeakPtr())));
  for (const std::string& id : split.tab_ids) {
    extra_pane_subscriptions_.push_back(
        window_->FindTab(id)->RegisterWillDetach(base::BindRepeating(
            [](base::WeakPtr<CluWindowMultitask> self, tabs::TabInterface* tab,
               tabs::TabInterface::DetachReason reason) {
              if (self) {
                self->OnExtraPaneTabLeaving(tab);
              }
            },
            weak_ptr_factory_.GetWeakPtr())));
  }
  window_->shell().SetExtraPanes(std::move(panes),
                                 PaneShares(split.sizes, split.tab_ids.size()),
                                 *active_index, split.dir == SplitDir::kCol);
}

void CluWindowMultitask::OnExtraPaneTabLeaving(tabs::TabInterface* tab) {
  ClearExtraPanes();
}

void CluWindowMultitask::ClearExtraPanes() {
  // The subscriptions may be running: they go after this task.
  if (!extra_pane_subscriptions_.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE,
        std::make_unique<std::vector<base::CallbackListSubscription>>(
            std::move(extra_pane_subscriptions_)));
    extra_pane_subscriptions_.clear();
  }
  window_->shell().ClearExtraPanes();
}

void CluWindowMultitask::ApplySplitToStrip(const SplitRow& split) {
  TabStripModel* strip = window_->browser()->GetTabStripModel();
  std::vector<tabs::TabInterface*> tabs;
  for (const std::string& id : split.tab_ids) {
    tabs::TabInterface* tab = window_->FindTab(id);
    if (!tab) {
      return;
    }
    tabs.push_back(tab);
  }
  base::AutoReset<bool> applying(&applying_, true);
  if (tabs.size() > 2) {
    // Chromium's split holds 2: a split of 3 or 4 shows through the shell's
    // hosted panes (UpdateExtraPanes), so no page of it stays in one.
    for (tabs::TabInterface* tab : tabs) {
      if (std::optional<split_tabs::SplitTabId> old = tab->GetSplit()) {
        strip->RemoveSplit(*old);
      }
    }
    return;
  }
  std::optional<split_tabs::SplitTabId> chromium =
      ChromiumSplitOf(split.tab_ids);
  const split_tabs::SplitTabVisualData visual(LayoutOf(split.dir), split.ratio);
  if (!chromium) {
    // Each page leaves the Chromium split it was in (AddToNewSplit CHECKs that
    // neither is split).
    for (tabs::TabInterface* tab : tabs) {
      if (std::optional<split_tabs::SplitTabId> old = tab->GetSplit()) {
        strip->RemoveSplit(*old);
      }
    }
    std::vector<int> indices;
    for (tabs::TabInterface* tab : tabs) {
      const int index = strip->GetIndexOfWebContents(tab->GetContents());
      if (index == TabStripModel::kNoTab) {
        return;
      }
      indices.push_back(index);
    }
    std::ranges::sort(indices);
    chromium = strip->AddToNewSplit(
        indices, visual, split_tabs::SplitTabCreatedSource::kTabContextMenu);
  }
  split_tabs::SplitTabData* data = strip->GetSplitData(*chromium);
  // The panes follow the model's order: MultiContentsView shows the strip's
  // order.
  const std::vector<tabs::TabInterface*> listed = data->ListTabs();
  if (!listed.empty() && listed.front() != tabs.front()) {
    strip->ReverseTabsInSplit(*chromium);
  }
  data = strip->GetSplitData(*chromium);
  if (data->visual_data()->split_layout() != visual.split_layout()) {
    strip->UpdateSplitLayout(*chromium, visual.split_layout());
  }
  if (data->visual_data()->split_ratio() != visual.split_ratio()) {
    strip->UpdateSplitRatio(*chromium, visual.split_ratio());
  }
}

void CluWindowMultitask::RemoveStripSplitOf(const std::string& row_id) {
  tabs::TabInterface* tab = window_->FindTab(row_id);
  std::optional<split_tabs::SplitTabId> chromium =
      tab ? tab->GetSplit() : std::nullopt;
  if (!chromium) {
    return;
  }
  base::AutoReset<bool> applying(&applying_, true);
  window_->browser()->GetTabStripModel()->RemoveSplit(*chromium);
}

void CluWindowMultitask::OnSplitTabChanged(const SplitTabChange& change) {
  window_->MarkDirty();
  if (applying_ || is_mini()) {
    return;
  }
  switch (change.type) {
    case SplitTabChange::Type::kAdded: {
      const SplitTabChange::AddedChange* added = change.GetAddedChange();
      if (added->reason() ==
          SplitTabChange::SplitTabAddReason::kInsertedFromAnotherTabstrip) {
        return;
      }
      // A split the user made in Chromium (a link dragged to the drop target, a
      // link's "Open in split view"): the model takes it, or it ends when it
      // cannot (a favorite, a placeholder).
      Post(weak_ptr_factory_.GetWeakPtr(),
           base::BindOnce(
               [](std::vector<std::string> ids, SplitDir dir, double ratio,
                  split_tabs::SplitTabId chromium, CluWindowMultitask* self) {
                 self->OnStripSplitAdded(std::move(ids), dir, ratio, chromium);
               },
               RowIdsOf(added->tabs()),
               DirOf(added->visual_data().split_layout()),
               added->visual_data().split_ratio(), change.split_id));
      return;
    }
    case SplitTabChange::Type::kVisualsChanged: {
      const SplitTabChange::VisualsChange* visuals = change.GetVisualsChange();
      // Every step of a divider drag is stored: the release repeats the last
      // step's ratio, which Chromium drops as no change, so waiting for it
      // would lose the share. SetSplitRatio writes 500 ms after the last step
      // (05 "Dividers"). A layout change is never intermediate.
      CluModelService* m = window_->model();
      TabStripModel* strip = window_->browser()->GetTabStripModel();
      if (!m || !strip->ContainsSplit(change.split_id)) {
        return;
      }
      std::vector<std::pair<tabs::TabInterface*, int>> tabs;
      for (tabs::TabInterface* tab :
           strip->GetSplitData(change.split_id)->ListTabs()) {
        tabs.emplace_back(tab,
                          strip->GetIndexOfWebContents(tab->GetContents()));
      }
      const std::vector<std::string> ids = RowIdsOf(tabs);
      const SplitRow* split = ids.empty() ? nullptr : m->SplitOf(ids.front());
      if (!split) {
        return;
      }
      const std::string id = split->id;
      if (visuals->reason() ==
          SplitTabChange::SplitVisualChangeReason::kRatioUpdated) {
        m->SetSplitRatio(id, visuals->new_visual_data().split_ratio());
      } else {
        m->ArrangeSplit(id, split->tab_ids,
                        DirOf(visuals->new_visual_data().split_layout()));
      }
      return;
    }
    case SplitTabChange::Type::kContentsChanged: {
      // Chromium's swap (a double click on the divider, the split menu's
      // Reverse).
      CluModelService* m = window_->model();
      const std::vector<std::string> ids =
          RowIdsOf(change.GetContentsChange()->new_tabs());
      const SplitRow* split =
          m && !ids.empty() ? m->SplitOf(ids.front()) : nullptr;
      if (split && ids.size() == split->tab_ids.size()) {
        const std::string id = split->id;
        m->ArrangeSplit(id, ids, split->dir);
        // ReverseTabsInSplit keeps Chromium's share with the new first pane,
        // which ArrangeSplit flipped.
        TabStripModel* strip = window_->browser()->GetTabStripModel();
        if (strip->ContainsSplit(change.split_id)) {
          m->SetSplitRatio(id, strip->GetSplitData(change.split_id)
                                   ->visual_data()
                                   ->split_ratio());
        }
      }
      return;
    }
    case SplitTabChange::Type::kRemoved: {
      const SplitTabChange::RemovedChange* removed = change.GetRemovedChange();
      // A page moving to another window, or the window closing, ends the
      // Chromium split only.
      if (removed->reason() !=
              SplitTabChange::SplitTabRemoveReason::kSplitTabRemoved ||
          window_->browser()->GetTabStripModel()->closing_all()) {
        return;
      }
      // The user unsplit in Chromium, or closed a pane: the split ends (05
      // "Ending a split").
      Post(weak_ptr_factory_.GetWeakPtr(),
           base::BindOnce(
               [](std::vector<std::string> ids, CluWindowMultitask* self) {
                 self->OnStripSplitRemoved(std::move(ids));
               },
               RowIdsOf(removed->tabs())));
      return;
    }
  }
}

void CluWindowMultitask::OnStripSplitAdded(std::vector<std::string> row_ids,
                                           SplitDir dir,
                                           double ratio,
                                           split_tabs::SplitTabId chromium) {
  CluModelService* m = window_->model();
  TabStripModel* strip = window_->browser()->GetTabStripModel();
  if (!m || row_ids.size() != 2) {
    // A pane that is no row (a placeholder): Chromium's split ends too.
    if (strip->ContainsSplit(chromium)) {
      base::AutoReset<bool> applying(&applying_, true);
      strip->RemoveSplit(chromium);
    }
    return;
  }
  const SplitRow* existing = m->SplitOf(row_ids[0]);
  if (existing && m->SplitOf(row_ids[1]) == existing) {
    return;
  }
  base::expected<std::string, std::string> id =
      m->CreateSplit(row_ids, dir, window_->ActiveRowId());
  if (!id.has_value()) {
    // Not a split Clu can keep (a favorite, a row of another space): Chromium's
    // split ends too.
    RemoveStripSplitOf(row_ids[0]);
    return;
  }
  m->SetSplitRatio(id.value(), ratio);
}

void CluWindowMultitask::OnStripSplitRemoved(std::vector<std::string> row_ids) {
  CluModelService* m = window_->model();
  if (!m || window_->browser()->GetTabStripModel()->closing_all()) {
    return;
  }
  for (const std::string& id : row_ids) {
    if (const SplitRow* split = m->SplitOf(id)) {
      // Still a Chromium split of the same pages: Clu itself moved or rebuilt
      // it.
      if (ChromiumSplitOf(split->tab_ids)) {
        return;
      }
      // A page of it went to another window, which shows the split now
      // (TakePageFrom detaches with kSplitTabRemoved, as a close does).
      for (const std::string& tab_id : split->tab_ids) {
        if (m->FindContents(tab_id) && !window_->FindTab(tab_id)) {
          return;
        }
      }
      m->DissolveSplit(split->id);
      return;
    }
  }
}

gfx::Rect CluWindowMultitask::PaneRectOf(const std::string& tab_id) {
  tabs::TabInterface* tab = window_->FindTab(tab_id);
  if (!tab) {
    return gfx::Rect();
  }
  for (const CluShell::PaneRect& pane : window_->shell().SplitPaneRects()) {
    if (pane.contents == tab->GetContents()) {
      return pane.rect;
    }
  }
  // A single page fills the page rect.
  return tab == window_->ActiveTab() ? window_->shell().shown_page_rect()
                                     : gfx::Rect();
}

std::optional<std::string> CluWindowMultitask::AdoptContentsAsRow(
    std::unique_ptr<content::WebContents> contents,
    const std::optional<std::string>& after_row) {
  CluModelService* m = window_->model();
  if (!contents || !m || is_mini()) {
    return std::nullopt;
  }
  DropTarget target = DropTarget::Today(window_->space_id(), 0);
  if (after_row) {
    const TabNode* after = m->tree().Get(*after_row);
    if (after && after->section == Section::kToday &&
        after->space_id == window_->space_id()) {
      target.index = after->position + 1;
    }
  }
  // The page keeps its Clu tab id as its row id, so the strip's insert finds
  // the row (AdoptPage()).
  NewTab init;
  init.id = CluTabId::Get(contents.get());
  const std::string url = PageUrl(contents.get());
  init.url = IsRestorableTabUrl(url) ? url : std::string("about:blank");
  init.title = base::UTF16ToUTF8(contents->GetTitle());
  init.muted = contents->IsAudioMuted();
  base::expected<std::string, TreeError> id =
      m->mutable_tree().InsertTab(init, target, m->NowMs());
  if (!id.has_value()) {
    return std::nullopt;
  }
  m->Commit();
  CluTabId::Set(contents.get(), id.value());
  TabStripModel* strip = window_->browser()->GetTabStripModel();
  // No reload: the same WebContents, its history and its page state
  // (docs/chromium/PLAN.md M6).
  strip->InsertWebContentsAt(strip->count(), std::move(contents),
                             AddTabTypes::ADD_ACTIVE);
  window_->UpdatePageShown();
  window_->MarkDirty();
  return id.value();
}

// Media card and picture-in-picture -----------------------------------------------------------------------------------

void CluWindowMultitask::Watch(content::WebContents* contents) {
  if (!contents || media_observers_.contains(contents) ||
      CluPlaceholder::Is(contents)) {
    return;
  }
  media_observers_.emplace(contents,
                           std::make_unique<TabMediaObserver>(*this, contents));
}

void CluWindowMultitask::Unwatch(content::WebContents* contents) {
  if (media_observers_.erase(contents) && CluTabId::Has(contents)) {
    media_.Reset(CluTabId::Get(contents));
  }
}

void CluWindowMultitask::OnMediaChanged() {
  window_->MarkDirty();
}

bool CluWindowMultitask::ToggleMedia(const std::string& tab_id) {
  tabs::TabInterface* tab = window_->FindTab(tab_id);
  if (!tab) {
    return false;
  }
  content::MediaSession* session =
      content::MediaSession::Get(tab->GetContents());
  switch (media_.Toggle(tab_id)) {
    case CluMediaCards::ToggleAction::kNone:
      return false;
    case CluMediaCards::ToggleAction::kPause:
      session->Suspend(content::MediaSession::SuspendType::kUI);
      break;
    case CluMediaCards::ToggleAction::kPlay:
      session->Resume(content::MediaSession::SuspendType::kUI);
      break;
  }
  window_->MarkDirty();
  return true;
}

bool CluWindowMultitask::SkipMedia(const std::string& tab_id, bool next) {
  tabs::TabInterface* tab = window_->FindTab(tab_id);
  if (!tab) {
    return false;
  }
  content::MediaSession* session =
      content::MediaSession::GetIfExists(tab->GetContents());
  if (!session) {
    return false;
  }
  // A newer press replaces one still waiting.
  media_action_ = std::make_unique<CluMediaActionWaiter>(
      session,
      next ? media_session::mojom::MediaSessionAction::kNextTrack
           : media_session::mojom::MediaSessionAction::kPreviousTrack,
      base::BindOnce(
          [](base::WeakPtr<CluWindowMultitask> self, int seq) {
            if (self && self->media_action_seq_ == seq) {
              self->media_action_.reset();
            }
          },
          weak_ptr_factory_.GetWeakPtr(), ++media_action_seq_));
  return true;
}

bool CluWindowMultitask::FocusMedia(const std::string& tab_id) {
  return window_->ActivateRow(tab_id);
}

std::vector<std::string> CluWindowMultitask::ShownRows() {
  std::vector<std::string> shown;
  const std::string active = window_->ActiveRowId();
  if (!active.empty()) {
    shown.push_back(active);
  }
  for (const CluShell::PaneRect& pane : window_->shell().SplitPaneRects()) {
    if (pane.contents && CluTabId::Has(pane.contents)) {
      const std::string& id = CluTabId::Get(pane.contents);
      if (!std::ranges::contains(shown, id)) {
        shown.push_back(id);
      }
    }
  }
  return shown;
}

// Peek ----------------------------------------------------------------------------------------------------------------

CluPeek& CluWindowMultitask::peek() {
  if (!peek_) {
    peek_ = std::make_unique<CluPeek>(*window_);
  }
  return *peek_;
}

bool CluWindowMultitask::has_peek() {
  return peek_ && peek_->is_open();
}

// Mini window ---------------------------------------------------------------------------------------------------------

bool CluWindowMultitask::is_mini() const {
  return window_->shell().is_mini();
}

void CluWindowMultitask::InitMini() {
  mini_ = std::make_unique<Mini>();
  mini_->tick_clock = base::DefaultTickClock::GetInstance();
  mini_->last_used = mini_->tick_clock->NowTicks();
  mini_->idle_timer.Start(
      FROM_HERE, kMiniIdleCheck,
      base::BindRepeating(&CluWindowMultitask::CheckMiniIdle,
                          base::Unretained(this)));
  UpdateMiniTarget();
}

void CluWindowMultitask::SetMiniTickClockForTesting(
    const base::TickClock* clock) {
  if (mini_) {
    mini_->tick_clock = clock;
    mini_->last_used = clock->NowTicks();
  }
}

void CluWindowMultitask::SetMiniPrompt() {
  if (mini_) {
    mini_->prompt = true;
  }
}

void CluWindowMultitask::GrowMiniPrompt() {
  ui::BaseWindow* window = window_->browser()->GetWindow();
  const gfx::Rect bounds = window->GetBounds();
  const gfx::Rect work_area =
      display::Screen::Get()->GetDisplayMatching(bounds).work_area();
  window->SetBounds(MiniGrownBounds(bounds, work_area));
}

void CluWindowMultitask::NoteMiniUse() {
  if (mini_) {
    mini_->last_used = mini_->tick_clock->NowTicks();
  }
}

void CluWindowMultitask::CheckMiniIdle() {
  if (!mini_ || window_->browser()->IsActive()) {
    NoteMiniUse();
    return;
  }
  if (MiniIdleExpired(mini_->last_used, mini_->tick_clock->NowTicks())) {
    // Closing archives the page with reason mini (OnMiniPageClosing).
    window_->browser()->GetWindow()->Close();
  }
}

void CluWindowMultitask::UpdateMiniTarget() {
  if (!mini_) {
    return;
  }
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces) {
    return;
  }
  // The active space of the most recently focused main window, else the first
  // space of this profile.
  const SpaceEntry* target = spaces->list().FindSpace(mini_->target_space_id);
  if (!target) {
    if (CluWindowState* main = mini_windows::LastMainWindow()) {
      target = spaces->list().FindSpace(main->space_id());
    }
  }
  if (!target) {
    const std::string profile_id = spaces->ProfileIdOf(window_->profile());
    for (const SpaceEntry& space : spaces->list().spaces()) {
      if (space.profile_id == profile_id) {
        target = &space;
        break;
      }
    }
  }
  if (target) {
    mini_->target_space_id = target->id;
  }
}

base::expected<void, std::string> CluWindowMultitask::MoveToSpace(
    const std::optional<std::string>& space_id,
    bool confirm_profile_change) {
  if (!mini_) {
    return base::unexpected("not-mini");
  }
  CluSpacesService* spaces = CluSpacesService::Get();
  const std::string target_id = space_id.value_or(mini_->target_space_id);
  const SpaceEntry* target =
      spaces ? spaces->list().FindSpace(target_id) : nullptr;
  tabs::TabInterface* tab = window_->ActiveTab();
  if (!target || !tab) {
    return base::unexpected("not-found");
  }
  mini_->target_space_id = target->id;
  const bool same_profile =
      target->profile_id == spaces->ProfileIdOf(window_->profile());
  if (!same_profile && !confirm_profile_change) {
    // The confirm strip: "<Space> uses a different profile. Logins and form
    // data won't carry over."
    mini_->confirming = true;
    window_->shell().SetMiniConfirming(true);
    window_->MarkDirty();
    return base::ok();
  }
  const GURL url = tab->GetContents()->GetLastCommittedURL();
  const std::string space = target->id;
  mini_->confirming = false;
  mini_->moved = true;
  if (same_profile) {
    // The live page moves: no reload, history kept (05 "Moving to a space").
    TabStripModel* strip = window_->browser()->GetTabStripModel();
    const int index = strip->GetIndexOfWebContents(tab->GetContents());
    std::unique_ptr<content::WebContents> contents =
        strip->DetachWebContentsAtForInsertion(index);
    mini_windows::AdoptIntoSpace(window_->profile(), space,
                                 std::move(contents));
  } else {
    // Another profile: the URL opens fresh there, after the confirmation.
    mini_windows::OpenUrlInSpace(space, url);
  }
  // The popup closes by itself once its strip is empty; a cross-profile move
  // closes it here.
  if (window_->browser()->GetTabStripModel()->count() > 0) {
    window_->browser()->GetWindow()->Close();
  }
  return base::ok();
}

void CluWindowMultitask::PickSpace() {
  if (!mini_) {
    return;
  }
  mini_windows::ShowSpacePicker(*window_);
}

void CluWindowMultitask::CancelMove() {
  if (!mini_ || !mini_->confirming) {
    return;
  }
  mini_->confirming = false;
  window_->shell().SetMiniConfirming(false);
  window_->MarkDirty();
}

bool CluWindowMultitask::MiniBack() {
  tabs::TabInterface* tab = window_->ActiveTab();
  if (!mini_ || !tab || !tab->GetContents()->GetController().CanGoBack()) {
    return false;
  }
  tab->GetContents()->GetController().GoBack();
  return true;
}

void CluWindowMultitask::OnMiniPageClosing(content::WebContents* contents) {
  if (!mini_ || mini_->moved || !contents) {
    return;
  }
  CluModelService* model = CluModelService::Get(window_->profile());
  if (!model || !model->loaded()) {
    return;
  }
  model->ArchiveDetachedPage(CluModelService::Capture(contents),
                             mini_->target_space_id, ArchiveReason::kMini);
}

// CluWindowState's hooks ----------------------------------------------------------------------------------------------

std::optional<bool> CluWindowMultitask::Dispatch(std::string_view id,
                                                 bool run) {
  // A Mini window runs its own variant; Cmd+T, Cmd+L and the rest run in the
  // most recent main window.
  if (mini_) {
    if (id == "tabs.close") {
      if (run) {
        window_->browser()->GetWindow()->Close();
      }
      return true;
    }
    if (id == "mini.openInSpace") {
      if (run) {
        std::ignore = MoveToSpace(std::nullopt, false);
      }
      return true;
    }
    if (id == "mini.pickSpace") {
      if (run) {
        PickSpace();
      }
      return true;
    }
    if (id == "mini.new") {
      if (run) {
        mini_windows::Open(window_->profile(), GURL(url::kAboutBlankURL),
                           /*command_bar=*/true);
      }
      return true;
    }
    if (id == "nav.back") {
      return run ? MiniBack() : true;
    }
    if (id.starts_with("nav.") || id.starts_with("edit.copyUrl") ||
        id.starts_with("zoom.") || id.starts_with("find.")) {
      // Page commands act on the Mini window's page.
      return std::nullopt;
    }
    CluWindowState* main = mini_windows::LastMainWindow();
    if (!main) {
      return false;
    }
    return run ? main->RunCommand(id) : main->CanRunCommand(id);
  }

  // An open Peek: Cmd+W closes it, Cmd+Shift+O opens it as a tab, page commands
  // act on it while it has focus.
  if (has_peek()) {
    if (id == "tabs.close") {
      if (run) {
        peek_->Close();
      }
      return true;
    }
    if (std::optional<bool> page = peek_->RunPageCommand(id, run)) {
      return *page;
    }
  }
  if (id == "peek.expand") {
    if (!has_peek()) {
      return false;
    }
    if (run) {
      peek_->Expand();
    }
    return true;
  }
  if (id == "mini.new") {
    if (window_->is_private()) {
      return false;
    }
    if (run) {
      mini_windows::Open(window_->profile(), GURL(url::kAboutBlankURL),
                         /*command_bar=*/true);
    }
    return true;
  }
  if (!id.starts_with("split.")) {
    return std::nullopt;
  }
  if (!window_->model()) {
    return false;
  }
  const std::string shown = window_->ActiveRowId();
  if (id == "split.add" || id == "split.addToSplit" ||
      id == "split.openInSplit") {
    if (shown.empty()) {
      return false;
    }
    if (run) {
      // A split of 4 on screen is full; else the bar picks the next pane.
      if (const SplitRow* on_screen = ShownSplit();
          on_screen && on_screen->tab_ids.size() >= kMaxSplitPanes) {
        window_->ShowToast(kSplitFullToast, std::nullopt, kSplitToast);
      } else {
        window_->OpenCommandBar(CommandBarMode::kSplitAdd);
      }
    }
    return true;
  }
  if (!ShownSplit()) {
    return false;
  }
  if (id == "split.separate") {
    if (run) {
      std::ignore = RemovePane(shown, /*close=*/false);
    }
    return true;
  }
  // View ▸ Expand Current Split: the focused pane takes the room the others'
  // minimum leaves; once it has it, the panes even out again.
  if (id == "split.expand") {
    const SplitRow& split = *ShownSplit();
    const size_t count = split.tab_ids.size();
    const auto it = std::ranges::find(split.tab_ids, shown);
    const int min_pane =
        split.dir == SplitDir::kRow ? kMinPaneWidth : kMinPaneHeight;
    const int room =
        count > 2 ? window_->shell().ExtraPaneRoom(
                        window_->shell().layout().page_rect)
                  : SplitRoom(split);
    const size_t index = static_cast<size_t>(it - split.tab_ids.begin());
    const std::vector<double> expanded =
        ExpandedShares(count, index, room, min_pane);
    if (expanded.empty()) {
      return false;
    }
    if (run) {
      const std::vector<double> now =
          count > 2 ? PaneShares(split.sizes, count)
                    : std::vector<double>{split.ratio, 1 - split.ratio};
      const std::string split_id = split.id;
      if (now[index] >= expanded[index] - 0.005) {
        std::ignore = Equalize(split_id, {});
      } else {
        std::ignore = Resize(split_id, {}, expanded);
      }
    }
    return true;
  }
  if (id == "split.next" || id == "split.prev") {
    return run ? FocusPaneStep(id == "split.next" ? 1 : -1) : true;
  }
  if (id.starts_with("split.focus") && id.size() == 12) {
    const int index = id.back() - '0';
    if (index < 1 || index > static_cast<int>(ShownSplit()->tab_ids.size())) {
      return false;
    }
    return run ? FocusPaneIndex(index) : true;
  }
  return false;
}

void CluWindowMultitask::OnTabStripModelChanged(
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted) {
    for (const auto& inserted : change.GetInsert()->contents) {
      Watch(inserted.contents);
    }
  } else if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      // The Mini window's page leaves: archived with reason mini unless it
      // moved into a space.
      if (mini_ && removed.remove_reason == TabRemovedReason::kDeleted) {
        OnMiniPageClosing(removed.contents);
      }
      Unwatch(removed.contents);
    }
  } else if (change.type() == TabStripModelChange::kReplaced) {
    Unwatch(change.GetReplace()->old_contents);
    Watch(change.GetReplace()->new_contents);
  }
  if (!selection.active_tab_changed() || applying_) {
    return;
  }
  // Switching tabs or spaces, or closing the source, closes the Peek (05 "Peek
  // rules").
  if (has_peek()) {
    content::WebContents* active = selection.new_contents;
    if (!active || !CluTabId::Has(active) ||
        CluTabId::Get(active) != peek_->source_row_id()) {
      peek_->Close();
    }
  }
  if (mini_) {
    NoteMiniUse();
    return;
  }
  // A pane activated through Chromium (a click in it, the PiP window's
  // back-to-tab) shows its split.
  if (selection.new_contents && CluTabId::Has(selection.new_contents)) {
    Post(
        weak_ptr_factory_.GetWeakPtr(),
        base::BindOnce([](std::string row,
                          CluWindowMultitask* self) { self->ShowSplitOf(row); },
                       CluTabId::Get(selection.new_contents)));
  }
}

void CluWindowMultitask::OnShellChanged() {
  if (has_peek()) {
    peek_->Relayout();
  }
}

void CluWindowMultitask::FillSnapshot(WindowSnapshot& s) {
  s.kind = mini_ ? "mini" : "main";
  if (CluModelService* m = window_->model()) {
    for (const auto& [id, split] : m->splits()) {
      s.splits.push_back(split);
    }
  }
  // The split on screen, at the panes' rects in the frame.
  if (const SplitRow* split = ShownSplit()) {
    s.active_split_id = split->id;
    SplitViewSnapshot view;
    view.split_id = split->id;
    if (window_->shell().has_extra_panes()) {
      view.bar_height = CluShell::kPaneBarHeight;
      view.dir = std::string(SplitDirName(split->dir));
      view.sizes = PaneShares(split->sizes, split->tab_ids.size());
      view.avail = window_->shell().ExtraPaneRoom(
          window_->shell().layout().page_rect);
    }
    for (const std::string& id : split->tab_ids) {
      tabs::TabInterface* tab = window_->FindTab(id);
      for (const CluShell::PaneRect& pane : window_->shell().SplitPaneRects()) {
        if (tab && pane.contents == tab->GetContents()) {
          view.panes.emplace_back(id, pane.rect);
        }
      }
    }
    if (view.panes.size() == split->tab_ids.size()) {
      s.split_view = std::move(view);
    }
  }
  s.split_drop = split_drop_;

  // Media and picture-in-picture of every live page.
  media_.SetShown(ShownRows());
  for (TabSnapshot& tab : s.tabs) {
    tabs::TabInterface* page = window_->FindTab(tab.id);
    if (!page) {
      continue;
    }
    content::WebContents* contents = page->GetContents();
    tab.media_playing = media_.IsPlaying(tab.id);
    if (auto* helper = AutoPictureInPictureTabHelper::FromWebContents(contents);
        helper && helper->IsInAutoPictureInPicture()) {
      tab.pip = "auto";
    } else if (contents->HasPictureInPictureVideo() ||
               contents->HasPictureInPictureDocument()) {
      tab.pip = "manual";
    }
  }
  s.media_tab_id = media_.Card([&s](std::string_view id) {
    for (const TabSnapshot& tab : s.tabs) {
      if (tab.id == id) {
        return tab.muted;
      }
    }
    return false;
  });
  if (s.media_tab_id &&
      std::ranges::none_of(s.tabs, [&s](const TabSnapshot& tab) {
        return tab.id == *s.media_tab_id;
      })) {
    s.media_tab_id.reset();
  }

  if (peek_) {
    s.peek = peek_->Snapshot();
  }

  if (mini_) {
    // The Mini window's one page is no tab row (MiniWindowState.tabId is its
    // page id).
    UpdateMiniTarget();
    MiniSnapshot mini;
    if (tabs::TabInterface* tab = window_->ActiveTab()) {
      content::WebContents* contents = tab->GetContents();
      mini.tab_id = CluTabId::Get(contents);
      mini.url = StateUrl(contents->GetVisibleURL());
      // The prompt's page loads: the window grows to the page size, after this snapshot.
      if (mini_->prompt && !contents->GetVisibleURL().is_empty() &&
          !contents->GetVisibleURL().IsAboutBlank()) {
        mini_->prompt = false;
        Post(weak_ptr_factory_.GetWeakPtr(),
             base::BindOnce(&CluWindowMultitask::GrowMiniPrompt));
      }
      mini.title = contents->GetTitle();
      for (const TabSnapshot& snap : s.tabs) {
        if (snap.id == mini.tab_id) {
          mini.favicon = snap.favicon;
          mini.loading = snap.loading;
          mini.progress = snap.progress;
          mini.can_go_back = snap.can_go_back;
          mini.site = snap.site;
        }
      }
    }
    CluSpacesService* spaces = CluSpacesService::Get();
    const SpaceEntry* target =
        spaces ? spaces->list().FindSpace(mini_->target_space_id) : nullptr;
    if (target) {
      mini.target_space_id = target->id;
      mini.target_space_name = target->name;
      mini.profile_mismatch =
          target->profile_id != spaces->ProfileIdOf(window_->profile());
    }
    mini.confirming = mini_->confirming;
    s.mini = std::move(mini);
    s.tabs.clear();
    s.active_tab_id.reset();
    s.media_tab_id.reset();
  }
}

void CluWindowMultitask::AddSurfaces(
    std::vector<OverlaySurfaceSnapshot>& surfaces) {
  if (peek_) {
    if (std::optional<OverlaySurfaceSnapshot> surface = peek_->Surface()) {
      surfaces.push_back(std::move(*surface));
    }
  }
  if (split_drop_) {
    // The drop preview in the top layer; input stays with the frame's drag.
    OverlaySurfaceSnapshot surface;
    surface.instance_id = split_drop_instance_;
    surface.surface = "split-drop";
    surface.layer = "top";
    surface.modality = "passive";
    surface.focus = false;
    surfaces.push_back(std::move(surface));
  }
}

}  // namespace clu
