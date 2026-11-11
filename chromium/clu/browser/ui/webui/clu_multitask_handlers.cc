// Copyright 2026 The Rio Authors. All rights reserved.

// The M6 handlers of clu_window_handlers.h (docs/chromium/m6.md): split.*,
// media.*, peek.*, mini.*, window.close, window.minimize and window.toggleFullscreen.

#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/values.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/bridge/call_result.h"
#include "clu/browser/ui/webui/clu_mini_windows.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace clu {

namespace {

mojom::CallResultPtr Gone() {
  return ErrorResult(mojom::ErrorCode::kRejected, "the window is closing");
}

mojom::CallResultPtr BadPayload(std::string_view why) {
  return ErrorResult(mojom::ErrorCode::kBadPayload, why);
}

// The reasons CluWindowMultitask answers, as the UI reads them.
mojom::CallResultPtr SplitError(const std::string& reason) {
  if (reason == "split-full") {
    return RejectedResult("split-full");
  }
  if (reason == "stale") {
    return ErrorResult(mojom::ErrorCode::kStale, "the split changed");
  }
  if (reason == "no-rows") {
    return ErrorResult(mojom::ErrorCode::kNotImplemented,
                       "this window keeps no tab rows");
  }
  if (reason == "other-space" || reason == "favorite" || reason == "same-tab") {
    return BadPayload("tabs must be open tabs of one space");
  }
  return ErrorResult(mojom::ErrorCode::kNotFound, reason);
}

mojom::CallResultPtr SplitIdResult(const std::string& split_id) {
  return ValueResult(base::Value(base::DictValue().Set("splitId", split_id)));
}

SplitEdge EdgeOf(mojom::SplitDropTabInputEdge edge) {
  switch (edge) {
    case mojom::SplitDropTabInputEdge::kLeft:
      return SplitEdge::kLeft;
    case mojom::SplitDropTabInputEdge::kRight:
      return SplitEdge::kRight;
    case mojom::SplitDropTabInputEdge::kTop:
      return SplitEdge::kTop;
    case mojom::SplitDropTabInputEdge::kBottom:
      return SplitEdge::kBottom;
  }
  return SplitEdge::kRight;
}

SplitEdge EdgeOf(mojom::SplitDragStateInputEdge edge) {
  switch (edge) {
    case mojom::SplitDragStateInputEdge::kLeft:
      return SplitEdge::kLeft;
    case mojom::SplitDragStateInputEdge::kRight:
      return SplitEdge::kRight;
    case mojom::SplitDragStateInputEdge::kTop:
      return SplitEdge::kTop;
    case mojom::SplitDragStateInputEdge::kBottom:
      return SplitEdge::kBottom;
  }
  return SplitEdge::kRight;
}

mojom::CallResultPtr VoidResult(const base::expected<void, std::string>& r) {
  return r.has_value() ? NullResult() : SplitError(r.error());
}

}  // namespace

// window.close --------------------------------------------------------------------------------------------------------

void CluWindowHandler::Close(CloseCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  // Answered first: closing the window takes this document with it.
  std::move(callback).Run(NullResult());
  window_->browser()->GetWindow()->Close();
}

void CluWindowHandler::Minimize(MinimizeCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(NullResult());
  window_->browser()->GetWindow()->Minimize();
}

void CluWindowHandler::ToggleFullscreen(ToggleFullscreenCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(NullResult());
  // The green button's click, as macOS's own: the window's full screen.
  chrome::ToggleFullscreenMode(window_->browser(), /*user_initiated=*/true);
}

// split.* -------------------------------------------------------------------------------------------------------------

CluSplitHandler::CluSplitHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluSplitHandler::~CluSplitHandler() = default;

void CluSplitHandler::Create(mojom::SplitCreateInputPtr input,
                             CreateCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (!UniqueIds(input->tab_ids)) {
    std::move(callback).Run(BadPayload("duplicate tab ids"));
    return;
  }
  base::expected<std::string, std::string> id =
      window_->multitask().CreateSplit(
          input->tab_ids,
          input->dir == mojom::SplitCreateInputDir::kCol ? SplitDir::kCol
                                                         : SplitDir::kRow,
          input->tab_ids.front());
  std::move(callback).Run(id.has_value() ? SplitIdResult(id.value())
                                         : SplitError(id.error()));
}

void CluSplitHandler::DropTab(mojom::SplitDropTabInputPtr input,
                              DropTabCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (input->tab_id == input->target_tab_id) {
    std::move(callback).Run(BadPayload("a tab cannot drop onto itself"));
    return;
  }
  base::expected<std::string, std::string> id = window_->multitask().DropTab(
      input->tab_id, input->target_tab_id, EdgeOf(input->edge));
  std::move(callback).Run(id.has_value() ? SplitIdResult(id.value())
                                         : SplitError(id.error()));
}

void CluSplitHandler::Swap(mojom::SplitSwapInputPtr input,
                           SwapCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (input->tab_id == input->target_tab_id) {
    std::move(callback).Run(BadPayload("a tab cannot swap with itself"));
    return;
  }
  std::move(callback).Run(VoidResult(
      window_->multitask().Swap(input->tab_id, input->target_tab_id)));
}

void CluSplitHandler::Resize(mojom::SplitResizeInputPtr input,
                             ResizeCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (!SizesSumToOne(input->sizes)) {
    std::move(callback).Run(BadPayload("sizes must sum to 1"));
    return;
  }
  std::move(callback).Run(VoidResult(
      window_->multitask().Resize(input->split_id, input->path, input->sizes)));
}

void CluSplitHandler::Equalize(mojom::SplitEqualizeInputPtr input,
                               EqualizeCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(
      VoidResult(window_->multitask().Equalize(input->split_id, input->path)));
}

void CluSplitHandler::RemovePane(mojom::SplitRemovePaneInputPtr input,
                                 RemovePaneCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(VoidResult(window_->multitask().RemovePane(
      input->tab_id, input->mode == mojom::SplitRemovePaneInputMode::kClose)));
}

void CluSplitHandler::FocusPane(mojom::SplitFocusPaneInputPtr input,
                                FocusPaneCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  CluWindowMultitask& multitask = window_->multitask();
  bool focused = false;
  if (input->is_tab_id()) {
    focused = multitask.FocusPaneTab(input->get_tab_id()->tab_id);
  } else if (input->is_index()) {
    focused = multitask.FocusPaneIndex(input->get_index()->index);
  } else {
    focused = multitask.FocusPaneStep(input->get_step()->step);
  }
  std::move(callback).Run(
      focused ? NullResult()
              : ErrorResult(mojom::ErrorCode::kNotFound, "no such pane"));
}

void CluSplitHandler::DragState(mojom::SplitDragStateInputPtr input,
                                DragStateCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  CluWindowMultitask::DragPhase phase = CluWindowMultitask::DragPhase::kCancel;
  switch (input->phase) {
    case mojom::SplitDragStateInputPhase::kStart:
      phase = CluWindowMultitask::DragPhase::kStart;
      break;
    case mojom::SplitDragStateInputPhase::kHover:
      phase = CluWindowMultitask::DragPhase::kHover;
      break;
    case mojom::SplitDragStateInputPhase::kEnd:
      phase = CluWindowMultitask::DragPhase::kEnd;
      break;
    case mojom::SplitDragStateInputPhase::kCancel:
      phase = CluWindowMultitask::DragPhase::kCancel;
      break;
  }
  std::optional<SplitEdge> edge;
  if (input->edge) {
    edge = EdgeOf(*input->edge);
  }
  window_->multitask().DragState(phase, input->tab_id, input->target_tab_id,
                                 edge);
  std::move(callback).Run(NullResult());
}

// media.* -------------------------------------------------------------------------------------------------------------

CluMediaHandler::CluMediaHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluMediaHandler::~CluMediaHandler() = default;

void CluMediaHandler::Toggle(mojom::MediaToggleInputPtr input,
                             ToggleCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(
      window_->multitask().ToggleMedia(input->tab_id)
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, "the tab plays no media"));
}

void CluMediaHandler::Previous(mojom::MediaPreviousInputPtr input,
                               PreviousCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(
      window_->multitask().SkipMedia(input->tab_id, /*next=*/false)
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, "the tab plays no media"));
}

void CluMediaHandler::Next(mojom::MediaNextInputPtr input,
                           NextCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(
      window_->multitask().SkipMedia(input->tab_id, /*next=*/true)
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, "the tab plays no media"));
}

void CluMediaHandler::Focus(mojom::MediaFocusInputPtr input,
                            FocusCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  std::move(callback).Run(
      window_->multitask().FocusMedia(input->tab_id)
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, "no such tab"));
}

// peek.* --------------------------------------------------------------------------------------------------------------

CluPeekHandler::CluPeekHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluPeekHandler::~CluPeekHandler() = default;

void CluPeekHandler::Close(CloseCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (window_->multitask().has_peek()) {
    window_->multitask().peek().Close();
  }
  std::move(callback).Run(NullResult());
}

void CluPeekHandler::Expand(ExpandCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (!window_->multitask().has_peek()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no Peek is open"));
    return;
  }
  std::move(callback).Run(window_->multitask().peek().Expand()
                              ? NullResult()
                              : ErrorResult(mojom::ErrorCode::kRejected,
                                            "this window keeps no tab rows"));
}

void CluPeekHandler::ToSplit(ToSplitCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (!window_->multitask().has_peek()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no Peek is open"));
    return;
  }
  // A source that cannot take a pane keeps the Peek open with the toast.
  std::move(callback).Run(window_->multitask().peek().ToSplit()
                              ? NullResult()
                              : RejectedResult("split-full"));
}

void CluPeekHandler::Nav(mojom::PeekNavInputPtr input, NavCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  if (!window_->multitask().has_peek()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no Peek is open"));
    return;
  }
  CluPeek::NavAction action = CluPeek::NavAction::kReload;
  switch (input->action) {
    case mojom::PeekNavInputAction::kBack:
      action = CluPeek::NavAction::kBack;
      break;
    case mojom::PeekNavInputAction::kForward:
      action = CluPeek::NavAction::kForward;
      break;
    case mojom::PeekNavInputAction::kReload:
      action = CluPeek::NavAction::kReload;
      break;
    case mojom::PeekNavInputAction::kHardReload:
      action = CluPeek::NavAction::kHardReload;
      break;
    case mojom::PeekNavInputAction::kStop:
      action = CluPeek::NavAction::kStop;
      break;
  }
  window_->multitask().peek().Nav(action);
  // A rail button took focus from the Peek page; it goes back.
  if (CluPeek& peek = window_->multitask().peek(); peek.page()) {
    peek.page()->Focus();
  }
  std::move(callback).Run(NullResult());
}

void CluPeekHandler::AnimationDone(mojom::PeekAnimationDoneInputPtr input,
                                   AnimationDoneCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  // seq is a whole number within JS's safe integers (the guard); Clu's own
  // never pass int's range.
  if (input->seq >= 0 && input->seq <= std::numeric_limits<int>::max()) {
    window_->multitask().peek().AnimationDone(static_cast<int>(input->seq));
  }
  std::move(callback).Run(NullResult());
}

// mini.* --------------------------------------------------------------------------------------------------------------

CluMiniHandler::CluMiniHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluMiniHandler::~CluMiniHandler() = default;

void CluMiniHandler::Open(mojom::MiniOpenInputPtr input,
                          OpenCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  const GURL url(input->url.value_or(url::kAboutBlankURL));
  content::WebContents* shown = mini_windows::Open(
      window_->profile(), url, /*command_bar=*/!input->url.has_value());
  std::move(callback).Run(
      shown ? NullResult()
            : ErrorResult(mojom::ErrorCode::kRejected, "blocked-url"));
}

void CluMiniHandler::MoveToSpace(mojom::MiniMoveToSpaceInputPtr input,
                                 MoveToSpaceCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  base::expected<void, std::string> moved = window_->multitask().MoveToSpace(
      input->space_id, input->confirm_profile_change.value_or(false));
  std::move(callback).Run(
      moved.has_value()
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, moved.error()));
}

void CluMiniHandler::PickSpace(PickSpaceCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  window_->multitask().PickSpace();
  std::move(callback).Run(NullResult());
}

void CluMiniHandler::CancelMove(CancelMoveCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  window_->multitask().CancelMove();
  std::move(callback).Run(NullResult());
}

void CluMiniHandler::Back(BackCallback callback) {
  if (!window_) {
    std::move(callback).Run(Gone());
    return;
  }
  window_->multitask().MiniBack();
  std::move(callback).Run(NullResult());
}

}  // namespace clu
