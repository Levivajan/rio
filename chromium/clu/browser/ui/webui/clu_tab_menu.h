// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_TAB_MENU_H_
#define CLU_BROWSER_UI_WEBUI_CLU_TAB_MENU_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "clu/browser/model/clu_menu_items.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"

class GURL;

namespace views {
class MenuRunner;
}  // namespace views

namespace clu {

class CluWindowState;

// The sidebar menus of PLAN 8.2 that tabs owns, as native menus (docs/chromium/PLAN.md 1.3: "Row and empty-area
// context menus are native NSMenus built in C++ from the 8.2 table"): the row, folder, multi-select, empty-area and
// + menus, with Move To in two submenu levels. Build() makes the items (clu_menu_items.h), Open() shows them at the
// sidebar's anchor, and Run() does what an item says; the browser tests call Build() and Run() directly, since a
// native menu cannot be clicked from a test.
class CluTabMenu : public ui::SimpleMenuModel::Delegate {
 public:
  explicit CluTabMenu(CluWindowState& window);
  CluTabMenu(const CluTabMenu&) = delete;
  CluTabMenu& operator=(const CluTabMenu&) = delete;
  ~CluTabMenu() override;

  struct Request {
    Request();
    Request(const Request&);
    Request& operator=(const Request&);
    ~Request();

    // "row", "empty", "new" or "more".
    std::string menu;
    std::optional<std::string> node_id;
    // Two or more: the multi-select menu.
    std::vector<std::string> tab_ids;
  };

  // The menu `request` asks for; empty when nothing applies (an unknown row).
  std::vector<MenuItem> Build(const Request& request, bool can_paste);
  // Runs `picked` when it is an enabled leaf of the menu Build() makes. Returns the row the sidebar puts into inline
  // rename (Rename, New Folder, New Folder Inside). `clipboard_text` is what Paste and Go opens.
  std::optional<std::string> Run(const Request& request,
                                 const std::string& picked,
                                 const std::string& clipboard_text);
  // Shows the menu at `anchor` (frame WebUI coordinates) and answers `done` once it closes.
  void Open(const std::string& menu,
            const std::optional<std::string>& node_id,
            const std::vector<std::string>& tab_ids,
            const gfx::Rect& anchor,
            base::OnceCallback<void(std::optional<std::string>)> done);

  // A multi-select action on `ids` (tabs.bulk and the multi-select menu): kMenuBulkArchive, kMenuBulkClose,
  // kMenuBulkCopy, kMenuBulkCopyMarkdown, or a kMenuBulkMoveTo pick.
  void RunBulk(const std::vector<std::string>& ids, const std::string& picked);

  // Paste and Go (src/main/tabs/menus.ts pasteTarget): a URL as pasted, a bare host with https://, else a search.
  static GURL PasteTarget(const std::string& text, const GURL& search_url);

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  // The key an item with MenuItem::keys shows: its key table command's accelerator.
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  void Show(const Request& request,
            const gfx::Rect& anchor,
            std::string clipboard_text);
  ui::SimpleMenuModel* AddModel(const std::vector<MenuItem>& items);
  void OnMenuClosed();
  std::vector<MoveToSpace> Destinations();
  // A private window's row menu (PrivateRowMenuItems()), and what its items do: the window keeps no rows, so they act
  // on the page itself.
  std::vector<MenuItem> BuildPrivate(const Request& request);
  std::optional<std::string> RunPrivate(const Request& request,
                                        const std::string& picked);
  std::optional<std::string> RunTabItem(const std::string& tab_id,
                                        const std::string& picked);
  std::optional<std::string> RunBulkItem(const std::vector<std::string>& ids,
                                         const std::string& picked);
  DropTarget TargetOf(const MoveToPick& pick);
  // Move To: in place, or into a space of another profile after the window asks (space_actions::MoveRows()).
  void MoveAnywhere(const std::vector<std::string>& ids, const MoveToPick& pick);
  // The folder menu's Share Folder…, Duplicate and Turn "name" into Space.
  void ShareFolder(const std::string& folder_id);
  void DuplicateFolder(const std::string& folder_id);
  void FolderToSpace(const std::string& folder_id);
  void CopyPinned(const std::vector<std::string>& ids,
                  const std::string& space_id,
                  const std::string& parent);
  void CopyText(const std::u16string& text, const std::string& toast);

  const raw_ref<CluWindowState> window_;
  std::vector<std::unique_ptr<ui::SimpleMenuModel>> models_;
  std::map<int, std::string> command_ids_;
  std::map<int, std::string> command_keys_;
  std::set<int> alternate_ids_;
  // Items drawn with a checkmark (MenuItem::checked).
  std::set<int> checked_ids_;
  std::unique_ptr<views::MenuRunner> runner_;
  std::optional<Request> open_request_;
  std::string open_clipboard_;
  std::optional<std::string> picked_;
  base::OnceCallback<void(std::optional<std::string>)> done_;
  base::WeakPtrFactory<CluTabMenu> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_TAB_MENU_H_
