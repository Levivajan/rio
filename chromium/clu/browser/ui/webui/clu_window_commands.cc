// Copyright 2026 The Rio Authors. All rights reserved.

// CluWindowState's commands: the docs/PLAN.md 8 key table, the main menu and the command bar's palette rows all run
// through DispatchCommand() (docs/chromium/m5.md section 2).

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "build/build_config.h"
#include "base/strings/utf_string_conversions.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "base/version_info/version_info.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/webui_url_constants.h"
#include "clu/browser/command/clu_copy_url.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/data/clu_browsing_data.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_page_state.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/webui/clu_default_browser.h"
#include "clu/browser/ui/webui/clu_mini_windows.h"
#include "clu/browser/ui/webui/clu_overlay_rects.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_platform_handler.h"
#include "clu/browser/ui/webui/clu_settings_window.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_space_menu.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_text_recognizer.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/ui/webui/clu_window_site.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/browser/updater/clu_update_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/common/clu_urls.h"
#include "ui/base/base_window.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/ui_base_types.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "clu/browser/ui/webui/clu_share_services.h"
#endif

namespace clu {

namespace {

constexpr base::TimeDelta kCopyToast = base::Milliseconds(1500);

// N of `prefix`N for N in 1..9 (tabs.goto3 gives 3), else 0.
int DigitAfter(std::string_view id, std::string_view prefix) {
  if (id.size() != prefix.size() + 1 || !id.starts_with(prefix)) {
    return 0;
  }
  const char digit = id.back();
  return digit >= '1' && digit <= '9' ? digit - '0' : 0;
}

// The appearance.mode of View ▸ Appearance's `id`, or empty for another command.
std::string_view AppearanceModeFor(std::string_view id) {
  if (id == "theme.appearanceAuto") {
    return "auto";
  }
  if (id == "theme.appearanceLight") {
    return "light";
  }
  if (id == "theme.appearanceDark") {
    return "dark";
  }
  return std::string_view();
}

// Folders nest at most a few deep (TreeError::kTooDeep); a corrupt parent chain stops here.
constexpr int kMaxFolderWalk = 16;

// The tab rows of `ids` and of the folders among them, depth first.
void CollectTabs(const CluTabTree& tree,
                 const std::vector<std::string>& ids,
                 int depth,
                 std::vector<const TabNode*>& out) {
  if (depth > kMaxFolderWalk) {
    return;
  }
  for (const std::string& id : ids) {
    const TabNode* node = tree.Get(id);
    if (!node) {
      continue;
    }
    if (node->is_folder()) {
      CollectTabs(tree, tree.Children(id), depth + 1, out);
    } else {
      out.push_back(node);
    }
  }
}

}  // namespace

bool CluWindowState::RunCommand(std::string_view id) {
  return DispatchCommand(id, /*run=*/true, /*from_key=*/false);
}

bool CluWindowState::RunKeyCommand(std::string_view id) {
  return DispatchCommand(id, /*run=*/true, /*from_key=*/true);
}

bool CluWindowState::CanRunCommand(std::string_view id) {
  return DispatchCommand(id, /*run=*/false, /*from_key=*/false);
}

void CluWindowState::OpenSiteControls(const gfx::Rect& anchor) {
  site().OpenSiteControls(anchor);
}

std::optional<bool> CluWindowState::PeekPageCommand(std::string_view id,
                                                    bool run) {
  return multitask_->has_peek() ? multitask_->peek().RunPageCommand(id, run)
                                : std::nullopt;
}

bool CluWindowState::DispatchCommand(std::string_view id,
                                     bool run,
                                     bool from_key) {
  // With `run` false this only answers whether it would run: every branch decides before it acts. Split view, an open
  // Peek and the Mini window come first (docs/chromium/m6.md): Cmd+W closes a Peek, page commands act on a focused
  // Peek, a Mini window runs its own variant.
  if (std::optional<bool> multitask = multitask_->Dispatch(id, run)) {
    return *multitask;
  }
  if (id == "commandbar.newTab" || id == "commandbar.editUrl") {
    if (run) {
      OpenCommandBar(id == "commandbar.newTab" ? CommandBarMode::kNewTab
                                               : CommandBarMode::kEditUrl);
    }
    return true;
  }
  if (id == "sidebar.toggle") {
    if (run) {
      shell_->ToggleSidebar();
    }
    return true;
  }
  if (id == "shell.focusMode") {
    if (run) {
      shell_->ToggleFocusMode();
    }
    return true;
  }
  if (id == "shell.toolbar") {
    if (run) {
      shell_->ToggleToolbar();
    }
    return true;
  }
  if (id == "edit.copyUrl" || id == "edit.copyUrlMarkdown") {
    // Always runs: a page with nothing to share says so in a toast.
    if (run) {
      CopyUrl(std::nullopt, id == "edit.copyUrlMarkdown");
    }
    return true;
  }
  // View ▸ Collapse Pinned Tabs for the window's space.
  if (id == "tabs.collapsePinned") {
    CluModelService* m = model();
    if (!m || is_private() || !m->FindSpace(space_id())) {
      return false;
    }
    if (run) {
      SpaceTabState& state = m->mutable_tree().MutableSpaceState(space_id());
      state.pinned_collapsed = !state.pinned_collapsed;
      m->Commit();
    }
    return true;
  }
  // View ▸ Appearance ▸ Automatic, Light, Dark: the app's appearance.mode, as Settings sets it.
  if (const std::string_view mode = AppearanceModeFor(id); !mode.empty()) {
    CluAppSettings* settings = CluAppSettings::Get();
    if (!settings) {
      return false;
    }
    if (run) {
      settings->SetGlobal("appearance.mode", base::Value(mode));
    }
    return true;
  }
  // Tabs ▸ New Folder…: the space menu's New Folder for the window's space, then its row enters rename.
  if (id == "tabs.newFolder") {
    CluModelService* m = model();
    if (!m || is_private() || !m->FindSpace(space_id())) {
      return false;
    }
    if (run) {
      const std::optional<std::string> folder =
          space_menu().RunSpaceItem(space_id(), CluSpaceMenu::kNewFolder);
      if (!folder) {
        return false;
      }
      AskSidebar("rename", *folder);
      // A menu pick leaves the keyboard where it was, usually the page; the rename field needs it. The floating
      // sidebar takes it when its rename field holds it.
      if (!sidebar_request_->floating) {
        if (views::WebView* frame = shell_->frame_view()) {
          frame->RequestFocus();
          if (content::WebContents* contents = frame->web_contents()) {
            contents->Focus();
          }
        }
      }
    }
    return true;
  }
  // Tabs ▸ Reveal Tab in Sidebar: the sidebar shows, the active row's Pinned section and folders open, and the row
  // scrolls into view.
  if (id == "tabs.reveal") {
    const std::string row = ActiveRowId();
    if (row.empty()) {
      return false;
    }
    if (run) {
      CluModelService* m = model();
      const TabNode* node = m ? m->tree().Get(row) : nullptr;
      if (node && node->section == Section::kPinned) {
        CluTabTree& tree = m->mutable_tree();
        bool changed = false;
        if (tree.SpaceState(node->space_id).pinned_collapsed) {
          tree.MutableSpaceState(node->space_id).pinned_collapsed = false;
          changed = true;
        }
        std::string parent = node->parent_id;
        for (int depth = 0; !parent.empty() && depth < kMaxFolderWalk;
             ++depth) {
          const TabNode* folder = tree.Get(parent);
          if (!folder) {
            break;
          }
          if (folder->collapsed) {
            tree.Mutable(parent)->collapsed = false;
            changed = true;
          }
          parent = folder->parent_id;
        }
        if (changed) {
          m->Commit();
        }
      }
      AskSidebar("reveal", row);
    }
    return true;
  }
  // Tabs ▸ Reset all tabs in this Space: every pinned row of the window's space that left its pinned URL goes back to
  // it. Nothing to reset leaves the item disabled.
  if (id == "tabs.resetAll") {
    CluModelService* m = model();
    if (!m || is_private()) {
      return false;
    }
    std::vector<const TabNode*> pinned;
    CollectTabs(m->tree(), m->tree().Pinned(space_id()), 0, pinned);
    std::vector<std::string> off;
    for (const TabNode* node : pinned) {
      content::WebContents* contents = m->FindContents(node->id);
      if (node->pinned_url &&
          IsOffPinned(contents ? PageUrl(contents) : node->url,
                      node->pinned_url)) {
        off.push_back(node->id);
      }
    }
    if (off.empty()) {
      return false;
    }
    if (run) {
      for (const std::string& row_id : off) {
        ResetToPinned(row_id);
      }
    }
    return true;
  }
  if (id == "tabs.close") {
    if (!run) {
      return true;
    }
    // F9.1: an open overlay surface closes first.
    if (!prompts_.empty()) {
      CloseOverlayInstance(prompts_.back().instance_id);
      return true;
    }
    if (command_) {
      CloseCommandBar();
      return true;
    }
    CloseRowCommand(std::nullopt);
    return true;
  }
  if (const int number = DigitAfter(id, "tabs.goto")) {
    if (run) {
      GotoRow(number);
    }
    return true;
  }
  if (id == "tabs.next" || id == "tabs.prev") {
    if (run) {
      StepRow(id == "tabs.next" ? 1 : -1);
    }
    return true;
  }
  if (id == "platform.checkForUpdates") {
    // Listed, enabled and runnable only in a build that updates (docs/chromium/m12.md).
    if (!CluUpdateService::Get().available()) {
      return false;
    }
    if (run) {
      CheckForUpdatesFrom(*this);
    }
    return true;
  }
  // Help ▸ Troubleshooting: Reveal Rio Data shows the data folder in Finder, Copy Rio Info copies the versions
  // a bug report needs. Help ▸ Essential Keyboard Shortcuts opens Settings at Shortcuts.
  if (id == "platform.showLogs") {
    base::FilePath dir;
    if (!base::PathService::Get(chrome::DIR_USER_DATA, &dir)) {
      return false;
    }
    if (run) {
      platform_util::ShowItemInFolder(profile(), dir);
    }
    return true;
  }
  if (id == "platform.copyDiagnostics") {
    if (run) {
      const std::string info = base::StrCat(
          {"Rio ", version_info::GetVersionNumber(), "\nmacOS ",
           base::SysInfo::OperatingSystemVersion(), " (",
           base::SysInfo::OperatingSystemArchitecture(), ")"});
      {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(base::UTF8ToUTF16(info));
      }
      ShowToast("Copied Rio Info", std::nullopt, kCopyToast);
    }
    return true;
  }
  if (id == "platform.shortcuts") {
    if (run) {
      return OpenAppSettings("shortcuts");
    }
    return true;
  }
  // Rio ▸ Set as Default Browser: macOS asks the user. Rio already the default, or a build macOS can't name, says so
  // in a toast.
  if (id == "platform.setDefaultBrowser") {
    if (run) {
      CluDefaultBrowser::Get().Request(base::BindOnce(
          [](base::WeakPtr<CluWindowState> window, std::string_view status) {
            if (!window) {
              return;
            }
            if (status == "already-default") {
              window->ShowToast("Rio is your default browser", std::nullopt,
                                kCopyToast);
            } else if (status == "not-packaged") {
              window->ShowToast(
                  "Install Rio in Applications to make it your default "
                  "browser",
                  std::nullopt, kCopyToast);
            }
          },
          GetWeakPtr()));
    }
    return true;
  }
  // Help ▸ Contact the Team: the issue form as a Today tab, like the command bar's help row. A private window has
  // no rows.
  if (id == "platform.contactTeam") {
    if (!model() || is_private()) {
      return false;
    }
    if (run) {
      return CreateRow(GURL(kCluContactUrl), space_id_, /*activate=*/true)
          .has_value();
    }
    return true;
  }
  // Window ▸ Stay On Top: the window floats above normal windows until chosen again.
  if (id == "window.stayOnTop") {
    ui::BaseWindow* window = browser()->GetWindow();
    if (!window) {
      return false;
    }
    if (run) {
      window->SetZOrderLevel(
          window->GetZOrderLevel() == ui::ZOrderLevel::kFloatingWindow
              ? ui::ZOrderLevel::kNormal
              : ui::ZOrderLevel::kFloatingWindow);
    }
    return true;
  }
  if (id == "window.newPrivate") {
    if (run) {
      space_actions::OpenPrivateWindow(profile());
    }
    return true;
  }
  // A private window has no spaces: Chromium's own commands run there.
  if ((id.starts_with("spaces.") || id.starts_with("profiles.")) &&
      (is_private() || !model())) {
    return false;
  }
  if (const int number = DigitAfter(id, "spaces.goto")) {
    // Space N exists, in switcher order across profiles.
    if (static_cast<size_t>(number) >
        CluSpacesService::Get()->list().spaces().size()) {
      return false;
    }
    if (run) {
      space_actions::SwitchWindow(this, SwitchTarget{.index = number - 1});
    }
    return true;
  }
  if (id == "spaces.next" || id == "spaces.prev") {
    const SwitchTarget target{.step = id == "spaces.next" ? 1 : -1};
    if (!run) {
      // No wrap at either end: the bar and the menu offer it only when a space lies that way, so never in a window
      // with one space. The chord stays the window's either way.
      return ResolveSwitch(CluSpacesService::Get()->list().Order(), space_id_,
                           target)
          .has_value();
    }
    space_actions::SwitchWindow(this, target);
    return true;
  }
  if (id == "spaces.newSpace" || id == "spaces.editSpace" ||
      id == "profiles.newProfile") {
    if (run) {
      if (id == "profiles.newProfile") {
        OpenSpaceEditor(std::nullopt, "profiles");
      } else {
        OpenSpaceEditor(id == "spaces.editSpace"
                            ? std::optional<std::string>(space_id_)
                            : std::nullopt,
                        "space");
      }
    }
    return true;
  }
  if (id == "archive.mruNext" || id == "archive.mruPrev") {
    if (run) {
      const int direction = id == "archive.mruNext" ? 1 : -1;
      // Only the held chord ends with a Control key-up; a menu or palette pick would leave the switcher up until
      // the next key.
      if (from_key) {
        switcher_->Start(direction);
        UpdateEventMonitor();
      } else {
        switcher_->Jump(direction);
      }
    }
    return true;
  }
  // A private window keeps no archive: Chromium's own commands run there.
  if (id.starts_with("archive.") && !model()) {
    return false;
  }
  if (id == "archive.reopenClosed" || id == "archive.clearToday" ||
      id == "archive.undoClear" || id == "archive.open") {
    if (!run) {
      return true;
    }
    if (id == "archive.reopenClosed") {
      ReopenClosed();
    } else if (id == "archive.clearToday") {
      ClearToday(std::nullopt);
    } else if (id == "archive.undoClear") {
      model()->UndoClear();
    } else {
      SetArchivePanel(!archive_panel_);
    }
    return true;
  }

  // M9 (docs/chromium/m9.md): Cmd+Shift+J and the sidebar's downloads button open Chromium's downloads bubble on
  // CluAnchorView, or chrome://downloads when it has nothing to list.
  if (id == "downloads.open") {
    if (run) {
      ShowDownloadsUi(browser_.get());
    }
    return true;
  }
  // View ▸ Clear Cookies and Refresh, Clear Cache and Refresh: the shown site's data only, then the page
  // reloads from the network.
  if (id == "page.clearCookiesAndRefresh" ||
      id == "page.clearCacheAndRefresh") {
    tabs::TabInterface* shown = ActiveTab();
    content::WebContents* contents = shown ? shown->GetContents() : nullptr;
    const GURL url = contents ? contents->GetLastCommittedURL() : GURL();
    if (!IsWebUrl(url)) {
      return false;
    }
    if (run) {
      const uint64_t mask =
          id == "page.clearCookiesAndRefresh"
              ? content::BrowsingDataRemover::DATA_TYPE_COOKIES
              : content::BrowsingDataRemover::DATA_TYPE_CACHE;
      ClearSiteData(
          profile(), url, mask,
          base::BindOnce(
              [](base::WeakPtr<content::WebContents> page) {
                if (page) {
                  page->GetController().Reload(
                      content::ReloadType::BYPASSING_CACHE, false);
                }
              },
              contents->GetWeakPtr()));
    }
    return true;
  }
  // Site Permissions…: Chromium's settings page for the site the window shows.
  if (id == "permissions.openSite") {
    tabs::TabInterface* shown = ActiveTab();
    const GURL url =
        shown ? shown->GetContents()->GetLastCommittedURL() : GURL();
    if (!IsWebUrl(url)) {
      return false;
    }
    if (run) {
      chrome::ShowSiteSettings(browser_.get(), url);
    }
    return true;
  }
  // The app settings window opens from any Rio window, a private one too (docs/chromium/parity-settings.md); Browser
  // Settings is Chromium's own page.
  if (id == "settings.open" || id == "settings.browser") {
    if (run) {
      return id == "settings.open" ? OpenAppSettings(std::string_view())
                                   : OpenBrowserSettings(std::string_view());
    }
    return true;
  }
  // Import from Google Chrome, and the import from another browser, open the Chrome import itself
  // (#import-chrome, General with the Chrome import first), never General alone; Import from Arc opens General with
  // its import card open (#import-arc).
  if (id == "settings.import" || id == "settings.importChrome") {
    return run ? OpenAppSettings("import-chrome") : true;
  }
  if (id == "settings.importArc") {
    return run ? OpenAppSettings("import-arc") : true;
  }
  // M8: History, Settings and Clear Browsing Data are Clu's own in a window with rows (focus-or-open in the window's
  // space, Clu's settings page, Clu's dialog with the archive box), and Edit Theme opens the picker. A private window
  // keeps no rows: Chromium's own commands run there (History and Settings open in a regular window).
  if (id == "history.open" || id == "browsingData.open" ||
      id == "theme.editTheme") {
    if (is_private() || !model()) {
      return false;
    }
    if (run) {
      if (id == "history.open") {
        OpenInternalPage(GURL(chrome::kChromeUIHistoryURL));
      } else if (id == "browsingData.open") {
        OpenPrompt("clear-data",
                   base::DictValue().Set("profileId", state_profile_id()));
      } else {
        OpenThemePicker(space_id_, std::nullopt);
      }
    }
    return true;
  }

  // Chromium's own features (Find, Zoom, Print, Passwords, ...), when Chromium has them on now.
  if (const int native = ChromeCommandFor(id)) {
    if (!chrome::IsCommandEnabled(browser_, native)) {
      return false;
    }
    if (run) {
      chrome::ExecuteCommand(browser_, native);
    }
    return true;
  }

  // The rest acts on the row the window shows.
  const std::string active = ActiveRowId();
  tabs::TabInterface* tab = ActiveTab();
  if (active.empty()) {
    // Cmd+Shift+M with nothing to mute is still Clu's: let go, Chromium's key lookup would open the avatar menu.
    return from_key && id == "tabs.toggleMute";
  }
  if ((id == "tabs.togglePin" || id == "tabs.duplicate") && !model()) {
    // A private window keeps no rows: Duplicate is Chromium's own, and Cmd+D bookmarks as it did through M4 (the
    // Pin Tab item stays disabled).
    if (id == "tabs.duplicate") {
      if (!chrome::CanDuplicateTab(browser_)) {
        return false;
      }
      if (run) {
        chrome::DuplicateTab(browser_);
      }
      return true;
    }
    if (!from_key || !chrome::IsCommandEnabled(browser_, IDC_BOOKMARK_THIS_TAB)) {
      return false;
    }
    chrome::ExecuteCommand(browser_, IDC_BOOKMARK_THIS_TAB);
    return true;
  }
  if (id == "tabs.togglePin" || id == "tabs.duplicate") {
    if (run) {
      if (id == "tabs.togglePin") {
        std::ignore = TogglePin(active);
      } else {
        DuplicateRow(active);
      }
    }
    return true;
  }
  if (!tab) {
    return from_key && id == "tabs.toggleMute";
  }
  if (id == "tabs.toggleMute") {
    if (run) {
      SetRowMuted(active, !tab->GetContents()->IsAudioMuted());
    }
    return true;
  }
  if (id == "nav.back" || id == "nav.forward" || id == "nav.reload" ||
      id == "nav.hardReload" || id == "nav.stop") {
    if (!run) {
      return true;
    }
    if (id == "nav.back") {
      GoBack(tab);
    } else if (id == "nav.forward") {
      GoForward(tab);
    } else if (id == "nav.reload") {
      Reload(tab, /*bypass_cache=*/false);
    } else if (id == "nav.hardReload") {
      Reload(tab, /*bypass_cache=*/true);
    } else {
      StopLoading(tab);
    }
    return true;
  }
  // The blocker and Site tweaks (M7).
  if (const std::optional<bool> site = site_->DispatchCommand(id, run)) {
    return *site;
  }
  return false;
}

std::optional<std::string> CluWindowState::CopyUrl(
    const std::optional<std::string>& tab_id,
    bool markdown) {
  tabs::TabInterface* tab = tab_id ? FindTab(*tab_id) : ActiveTab();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  const std::optional<std::string> url =
      contents ? ShareableUrl(contents->GetLastCommittedURL()) : std::nullopt;
  // Only web pages have a URL worth sharing; the clipboard stays as it was.
  if (!url) {
    ShowToast("No URL to copy", std::nullopt, kCopyToast);
    return std::nullopt;
  }
  // A renamed row links by its own name, as the bulk copy does.
  std::u16string title = contents->GetTitle();
  if (CluModelService* m = model()) {
    const TabNode* node = m->tree().Get(CluTabId::Get(contents));
    if (node && node->title_override && !node->title_override->empty()) {
      title = base::UTF8ToUTF16(*node->title_override);
    }
  } else if (const std::optional<std::string> name =
                 CluPageName::Get(contents)) {
    title = base::UTF8ToUTF16(*name);
  }
  const std::string text = markdown ? MarkdownLink(title, *url) : *url;
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    // A private window's copy stays on this Mac and out of clipboard history, as Chromium's incognito copies do.
    if (profile()->IsOffTheRecord()) {
      writer.MarkAsOffTheRecord();
    }
    writer.WriteText(base::UTF8ToUTF16(text));
  }
  // The copy toast carries a share button.
  ShowToast(markdown ? "Copied link as Markdown" : "Copied Current URL",
            std::string(kShareToastAction), kCopyToast);
  return text;
}

void CluWindowState::ShareUrl(const std::optional<gfx::Rect>& anchor) {
  tabs::TabInterface* tab = ActiveTab();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  const std::optional<std::string> url =
      contents ? ShareableUrl(contents->GetLastCommittedURL()) : std::nullopt;
  if (!url) {
    ShowToast("No URL to share", std::nullopt, kCopyToast);
    return;
  }
  // The toast's share button sends no anchor: the picker points at the toast.
  const gfx::Rect at =
      anchor ? *anchor
             : ToastRect(shell_->layout().page_rect,
                         std::max<size_t>(1, toasts_.size()));
#if BUILDFLAG(IS_MAC)
  views::WebView* frame = shell_->frame_view();
  if (frame && ShowSharePicker(frame->web_contents(), at,
                               base::UTF8ToUTF16(*url))) {
    return;
  }
#endif
  ShowToast("Couldn't share this page", std::nullopt, kCopyToast);
}

bool CluWindowState::TakeScreenshot() {
  tabs::TabInterface* tab = ActiveTab();
  if (!tab || !tab->GetContents() ||
      !chrome::IsCommandEnabled(browser_, IDC_SHARING_HUB_SCREENSHOT)) {
    ShowToast("Nothing to capture here", std::nullopt, kCopyToast);
    return false;
  }
  return chrome::ExecuteCommand(browser_, IDC_SHARING_HUB_SCREENSHOT);
}

bool CluWindowState::CaptureText() {
  tabs::TabInterface* tab = ActiveTab();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents || !IsWebUrl(contents->GetLastCommittedURL())) {
    ShowToast("No text to capture here", std::nullopt, kCopyToast);
    return false;
  }
  // A second click while the region selection is up restarts it on the page shown now.
  if (capture_text_flow_) {
    capture_text_flow_->CancelCapture();
  }
  capture_text_flow_ = std::make_unique<image_editor::ScreenshotFlow>(contents);
  capture_text_flow_->Start(base::BindOnce(&CluWindowState::OnTextCaptured,
                                           weak_ptr_factory_.GetWeakPtr()));
  return true;
}

bool CluWindowState::IsCapturingText() {
  return capture_text_flow_ && capture_text_flow_->IsCaptureModeActive();
}

void CluWindowState::OnTextCaptured(
    const image_editor::ScreenshotCaptureResult& result) {
  // Esc or leaving the page ends the selection without a toast, as the Screenshot tile does.
  if (result.result_code !=
      image_editor::ScreenshotCaptureResultCode::SUCCESS) {
    return;
  }
  if (result.image.IsEmpty()) {
    ShowToast("Couldn't capture that area", std::nullopt, kCopyToast);
    return;
  }
#if BUILDFLAG(IS_MAC)
  RecognizeText(result.image.AsBitmap(),
                base::BindOnce(&CluWindowState::CopyCapturedText,
                               weak_ptr_factory_.GetWeakPtr()));
#else
  CopyCapturedText(std::u16string());
#endif
}

void CluWindowState::CopyCapturedText(std::u16string text) {
  if (text.empty()) {
    ShowToast("No text found", std::nullopt, kCopyToast);
    return;
  }
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    // A private window's capture stays out of clipboard history, as its copied links do.
    if (profile()->IsOffTheRecord()) {
      writer.MarkAsOffTheRecord();
    }
    writer.WriteText(text);
  }
  ShowToast("Copied Text", std::nullopt, kCopyToast);
}

std::optional<std::u16string> CluWindowState::CommandTitle(
    std::string_view id) {
  // A private window has one unsaved space with a fixed dark look and no switcher (docs/PLAN.md 3.15): New
  // Space…, Edit Theme…, Edit Space…, Next and Previous Space and Ctrl+1..9 make no sense there, so each gets an
  // empty title and the Spaces menu hides it instead of greying it.
  if (is_private() &&
      (id.starts_with("spaces.") || id == "theme.editTheme")) {
    return std::u16string();
  }
  // Tabs ▸ Pin Tab or Unpin Tab for the active row.
  if (id == "tabs.togglePin") {
    CluModelService* m = model();
    const TabNode* node = m ? m->tree().Get(ActiveRowId()) : nullptr;
    return node && node->section == Section::kPinned ? u"Unpin Tab"
                                                     : u"Pin Tab";
  }
  // View ▸ Expand Pinned Tabs while the window's space has them collapsed.
  if (id == "tabs.collapsePinned") {
    CluModelService* m = model();
    return m && m->tree().SpaceState(space_id()).pinned_collapsed
               ? u"Expand Pinned Tabs"
               : u"Collapse Pinned Tabs";
  }
  // View ▸ Hide Sidebar or Show Sidebar.
  if (id == "sidebar.toggle") {
    return shell_->chrome_state().sidebar_hidden ? u"Show Sidebar"
                                                 : u"Hide Sidebar";
  }
  // Ctrl+1..9 read as the space names (docs/PLAN.md 8 `commands.setTitle`).
  const int number = DigitAfter(id, "spaces.goto");
  if (!number) {
    return std::nullopt;
  }
  const std::vector<SpaceEntry>& spaces =
      CluSpacesService::Get()->list().spaces();
  // No such space: an empty title, so the Spaces menu hides the row. A space with no name keeps the table's
  // "Space N".
  if (static_cast<size_t>(number) > spaces.size()) {
    return std::u16string();
  }
  if (spaces[number - 1].name.empty()) {
    return std::nullopt;
  }
  return base::UTF8ToUTF16(spaces[number - 1].name);
}

std::optional<bool> CluWindowState::CommandChecked(std::string_view id) {
  const std::string_view mode = AppearanceModeFor(id);
  if (mode.empty()) {
    return std::nullopt;
  }
  CluAppSettings* settings = CluAppSettings::Get();
  return settings && settings->AppearanceMode() == mode;
}

bool CluWindowState::DockedSidebarShown() const {
  return !shell_->chrome_state().sidebar_hidden &&
         !shell_->chrome_state().focus_mode;
}

void CluWindowState::ShowDockedSidebar() {
  if (shell_->chrome_state().focus_mode) {
    shell_->ToggleFocusMode();
  }
  if (shell_->chrome_state().sidebar_hidden) {
    shell_->ToggleSidebar();
  }
}

void CluWindowState::AskSidebar(std::string_view kind,
                                const std::string& node_id) {
  const bool floating =
      !DockedSidebarShown() && sidebar_peek_ && sidebar_peek_->is_open();
  if (!floating) {
    ShowDockedSidebar();
  }
  // The tab list that acts on the ask shows only with the Library closed.
  SetArchivePanel(false);
  // The floating sidebar may have gone with its Library: the docked one answers then.
  if (floating && !sidebar_peek_->is_open()) {
    ShowDockedSidebar();
  }
  sidebar_request_ = SidebarRequestSnapshot{
      .seq = sidebar_request_ ? sidebar_request_->seq + 1 : 1,
      .kind = std::string(kind),
      .node_id = node_id,
      .floating = floating && sidebar_peek_->is_open()};
  // The floating sidebar stays while it acts on the ask, after the Library's hold ended above.
  if (sidebar_request_->floating) {
    sidebar_peek_->HoldForAction(kind);
  }
  MarkDirty();
}

bool CluWindowState::OpenAppSettings(std::string_view tab) {
  BrowserWindowInterface* target = browser();
  CluShell* shell = CluShell::From(target);
  if (shell && shell->is_mini()) {
    if (CluWindowState* main = mini_windows::LastMainWindow()) {
      target = main->browser();
    }
  }
  return CluSettingsWindow::Show(target, tab);
}

bool CluWindowState::OpenBrowserSettings(std::string_view path) {
  if (is_private() || !model()) {
    chrome::ShowSettingsSubPage(browser(), path);
    return true;
  }
  if (!OpenInternalPage(
          GURL(base::StrCat({chrome::kChromeUISettingsURL, path})))) {
    return false;
  }
  // From the settings window: the page shows in this window, which comes to the front.
  browser()->GetWindow()->Activate();
  return true;
}

}  // namespace clu
