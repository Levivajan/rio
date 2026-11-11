// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_tab_menu.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_page_state.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "build/build_config.h"
#include "clu/browser/ui/webui/clu_overlay_rects.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_space_menu.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/models/menu_separator_types.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "clu/browser/ui/webui/clu_menu_appearance.h"
#include "clu/browser/ui/webui/clu_share_services.h"
#endif

namespace clu {

namespace {

// The end of a list Clu cannot see (a space of another profile): Put() clamps it.
constexpr int kMaxSpaceListIndex = 1 << 20;

constexpr size_t kMaxPaste = 32 * 1024;
constexpr size_t kMaxSearch = 2048;

bool IsHostChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '.' || c == ':';
}

}  // namespace

CluTabMenu::Request::Request() = default;
CluTabMenu::Request::Request(const Request&) = default;
CluTabMenu::Request& CluTabMenu::Request::operator=(const Request&) = default;
CluTabMenu::Request::~Request() = default;

CluTabMenu::CluTabMenu(CluWindowState& window) : window_(window) {}

CluTabMenu::~CluTabMenu() = default;

// static
GURL CluTabMenu::PasteTarget(const std::string& text, const GURL& search_url) {
  const std::string input(
      base::TrimWhitespaceASCII(text.substr(0, kMaxPaste), base::TRIM_ALL));
  if (input.empty()) {
    return GURL();
  }
  const GURL direct(input);
  if (direct.is_valid() && direct.SchemeIsHTTPOrHTTPS()) {
    return direct;
  }
  // A bare host (example.com, example.com:8080/path) gets https://.
  const size_t path = input.find('/');
  const std::string host = input.substr(0, path);
  const bool bare_host =
      input.find_first_of(" \t\r\n") == std::string::npos &&
      host.find('.') != std::string::npos && host.front() != '.' &&
      host.back() != '.' && std::ranges::all_of(host, IsHostChar);
  if (bare_host) {
    const GURL with_scheme(base::StrCat({"https://", input}));
    if (with_scheme.is_valid()) {
      return with_scheme;
    }
  }
  return search_url;
}

std::vector<MoveToSpace> CluTabMenu::Destinations() {
  std::vector<MoveToSpace> spaces;
  CluModelService* m = window_->model();
  if (!m) {
    return spaces;
  }
  auto add = [&](const std::string& id, const std::string& name) {
    MoveToSpace space;
    space.id = id;
    space.name = name;
    // Folders of this profile's spaces; a space of another profile offers its Pinned and Today (M4).
    if (m->FindSpace(id)) {
      for (const std::string& node_id : m->tree().Pinned(id)) {
        const TabNode* node = m->tree().Get(node_id);
        if (node && node->is_folder()) {
          space.folders.emplace_back(
              node_id, node->title_override.value_or(kDefaultFolderName));
        }
      }
    }
    spaces.push_back(std::move(space));
  };
  // Every space in switcher order, across profiles.
  if (CluSpacesService* service = CluSpacesService::Get()) {
    for (const SpaceEntry& entry : service->list().spaces()) {
      add(entry.id, entry.name);
    }
    return spaces;
  }
  for (const SpaceRow& row : m->spaces()) {
    add(row.id, row.name);
  }
  return spaces;
}

std::vector<MenuItem> CluTabMenu::Build(const Request& request,
                                        bool can_paste) {
  // The narrow top row's "...": every window with a sidebar has it, a private one too.
  if (request.menu == "more") {
    return MoreMenuItems();
  }
  if (request.menu == "space") {
    return request.node_id ? window_->space_menu().BuildSpaceMenu(*request.node_id)
                           : std::vector<MenuItem>();
  }
  CluModelService* m = window_->model();
  if (!m) {
    return BuildPrivate(request);
  }
  const CluTabTree& tree = m->tree();
  if (request.menu == "empty") {
    return EmptyMenuItems(can_paste);
  }
  if (request.menu == "new") {
    return NewMenuItems();
  }
  if (request.menu != "row") {
    return {};
  }
  if (request.tab_ids.size() >= 2) {
    size_t live = 0;
    size_t incoming = 0;
    // Add to Split View takes tabs of the space the window shows (favorites belong to every space), as split.create
    // does.
    bool same_space = true;
    for (const std::string& id : request.tab_ids) {
      const TabNode* node = tree.Get(id);
      if (node && node->is_tab()) {
        ++live;
        if (node->section != Section::kFavorite) {
          ++incoming;
          same_space = same_space && node->space_id == window_->space_id();
        }
      }
    }
    if (live < 2) {
      return {};
    }
    return MultiMenuItems(
        live, Destinations(),
        tree.Favorites().size() + incoming > static_cast<size_t>(kMaxFavorites),
        /*can_split=*/same_space);
  }
  const TabNode* node = request.node_id ? tree.Get(*request.node_id) : nullptr;
  if (!node) {
    return {};
  }
  if (node->is_folder()) {
    return FolderMenuItems(*node, tree.Depth(node->id) < kMaxFolderDepth,
                           !tree.TabsUnder(node->id).empty(), Destinations());
  }
  RowMenuInput input;
  input.node = *node;
  std::string title = node->title;
  if (content::WebContents* contents = m->FindContents(node->id)) {
    input.node.url = PageUrl(contents);
    input.audible = contents->IsCurrentlyAudible();
    title = base::UTF16ToUTF8(contents->GetTitle());
  }
  input.has_unread = UnreadCount(title) > 0;
  input.off_pinned = IsOffPinned(input.node.url, node->pinned_url);
  input.unread_badge = m->UnreadBadgeShown(node->id);
#if BUILDFLAG(IS_MAC)
  input.share_services = ShareServiceNames(GURL(input.node.url));
#endif
  input.spaces = Destinations();
  input.favorites_full =
      tree.Favorites().size() >= static_cast<size_t>(kMaxFavorites);
  // Today rows next to the row: its own list for a Today row, the window's space for an anchored one.
  const std::vector<std::string>& today = tree.Today(
      node->section == Section::kToday ? node->space_id : window_->space_id());
  const auto at = std::ranges::find(today, node->id);
  const size_t index = static_cast<size_t>(at - today.begin());
  input.others = today.size() - (at != today.end() ? 1 : 0);
  if (at != today.end()) {
    input.above = index;
    input.below = today.size() - index - 1;
  }
  return RowMenuItems(input);
}

std::vector<MenuItem> CluTabMenu::BuildPrivate(const Request& request) {
  // One page's menu only: a Mini window has no sidebar, and private multi-select keeps Chromium's own commands.
  if (!window_->is_private() || request.menu != "row" || !request.node_id ||
      request.tab_ids.size() >= 2) {
    return {};
  }
  tabs::TabInterface* tab = window_->FindTab(*request.node_id);
  if (!tab) {
    return {};
  }
  content::WebContents* contents = tab->GetContents();
  return PrivateRowMenuItems(contents->GetLastCommittedURL().spec(),
                             contents->IsAudioMuted());
}

std::optional<std::string> CluTabMenu::RunPrivate(const Request& request,
                                                  const std::string& picked) {
  if (!std::ranges::contains(EnabledLeaves(BuildPrivate(request)), picked)) {
    return std::nullopt;
  }
  const std::string tab_id = *request.node_id;
  tabs::TabInterface* tab = window_->FindTab(tab_id);
  if (!tab) {
    return std::nullopt;
  }
  if (picked == kMenuRename) {
    return tab_id;
  }
  if (picked == kMenuCopyLink) {
    // Marked off the record, as every private copy is.
    window_->CopyUrl(tab_id, /*markdown=*/false);
  } else if (picked == kMenuDuplicate) {
    // Chromium's own duplicate, as tabs.duplicate in a private window.
    const std::optional<int> index = window_->IndexOf(tab);
    if (index && chrome::CanDuplicateTabAt(window_->browser(), *index)) {
      chrome::DuplicateTabAt(window_->browser(), *index);
    }
  } else if (picked == kMenuMute) {
    window_->SetRowMuted(tab_id, !tab->GetContents()->IsAudioMuted());
  } else if (picked == kMenuClose) {
    window_->CloseRowCommand(tab_id);
  }
  return std::nullopt;
}

DropTarget CluTabMenu::TargetOf(const MoveToPick& pick) {
  CluModelService* m = window_->model();
  const CluTabTree& tree = m->tree();
  // A space of another profile: the end of its Pinned, or the top of its Today (the list clamps the index).
  const bool own = m->FindSpace(pick.space_id) != nullptr;
  switch (pick.kind) {
    case MoveToPick::Kind::kFavorite:
      return DropTarget::Favorite(static_cast<int>(tree.Favorites().size()));
    case MoveToPick::Kind::kToday:
      return DropTarget::Today(pick.space_id, 0);
    case MoveToPick::Kind::kFolder:
      return DropTarget::Pinned(
          pick.space_id, pick.folder_id,
          static_cast<int>(tree.Children(pick.folder_id).size()));
    case MoveToPick::Kind::kPinned:
      return DropTarget::Pinned(
          pick.space_id, std::string(),
          own ? static_cast<int>(tree.Pinned(pick.space_id).size())
              : kMaxSpaceListIndex);
  }
}

void CluTabMenu::CopyText(const std::u16string& text,
                          const std::string& toast) {
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(text);
  }
  window_->ShowToast(toast, std::nullopt, base::Milliseconds(1500));
}

std::optional<std::string> CluTabMenu::Run(const Request& request,
                                           const std::string& picked,
                                           const std::string& clipboard_text) {
  if (request.menu == "space") {
    return request.node_id
               ? window_->space_menu().RunSpaceItem(*request.node_id, picked)
               : std::nullopt;
  }
  // "..." (Reload, Toggle Sidebar) and the + menu's New Space and New Split run the key table's commands.
  if (request.menu == "more" || picked == kMenuNewSpace ||
      picked == kMenuNewSplit) {
    if (std::ranges::contains(EnabledLeaves(Build(request, false)), picked)) {
      window_->RunCommand(picked);
    }
    return std::nullopt;
  }
  CluModelService* m = window_->model();
  if (!m) {
    return RunPrivate(request, picked);
  }
  // Only an enabled leaf of the menu that would show runs.
  const std::vector<MenuItem> items =
      Build(request, !clipboard_text.empty() || request.menu != "empty");
  if (!std::ranges::contains(EnabledLeaves(items), picked)) {
    return std::nullopt;
  }
  CluTabTree& tree = m->mutable_tree();
  const std::string& space_id = window_->space_id();

  if (picked == kMenuNewTab) {
    window_->OpenCommandBar(CommandBarMode::kNewTab);
    return std::nullopt;
  }
  if (picked == kMenuPasteAndGo) {
    GURL search;
    if (TemplateURLService* service =
            TemplateURLServiceFactory::GetForProfile(window_->profile())) {
      if (const TemplateURL* engine = service->GetDefaultSearchProvider()) {
        search = GURL(engine->url_ref().ReplaceSearchTerms(
            TemplateURLRef::SearchTermsArgs(base::UTF8ToUTF16(
                clipboard_text.substr(0, kMaxSearch))),
            service->search_terms_data()));
      }
    }
    const GURL url = PasteTarget(clipboard_text, search);
    if (url.is_valid()) {
      std::ignore = window_->CreateRow(url, std::nullopt, /*activate=*/true);
    }
    return std::nullopt;
  }
  if (picked == kMenuNewFolder) {
    base::expected<std::string, TreeError> folder = tree.CreateFolder(
        space_id, std::string(), std::string(), std::nullopt, m->NowMs());
    if (!folder.has_value()) {
      return std::nullopt;
    }
    // The new folder must be visible to rename it.
    tree.MutableSpaceState(space_id).pinned_collapsed = false;
    m->Commit();
    return folder.value();
  }
  if (request.tab_ids.size() >= 2) {
    std::vector<std::string> ids;
    for (const std::string& id : request.tab_ids) {
      const TabNode* node = tree.Get(id);
      if (node && node->is_tab() && !std::ranges::contains(ids, id)) {
        ids.push_back(id);
      }
    }
    return RunBulkItem(ids, picked);
  }
  const TabNode* node = request.node_id ? tree.Get(*request.node_id) : nullptr;
  if (!node) {
    return std::nullopt;
  }
  const std::string node_id = node->id;
  if (picked == kMenuRename) {
    return node_id;
  }
  if (picked == kMenuSetIcon) {
    window_->OpenPrompt(
        "tabs.setIcon",
        base::DictValue()
            .Set("nodeId", node_id)
            .Set("icon", node->custom_icon ? base::Value(*node->custom_icon)
                                           : base::Value()));
    return std::nullopt;
  }
  if (picked == kMenuRemoveIcon) {
    tree.Mutable(node_id)->custom_icon.reset();
    m->Commit();
    return std::nullopt;
  }
  if (node->is_folder()) {
    if (picked == kMenuFolderCopyLinks ||
        picked == kMenuFolderCopyLinksMarkdown) {
      RunBulk(tree.TabsUnder(node_id), picked == kMenuFolderCopyLinks
                                           ? kMenuBulkCopy
                                           : kMenuBulkCopyMarkdown);
      return std::nullopt;
    }
    if (picked == kMenuDeleteFolder) {
      if (tree.DeleteFolder(node_id, m->NowMs()).has_value()) {
        m->Commit();
      }
      return std::nullopt;
    }
    if (picked == kMenuFolderShare) {
      ShareFolder(node_id);
      return std::nullopt;
    }
    if (picked == kMenuFolderDuplicate) {
      DuplicateFolder(node_id);
      return std::nullopt;
    }
    if (picked == kMenuFolderToSpace) {
      FolderToSpace(node_id);
      return std::nullopt;
    }
    if (picked == kMenuMoveToNewSpace) {
      window_->RunCommand(kMenuNewSpace);
      return std::nullopt;
    }
    if (const std::optional<MoveToPick> pick = ParseMoveTo(picked, kMenuMoveTo)) {
      // The checked space is the folder's own: it stays where it is.
      if (pick->space_id != node->space_id) {
        MoveAnywhere({node_id}, *pick);
      }
      return std::nullopt;
    }
    if (picked == kMenuNewFolderInside) {
      const std::string folder_space = node->space_id;
      base::expected<std::string, TreeError> folder = tree.CreateFolder(
          folder_space, node_id, std::string(), std::nullopt, m->NowMs());
      if (!folder.has_value()) {
        return std::nullopt;
      }
      tree.Mutable(node_id)->collapsed = false;
      m->Commit();
      return folder.value();
    }
    return std::nullopt;
  }
  return RunTabItem(node_id, picked);
}

std::optional<std::string> CluTabMenu::RunTabItem(const std::string& tab_id,
                                                  const std::string& picked) {
  CluModelService* m = window_->model();
  const TabNode* node = m->tree().Get(tab_id);
  if (!node) {
    return std::nullopt;
  }
  content::WebContents* contents = m->FindContents(tab_id);
  const std::string url = contents ? PageUrl(contents) : node->url;
  if (picked == kMenuCopyLink) {
    // Rio's own pages copy as their rio:// URL, which the command bar opens.
    CopyText(base::UTF8ToUTF16(DisplayUrl(StripTracking(url))), "Copied link");
  } else if (picked == kMenuOpenInSplit) {
    // Beside the page on screen; the row on screen just shows.
    std::ignore = window_->multitask().OpenRowInSplit(tab_id);
  } else if (picked == kMenuDuplicate) {
    window_->DuplicateRow(tab_id);
  } else if (picked == kMenuMute) {
    window_->SetRowMuted(tab_id, !node->muted);
  } else if (picked == kMenuUnreadBadge) {
    std::ignore = m->SetUnreadBadgeShown(tab_id, !m->UnreadBadgeShown(tab_id));
  } else if (picked == kMenuCustomize) {
    // Site tweaks act on the page on screen: the row's page comes forward first.
    if (window_->ActivateRow(tab_id)) {
      window_->RunCommand("tweaks.open");
    }
  } else if (picked == kMenuPin) {
    if (node->section == Section::kToday) {
      std::ignore = window_->TogglePin(tab_id);
    } else {
      // Remove Pin closes the pinned tab into the archive; Cmd+D
      // and Move to still move it to Today.
      m->ArchiveRows({tab_id}, ArchiveReason::kClose);
    }
  } else if (picked == kMenuFavorite) {
    if (node->section == Section::kFavorite) {
      std::ignore = window_->TogglePin(tab_id);
    } else {
      std::ignore = window_->MoveRows({tab_id},
                        TargetOf({.kind = MoveToPick::Kind::kFavorite}),
                        /*toast=*/true);
    }
  } else if (picked == kMenuPinnedUrlCurrent) {
    if (IsRestorableTabUrl(url)) {
      m->mutable_tree().Mutable(tab_id)->pinned_url = url;
      m->Commit();
    }
  } else if (picked == kMenuPinnedUrlEnter) {
    // F2: a typed pinned URL goes through the prompt, which calls tabs.setPinnedUrl (the policy runs there again).
    window_->OpenPrompt("tabs.pinnedUrl",
                        base::DictValue()
                            .Set("tabId", tab_id)
                            .Set("url", DisplayUrl(node->pinned_url.value_or(url))));
  } else if (picked == kMenuToNewWindow) {
    window_->MoveRowToNewWindow(tab_id);
  } else if (picked == kMenuArchive) {
    m->ArchiveRows({tab_id}, ArchiveReason::kClose);
  } else if (picked == kMenuArchiveOthers || picked == kMenuArchiveAbove ||
             picked == kMenuArchiveBelow) {
    const std::vector<std::string> today = m->tree().Today(
        node->section == Section::kToday ? node->space_id : window_->space_id());
    const auto at = std::ranges::find(today, tab_id);
    std::vector<std::string> ids;
    for (auto it = today.begin(); it != today.end(); ++it) {
      if (*it == tab_id) {
        continue;
      }
      const bool above = at != today.end() && it < at;
      const bool below = at != today.end() && it > at;
      if (picked == kMenuArchiveOthers || (picked == kMenuArchiveAbove && above) ||
          (picked == kMenuArchiveBelow && below)) {
        ids.push_back(*it);
      }
    }
    m->ArchiveRows(ids, ArchiveReason::kClose);
  } else if (picked == kMenuClose) {
    window_->CloseRowCommand(tab_id);
  } else if (picked == kMenuMoveToNewSpace) {
    window_->RunCommand(kMenuNewSpace);
  } else if (picked.starts_with(kMenuShareService)) {
#if BUILDFLAG(IS_MAC)
    size_t index = 0;
    if (base::StringToSizeT(
            std::string_view(picked).substr(sizeof(kMenuShareService) - 1),
            &index)) {
      const std::string title = node->title_override.value_or(
          contents ? base::UTF16ToUTF8(contents->GetTitle()) : node->title);
      ShareWithService(index, GURL(StripTracking(url)),
                       base::UTF8ToUTF16(title));
    }
#endif
  } else if (const std::optional<MoveToPick> pick =
                 ParseMoveTo(picked, kMenuMoveTo)) {
    // The checked space is the row's own: it stays where it is. A favorite has none checked.
    if (node->section == Section::kFavorite ||
        pick->space_id != node->space_id) {
      MoveAnywhere({tab_id}, *pick);
    }
  }
  return std::nullopt;
}

std::optional<std::string> CluTabMenu::RunBulkItem(
    const std::vector<std::string>& ids,
    const std::string& picked) {
  RunBulk(ids, picked);
  return std::nullopt;
}

void CluTabMenu::RunBulk(const std::vector<std::string>& ids,
                         const std::string& picked) {
  CluModelService* m = window_->model();
  if (!m || ids.empty()) {
    return;
  }
  if (picked == kMenuBulkArchive) {
    // One batch: one closed-stack entry brings them all back.
    m->ArchiveRows(ids, ArchiveReason::kClose);
  } else if (picked == kMenuBulkClose) {
    // F9 per row: Today rows archive in one batch, anchored rows reset to their pinned URL.
    std::vector<std::string> today;
    std::vector<std::string> anchored;
    for (const std::string& id : ids) {
      const TabNode* node = m->tree().Get(id);
      if (node && node->is_tab()) {
        (node->anchored() ? anchored : today).push_back(id);
      }
    }
    m->ArchiveRows(today, ArchiveReason::kClose);
    for (const std::string& id : anchored) {
      m->CloseRow(id, std::nullopt);
    }
  } else if (picked == kMenuBulkCopy || picked == kMenuBulkCopyMarkdown) {
    const bool markdown = picked == kMenuBulkCopyMarkdown;
    std::vector<std::string> lines;
    for (const std::string& id : ids) {
      const TabNode* node = m->tree().Get(id);
      if (!node || !node->is_tab()) {
        continue;
      }
      content::WebContents* contents = m->FindContents(id);
      const std::string url =
          StripTracking(contents ? PageUrl(contents) : node->url);
      std::string title = node->title_override.value_or(
          contents ? base::UTF16ToUTF8(contents->GetTitle()) : node->title);
      if (title.empty()) {
        title = DisplayHost(url);
      }
      lines.push_back(markdown ? MarkdownLink(title, url) : url);
    }
    const size_t n = lines.size();
    CopyText(base::UTF8ToUTF16(base::JoinString(lines, "\n")),
             base::StrCat({"Copied ", base::NumberToString(n),
                           n == 1 ? " link" : " links",
                           markdown ? " as Markdown" : ""}));
  } else if (picked == kMenuBulkSplit) {
    // The selected tabs become one split, shown with the first focused (more than two shows the "split full" toast).
    std::vector<std::string> tabs;
    for (const std::string& id : ids) {
      const TabNode* node = m->tree().Get(id);
      if (node && node->is_tab() && !std::ranges::contains(tabs, id)) {
        tabs.push_back(id);
      }
    }
    if (tabs.size() >= 2) {
      std::ignore =
          window_->multitask().CreateSplit(tabs, SplitDir::kRow, tabs.front());
    }
  } else if (const std::optional<MoveToPick> pick =
                 ParseMoveTo(picked, kMenuBulkMoveTo)) {
    MoveAnywhere(ids, *pick);
  }
}

void CluTabMenu::ShareFolder(const std::string& folder_id) {
  CluModelService* m = window_->model();
  const TabNode* folder = m ? m->tree().Get(folder_id) : nullptr;
  if (!folder) {
    return;
  }
  const CluTabTree& tree = m->tree();
  std::vector<std::string> lines = {
      folder->title_override.value_or("Untitled"), std::string()};
  constexpr size_t kMaxLinks = 500;
  for (const std::string& id : tree.TabsUnder(folder_id)) {
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
#if BUILDFLAG(IS_MAC)
  views::WebView* frame = window_->shell().frame_view();
  const gfx::Rect at(
      12, 44,
      std::max(0, window_->shell().chrome_state().sidebar_width - 24), 28);
  if (frame &&
      ShowSharePicker(frame->web_contents(), at,
                      base::UTF8ToUTF16(base::JoinString(lines, "\n")))) {
    return;
  }
#endif
  window_->ShowToast("Couldn't share this folder", std::nullopt,
                     base::Milliseconds(1500));
}

// Copies `ids` (tabs and folders, recursively) into `parent` of `space_id`'s pinned tree; rows the tree refuses (a
// folder too deep, a URL the policy blocks) are left out.
void CluTabMenu::CopyPinned(const std::vector<std::string>& ids,
                            const std::string& space_id,
                            const std::string& parent) {
  CluModelService* m = window_->model();
  CluTabTree& tree = m->mutable_tree();
  const int64_t now = m->NowMs();
  for (const std::string& id : ids) {
    const TabNode* node = tree.Get(id);
    if (!node) {
      continue;
    }
    if (node->is_folder()) {
      const std::vector<std::string> children = tree.Children(id);
      base::expected<std::string, TreeError> sub = tree.CreateFolder(
          space_id, parent, node->title_override.value_or(std::string()),
          std::nullopt, now);
      if (sub.has_value()) {
        CopyPinned(children, space_id, sub.value());
      }
      continue;
    }
    NewTab tab;
    tab.url = node->pinned_url.value_or(node->url);
    tab.title = node->title_override.value_or(node->title);
    std::ignore = tree.InsertTab(
        tab,
        DropTarget::Pinned(space_id, parent,
                           static_cast<int>(tree.Children(parent).size())),
        now);
  }
}

void CluTabMenu::DuplicateFolder(const std::string& folder_id) {
  CluModelService* m = window_->model();
  const TabNode* folder = m ? m->tree().Get(folder_id) : nullptr;
  if (!folder || !folder->is_folder()) {
    return;
  }
  CluTabTree& tree = m->mutable_tree();
  const std::string space_id = folder->space_id;
  const std::string parent = folder->parent_id;
  const std::string name = folder->title_override.value_or(std::string());
  const std::vector<std::string> children = tree.Children(folder_id);
  const std::vector<std::string> siblings =
      parent.empty() ? tree.Pinned(space_id) : tree.Children(parent);
  const auto at = std::ranges::find(siblings, folder_id);
  // The copy lands right after the folder.
  base::expected<std::string, TreeError> copy = tree.CreateFolder(
      space_id, parent, name,
      at == siblings.end() ? std::nullopt
                           : std::optional<int>(at - siblings.begin() + 1),
      m->NowMs());
  if (!copy.has_value()) {
    window_->ShowToast(std::string(TreeErrorText(copy.error())), std::nullopt,
                       base::Milliseconds(1500));
    return;
  }
  CopyPinned(children, space_id, copy.value());
  m->Commit();
}

void CluTabMenu::FolderToSpace(const std::string& folder_id) {
  CluModelService* m = window_->model();
  const TabNode* folder = m ? m->tree().Get(folder_id) : nullptr;
  if (!folder || !folder->is_folder()) {
    return;
  }
  const std::optional<std::string> name = folder->title_override;
  const std::vector<std::string> children = m->tree().Children(folder_id);
  // The new space shows in this window; the folder's rows become its Pinned and the emptied folder goes.
  base::expected<std::string, SpacesError> space = space_actions::CreateSpace(
      &window_.get(), name, std::nullopt, std::nullopt, std::nullopt);
  if (!space.has_value() || !m->FindSpace(space.value())) {
    window_->ShowToast("Couldn't make a space of this folder", std::nullopt,
                       base::Milliseconds(1500));
    return;
  }
  CluTabTree& tree = m->mutable_tree();
  if (!children.empty() &&
      !tree.MoveMany(children, DropTarget::Pinned(space.value(), "", 0),
                     m->NowMs())
           .has_value()) {
    m->Commit();
    return;
  }
  std::ignore = tree.DeleteFolder(folder_id, m->NowMs());
  m->Commit();
}

void CluTabMenu::MoveAnywhere(const std::vector<std::string>& ids,
                               const MoveToPick& pick) {
  CluModelService* m = window_->model();
  CluSpacesService* spaces = CluSpacesService::Get();
  const bool known = pick.kind == MoveToPick::Kind::kFavorite ||
                     (m && m->FindSpace(pick.space_id)) ||
                     (spaces && spaces->list().FindSpace(pick.space_id));
  if (!m || !known) {
    return;
  }
  // A space of another profile asks first (F11); a refused move shows its toast.
  space_actions::MoveRows(
      &window_.get(), ids, TargetOf(pick), /*confirmed=*/false, /*ask=*/true,
      base::BindOnce(
          [](base::WeakPtr<CluWindowState> window,
             base::expected<space_actions::MoveStatus, TreeError> result) {
            if (window && !result.has_value() &&
                result.error() != TreeError::kNotFound) {
              window->ShowToast(std::string(TreeErrorText(result.error())),
                                std::nullopt, base::Milliseconds(1500));
            }
          },
          window_->GetWeakPtr()));
}

ui::SimpleMenuModel* CluTabMenu::AddModel(const std::vector<MenuItem>& items) {
  models_.push_back(std::make_unique<ui::SimpleMenuModel>(this));
  ui::SimpleMenuModel* model = models_.back().get();
  for (const MenuItem& item : items) {
    if (item.separator) {
      model->AddSeparator(ui::NORMAL_SEPARATOR);
      continue;
    }
    const int command_id = static_cast<int>(command_ids_.size()) + 1;
    command_ids_[command_id] = item.id;
    const std::u16string label = base::UTF8ToUTF16(item.label);
    if (!item.submenu.empty()) {
      model->AddSubMenu(command_id, label, AddModel(item.submenu));
    } else if (item.checked) {
      model->AddCheckItem(command_id, label);
      checked_ids_.insert(command_id);
    } else {
      model->AddItem(command_id, label);
    }
    if (!item.keys.empty()) {
      command_keys_[command_id] = item.keys;
    }
    if (item.alternate) {
      alternate_ids_.insert(command_id);
    }
    if (const std::optional<size_t> index =
            model->GetIndexOfCommandId(command_id)) {
      model->SetEnabledAt(*index, item.enabled);
      // macOS context menus hide shortcuts unless asked; these show theirs, such as Copy Link's ⇧⌘C.
      if (!item.keys.empty()) {
        model->SetForceShowAcceleratorForItemAt(*index, true);
      }
    }
  }
  return model;
}

void CluTabMenu::Open(
    const std::string& menu,
    const std::optional<std::string>& node_id,
    const std::vector<std::string>& tab_ids,
    const gfx::Rect& anchor,
    base::OnceCallback<void(std::optional<std::string>)> done) {
  if (runner_ && runner_->IsRunning()) {
    std::move(done).Run(std::nullopt);
    return;
  }
  // A menu closed whose pick has not run yet: its caller gets its answer now, so no Mojo callback is dropped.
  if (done_) {
    open_request_.reset();
    picked_.reset();
    std::move(done_).Run(std::nullopt);
  }
  done_ = std::move(done);
  Request request;
  request.menu = menu;
  request.node_id = node_id;
  request.tab_ids = tab_ids;
  if (menu != "empty") {
    Show(request, anchor, std::string());
    return;
  }
  // Paste and Go is on only while the clipboard holds text.
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<CluTabMenu> self, Request request, gfx::Rect anchor,
             std::u16string text) {
            if (self) {
              self->Show(request, anchor, base::UTF16ToUTF8(text));
            }
          },
          weak_ptr_factory_.GetWeakPtr(), request, anchor));
}

void CluTabMenu::Show(const Request& request,
                      const gfx::Rect& anchor,
                      std::string clipboard_text) {
  const std::string trimmed(
      base::TrimWhitespaceASCII(clipboard_text, base::TRIM_ALL));
  const std::vector<MenuItem> items = Build(request, !trimmed.empty());
  views::WebView* frame = window_->shell().frame_view();
  if (items.empty() || !frame || !frame->GetWidget()) {
    if (done_) {
      std::move(done_).Run(std::nullopt);
    }
    return;
  }
  // The old runner points at the old models: it goes first.
  runner_.reset();
  models_.clear();
  command_ids_.clear();
  command_keys_.clear();
  alternate_ids_.clear();
  checked_ids_.clear();
  picked_.reset();
  open_request_ = request;
  open_clipboard_ = trimmed;
  // "…" and "+" are dark popovers with icons and shortcuts, drawn by
  // the overlay's menu-popover surface; the pick comes back as the item's id and runs like a native menu's.
  if (request.menu == "more" || request.menu == "new") {
    base::ListValue list;
    for (const MenuItem& item : items) {
      if (item.separator) {
        list.Append(base::DictValue().Set("separator", true));
        continue;
      }
      base::DictValue entry = base::DictValue()
                                  .Set("id", item.id)
                                  .Set("label", item.label)
                                  .Set("enabled", item.enabled);
      if (!item.keys.empty()) {
        entry.Set("command", item.keys);
      }
      list.Append(std::move(entry));
    }
    window_->OpenAnchoredPrompt(
        "menu-popover", anchor,
        base::DictValue().Set("menu", request.menu).Set("items", std::move(list)),
        base::BindOnce(
            [](base::WeakPtr<CluTabMenu> self, base::Value result) {
              if (!self) {
                return;
              }
              if (result.is_string()) {
                self->picked_ = result.GetString();
              }
              self->OnMenuClosed();
            },
            weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  ui::SimpleMenuModel* root = AddModel(items);
  gfx::Rect bounds = anchor;
  views::View::ConvertRectToScreen(frame, &bounds);
  runner_ = std::make_unique<views::MenuRunner>(
      root, views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&CluTabMenu::OnMenuClosed,
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

void CluTabMenu::ExecuteCommand(int command_id, int event_flags) {
  const auto it = command_ids_.find(command_id);
  if (it != command_ids_.end()) {
    picked_ = it->second;
  }
}

bool CluTabMenu::IsCommandIdChecked(int command_id) const {
  return checked_ids_.contains(command_id);
}

bool CluTabMenu::GetAcceleratorForCommandId(int command_id,
                                            ui::Accelerator* accelerator) const {
  // The Option key alone marks an Option alternate (patch ui-menus-cocoa-menu_controller.mm).
  if (alternate_ids_.contains(command_id)) {
    *accelerator = ui::Accelerator(ui::VKEY_MENU, ui::EF_ALT_DOWN);
    return true;
  }
  const auto it = command_keys_.find(command_id);
  if (it == command_keys_.end()) {
    return false;
  }
  const CommandSpec* spec = FindCommand(it->second);
  if (!spec || !spec->has_accelerator) {
    return false;
  }
  *accelerator =
      ui::Accelerator(spec->accelerator.key, spec->accelerator.modifiers);
  return true;
}

void CluTabMenu::OnMenuClosed() {
  // The pick runs after the native menu's loop has returned.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<CluTabMenu> self) {
                       if (!self || !self->open_request_) {
                         return;
                       }
                       const Request request = *self->open_request_;
                       const std::optional<std::string> picked = self->picked_;
                       self->open_request_.reset();
                       self->picked_.reset();
                       std::optional<std::string> rename;
                       if (picked) {
                         rename = self->Run(request, *picked,
                                            self->open_clipboard_);
                       }
                       // Focus goes back to the page, not by tab traversal to the frame's first button, whose
                       // focus ring and tooltip would stay up.
                       if (self && !rename) {
                         self->window_->FocusActivePage();
                       }
                       if (self && self->done_) {
                         std::move(self->done_).Run(rename);
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr()));
}

}  // namespace clu
