// Copyright 2026 The Rio Authors. All rights reserved.

// CluWindowState's spaces (docs/plan/03-spaces.md through docs/chromium/m4.md): which space the window shows, the
// switch within its profile, the swipe, the sibling group a cross-profile switch uses, the private window, and the
// confirm and editor surfaces of the top overlay.

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_space_menu.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"

namespace clu {

namespace {

// A swipe with no message for this long is dropped (the frame reloaded or crashed mid-drag; swipe.ts).
constexpr base::TimeDelta kSwipeStale = base::Milliseconds(1500);
// The toast that names the space when the sidebar is hidden.
constexpr base::TimeDelta kSpaceToast = base::Seconds(1);
constexpr char kSpaceEditorSurface[] = "space-editor";
constexpr char kConfirmSurface[] = "confirm";
constexpr char kThemePickerSurface[] = "theme-picker";
// The theme editor's top when nothing anchors it: its inner group sits at y 133 of an 800 pt window.
constexpr int kThemePickerTop = 133;
// A switch the user asks for shows the incoming page (its WebContents made visible) in one task and swaps the
// page view (TabStripModel::ActivateTab hides the old one and lays the window out) a frame later, so the two halves
// never hold the same frame. The strip lands about 240 ms in, long after both.
constexpr base::TimeDelta kSpacePageGap = base::Milliseconds(20);

void PostToSelf(base::WeakPtr<CluWindowState> self,
                base::OnceCallback<void(CluWindowState*)> task,
                base::TimeDelta delay = base::TimeDelta()) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<CluWindowState> self,
             base::OnceCallback<void(CluWindowState*)> task) {
            if (self) {
              std::move(task).Run(self.get());
            }
          },
          std::move(self), std::move(task)),
      delay);
}

}  // namespace

bool CluWindowState::is_private() {
  return profile()->IsOffTheRecord();
}

bool CluWindowState::rows_ready() {
  return model_ready_ || private_ready_;
}

void CluWindowState::SetGroup(const std::string& group_id) {
  if (!group_id.empty()) {
    group_id_ = group_id;
  }
}

void CluWindowState::SetInitialSpace(const std::string& space_id) {
  initial_space_ = space_id;
}

void CluWindowState::SettleSpace() {
  CluModelService* m = model();
  if (!m || switch_pending_) {
    return;
  }
  std::vector<std::string> candidates;
  if (initial_space_) {
    candidates.push_back(*initial_space_);
  }
  candidates.push_back(space_id_);
  if (CluSpacesService* spaces = CluSpacesService::Get()) {
    candidates.push_back(spaces->list().last_space());
  }
  for (const std::string& candidate : candidates) {
    if (m->FindSpace(candidate)) {
      space_id_ = candidate;
      return;
    }
  }
  if (!m->spaces().empty()) {
    space_id_ = m->default_space_id();
    return;
  }
  // This profile has no space left (they moved to other profiles or were deleted). A hidden sibling just goes; a
  // window on screen moves to the first space, in its profile's window, and goes.
  if (hidden_by_switch_) {
    PostToSelf(GetWeakPtr(), base::BindOnce([](CluWindowState* self) {
                 self->browser()->GetWindow()->Close();
               }));
    return;
  }
  CluSpacesService* spaces = CluSpacesService::Get();
  if (spaces && !spaces->list().spaces().empty()) {
    const std::string first = spaces->list().spaces().front().id;
    PostToSelf(GetWeakPtr(),
               base::BindOnce(
                   [](std::string first, CluWindowState* self) {
                     space_actions::SwitchWindowToSpace(self, first,
                                                        /*close_source=*/true);
                   },
                   first));
  }
}

void CluWindowState::ActivateSpaceTarget() {
  if (!model()) {
    return;
  }
  const std::optional<std::string> target = SpaceTarget();
  if (!target || !ActivateRow(*target)) {
    ShowEmpty();
  }
}

std::optional<std::string> CluWindowState::SpaceTarget() {
  CluModelService* m = model();
  if (!m) {
    return std::nullopt;
  }
  const CluTabTree& tree = m->tree();
  std::vector<std::string> pinned_tabs;
  for (const std::string& id : tree.Pinned(space_id_)) {
    for (const std::string& tab : tree.TabsUnder(id)) {
      pinned_tabs.push_back(tab);
    }
  }
  return PickSpaceTarget(
      tree.SpaceState(space_id_).active_tab_id, tree.Today(space_id_),
      pinned_tabs, [&](std::string_view id) {
        return m->IsInSpace(id, space_id_) &&
               !m->pending_clear().contains(std::string(id)) &&
               !m->WindowShowing(id, this);
      });
}

bool CluWindowState::ShowSpace(const std::string& space_id,
                               bool page_after_frame) {
  CluModelService* m = model();
  if (!m || !m->FindSpace(space_id)) {
    return false;
  }
  // The row shown before the switch: a favorite counts as in every space, so only a row picked after the switch keeps
  // the page in ShowSpacePage().
  const std::string before = ActiveRowId();
  const bool changed = EnterSpace(space_id);
  if (changed && page_after_frame) {
    // The frame WebUI slides its strip from the activeSpaceId patch, so it goes out first. Swapping the page
    // view (TabStripModel::ActivateTab hides one WebContents, shows the other and lays the window out) ran 25 to 54 ms
    // before the patch with heavy pages: the window drew nothing for up to 50 ms and the strip started 85 ms in,
    // after the page had already changed. Now the strip starts on the frame's compositor and the page swaps under it.
    MarkDirty();
    Flush();
    PostToSelf(GetWeakPtr(),
               base::BindOnce(
                   [](std::string space_id, std::string before,
                      CluWindowState* self) {
                     self->PrepareSpacePage(space_id, before);
                   },
                   space_id, before));
    ++pending_space_pages_;
    PostToSelf(GetWeakPtr(),
               base::BindOnce(
                   [](std::string space_id, std::string before,
                      CluWindowState* self) {
                     self->ShowSpacePage(space_id, before);
                   },
                   space_id, before),
               kSpacePageGap);
    return true;
  }
  if (changed || !ActiveTab()) {
    ActivateSpaceTarget();
  }
  UpdatePageShown();
  MarkDirty();
  Flush();
  return true;
}

bool CluWindowState::SpacePageMoves(const std::string& before) {
  // A row opened or picked in the new space since the switch already shows. The row from before the switch never
  // counts, even a favorite, which is in every space: the new space shows its own target, as ShowSpace() does.
  CluModelService* m = model();
  const std::string active = ActiveRowId();
  return m && (active.empty() || active == before ||
               !m->IsInSpace(active, space_id_));
}

void CluWindowState::PrepareSpacePage(const std::string& space_id,
                                      const std::string& before) {
  // A later switch took over (a quick second press): its own task shows its page.
  if (!model() || space_id_ != space_id || !SpacePageMoves(before)) {
    return;
  }
  // Only a page already in this window's tab strip: one still to load or in another window swaps in ShowSpacePage().
  const std::optional<std::string> target = SpaceTarget();
  tabs::TabInterface* tab = target ? FindTab(*target) : nullptr;
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents || tab == ActiveTab() ||
      contents->GetVisibility() == content::Visibility::VISIBLE) {
    return;
  }
  HidePreShownPage();
  contents->WasShown();
  pre_shown_ = contents->GetWeakPtr();
}

void CluWindowState::HidePreShownPage() {
  content::WebContents* shown = pre_shown_.get();
  pre_shown_.reset();
  tabs::TabInterface* active = ActiveTab();
  if (!shown || (active && active->GetContents() == shown)) {
    return;
  }
  // A pane of the split on screen (the active tab's partner, or a third or fourth pane) stays drawn: Chromium shows a
  // page again only on a view or occlusion change, so hiding it here would freeze it.
  const SplitRow* split = multitask_->ShownSplit();
  if (split && CluTabId::Has(shown) &&
      std::ranges::contains(split->tab_ids, CluTabId::Get(shown))) {
    return;
  }
  shown->WasHidden();
}

void CluWindowState::ShowSpacePage(const std::string& space_id,
                                   const std::string& before) {
  pending_space_pages_ = std::max(0, pending_space_pages_ - 1);
  CluModelService* m = model();
  // A later switch took over (a quick second press, or a switch that swapped the page at once): its own task shows
  // its page.
  const bool current = m && space_id_ == space_id;
  if (current && SpacePageMoves(before)) {
    ActivateSpaceTarget();
  }
  // The page shown ahead that did not become the window's page (a row picked meanwhile, a switch that moved on) goes
  // back to hidden, once no later switch's page half is still to run.
  if (current || pending_space_pages_ == 0) {
    HidePreShownPage();
  }
  if (!current) {
    return;
  }
  UpdatePageShown();
  MarkDirty();
  Flush();
}

bool CluWindowState::EnterSpace(const std::string& space_id) {
  const bool changed = space_id != space_id_;
  space_id_ = space_id;
  initial_space_.reset();
  if (CluSpacesService* spaces = CluSpacesService::Get()) {
    spaces->SetLastSpace(space_id);
  }
  CluModelService* m = model();
  const SpaceRow* space = m ? m->FindSpace(space_id) : nullptr;
  // The floating sidebar shows the space it switched to.
  if (changed && space && !DockedSidebarShown() &&
      !(sidebar_peek_ && sidebar_peek_->is_open())) {
    ShowToast(space->icon_kind == "emoji"
                  ? base::StrCat({space->icon_value, "  ", space->name})
                  : space->name,
              std::nullopt, kSpaceToast);
  }
  return changed;
}

void CluWindowState::Swipe(SwipePhase phase, double dx, bool momentum) {
  CluSpacesService* spaces = CluSpacesService::Get();
  // Private windows have no switcher and no swipe.
  if (is_private() || !model() || !spaces) {
    return;
  }
  if (phase == SwipePhase::kBegin || phase == SwipePhase::kUpdate) {
    // Momentum never moves the drag.
    if (momentum) {
      return;
    }
    const int width = shell_->layout().sidebar_rect.width() > 0
                          ? shell_->layout().sidebar_rect.width()
                          : shell_->chrome_state().sidebar_width;
    swipe_ = ComputeSwipe(dx, width, spaces->list().Order(), space_id_);
    swipe_stale_timer_.Start(FROM_HERE, kSwipeStale,
                             base::BindOnce(&CluWindowState::ClearSwipe,
                                            base::Unretained(this)));
    MarkDirty();
    return;
  }
  const double fraction = swipe_ ? swipe_->fraction : 0;
  const double direction = dx != 0 ? dx : fraction;
  ClearSwipe();
  // The switch runs as the settle starts, not after it (spaces-t4).
  if (phase == SwipePhase::kEnd && direction != 0) {
    space_actions::SwitchWindow(this,
                                SwitchTarget{.step = direction > 0 ? 1 : -1});
  }
}

void CluWindowState::OnWheelPhase(bool phased, bool lifted) {
  // The current gesture's device: a mouse after a trackpad gesture goes back to the idle end at once.
  if (wheel_phased_ != phased) {
    wheel_phased_ = phased;
    MarkDirty();
    Flush();
  }
  if (lifted) {
    ++wheel_lifts_;
    // Once per gesture: the sidebar releases the swipe as soon as the fingers lift, not on the next batch.
    MarkDirty();
    Flush();
  }
}

void CluWindowState::ClearSwipe() {
  swipe_stale_timer_.Stop();
  if (swipe_) {
    swipe_.reset();
    MarkDirty();
  }
}

void CluWindowState::SetHiddenBySwitch(bool hidden) {
  hidden_by_switch_ = hidden;
  if (!hidden) {
    return;
  }
  switch_pending_ = false;
  ClearSwipe();
  switcher_->Blur();
  Hover(std::nullopt, std::nullopt);
  // The space it showed may have moved on (deleted, or moved to the profile of the window that took over).
  PostToSelf(GetWeakPtr(), base::BindOnce([](CluWindowState* self) {
               self->ValidateSpace();
             }));
}

void CluWindowState::ValidateSpace() {
  CluModelService* m = model();
  if (!m || switch_pending_ || m->FindSpace(space_id_)) {
    return;
  }
  SettleSpace();
  if (m->FindSpace(space_id_)) {
    ActivateSpaceTarget();
    UpdatePageShown();
    MarkDirty();
  }
}

void CluWindowState::OnSpacesChanged() {
  MarkDirty();
  // Posted: an action that changes spaces moves windows right after the change.
  PostToSelf(GetWeakPtr(), base::BindOnce([](CluWindowState* self) {
               self->ValidateSpace();
             }));
}

void CluWindowState::WhenFrameReady(base::OnceClosure callback) {
  if (frame_ready_for_testing()) {
    std::move(callback).Run();
    return;
  }
  on_frame_ready_.push_back(std::move(callback));
}

void CluWindowState::NotifyFrameReady() {
  if (!frame_ready_for_testing() || on_frame_ready_.empty()) {
    return;
  }
  // The snapshot with the space is out before anything shows the window.
  Flush();
  std::vector<base::OnceClosure> callbacks = std::move(on_frame_ready_);
  for (base::OnceClosure& callback : callbacks) {
    std::move(callback).Run();
  }
}

void CluWindowState::PrepareEmpty() {
  if (!ActiveTab()) {
    ShowEmpty();
  }
}

void CluWindowState::LeaveRow(const std::string& row_id) {
  if (ActiveRowId() == row_id) {
    LinkActivate(NextRowAfter(row_id));
  }
}

void CluWindowState::SetFade(std::unique_ptr<CluWindowFade> fade) {
  fade_ = std::move(fade);
}

bool CluWindowState::FadeRunning() const {
  return fade_ && !fade_->finished();
}

void CluWindowState::FinishFade() {
  if (FadeRunning()) {
    fade_->Finish();
  }
}

void CluWindowState::ClearFade(CluWindowFade* fade) {
  if (fade_.get() == fade) {
    fade_.reset();
  }
}

void CluWindowState::OnPrivateWindowReady() {
  if (!is_private()) {
    return;
  }
  private_ready_ = true;
  // Clu has no new-tab page: a private window opens on the command bar (PLAN 3.15).
  TabStripModel* strip = browser_->GetTabStripModel();
  if (strip->count() == 1 && IsNewTabPageContents(strip->GetWebContentsAt(0))) {
    CluPlaceholder::Mark(strip->GetWebContentsAt(0));
    OpenCommandBar(CommandBarMode::kNewTab);
  }
  UpdatePageShown();
  MarkDirty();
  NotifyFrameReady();
}

// Overlay prompts ------------------------------------------------------------------------------------------------------

void CluWindowState::OpenConfirm(base::DictValue props,
                                 base::OnceCallback<void(bool)> done) {
  OpenPrompt(kConfirmSurface, std::move(props));
  if (prompts_.empty()) {
    std::move(done).Run(false);
    return;
  }
  prompt_callbacks_[prompts_.back().instance_id] = base::BindOnce(
      [](base::OnceCallback<void(bool)> done, base::Value result) {
        std::move(done).Run(result.is_bool() && result.GetBool());
      },
      std::move(done));
}

bool CluWindowState::RespondOverlay(std::string_view instance_id,
                                    const base::Value& result) {
  const auto it = std::ranges::find_if(
      prompts_, [&](const OverlaySurfaceSnapshot& prompt) {
        return prompt.instance_id == instance_id;
      });
  if (it == prompts_.end()) {
    return false;
  }
  prompts_.erase(it);
  base::OnceCallback<void(base::Value)> callback;
  if (const auto found = prompt_callbacks_.find(instance_id);
      found != prompt_callbacks_.end()) {
    callback = std::move(found->second);
    prompt_callbacks_.erase(found);
  }
  MarkDirty();
  Flush();
  UpdateTopOverlay();
  FocusActivePage();
  if (callback) {
    std::move(callback).Run(result.Clone());
  }
  return true;
}

void CluWindowState::FailPrompts() {
  auto callbacks = std::move(prompt_callbacks_);
  prompt_callbacks_.clear();
  for (auto& [id, callback] : callbacks) {
    std::move(callback).Run(base::Value(false));
  }
}

void CluWindowState::OpenThemePicker(const std::string& space_id,
                                     const std::optional<gfx::Rect>& anchor) {
  if (is_private()) {
    return;
  }
  // Without an anchor (the space menu, the View menu, the command bar): the default place, its inner group at
  // y 133 of the window.
  const gfx::Rect at = anchor.value_or(gfx::Rect(
      12, kThemePickerTop,
      std::max(0, shell_->chrome_state().sidebar_width - 24), 28));
  // Nothing dims while the editor is open: the sidebar shows the edit live. A click outside closes it.
  OpenPrompt(kThemePickerSurface,
             base::DictValue()
                 .Set("spaceId", space_id)
                 .Set("anchor", base::DictValue()
                                    .Set("x", at.x())
                                    .Set("y", at.y())
                                    .Set("width", at.width())
                                    .Set("height", at.height())),
             "light-dismiss");
  if (prompts_.empty()) {
    return;
  }
  // Every way the picker goes (Esc, a click outside, another prompt, the window closing) ends its preview.
  prompt_callbacks_[prompts_.back().instance_id] = base::BindOnce(
      [](std::string space_id, base::Value) {
        if (CluAppSettings* settings = CluAppSettings::Get()) {
          settings->RevertPreview(space_id);
        }
      },
      space_id);
}

void CluWindowState::OpenSpaceEditor(const std::optional<std::string>& space_id,
                                     const std::string& view) {
  if (is_private()) {
    return;
  }
  // Create a Space has no backing of its own: it draws over the docked sidebar's empty stretch, so the docked sidebar
  // comes back first (from Cmd+S or focus mode) and the Library gives that stretch back.
  if (!space_id && view != "profiles") {
    ShowDockedSidebar();
    SetArchivePanel(false);
  }
  OpenPrompt(kSpaceEditorSurface,
             base::DictValue()
                 .Set("spaceId", space_id ? base::Value(*space_id)
                                          : base::Value())
                 .Set("view", view == "profiles" ? "profiles" : "space"));
}

}  // namespace clu
