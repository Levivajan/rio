// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_space_menu.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "clu/browser/model/clu_live_feed.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "build/build_config.h"
#include "ui/gfx/geometry/rect.h"
#if BUILDFLAG(IS_MAC)
#include "clu/browser/ui/webui/clu_menu_appearance.h"
#include "clu/browser/ui/webui/clu_share_services.h"
#endif
#include "ui/base/models/menu_separator_types.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"

namespace clu {

namespace {

std::string SpaceLabel(const SpaceEntry& space) {
  return space.icon_kind == "emoji"
             ? base::StrCat({space.icon_value, "  ", space.name})
             : space.name;
}

}  // namespace

CluSpaceMenu::CluSpaceMenu(CluWindowState& window) : window_(window) {}

CluSpaceMenu::~CluSpaceMenu() = default;

std::vector<MenuItem> CluSpaceMenu::BuildIconMenu(const std::string& space_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces || window_->is_private()) {
    return {};
  }
  const CluSpaceList& list = spaces->list();
  const int index = list.IndexOf(space_id);
  if (index < 0) {
    return {};
  }
  const int count = static_cast<int>(list.spaces().size());
  return {MenuItem::Item(kEdit, "Edit Space…"), MenuItem::Separator(),
          MenuItem::Item(kMoveLeft, "Move Left", index > 0),
          MenuItem::Item(kMoveRight, "Move Right", index < count - 1),
          MenuItem::Separator(),
          MenuItem::Item(kDelete, "Delete Space…", count > 1)};
}

std::vector<MenuItem> CluSpaceMenu::BuildSpaceMenu(
    const std::string& space_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces || window_->is_private()) {
    return {};
  }
  const CluSpaceList& list = spaces->list();
  const SpaceEntry* space = list.FindSpace(space_id);
  if (!space) {
    return {};
  }
  std::vector<MenuItem> profiles;
  for (const ProfileEntry& profile : list.profiles()) {
    profiles.push_back(MenuItem::Check(
        base::StrCat({kProfilePrefix, profile.id}), profile.name,
        profile.id == space->profile_id, profile.id != space->profile_id));
  }
  std::vector<MenuItem> exports;
  for (const SpaceEntry& other : list.spaces()) {
    // Only spaces of this window's profile share its tab tree.
    if (other.id != space_id && window_->model() &&
        window_->model()->FindSpace(other.id)) {
      exports.push_back(MenuItem::Item(base::StrCat({kExportPrefix, other.id}),
                                       SpaceLabel(other)));
    }
  }
  CluModelService* m = window_->model();
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item(kIcon, "Change Space Icon…"));
  items.push_back(MenuItem::Item(kRename, "Rename Space…"));
  items.push_back(MenuItem::Item(kTheme, "Edit Theme Color…"));
  items.push_back(MenuItem::Submenu(kProfile, "Set Profile", std::move(profiles)));
  items.push_back(MenuItem::Separator());
  const bool has_tree = m && m->FindSpace(space_id) != nullptr;
  items.push_back(MenuItem::Item(kNewFolder, "New Folder", has_tree));
  // Live Folders ▸: folders that follow public feeds, nothing that needs a sign-in (clu_live_feed.h).
  items.push_back(MenuItem::Submenu(
      kLive, "Live Folders",
      {MenuItem::Item(base::StrCat({kLivePrefix, kLiveFeed}),
                      "RSS or Atom Feed…", has_tree),
       MenuItem::Item(base::StrCat({kLivePrefix, kLiveGithubReleases}),
                      "GitHub Releases…", has_tree),
       MenuItem::Item(base::StrCat({kLivePrefix, kLiveGithubCommits}),
                      "GitHub Commits…", has_tree)}));
  items.push_back(MenuItem::Separator());
  // Always pickable: a space with no pinned tabs shares its Today tabs.
  items.push_back(MenuItem::Item(kShare, "Share Space…", has_tree));
  const std::string export_label =
      base::StrCat({"Export “", space->name, "” into Folder"});
  // Always a submenu; with nothing to export its one item says why.
  if (!has_tree || m->tree().Pinned(space_id).empty()) {
    exports = {MenuItem::Item("space.export.none", "No Pinned Tabs", false)};
  } else if (exports.empty()) {
    exports = {MenuItem::Item("space.export.none", "No Other Spaces", false)};
  }
  items.push_back(MenuItem::Submenu(kExport, export_label, std::move(exports)));
  items.push_back(MenuItem::Separator());
  items.push_back(MenuItem::Item(kManage, "Manage Spaces…"));
  items.push_back(MenuItem::Separator());
  items.push_back(
      MenuItem::Item(kDelete, "Delete Space", list.spaces().size() > 1));
  return items;
}

std::optional<std::string> CluSpaceMenu::RunSpaceItem(
    const std::string& space_id,
    const std::string& picked) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces || !std::ranges::contains(EnabledLeaves(BuildSpaceMenu(space_id)),
                                        picked)) {
    return std::nullopt;
  }
  if (picked == kIcon) {
    window_->OpenSpaceEditor(space_id, "space");
  } else if (picked == kRename) {
    return space_id;
  } else if (picked == kTheme) {
    window_->OpenThemePicker(space_id, std::nullopt);
  } else if (picked == kNewFolder) {
    CluModelService* m = window_->model();
    CluTabTree& tree = m->mutable_tree();
    base::expected<std::string, TreeError> folder = tree.CreateFolder(
        space_id, std::string(), std::string(), std::nullopt, m->NowMs());
    if (!folder.has_value()) {
      return std::nullopt;
    }
    tree.MutableSpaceState(space_id).pinned_collapsed = false;
    m->Commit();
    return folder.value();
  } else if (picked.starts_with(kLivePrefix)) {
    window_->OpenPrompt(
        "folders.live",
        base::DictValue()
            .Set("spaceId", space_id)
            .Set("kind", picked.substr(sizeof(kLivePrefix) - 1)));
  } else if (picked == kManage) {
    window_->OpenSpaceEditor(space_id, "profiles");
  } else if (picked == kShare) {
    ShareSpace(space_id);
  } else if (picked.starts_with(kExportPrefix)) {
    ExportSpace(space_id, picked.substr(sizeof(kExportPrefix) - 1));
  } else if (picked == kDelete) {
    space_actions::ConfirmDeleteSpace(&window_.get(), space_id);
  } else if (picked.starts_with(kProfilePrefix)) {
    space_actions::SetSpaceProfile(
        space_id, picked.substr(sizeof(kProfilePrefix) - 1),
        /*confirmed=*/true, base::DoNothing());
  }
  return std::nullopt;
}

void CluSpaceMenu::ShareSpace(const std::string& space_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  CluModelService* m = window_->model();
  const SpaceEntry* space = spaces ? spaces->list().FindSpace(space_id) : nullptr;
  if (!space || !m) {
    return;
  }
  const CluTabTree& tree = m->tree();
  std::vector<std::string> lines = {space->name, std::string()};
  constexpr size_t kMaxLinks = 500;
  const std::vector<std::string>& roots = tree.Pinned(space_id).empty()
                                              ? tree.Today(space_id)
                                              : tree.Pinned(space_id);
  for (const std::string& root : roots) {
    for (const std::string& id : tree.TabsUnder(root)) {
      const TabNode* node = tree.Get(id);
      if (!node || lines.size() >= kMaxLinks + 2) {
        continue;
      }
      const std::string url = node->pinned_url.value_or(node->url);
      const std::string title = node->title_override.value_or(node->title);
      lines.push_back(title.empty() || title == url
                          ? url
                          : base::StrCat({title, " — ", url}));
    }
  }
#if BUILDFLAG(IS_MAC)
  views::WebView* frame = window_->shell().frame_view();
  const gfx::Rect at(12, 44, std::max(0, window_->shell().chrome_state().sidebar_width - 24), 28);
  if (frame && ShowSharePicker(frame->web_contents(), at,
                               base::UTF8ToUTF16(base::JoinString(lines, "\n")))) {
    return;
  }
#endif
  window_->ShowToast("Couldn't share this space", std::nullopt,
                     base::Milliseconds(1500));
}

void CluSpaceMenu::ExportSpace(const std::string& space_id,
                               const std::string& target_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  CluModelService* m = window_->model();
  const SpaceEntry* space = spaces ? spaces->list().FindSpace(space_id) : nullptr;
  const SpaceEntry* target = spaces ? spaces->list().FindSpace(target_id) : nullptr;
  if (!space || !target || !m || !m->FindSpace(space_id) ||
      !m->FindSpace(target_id) || space_id == target_id) {
    return;
  }
  CluTabTree& tree = m->mutable_tree();
  const int64_t now = m->NowMs();
  base::expected<std::string, TreeError> folder =
      tree.CreateFolder(target_id, std::string(), space->name, std::nullopt, now);
  if (!folder.has_value()) {
    window_->ShowToast(std::string(TreeErrorText(folder.error())), std::nullopt,
                       base::Milliseconds(1500));
    return;
  }
  // Rows the tree refuses (a folder too deep, a URL the policy blocks) are left out; the rest still land.
  auto copy = [&](auto& self, std::vector<std::string> ids,
                  const std::string& parent) -> void {
    for (const std::string& id : ids) {
      const TabNode* node = tree.Get(id);
      if (!node) {
        continue;
      }
      if (node->is_folder()) {
        base::expected<std::string, TreeError> sub = tree.CreateFolder(
            target_id, parent, node->title_override.value_or(std::string()),
            std::nullopt, now);
        if (sub.has_value()) {
          self(self, tree.Children(id), sub.value());
        }
        continue;
      }
      NewTab tab;
      tab.url = node->pinned_url.value_or(node->url);
      tab.title = node->title_override.value_or(node->title);
      std::ignore = tree.InsertTab(
          tab,
          DropTarget::Pinned(target_id, parent,
                             static_cast<int>(tree.Children(parent).size())),
          now);
    }
  };
  copy(copy, tree.Pinned(space_id), folder.value());
  tree.MutableSpaceState(target_id).pinned_collapsed = false;
  m->Commit();
  window_->ShowToast(base::StrCat({"Exported to ", target->name}), std::nullopt,
                     base::Milliseconds(1500));
}

std::vector<MenuItem> CluSpaceMenu::BuildOverflow() {
  CluSpacesService* spaces = CluSpacesService::Get();
  std::vector<MenuItem> items;
  if (!spaces || window_->is_private()) {
    return items;
  }
  for (const SpaceEntry& space : spaces->list().spaces()) {
    // The menu draws no check state, so the shown space carries a mark in its label.
    const std::string label = SpaceLabel(space);
    items.push_back(MenuItem::Item(
        space.id, space.id == window_->space_id()
                      ? base::StrCat({"✓  ", label})
                      : label));
  }
  return items;
}

void CluSpaceMenu::RunIconItem(const std::string& space_id,
                               const std::string& picked) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces) {
    return;
  }
  const int index = spaces->list().IndexOf(space_id);
  if (index < 0) {
    return;
  }
  if (picked == kEdit) {
    window_->OpenSpaceEditor(space_id, "space");
  } else if (picked == kMoveLeft && index > 0) {
    std::ignore = spaces->ReorderSpace(space_id, index - 1);
  } else if (picked == kMoveRight) {
    std::ignore = spaces->ReorderSpace(space_id, index + 1);
  } else if (picked == kDelete) {
    space_actions::ConfirmDeleteSpace(&window_.get(), space_id);
  }
}

void CluSpaceMenu::RunOverflowItem(const std::string& picked) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (spaces && spaces->list().FindSpace(picked)) {
    space_actions::SwitchWindowToSpace(&window_.get(), picked,
                                       /*close_source=*/false);
  }
}

void CluSpaceMenu::OpenIconMenu(const std::string& space_id,
                                const gfx::Rect& anchor) {
  Show(BuildIconMenu(space_id), anchor,
       base::BindOnce(
           [](base::WeakPtr<CluSpaceMenu> self, std::string space_id,
              const std::string& picked) {
             if (self) {
               self->RunIconItem(space_id, picked);
             }
           },
           weak_ptr_factory_.GetWeakPtr(), space_id));
}

void CluSpaceMenu::OpenOverflow(const gfx::Rect& anchor) {
  Show(BuildOverflow(), anchor,
       base::BindOnce(
           [](base::WeakPtr<CluSpaceMenu> self, const std::string& picked) {
             if (self) {
               self->RunOverflowItem(picked);
             }
           },
           weak_ptr_factory_.GetWeakPtr()));
}

void CluSpaceMenu::Show(const std::vector<MenuItem>& items,
                        const gfx::Rect& anchor,
                        base::OnceCallback<void(const std::string&)> on_pick) {
  views::WebView* frame = window_->shell().frame_view();
  if (items.empty() || !frame || !frame->GetWidget() ||
      (runner_ && runner_->IsRunning())) {
    return;
  }
  // The old runner points at the old model: it goes first.
  runner_.reset();
  model_ = std::make_unique<ui::SimpleMenuModel>(this);
  command_ids_.clear();
  picked_.reset();
  on_pick_ = std::move(on_pick);
  for (const MenuItem& item : items) {
    if (item.separator) {
      model_->AddSeparator(ui::NORMAL_SEPARATOR);
      continue;
    }
    const int command_id = static_cast<int>(command_ids_.size()) + 1;
    command_ids_[command_id] = item.id;
    model_->AddItem(command_id, base::UTF8ToUTF16(item.label));
    if (const std::optional<size_t> index =
            model_->GetIndexOfCommandId(command_id)) {
      model_->SetEnabledAt(*index, item.enabled);
    }
  }
  gfx::Rect bounds = anchor;
  views::View::ConvertRectToScreen(frame, &bounds);
  runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&CluSpaceMenu::OnMenuClosed,
                          weak_ptr_factory_.GetWeakPtr()));
#if BUILDFLAG(IS_MAC)
  // Light on a light space, dark gray only on a dark one.
  const ScopedMenuAppearance appearance(frame->GetWidget(),
                                        window_->menu_light());
#endif
  runner_->RunMenuAt(frame->GetWidget(), /*button_controller=*/nullptr, bounds,
                     views::MenuAnchorPosition::kTopLeft,
                     ui::mojom::MenuSourceType::kMouse);
}

void CluSpaceMenu::ExecuteCommand(int command_id, int event_flags) {
  const auto it = command_ids_.find(command_id);
  if (it != command_ids_.end()) {
    picked_ = it->second;
  }
}

void CluSpaceMenu::OnMenuClosed() {
  // The pick runs after the native menu's loop has returned.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<CluSpaceMenu> self) {
                       if (!self) {
                         return;
                       }
                       std::optional<std::string> picked =
                           std::move(self->picked_);
                       self->picked_.reset();
                       base::OnceCallback<void(const std::string&)> on_pick =
                           std::move(self->on_pick_);
                       if (picked && on_pick) {
                         std::move(on_pick).Run(*picked);
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr()));
}

}  // namespace clu
