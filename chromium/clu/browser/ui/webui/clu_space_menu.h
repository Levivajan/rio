// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_SPACE_MENU_H_
#define CLU_BROWSER_UI_WEBUI_CLU_SPACE_MENU_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "clu/browser/model/clu_menu_items.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"

namespace views {
class MenuRunner;
}  // namespace views

namespace clu {

class CluWindowState;

// The space switcher's native menus (docs/plan/03-spaces.md T5, PLAN 8.2): an icon's Edit Space…, Move Left, Move
// Right and Delete Space… (with a confirmation), and the overflow button's list of every space. Build*() makes the
// items and Run*() does what one says; the browser tests call those directly, since a native menu cannot be clicked
// from a test.
class CluSpaceMenu : public ui::SimpleMenuModel::Delegate {
 public:
  static constexpr char kEdit[] = "edit";
  static constexpr char kMoveLeft[] = "left";
  static constexpr char kMoveRight[] = "right";
  static constexpr char kDelete[] = "delete";
  static constexpr char kIcon[] = "space.icon";
  static constexpr char kRename[] = "space.rename";
  static constexpr char kTheme[] = "space.theme";
  static constexpr char kProfile[] = "space.profile";
  // Set Profile ▸ <profile>: "space.profile:<profile id>".
  static constexpr char kProfilePrefix[] = "space.profile:";
  static constexpr char kNewFolder[] = "space.newFolder";
  // Live Folders ▸ <kind>: "space.live:feed", "space.live:githubReleases", "space.live:githubCommits".
  static constexpr char kLive[] = "space.live";
  static constexpr char kLivePrefix[] = "space.live:";
  static constexpr char kManage[] = "space.manage";
  static constexpr char kShare[] = "space.share";
  static constexpr char kExport[] = "space.export";
  // Export "<name>" into Folder ▸ <space>: "space.export:<space id>".
  static constexpr char kExportPrefix[] = "space.export:";

  explicit CluSpaceMenu(CluWindowState& window);
  CluSpaceMenu(const CluSpaceMenu&) = delete;
  CluSpaceMenu& operator=(const CluSpaceMenu&) = delete;
  ~CluSpaceMenu() override;

  // Empty for an unknown space or a private window. Move Left and Right are off at the ends; Delete is off for the
  // only space.
  std::vector<MenuItem> BuildIconMenu(const std::string& space_id);
  // The space header's and switcher icon's menu:
  // Change Space Icon…, Rename Space…, Edit Theme Color…, Set Profile ▸ (the space's profile checked), New Folder,
  // Share Space…, Export "<name>" into Folder ▸ <other spaces>, Manage Spaces…, Delete Space. Opened through
  // tabs.openMenu ("space"), so New Folder and Rename Space… answer with what to rename.
  std::vector<MenuItem> BuildSpaceMenu(const std::string& space_id);
  // Runs a pick of BuildSpaceMenu(); answers the new folder's id (New Folder) or the space's (Rename Space…).
  std::optional<std::string> RunSpaceItem(const std::string& space_id,
                                          const std::string& picked);
  // Every space in switcher order (its emoji, then its name); the one this window shows is marked.
  std::vector<MenuItem> BuildOverflow();
  // The menu was open a while: the space may be gone or have moved, so each pick checks again.
  void RunIconItem(const std::string& space_id, const std::string& picked);
  void RunOverflowItem(const std::string& picked);

  // Shows the menu at `anchor` (frame WebUI coordinates); the pick runs once it closes.
  void OpenIconMenu(const std::string& space_id, const gfx::Rect& anchor);
  void OpenOverflow(const gfx::Rect& anchor);

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // Share Space…: the space's name and its pinned links (its Today links when nothing is pinned), one per line, to
  // macOS's share picker.
  void ShareSpace(const std::string& space_id);
  // Export "<name>" into Folder ▸: a folder named after the space at the end of `target_id`'s pinned rows, holding a
  // copy of the space's pinned rows and folders. The space keeps its own.
  void ExportSpace(const std::string& space_id, const std::string& target_id);
  void Show(const std::vector<MenuItem>& items,
            const gfx::Rect& anchor,
            base::OnceCallback<void(const std::string&)> on_pick);
  void OnMenuClosed();

  const raw_ref<CluWindowState> window_;
  std::unique_ptr<ui::SimpleMenuModel> model_;
  std::map<int, std::string> command_ids_;
  std::unique_ptr<views::MenuRunner> runner_;
  std::optional<std::string> picked_;
  base::OnceCallback<void(const std::string&)> on_pick_;
  base::WeakPtrFactory<CluSpaceMenu> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_SPACE_MENU_H_
