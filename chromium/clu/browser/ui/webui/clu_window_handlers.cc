// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_window_handlers.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "clu/browser/ai/clu_ai_client.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/model/clu_live_feed.h"
#include "clu/browser/model/clu_menu_items.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/model/clu_tidy.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/spaces/clu_space_list.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/bridge/call_result.h"
#include "clu/browser/ui/webui/clu_command_bar.h"
#include "clu/browser/ui/webui/clu_haptic.h"
#include "clu/browser/ui/webui/clu_live_folders.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_space_menu.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_tab_menu.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/common/clu_urls.h"
#include "components/tabs/public/tab_interface.h"
#include "components/zoom/page_zoom.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_zoom.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "url/gurl.h"

namespace clu {

namespace {

// The width the sidebar drag locks at (LOCK_WIDTH in src/renderer/ui/shell/ResizeHandle.tsx).
constexpr int kSidebarLockWidth = 228;

mojom::CallResultPtr WindowGone() {
  return ErrorResult(mojom::ErrorCode::kRejected, "the window is closing");
}

mojom::CallResultPtr TabNotFound() {
  return ErrorResult(mojom::ErrorCode::kNotFound, "no such tab here");
}

// The tab `tab_id` names, or the active tab when it names none (optionalTab() in src/shared/ipc/common.ts).
tabs::TabInterface* ResolveTab(CluWindowState& window,
                               const std::optional<std::string>& tab_id) {
  return tab_id ? window.FindTab(*tab_id) : window.ActiveTab();
}

// A private window has tabs but no rows: the row commands answer this.
mojom::CallResultPtr NoRows() {
  return ErrorResult(mojom::ErrorCode::kNotImplemented,
                     "a private window keeps no tab rows");
}

// A tab row of the model, or (in a private window) a tab of this window.
bool RowExists(CluWindowState& window, const std::string& id) {
  if (CluModelService* model = window.model()) {
    const TabNode* node = model->tree().Get(id);
    return node && node->is_tab();
  }
  return window.FindTab(id) != nullptr;
}

mojom::CallResultPtr TreeErrorResult(TreeError error) {
  if (error == TreeError::kNotFound) {
    return ErrorResult(mojom::ErrorCode::kNotFound, TreeErrorText(error));
  }
  return RejectedResult(TreeErrorReason(error));
}

DropTarget ToTarget(const mojom::DropTargetPtr& target) {
  if (target->is_favorite()) {
    return DropTarget::Favorite(target->get_favorite()->index);
  }
  if (target->is_pinned()) {
    const mojom::DropTargetPinnedPtr& pinned = target->get_pinned();
    return DropTarget::Pinned(pinned->space_id,
                              pinned->parent_id.value_or(std::string()),
                              pinned->index);
  }
  const mojom::DropTargetTodayPtr& today = target->get_today();
  return DropTarget::Today(today->space_id, today->index);
}

// The answer of tabs.move and a tabs.bulk move: {status: 'moved'}, {status: 'needs-confirmation', reason:
// 'cross-profile'}, or the move rule that refused it. A sidebar drop (`drop`) that moved ticks the trackpad, unless
// Advanced's tabs.reorderHaptic is off.
void MoveResult(base::OnceCallback<void(mojom::CallResultPtr)> callback,
                bool sidebar_drop,
                base::expected<space_actions::MoveStatus, TreeError> result);

// tabs.move and a tabs.bulk move in a private window: its pages move within its one list (MovePrivatePages()).
void MovePrivate(CluWindowState& window,
                 const std::vector<std::string>& ids,
                 const DropTarget& target,
                 bool sidebar_drop,
                 base::OnceCallback<void(mojom::CallResultPtr)> callback) {
  const base::expected<void, TreeError> moved =
      window.MovePrivatePages(ids, target);
  if (!moved.has_value()) {
    MoveResult(std::move(callback), sidebar_drop,
               base::unexpected(moved.error()));
    return;
  }
  MoveResult(std::move(callback), sidebar_drop,
             space_actions::MoveStatus::kMoved);
}

void MoveResult(base::OnceCallback<void(mojom::CallResultPtr)> callback,
                bool sidebar_drop,
                base::expected<space_actions::MoveStatus, TreeError> result) {
  if (!result.has_value()) {
    std::move(callback).Run(TreeErrorResult(result.error()));
    return;
  }
  if (result.value() == space_actions::MoveStatus::kNeedsConfirmation) {
    std::move(callback).Run(ValueResult(base::Value(
        base::DictValue()
            .Set("status", "needs-confirmation")
            .Set("reason", "cross-profile"))));
    return;
  }
  if (sidebar_drop) {
    PerformReorderHaptic();
  }
  std::move(callback).Run(
      ValueResult(base::Value(base::DictValue().Set("status", "moved"))));
}

// SPACE_COLORS, in the order of the generated color enums.
constexpr auto kColorNames = std::to_array<const char*>(
    {"slate", "red", "orange", "amber", "lime", "green", "teal", "cyan", "blue",
     "indigo", "violet", "pink"});

template <typename Color>
std::optional<std::string> ColorName(const std::optional<Color>& color) {
  if (!color) {
    return std::nullopt;
  }
  const size_t index = static_cast<size_t>(*color);
  if (index >= kColorNames.size()) {
    return std::string("?");
  }
  return std::string(kColorNames[index]);
}

std::optional<SpaceIconInput> IconOf(const mojom::SpaceIconPtr& icon) {
  if (!icon) {
    return std::nullopt;
  }
  if (icon->is_emoji()) {
    return SpaceIconInput{.emoji = true, .value = icon->get_emoji()->value};
  }
  if (icon->is_symbol()) {
    // An index past SPACE_SYMBOLS keeps symbol = true with an empty value, which the space list refuses.
    return SpaceIconInput{
        .value = SpaceSymbolAt(static_cast<size_t>(icon->get_symbol()->value))
                     .value_or(std::string()),
        .symbol = true};
  }
  return SpaceIconInput{};
}

mojom::CallResultPtr SpacesErrorResult(SpacesError error) {
  switch (error) {
    case SpacesError::kNotFound:
      return ErrorResult(mojom::ErrorCode::kNotFound, "no such space or profile");
    case SpacesError::kBadPayload:
      return ErrorResult(mojom::ErrorCode::kBadPayload);
    default:
      return RejectedResult(SpacesErrorReason(error));
  }
}

mojom::CallResultPtr NoSpaces() {
  return ErrorResult(mojom::ErrorCode::kNotAllowed,
                     "a private window has no spaces");
}

gfx::Rect RectOf(const mojom::Rect& rect) {
  return gfx::ToEnclosingRect(
      gfx::RectF(static_cast<float>(rect.x), static_cast<float>(rect.y),
                 static_cast<float>(std::max(0.0, rect.width)),
                 static_cast<float>(std::max(0.0, rect.height))));
}

gfx::Rect AnchorRect(const mojom::AnchorPtr& anchor) {
  if (anchor->is_rect()) {
    return RectOf(*anchor->get_rect());
  }
  const mojom::PointPtr& point = anchor->get_point();
  return gfx::Rect(static_cast<int>(point->x), static_cast<int>(point->y), 0,
                   0);
}

}  // namespace

// state.* --------------------------------------------------------------------

CluStateHandler::CluStateHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluStateHandler::~CluStateHandler() = default;

void CluStateHandler::Get(GetCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::move(callback).Run(ValueResult(base::Value(window_->CurrentState())));
}

// ui.* -----------------------------------------------------------------------

CluUiHandler::CluUiHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluUiHandler::~CluUiHandler() = default;

void CluUiHandler::Ready(ReadyCallback callback) {
  std::move(callback).Run(window_ ? NullResult() : WindowGone());
}

// overlay.* ------------------------------------------------------------------

CluOverlayHandler::CluOverlayHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluOverlayHandler::~CluOverlayHandler() = default;

void CluOverlayHandler::Respond(mojom::OverlayRespondInputPtr input,
                                RespondCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The guard checked the JSON text and its caps.
  std::optional<base::Value> result =
      base::JSONReader::Read(input->result, base::JSON_PARSE_RFC);
  if (!result) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kBadPayload));
    return;
  }
  std::move(callback).Run(
      window_->RespondOverlay(input->instance_id, *result)
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, "no such surface"));
}

void CluOverlayHandler::Close(mojom::OverlayCloseInputPtr input,
                              CloseCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::move(callback).Run(
      window_->CloseOverlayInstance(input->instance_id)
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, "no such surface"));
}

// commands.* -----------------------------------------------------------------

CluCommandsHandler::CluCommandsHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluCommandsHandler::~CluCommandsHandler() = default;

void CluCommandsHandler::Run(mojom::CommandsRunInputPtr input,
                             RunCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const auto table = CommandTable();
  if (std::ranges::none_of(table, [&](const CommandSpec& spec) {
        return input->id == spec.id;
      })) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such command"));
    return;
  }
  if (!window_->RunCommand(input->id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotImplemented, input->id));
    return;
  }
  std::move(callback).Run(NullResult());
}

// tabs.* ---------------------------------------------------------------------

CluTabsHandler::CluTabsHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluTabsHandler::~CluTabsHandler() = default;

void CluTabsHandler::Create(mojom::TabsCreateInputPtr input,
                            CreateCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  base::expected<std::string, std::string> id = window_->CreateRow(
      GURL(input->url), input->space_id, input->activate.value_or(true));
  if (!id.has_value()) {
    std::move(callback).Run(
        id.error() == "not-found"
            ? ErrorResult(mojom::ErrorCode::kNotFound, "no such space")
            : RejectedResult(id.error()));
    return;
  }
  std::move(callback).Run(
      ValueResult(base::Value(base::DictValue().Set("tabId", id.value()))));
}

void CluTabsHandler::Activate(mojom::TabsActivateInputPtr input,
                              ActivateCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::move(callback).Run(window_->ActivateRow(input->tab_id) ? NullResult()
                                                               : TabNotFound());
}

void CluTabsHandler::Close(mojom::TabsCloseInputPtr input,
                           CloseCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (input->tab_id && !RowExists(*window_, *input->tab_id)) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  window_->CloseRowCommand(input->tab_id);
  std::move(callback).Run(NullResult());
}

void CluTabsHandler::Duplicate(mojom::TabsDuplicateInputPtr input,
                               DuplicateCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (!window_->model()) {
    std::move(callback).Run(NoRows());
    return;
  }
  const std::string id = input->tab_id.value_or(window_->ActiveRowId());
  std::move(callback).Run(window_->DuplicateRow(id) ? NullResult()
                                                     : TabNotFound());
}

void CluTabsHandler::SetMuted(mojom::TabsSetMutedInputPtr input,
                              SetMutedCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (!RowExists(*window_, input->tab_id)) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  window_->SetRowMuted(input->tab_id, input->muted);
  std::move(callback).Run(NullResult());
}

void CluTabsHandler::Rename(mojom::TabsRenameInputPtr input,
                            RenameCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model && window_ && window_->is_private()) {
    // A private window's page keeps its name itself (CluPageName); the same cleaning and cap as a row's (F3).
    tabs::TabInterface* tab = window_->FindTab(input->node_id);
    if (!tab) {
      std::move(callback).Run(TabNotFound());
      return;
    }
    CluPageName::Set(tab->GetContents(), CleanName(input->title));
    window_->MarkDirty();
    std::move(callback).Run(NullResult());
    return;
  }
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  TabNode* node = model->mutable_tree().Mutable(input->node_id);
  if (!node) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such tab or folder"));
    return;
  }
  // F3: the name is cleaned and capped; an empty name clears a tab's override and leaves a folder as it was.
  const std::string name = CleanName(input->title);
  if (node->is_folder() && name.empty()) {
    std::move(callback).Run(NullResult());
    return;
  }
  node->title_override =
      name.empty() ? std::nullopt : std::optional<std::string>(name);
  model->Commit();
  std::move(callback).Run(NullResult());
}

void CluTabsHandler::SetIcon(mojom::TabsSetIconInputPtr input,
                             SetIconCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  const TabNode* node = model->tree().Get(input->node_id);
  if (!node) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such tab or folder"));
    return;
  }
  if (node->is_tab() && node->section != Section::kPinned) {
    std::move(callback).Run(RejectedResult("bad-target"));
    return;
  }
  if (input->icon && !IsOneEmoji(*input->icon)) {
    std::move(callback).Run(RejectedResult("not-one-emoji"));
    return;
  }
  model->mutable_tree().Mutable(input->node_id)->custom_icon = input->icon;
  model->Commit();
  std::move(callback).Run(NullResult());
}

void CluTabsHandler::Move(mojom::TabsMoveInputPtr input,
                          MoveCallback callback) {
  if (window_ && !window_->model() && window_->is_private()) {
    MovePrivate(*window_, {input->node_id}, ToTarget(input->target),
                input->drop.value_or(false), std::move(callback));
    return;
  }
  if (!window_ || !window_->model()) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  // A target in a space of another profile answers needs-confirmation until `confirmed`, or asks in the overlay with
  // `ask` (F11, docs/chromium/m4.md).
  space_actions::MoveRows(
      window_.get(), {input->node_id}, ToTarget(input->target),
      input->confirmed.value_or(false), input->ask.value_or(false),
      base::BindOnce(&MoveResult, std::move(callback),
                     input->drop.value_or(false)));
}

void CluTabsHandler::Bulk(mojom::TabsBulkInputPtr input,
                          BulkCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model && window_ && window_->is_private() && input->is_move()) {
    // A multi-select drag in a private window: its pages move together within its one list.
    MovePrivate(*window_, input->get_move()->tab_ids,
                ToTarget(input->get_move()->target),
                input->get_move()->drop.value_or(false), std::move(callback));
    return;
  }
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  const std::vector<std::string>& raw_ids =
      input->is_move() ? input->get_move()->tab_ids
                       : input->get_action()->tab_ids;
  std::vector<std::string> ids;
  for (const std::string& id : raw_ids) {
    const TabNode* node = model->tree().Get(id);
    if (node && node->is_tab() && !std::ranges::contains(ids, id)) {
      ids.push_back(id);
    }
  }
  if (ids.empty()) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  if (input->is_move()) {
    space_actions::MoveRows(window_.get(), ids,
                            ToTarget(input->get_move()->target),
                            input->get_move()->confirmed.value_or(false),
                            input->get_move()->ask.value_or(false),
                            base::BindOnce(
                                &MoveResult, std::move(callback),
                                input->get_move()->drop.value_or(false)));
    return;
  }
  const char* picked = nullptr;
  switch (input->get_action()->action) {
    case mojom::TabsBulkInputActionAction::kArchive:
      picked = kMenuBulkArchive;
      break;
    case mojom::TabsBulkInputActionAction::kClose:
      picked = kMenuBulkClose;
      break;
    case mojom::TabsBulkInputActionAction::kCopyUrls:
      picked = kMenuBulkCopy;
      break;
    case mojom::TabsBulkInputActionAction::kCopyUrlsMarkdown:
      picked = kMenuBulkCopyMarkdown;
      break;
    case mojom::TabsBulkInputActionAction::kAddToSplit: {
      // The selected tabs become one split (two for now; more shows
      // the "split full" toast).
      base::expected<std::string, std::string> split =
          window_->multitask().CreateSplit(ids, SplitDir::kRow, ids.front());
      std::move(callback).Run(
          split.has_value() ? ValueResult(base::Value(
                                  base::DictValue().Set("status", "done")))
                            : RejectedResult(split.error()));
      return;
    }
  }
  window_->tab_menu().RunBulk(ids, picked);
  std::move(callback).Run(
      ValueResult(base::Value(base::DictValue().Set("status", "done"))));
}

// Tidy Today with the on-device model (ai.tidyTabs): rio-ai groups the space's Today tabs by topic and they move
// into that order, the tabs of a split kept together. Without a model, or with an answer that is not groups, the
// sidebar tidies by site with its rules (src/shared/tidy.ts).
void CluTabsHandler::TidyToday(mojom::TabsTidyTodayInputPtr input,
                               TidyTodayCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  if (!model->FindSpace(input->space_id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such space"));
    return;
  }
  auto status = [](const char* value) {
    return ValueResult(base::Value(base::DictValue().Set("status", value)));
  };
  const std::vector<std::string> ids = model->tree().Today(input->space_id);
  // The prompt stays small; a longer Today is tidied by the rules.
  constexpr size_t kMaxModelTabs = 60;
  if (ids.size() < 2 || ids.size() > kMaxModelTabs) {
    std::move(callback).Run(status(ids.size() < 2 ? "unchanged" : "no-model"));
    return;
  }
  std::vector<std::string> lines;
  for (size_t i = 0; i < ids.size(); ++i) {
    const TabNode* node = model->tree().Get(ids[i]);
    std::string title = node ? node->title_override.value_or(node->title) : "";
    const GURL url(node ? node->url : std::string());
    // A tab not loaded yet has no title: its address says what it is.
    if (title.empty()) {
      title = base::StrCat({url.host(), url.path()});
    }
    base::TruncateUTF8ToByteSize(title, 120, &title);
    lines.push_back(base::StrCat({base::NumberToString(i + 1), ". ", title,
                                  " (", url.host(), ")"}));
  }
  AskOnDeviceModel(
      {"tidy", base::JoinString(lines, "\n")},
      base::BindOnce(
          [](base::WeakPtr<CluWindowState> window, std::string space_id,
             std::vector<std::string> ids, TidyTodayCallback callback,
             std::optional<std::string> answer) {
            auto status = [](const char* value) {
              return ValueResult(
                  base::Value(base::DictValue().Set("status", value)));
            };
            CluModelService* m = window ? window->model() : nullptr;
            const std::optional<std::vector<int>> order =
                answer ? TidyOrderFromModel(*answer, static_cast<int>(ids.size()))
                       : std::nullopt;
            if (!order) {
              std::move(callback).Run(status("no-model"));
              return;
            }
            // Today changed while the model thought: nothing moves.
            if (!m || m->tree().Today(space_id) != ids) {
              std::move(callback).Run(status("unchanged"));
              return;
            }
            std::vector<std::string> sorted;
            for (const int index : *order) {
              const std::string& id = ids[index];
              if (std::ranges::contains(sorted, id)) {
                continue;
              }
              const TabNode* node = m->tree().Get(id);
              const std::optional<std::string> split =
                  node ? node->split_id : std::nullopt;
              if (!split) {
                sorted.push_back(id);
                continue;
              }
              // A split's tabs land together where its first one lands.
              for (const std::string& other : ids) {
                const TabNode* member = m->tree().Get(other);
                if (member && member->split_id == split) {
                  sorted.push_back(other);
                }
              }
            }
            if (sorted == ids) {
              std::move(callback).Run(status("unchanged"));
              return;
            }
            space_actions::MoveRows(
                window.get(), std::move(sorted),
                DropTarget::Today(space_id, 0), /*confirmed=*/true,
                /*ask=*/false,
                base::BindOnce(
                    [](TidyTodayCallback callback,
                       base::expected<space_actions::MoveStatus, TreeError>
                           moved) {
                      std::move(callback).Run(
                          moved.has_value()
                              ? ValueResult(base::Value(
                                    base::DictValue().Set("status", "tidied")))
                              : TreeErrorResult(moved.error()));
                    },
                    std::move(callback)));
          },
          window_, input->space_id, ids, std::move(callback)));
}

void CluTabsHandler::TogglePin(mojom::TabsTogglePinInputPtr input,
                               TogglePinCallback callback) {
  if (!window_ || !window_->model()) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  base::expected<void, TreeError> moved =
      window_->TogglePin(input->tab_id.value_or(window_->ActiveRowId()));
  std::move(callback).Run(moved.has_value() ? NullResult()
                                            : TreeErrorResult(moved.error()));
}

void CluTabsHandler::ResetToPinned(mojom::TabsResetToPinnedInputPtr input,
                                   ResetToPinnedCallback callback) {
  if (!window_ || !window_->model()) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  if (!RowExists(*window_, input->tab_id)) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  std::move(callback).Run(window_->ResetToPinned(input->tab_id)
                              ? NullResult()
                              : RejectedResult("not-anchored"));
}

void CluTabsHandler::SetPinnedUrl(mojom::TabsSetPinnedUrlInputPtr input,
                                  SetPinnedUrlCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  TabNode* node = model->mutable_tree().Mutable(input->tab_id);
  if (!node || !node->is_tab()) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  if (!node->pinned_url) {
    std::move(callback).Run(RejectedResult("not-anchored"));
    return;
  }
  // The URL policy runs here again, whatever the prompt classified (F2), on the page a rio:// URL names.
  const GURL url = ResolveCluUrl(GURL(input->url));
  if (!IsRestorableTabUrl(url.spec())) {
    std::move(callback).Run(RejectedResult("blocked-url"));
    return;
  }
  node->pinned_url = url.spec();
  model->Commit();
  std::move(callback).Run(NullResult());
}

void CluTabsHandler::Delete(mojom::TabsDeleteInputPtr input,
                            DeleteCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  const TabNode* node = model->tree().Get(input->node_id);
  if (!node || !node->is_tab()) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  // F9.6: an anchored row goes to the archive with reason delete (a Today row with reason close).
  model->ArchiveRows({input->node_id}, ArchiveReason::kClose);
  std::move(callback).Run(NullResult());
}

void CluTabsHandler::ToNewWindow(mojom::TabsToNewWindowInputPtr input,
                                 ToNewWindowCallback callback) {
  if (!window_ || !window_->model()) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  if (!RowExists(*window_, input->tab_id)) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  std::move(callback).Run(window_->MoveRowToNewWindow(input->tab_id)
                              ? NullResult()
                              : RejectedResult("shown-elsewhere"));
}

void CluTabsHandler::OpenMenu(mojom::TabsOpenMenuInputPtr input,
                              OpenMenuCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const char* menu = "row";
  switch (input->menu) {
    case mojom::TabsOpenMenuInputMenu::kRow:
      menu = "row";
      break;
    case mojom::TabsOpenMenuInputMenu::kEmpty:
      menu = "empty";
      break;
    case mojom::TabsOpenMenuInputMenu::kNew:
      menu = "new";
      break;
    case mojom::TabsOpenMenuInputMenu::kMore:
      menu = "more";
      break;
    case mojom::TabsOpenMenuInputMenu::kSpace:
      menu = "space";
      break;
  }
  // A row menu opened from the hover peek holds the peek open until it closes (F4, openedFrom).
  const bool from_peek = window_->sidebar_peek().is_open() &&
                         input->opened_from ==
                             window_->sidebar_peek().instance_id();
  if (from_peek) {
    window_->sidebar_peek().MenuOpened();
  }
  window_->set_menu_light(input->light);
  window_->OpenMenu(
      menu, input->node_id, input->tab_ids.value_or(std::vector<std::string>()),
      AnchorRect(input->anchor),
      base::BindOnce(
          [](base::WeakPtr<CluWindowState> window, bool from_peek,
             OpenMenuCallback callback, std::optional<std::string> rename) {
            if (window && from_peek) {
              window->sidebar_peek().MenuClosed();
            }
            std::move(callback).Run(ValueResult(base::Value(
                base::DictValue().Set("rename", rename ? base::Value(*rename)
                                                       : base::Value()))));
          },
          window_, from_peek, std::move(callback)));
}

void CluTabsHandler::Hover(mojom::TabsHoverInputPtr input,
                           HoverCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::optional<gfx::Rect> anchor;
  if (input->anchor) {
    anchor = RectOf(*input->anchor);
  }
  // Gmail's card holding itself open, or letting go.
  if (input->held) {
    if (input->tab_id) {
      window_->HoldHover(*input->tab_id, *input->held);
    }
    std::move(callback).Run(NullResult());
    return;
  }
  // The pointer leaving a row (no tab) fades the title pill out; Rio's own reasons to hide it do so at once.
  window_->Hover(input->tab_id, anchor, /*fade=*/!input->tab_id);
  std::move(callback).Run(NullResult());
}

void CluTabsHandler::OpenMail(mojom::TabsOpenMailInputPtr input,
                              OpenMailCallback callback) {
  if (!window_ || !window_->model()) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  if (!RowExists(*window_, input->tab_id)) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  std::move(callback).Run(window_->OpenMail(input->tab_id, input->url)
                              ? NullResult()
                              : RejectedResult("blocked-url"));
}

// folders.* and sections.* -------------------------------------------------------

CluFoldersHandler::CluFoldersHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluFoldersHandler::~CluFoldersHandler() = default;

void CluFoldersHandler::Create(mojom::FoldersCreateInputPtr input,
                               CreateCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  base::expected<std::string, TreeError> folder =
      model->mutable_tree().CreateFolder(
          input->space_id, input->parent_id.value_or(std::string()),
          std::string(), std::nullopt, model->NowMs());
  if (!folder.has_value()) {
    std::move(callback).Run(TreeErrorResult(folder.error()));
    return;
  }
  model->Commit();
  std::move(callback).Run(ValueResult(
      base::Value(base::DictValue().Set("folderId", folder.value()))));
}

void CluFoldersHandler::CreateLive(mojom::FoldersCreateLiveInputPtr input,
                                   CreateLiveCallback callback) {
  if (!window_ || !window_->model()) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  std::string_view kind = kLiveFeed;
  if (input->kind == mojom::FoldersCreateLiveInputKind::kGithubReleases) {
    kind = kLiveGithubReleases;
  } else if (input->kind == mojom::FoldersCreateLiveInputKind::kGithubCommits) {
    kind = kLiveGithubCommits;
  }
  base::expected<std::string, std::string> folder = CluLiveFolders::Get().Create(
      *window_, input->space_id, kind, input->source);
  if (!folder.has_value()) {
    std::move(callback).Run(
        folder.error() == "bad-source"
            ? ErrorResult(mojom::ErrorCode::kBadPayload, "no feed there")
            : ErrorResult(mojom::ErrorCode::kNotFound, "no such space"));
    return;
  }
  std::move(callback).Run(ValueResult(
      base::Value(base::DictValue().Set("folderId", folder.value()))));
}

void CluFoldersHandler::SetCollapsed(mojom::FoldersSetCollapsedInputPtr input,
                                     SetCollapsedCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  const TabNode* node = model->tree().Get(input->folder_id);
  if (!node || !node->is_folder()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such folder"));
    return;
  }
  model->mutable_tree().Mutable(input->folder_id)->collapsed = input->collapsed;
  model->Commit();
  std::move(callback).Run(NullResult());
}

void CluFoldersHandler::Peek(mojom::FoldersPeekInputPtr input,
                             PeekCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::optional<gfx::Rect> rect;
  if (input->rect) {
    rect = RectOf(*input->rect);
  }
  std::move(callback).Run(
      window_->FolderPeek(input->folder_id, rect, input->held)
          ? NullResult()
          : ErrorResult(mojom::ErrorCode::kNotFound, "no such folder"));
}

void CluFoldersHandler::Delete(mojom::FoldersDeleteInputPtr input,
                               DeleteCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  const TabNode* node = model->tree().Get(input->folder_id);
  if (!node || !node->is_folder()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such folder"));
    return;
  }
  // F3: its tabs go, flattened and in order, to the top of Today; nothing is lost.
  std::ignore =
      model->mutable_tree().DeleteFolder(input->folder_id, model->NowMs());
  model->Commit();
  std::move(callback).Run(NullResult());
}

CluSectionsHandler::CluSectionsHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluSectionsHandler::~CluSectionsHandler() = default;

void CluSectionsHandler::SetPinnedCollapsed(
    mojom::SectionsSetPinnedCollapsedInputPtr input,
    SetPinnedCollapsedCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  if (!model->FindSpace(input->space_id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such space"));
    return;
  }
  model->mutable_tree().MutableSpaceState(input->space_id).pinned_collapsed =
      input->collapsed;
  model->Commit();
  std::move(callback).Run(NullResult());
}

// archive.* and switcher.* ------------------------------------------------------

CluArchiveHandler::CluArchiveHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluArchiveHandler::~CluArchiveHandler() = default;

void CluArchiveHandler::List(mojom::ArchiveListInputPtr input,
                             ListCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  // A private window keeps no archive; another profile's archive is not this window's to read.
  if (!model || input->profile_id != model->profile_id()) {
    std::move(callback).Run(ValueResult(base::Value(
        base::DictValue()
            .Set("rows", base::ListValue())
            .Set("nextCursor", base::Value()))));
    return;
  }
  const std::string profile_id = model->profile_id();
  model->ListArchive(
      input->query.value_or(std::string()), input->cursor.value_or(std::string()),
      input->limit.value_or(100),
      base::BindOnce(
          [](ListCallback callback, std::string profile_id, ArchivePage page) {
            base::ListValue rows;
            for (const ArchiveRow& row : page.rows) {
              const std::optional<std::string> favicon =
                  FaviconUrlFor(GURL(row.url));
              rows.Append(
                  base::DictValue()
                      .Set("id", row.id)
                      .Set("profileId", profile_id)
                      .Set("spaceId", row.space_id.empty()
                                          ? base::Value()
                                          : base::Value(row.space_id))
                      .Set("title", row.title)
                      .Set("url", StateUrl(GURL(row.url)))
                      .Set("favicon",
                           favicon ? base::Value(*favicon) : base::Value())
                      .Set("reason", ArchiveReasonName(row.reason))
                      .Set("archivedAt",
                           static_cast<double>(row.archived_at)));
            }
            std::move(callback).Run(ValueResult(base::Value(
                base::DictValue()
                    .Set("rows", std::move(rows))
                    .Set("nextCursor", page.next_cursor
                                           ? base::Value(*page.next_cursor)
                                           : base::Value()))));
          },
          std::move(callback), profile_id));
}

void CluArchiveHandler::Restore(mojom::ArchiveRestoreInputPtr input,
                                RestoreCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  model->RestoreArchive(
      input->archive_id, window_.get(),
      base::BindOnce(
          [](RestoreCallback callback, std::optional<std::string> error) {
            if (!error) {
              std::move(callback).Run(NullResult());
            } else if (*error == "not-found") {
              std::move(callback).Run(
                  ErrorResult(mojom::ErrorCode::kNotFound, "no such row"));
            } else {
              std::move(callback).Run(RejectedResult(*error));
            }
          },
          std::move(callback)));
}

void CluArchiveHandler::Delete(mojom::ArchiveDeleteInputPtr input,
                               DeleteCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  model->DeleteArchive(
      input->archive_id,
      base::BindOnce(
          [](DeleteCallback callback, bool removed) {
            std::move(callback).Run(
                removed ? NullResult()
                        : ErrorResult(mojom::ErrorCode::kNotFound,
                                      "no such row"));
          },
          std::move(callback)));
}

void CluArchiveHandler::Open(OpenCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->SetArchivePanel(true);
  std::move(callback).Run(NullResult());
}

void CluArchiveHandler::Close(CloseCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->SetArchivePanel(false);
  std::move(callback).Run(NullResult());
}

// The Library's Spaces view asks to be wide enough for every card and its "+"; 0 means the default 417.
void CluArchiveHandler::SetWidth(mojom::ArchiveSetWidthInputPtr input,
                                 SetWidthCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->shell().SetLibraryWidth(input->width);
  std::move(callback).Run(NullResult());
}

void CluArchiveHandler::ClearToday(mojom::ArchiveClearTodayInputPtr input,
                                   ClearTodayCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  if (input->space_id && !model->FindSpace(*input->space_id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such space"));
    return;
  }
  window_->ClearToday(input->space_id);
  std::move(callback).Run(NullResult());
}

void CluArchiveHandler::UndoClear(UndoClearCallback callback) {
  CluModelService* model = window_ ? window_->model() : nullptr;
  if (!model) {
    std::move(callback).Run(window_ ? NoRows() : WindowGone());
    return;
  }
  model->UndoClear();
  std::move(callback).Run(NullResult());
}

void CluArchiveHandler::ReopenClosed(ReopenClosedCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->ReopenClosed();
  std::move(callback).Run(NullResult());
}

CluSwitcherHandler::CluSwitcherHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluSwitcherHandler::~CluSwitcherHandler() = default;

void CluSwitcherHandler::Respond(mojom::SwitcherRespondInputPtr input,
                                 RespondCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->switcher().Respond(
      input->action == mojom::SwitcherRespondInputAction::kCommit,
      input->tab_id);
  std::move(callback).Run(NullResult());
}

// settings.* -------------------------------------------------------------------

CluSettingsHandler::CluSettingsHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluSettingsHandler::~CluSettingsHandler() = default;

void CluSettingsHandler::Open(mojom::SettingsOpenInputPtr input,
                              OpenCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::string_view tab;
  if (input->section) {
    switch (*input->section) {
      case mojom::SettingsOpenInputSection::kGeneral:
        tab = "general";
        break;
      case mojom::SettingsOpenInputSection::kProfiles:
        tab = "profiles";
        break;
      case mojom::SettingsOpenInputSection::kAi:
        tab = "ai";
        break;
      case mojom::SettingsOpenInputSection::kLinks:
        tab = "links";
        break;
      case mojom::SettingsOpenInputSection::kShortcuts:
        tab = "shortcuts";
        break;
      case mojom::SettingsOpenInputSection::kIcon:
        tab = "icon";
        break;
      case mojom::SettingsOpenInputSection::kAdvanced:
        tab = "advanced";
        break;
    }
  }
  if (!window_->OpenAppSettings(tab)) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kNotImplemented,
                                        "no Rio window for the settings"));
    return;
  }
  std::move(callback).Run(NullResult());
}

void CluSettingsHandler::OpenBrowser(mojom::SettingsOpenBrowserInputPtr input,
                                     OpenBrowserCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::string_view path;
  if (input->page) {
    switch (*input->page) {
      case mojom::SettingsOpenBrowserInputPage::kRoot:
        break;
      case mojom::SettingsOpenBrowserInputPage::kPrivacy:
        path = "privacy";
        break;
      case mojom::SettingsOpenBrowserInputPage::kPasswords:
        path = "passwords";
        break;
      case mojom::SettingsOpenBrowserInputPage::kPayments:
        path = "payments";
        break;
      case mojom::SettingsOpenBrowserInputPage::kNotifications:
        path = "content/notifications";
        break;
      case mojom::SettingsOpenBrowserInputPage::kSiteSettings:
        path = "content";
        break;
      case mojom::SettingsOpenBrowserInputPage::kSearch:
        path = "search";
        break;
      case mojom::SettingsOpenBrowserInputPage::kDownloads:
        path = "downloads";
        break;
      case mojom::SettingsOpenBrowserInputPage::kAbout:
        path = "help";
        break;
    }
  }
  if (!window_->OpenBrowserSettings(path)) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kNotImplemented,
                                        "a private window has no settings row"));
    return;
  }
  std::move(callback).Run(NullResult());
}

void CluSettingsHandler::Set(mojom::SettingsSetInputPtr input,
                             SetCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const bool global = input->scope == "global";
  base::expected<base::Value, SettingError> value =
      CheckSetting(input->key, global, input->value);
  if (!value.has_value()) {
    // The guard lets any 64-character key and any JSON through; the registry decides (an invalid stored value would
    // read as the default anyway).
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kBadPayload,
                                        SettingErrorMessage(value.error())));
    return;
  }
  if (global) {
    CluAppSettings* settings = CluAppSettings::Get();
    if (!settings) {
      std::move(callback).Run(WindowGone());
      return;
    }
    settings->SetGlobal(input->key, std::move(value.value()));
    std::move(callback).Run(NullResult());
    return;
  }
  // A profile setting: the scope is a Clu profile id, this window's or another's (Settings has a profile picker).
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces || !spaces->list().FindProfile(input->scope)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such profile"));
    return;
  }
  auto apply = [](std::string key, base::Value value, SetCallback callback,
                  Profile* profile) {
    CluModelService* model = profile ? CluModelService::Get(profile) : nullptr;
    if (!model) {
      std::move(callback).Run(
          ErrorResult(mojom::ErrorCode::kNotFound, "no such profile"));
      return;
    }
    if (key == kSearchEngineIdKey) {
      std::move(callback).Run(
          SetDefaultSearchEngine(profile, value.GetString())
              ? NullResult()
              : ErrorResult(mojom::ErrorCode::kNotFound,
                            "this profile offers no such engine"));
      return;
    }
    // tabs.archiveAfterHours, once the profile's rows are read.
    model->WhenLoaded(base::BindOnce(
        [](base::WeakPtr<CluModelService> model, double hours,
           SetCallback callback) {
          std::move(callback).Run(model && model->SetArchiveAfterHours(hours)
                                      ? NullResult()
                                      : ErrorResult(mojom::ErrorCode::kBadPayload,
                                                    "tabs.archiveAfterHours takes "
                                                    "12, 24, 168 or 720"));
        },
        model->GetWeakPtr(), value.GetDouble(), std::move(callback)));
  };
  spaces->LoadProfile(input->scope,
                      base::BindOnce(apply, input->key, std::move(value.value()),
                                     std::move(callback)));
}

// nav.* ----------------------------------------------------------------------

CluNavHandler::CluNavHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluNavHandler::~CluNavHandler() = default;

void CluNavHandler::Back(mojom::NavBackInputPtr input, BackCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  tabs::TabInterface* tab = ResolveTab(*window_, input->tab_id);
  if (!tab) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  window_->GoBack(tab);
  std::move(callback).Run(NullResult());
}

void CluNavHandler::Forward(mojom::NavForwardInputPtr input,
                            ForwardCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  tabs::TabInterface* tab = ResolveTab(*window_, input->tab_id);
  if (!tab) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  window_->GoForward(tab);
  std::move(callback).Run(NullResult());
}

void CluNavHandler::Stop(mojom::NavStopInputPtr input, StopCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  tabs::TabInterface* tab = ResolveTab(*window_, input->tab_id);
  if (!tab) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  window_->StopLoading(tab);
  std::move(callback).Run(NullResult());
}

void CluNavHandler::Reload(mojom::NavReloadInputPtr input,
                           ReloadCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  tabs::TabInterface* tab = ResolveTab(*window_, input->tab_id);
  if (!tab) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  window_->Reload(tab, input->hard.value_or(false));
  std::move(callback).Run(NullResult());
}

// zoom.* ---------------------------------------------------------------------

CluZoomHandler::CluZoomHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluZoomHandler::~CluZoomHandler() = default;

void CluZoomHandler::Step(mojom::ZoomStepInputPtr input,
                          StepCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  tabs::TabInterface* tab = ResolveTab(*window_, input->tab_id);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  // The guard has kept dir to -1, 0 or 1; PageZoom stops at Chromium's 25% and 500%.
  zoom::PageZoom::Zoom(contents, input->dir < 0   ? content::PAGE_ZOOM_OUT
                                 : input->dir > 0 ? content::PAGE_ZOOM_IN
                                                  : content::PAGE_ZOOM_RESET);
  window_->MarkDirty();
  double factor = 1;
  if (auto* zoom = zoom::ZoomController::FromWebContents(contents)) {
    factor = zoom->GetZoomPercent() / 100.0;
  }
  std::move(callback).Run(
      ValueResult(base::Value(base::DictValue().Set("factor", factor))));
}

// shell.* --------------------------------------------------------------------

CluShellHandler::CluShellHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluShellHandler::~CluShellHandler() = default;

void CluShellHandler::ToggleSidebar(ToggleSidebarCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->shell().ToggleSidebar();
  std::move(callback).Run(NullResult());
}

void CluShellHandler::ToggleFocusMode(ToggleFocusModeCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->shell().ToggleFocusMode();
  std::move(callback).Run(NullResult());
}

void CluShellHandler::ResetSidebarWidth(ResetSidebarWidthCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->shell().ResetSidebarWidth();
  // Double-click resets to 214, which the next window starts with too (F5).
  if (CluAppSettings* settings = CluAppSettings::Get(); settings && !window_->is_private()) {
    settings->SetGlobal(kLastSidebarWidthKey,
                        base::Value(window_->shell().chrome_state().sidebar_width));
  }
  std::move(callback).Run(NullResult());
}

void CluShellHandler::SetSidebarWidth(
    mojom::ShellSetSidebarWidthInputPtr input,
    SetSidebarWidthCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const int before = window_->shell().chrome_state().sidebar_width;
  window_->shell().SetSidebarWidth(input->width);
  // A drag that reaches the 228 lock ticks the trackpad once (the frame holds the width there).
  if (const int after = window_->shell().chrome_state().sidebar_width;
      after == kSidebarLockWidth && before != after && !input->commit.value_or(false)) {
    PerformAlignmentHaptic();
  }
  // `commit` marks the end of a drag: the clamped width persists as shell.lastSidebarWidth (F5, F13).
  if (CluAppSettings* settings = CluAppSettings::Get();
      settings && input->commit.value_or(false) && !window_->is_private()) {
    settings->SetGlobal(kLastSidebarWidthKey,
                        base::Value(window_->shell().chrome_state().sidebar_width));
  }
  std::move(callback).Run(NullResult());
}

void CluShellHandler::OpenNavHistory(mojom::ShellOpenNavHistoryInputPtr input,
                                     OpenNavHistoryCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (!window_->ActiveTab()) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  window_->OpenNavHistory(
      input->direction == mojom::ShellOpenNavHistoryInputDirection::kBack,
      gfx::ToEnclosingRect(gfx::RectF(input->anchor->x, input->anchor->y,
                                      input->anchor->width,
                                      input->anchor->height)));
  std::move(callback).Run(NullResult());
}

void CluShellHandler::OpenToolbarMenu(
    mojom::ShellOpenToolbarMenuInputPtr input,
    OpenToolbarMenuCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The guard has checked the point is finite; the menu opens where it lands, in the frame's coordinates.
  window_->OpenToolbarMenu(gfx::ToFlooredPoint(
      gfx::PointF(input->anchor->x, input->anchor->y)));
  std::move(callback).Run(NullResult());
}

void CluShellHandler::SetSidebarPeek(mojom::ShellSetSidebarPeekInputPtr input,
                                     SetSidebarPeekCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->sidebar_peek().SetOpen(input->open);
  std::move(callback).Run(NullResult());
}

void CluShellHandler::Tooltip(mojom::ShellTooltipInputPtr input,
                              TooltipCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::optional<gfx::Rect> rect;
  if (input->rect) {
    rect = RectOf(*input->rect);
  }
  window_->Tooltip(input->label.value_or(std::string()),
                   input->keys.value_or(std::vector<std::string>()), rect,
                   input->above.value_or(false),
                   input->closing.value_or(false));
  std::move(callback).Run(NullResult());
}

void CluShellHandler::HoldSidebarPeek(mojom::ShellHoldSidebarPeekInputPtr input,
                                      HoldSidebarPeekCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->sidebar_peek().SetHeld(input->held);
  std::move(callback).Run(NullResult());
}

void CluShellHandler::PointerAlive(PointerAliveCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->sidebar_peek().Alive();
  std::move(callback).Run(NullResult());
}

// spaces.* --------------------------------------------------------------------

CluSpacesHandler::CluSpacesHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluSpacesHandler::~CluSpacesHandler() = default;

void CluSpacesHandler::Create(mojom::SpacesCreateInputPtr input,
                              CreateCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // A private window writes nothing to disk, so it never creates a saved space.
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  base::expected<std::string, SpacesError> id = space_actions::CreateSpace(
      window_.get(), input->name, IconOf(input->icon), ColorName(input->color),
      input->profile_id);
  if (!id.has_value()) {
    std::move(callback).Run(SpacesErrorResult(id.error()));
    return;
  }
  std::move(callback).Run(
      ValueResult(base::Value(base::DictValue().Set("spaceId", id.value()))));
}

void CluSpacesHandler::Update(mojom::SpacesUpdateInputPtr input,
                              UpdateCallback callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window_ || !spaces) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  base::expected<void, SpacesError> updated =
      spaces->UpdateSpace(input->space_id, input->name, IconOf(input->icon),
                          ColorName(input->color));
  std::move(callback).Run(updated.has_value()
                              ? NullResult()
                              : SpacesErrorResult(updated.error()));
}

void CluSpacesHandler::Reorder(mojom::SpacesReorderInputPtr input,
                               ReorderCallback callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window_ || !spaces) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  // toIndex is clamped to 0..n-1 (PLAN 3.11).
  base::expected<void, SpacesError> reordered =
      spaces->ReorderSpace(input->space_id, input->to_index);
  std::move(callback).Run(reordered.has_value()
                              ? NullResult()
                              : SpacesErrorResult(reordered.error()));
}

void CluSpacesHandler::Delete(mojom::SpacesDeleteInputPtr input,
                              DeleteCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  space_actions::DeleteSpace(
      input->space_id,
      base::BindOnce(
          [](DeleteCallback callback,
             base::expected<void, SpacesError> result) {
            std::move(callback).Run(result.has_value()
                                        ? NullResult()
                                        : SpacesErrorResult(result.error()));
          },
          std::move(callback)));
}

void CluSpacesHandler::Switch(mojom::SpacesSwitchInputPtr input,
                              SwitchCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // A private window has one space: switching is a no-op there.
  if (window_->is_private()) {
    std::move(callback).Run(NullResult());
    return;
  }
  SwitchTarget target;
  if (input->is_space_id()) {
    CluSpacesService* spaces = CluSpacesService::Get();
    const std::string& space_id = input->get_space_id()->space_id;
    if (!spaces || !spaces->list().FindSpace(space_id)) {
      std::move(callback).Run(
          ErrorResult(mojom::ErrorCode::kNotFound, "no such space"));
      return;
    }
    target.space_id = space_id;
  } else if (input->is_index()) {
    target.index = input->get_index()->index;
  } else {
    target.step = input->get_step()->step > 0 ? 1 : -1;
  }
  // An index past the end, a step past either end or the space already shown change nothing.
  space_actions::SwitchWindow(window_.get(), target);
  std::move(callback).Run(NullResult());
}

void CluSpacesHandler::SetProfile(mojom::SpacesSetProfileInputPtr input,
                                  SetProfileCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  space_actions::SetSpaceProfile(
      input->space_id, input->profile_id, input->confirmed.value_or(false),
      base::BindOnce(
          [](SetProfileCallback callback,
             base::expected<space_actions::SetProfileResult, SpacesError>
                 result) {
            if (!result.has_value()) {
              std::move(callback).Run(SpacesErrorResult(result.error()));
              return;
            }
            if (result->needs_confirmation) {
              std::move(callback).Run(ValueResult(base::Value(
                  base::DictValue()
                      .Set("status", "needs-confirmation")
                      .Set("tabCount", result->tab_count))));
              return;
            }
            std::move(callback).Run(ValueResult(
                base::Value(base::DictValue().Set("status", "done"))));
          },
          std::move(callback)));
}

void CluSpacesHandler::OpenOverflow(mojom::SpacesOpenOverflowInputPtr input,
                                    OpenOverflowCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  window_->set_menu_light(std::nullopt);  // no `light` in its input: the window's own
  window_->space_menu().OpenOverflow(AnchorRect(input->anchor));
  std::move(callback).Run(NullResult());
}

void CluSpacesHandler::OpenEditor(mojom::SpacesOpenEditorInputPtr input,
                                  OpenEditorCallback callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window_ || !spaces) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  if (input->space_id && !spaces->list().FindSpace(*input->space_id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such space"));
    return;
  }
  window_->OpenSpaceEditor(
      input->space_id,
      input->view == mojom::SpacesOpenEditorInputView::kProfiles ? "profiles"
                                                                 : "space");
  std::move(callback).Run(NullResult());
}

void CluSpacesHandler::OpenIconMenu(mojom::SpacesOpenIconMenuInputPtr input,
                                    OpenIconMenuCallback callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window_ || !spaces) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  if (!spaces->list().FindSpace(input->space_id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such space"));
    return;
  }
  window_->set_menu_light(input->light);
  window_->space_menu().OpenIconMenu(input->space_id,
                                     AnchorRect(input->anchor));
  std::move(callback).Run(NullResult());
}

void CluSpacesHandler::Swipe(mojom::SpacesSwipeInputPtr input,
                             SwipeCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  CluWindowState::SwipePhase phase = CluWindowState::SwipePhase::kCancel;
  switch (input->phase) {
    case mojom::SpacesSwipeInputPhase::kBegin:
      phase = CluWindowState::SwipePhase::kBegin;
      break;
    case mojom::SpacesSwipeInputPhase::kUpdate:
      phase = CluWindowState::SwipePhase::kUpdate;
      break;
    case mojom::SpacesSwipeInputPhase::kEnd:
      phase = CluWindowState::SwipePhase::kEnd;
      break;
    case mojom::SpacesSwipeInputPhase::kCancel:
      phase = CluWindowState::SwipePhase::kCancel;
      break;
  }
  window_->Swipe(phase, input->dx, input->momentum);
  std::move(callback).Run(NullResult());
}

// profiles.* -------------------------------------------------------------------

CluProfilesHandler::CluProfilesHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluProfilesHandler::~CluProfilesHandler() = default;

void CluProfilesHandler::Create(mojom::ProfilesCreateInputPtr input,
                                CreateCallback callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window_ || !spaces) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  spaces->CreateProfile(
      input->name,
      base::BindOnce(
          [](CreateCallback callback,
             base::expected<std::string, SpacesError> result) {
            if (!result.has_value()) {
              std::move(callback).Run(SpacesErrorResult(result.error()));
              return;
            }
            std::move(callback).Run(ValueResult(base::Value(
                base::DictValue().Set("profileId", result.value()))));
          },
          std::move(callback)));
}

void CluProfilesHandler::Rename(mojom::ProfilesRenameInputPtr input,
                                RenameCallback callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window_ || !spaces) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  base::expected<void, SpacesError> renamed =
      spaces->RenameProfile(input->profile_id, input->name);
  std::move(callback).Run(renamed.has_value()
                              ? NullResult()
                              : SpacesErrorResult(renamed.error()));
}

void CluProfilesHandler::Delete(mojom::ProfilesDeleteInputPtr input,
                                DeleteCallback callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window_ || !spaces) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (window_->is_private()) {
    std::move(callback).Run(NoSpaces());
    return;
  }
  base::expected<void, SpacesError> deleted =
      spaces->DeleteProfile(input->profile_id);
  std::move(callback).Run(deleted.has_value()
                              ? NullResult()
                              : SpacesErrorResult(deleted.error()));
}

// window.* ---------------------------------------------------------------------

CluWindowHandler::CluWindowHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluWindowHandler::~CluWindowHandler() = default;

void CluWindowHandler::NewPrivate(NewPrivateCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  space_actions::OpenPrivateWindow(window_->profile());
  std::move(callback).Run(NullResult());
}

}  // namespace clu
