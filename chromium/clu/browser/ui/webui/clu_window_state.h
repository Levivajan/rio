// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WINDOW_STATE_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WINDOW_STATE_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "chrome/browser/image_editor/screenshot_flow.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "clu/browser/capabilities/clu_downloads.h"
#include "clu/browser/capabilities/clu_screenshot_watcher.h"
#include "clu/browser/model/clu_archive_rules.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_switcher.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_gmail_feed.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "clu/browser/ui/webui/clu_wheel_lift_watcher.h"
#include "clu/common/mojom/bridge.mojom.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/render_widget_host_view.h"
#include "components/search_engines/template_url_service_observer.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

class BrowserWindowInterface;
class GURL;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace ui {
class MenuModel;
}  // namespace ui

namespace views {
class EventMonitor;
class MenuRunner;
}  // namespace views

namespace clu {

class CluIconBackfill;
class CluSpaceMenu;
class CluTabMenu;
class CluWindowAppState;
class CluWindowFade;
class CluWindowSite;
class CluWindowMultitask;
class CluWindowSidebarPeek;

// command.open modes (COMMAND_MODES in src/shared/ipc/command.ts). Split-add
// picks the page beside the one on screen, mini loads the Mini window's page
// (M6).
enum class CommandBarMode { kNewTab, kEditUrl, kSplitAdd, kMini };

// The copy toast's action: the overlay's toast shows it as a share button that calls siteControls.share.
inline constexpr char kShareToastAction[] = "siteControls.share";

// One window's Clu state for the bridge (docs/chromium/m2-ui-bridge.md 2.2, docs/chromium/m3.md). With the profile's
// model (CluModelService) it shows the model's tree: favorites, the window's space with its pinned tree and Today,
// rows with and without pages, the archive revision and panel, toasts and the overlay surfaces. A row's page is a tab
// of this window's TabStripModel whose CluTabId is the row id. In a private window there is no model, and state shows
// the tab strip as M2 did, so private tabs never reach tab_nodes.
//
// M4 (docs/chromium/m4.md): the window shows one space of its profile at a time. A window is one Chromium Browser,
// bound to one profile, so one Clu window is a group of sibling Browsers, one per profile it visited: switching to a
// space of another profile shows (or makes) the group's Browser of that profile at the same bounds and hides this
// one (CluSpaceActions). A private window shows the one unsaved "Private" space of its off-the-record profile.
//
// It streams StateMessages to every subscribed WebUI document: a snapshot on subscribe, then patches with rev + 1,
// coalesced per 16 ms. The window's CluShell owns it as its Client and destroys the WebUI documents before it, so the
// documents' handlers may hold a WeakPtr and never see it go mid-call.
class CluWindowState : public CluShell::Client,
                       public CluShell::Observer,
                       public TabStripModelObserver,
                       public CluModelService::Observer,
                       public CluModelService::WindowLink,
                       public CluSwitcher::Delegate,
                       public CluSpacesService::Observer,
                       public TemplateURLServiceObserver,
                       public ui::EventObserver {
 public:
  DECLARE_USER_DATA(CluWindowState);

  // The state of `browser`, created on first use. Null when `browser` has no Clu shell.
  static CluWindowState* GetOrCreate(BrowserWindowInterface* browser);

  CluWindowState(const CluWindowState&) = delete;
  CluWindowState& operator=(const CluWindowState&) = delete;
  ~CluWindowState() override;

  BrowserWindowInterface* browser() { return browser_; }
  Profile* profile();
  CluShell& shell() { return *shell_; }
  const std::string& window_id() const { return window_id_; }
  const std::string& space_id() const { return space_id_; }
  base::WeakPtr<CluWindowState> GetWeakPtr();
  // The profile's model once its rows are read; null in a private window and until then.
  CluModelService* model();

  // Adds a StateObserver and sends it the snapshot. `frame` is set for the frame WebUI's own document.
  void Subscribe(mojo::PendingRemote<mojom::StateObserver> observer,
                 bool frame);
  // The whole state as published now (state.get), with its rev.
  base::DictValue CurrentState();
  // Something in the window changed: publish on the next 16 ms batch.
  void MarkDirty();
  // Publish pending changes now.
  void Flush();

  // The page of row `tab_id` in this window, or null. Without a model, the tab whose CluTabId it is.
  tabs::TabInterface* FindTab(std::string_view tab_id);
  // The active tab, unless it is the placeholder.
  tabs::TabInterface* ActiveTab();
  // The id of the row the window shows, or empty.
  std::string ActiveRowId();
  // The tab strip index of `tab`, or nullopt.
  std::optional<int> IndexOf(tabs::TabInterface* tab);
  void FocusActivePage();

  // Page operations shared by the handlers and RunCommand(). Back and forward do nothing without history in that
  // direction.
  void ActivateTab(tabs::TabInterface* tab);
  void GoBack(tabs::TabInterface* tab);
  void GoForward(tabs::TabInterface* tab);
  void Reload(tabs::TabInterface* tab, bool bypass_cache);
  void StopLoading(tabs::TabInterface* tab);

  // ---- rows (docs/plan/02-tabs.md) ------------------------------------------------------------------------------

  // tabs.activate: shows the row, loading its page when it has none, moving it here when another window has it.
  // False for an unknown row.
  bool ActivateRow(const std::string& row_id);
  // tabs.create: a Today row at the top of the space's Today, shown unless `activate` is false. Returns the row id,
  // or the IpcError reason ("blocked-url", "not-found").
  base::expected<std::string, std::string> CreateRow(
      const GURL& url,
      const std::optional<std::string>& space_id,
      bool activate);
  // tabs.close (F9): a live page closes through the strip (a "Leave site?" prompt may keep it); a row without one
  // closes at once. With no row given and none shown, the window closes.
  void CloseRowCommand(const std::optional<std::string>& row_id);
  // tabs.duplicate (F10): a Today tab below a Today source, else at the top of the window's space, with history;
  // shown. Returns the new row id.
  std::optional<std::string> DuplicateRow(const std::string& row_id);
  void SetRowMuted(const std::string& row_id, bool muted);
  // tabs.move and the menus' Move To. A refused move answers its TreeError and, with `toast`, shows the toast.
  base::expected<void, TreeError> MoveRows(const std::vector<std::string>& ids,
                                           const DropTarget& target,
                                           bool toast);
  // tabs.move and a tabs.bulk move in a private window, which keeps no rows: its pages move within its one list, in
  // the given order, before the page now at `target.index` of the list without them (the tree's rule). Pinning stays
  // off (private windows stay flat), so only a Today target takes them.
  base::expected<void, TreeError> MovePrivatePages(
      const std::vector<std::string>& ids,
      const DropTarget& target);
  // Cmd+D (F2): Today to the end of its space's pinned root, pinned to the top of Today, favorite to the end of the
  // window's space's pinned root.
  // Tidy Tab Titles: the model's answer (or the rules') becomes the pinned row's title unless it was renamed meanwhile.
  void ApplyTidyTitle(const std::string& row_id,
                      const std::string& title,
                      const std::string& url,
                      std::optional<std::string> answer);
  // The link under the pointer in `source` (clu_link_hover.h), for hover previews.
  static void OnLinkHover(content::WebContents* source, const GURL& url);
  void LinkHovered(const GURL& url);
  // Shift held this long while the pointer rests on a link opens it in Peek, whichever came first.
  static constexpr base::TimeDelta kHoverPreviewDelay = base::Milliseconds(400);
  // How often the Shift key is read while a link is under the pointer (no key event reaches Rio for a page's Shift).
  static constexpr base::TimeDelta kHoverShiftPoll = base::Milliseconds(50);
  // The tab hover card's fade out.
  static constexpr base::TimeDelta kHoverCardOut = base::Milliseconds(90);
  // A hover preview closes once the cursor is this far (screen pt, either axis) from where it opened.
  static constexpr int kHoverLinkReach = 12;
  // Closes a hover preview when `cursor` has left the link at `origin`; true when the watch is over.
  bool CloseHoverPreviewIfLeft(const gfx::Point& origin, const gfx::Point& cursor);

  base::expected<void, TreeError> TogglePin(const std::string& row_id);
  // F2: an anchored row's page navigates back to its pinned URL (a row without a page just reads as it).
  bool ResetToPinned(const std::string& row_id);
  // tabs.toNewWindow (PLAN 3.15): a new window on the row's space shows it; this window falls back.
  bool MoveRowToNewWindow(const std::string& row_id);
  // Clear (Cmd+Shift+K) of this window's space, or of `space_id`.
  int ClearToday(const std::optional<std::string>& space_id);
  // Cmd+Shift+T.
  void ReopenClosed();
  // F12: Cmd+1..9 (1-based), and the next or previous row for tabs.next and tabs.prev (no wrap).
  bool GotoRow(int number);
  bool StepRow(int direction);
  // The archive panel replaces the tab list in this window's sidebar (02-tabs F7).
  void SetArchivePanel(bool open);
  bool archive_panel() const { return archive_panel_; }
  // Whether the docked sidebar shows: Cmd+S and focus mode both hide it.
  bool DockedSidebarShown() const;
  // Brings the docked sidebar back from either: focus mode ends and a hidden sidebar shows.
  void ShowDockedSidebar();

  // ---- spaces (docs/plan/03-spaces.md, docs/chromium/m4.md) ------------------------------------------------------

  // A private window: its profile is off the record, it keeps no rows and shows the "Private" space.
  bool is_private();
  // The windows one Clu window is made of share a group id, one window per profile.
  const std::string& group_id() const { return group_id_; }
  void SetGroup(const std::string& group_id);
  // The space a window opens on once its rows are read (a window a cross-profile switch made, or a new window).
  void SetInitialSpace(const std::string& space_id);
  // Switches this window to a space of its own profile (docs/plan/03-spaces.md "Space switching"): the space's last
  // active tab shows, else its first Today tab, else its first pinned tab, else the empty state. The frame WebUI
  // slides from the activeSpaceId patch; the page swaps at once. With the sidebar hidden a 1 s toast names the space.
  // False for a space the profile does not hold, or before the rows are read. With `page_after_frame` (a switch the
  // user asked for: spaces.switch, the keys, the command bar, a swipe's end) the frame WebUI hears of the new space
  // first and the page swaps in the next task (ShowSpacePage()), so the strip starts sliding while the browser swaps
  // the page view.
  bool ShowSpace(const std::string& space_id, bool page_after_frame = false);
  // spaces.swipe (src/main/spaces/swipe.ts): windows[id].swipe follows the drag; "end" switches one space toward it.
  enum class SwipePhase { kBegin, kUpdate, kEnd, kCancel };
  void Swipe(SwipePhase phase, double dx, bool momentum);
  // A cross-profile switch hid this window; showing it again clears the mark. Closing a window that is not hidden
  // closes its hidden siblings.
  bool hidden_by_switch() const { return hidden_by_switch_; }
  void SetHiddenBySwitch(bool hidden);
  // Runs `callback` once the frame WebUI has subscribed and the window shows its space (at once when it does): a
  // window a cross-profile switch made fades in only then, so it never shows an empty frame.
  void WhenFrameReady(base::OnceClosure callback);
  // A window made by a cross-profile switch has no tab yet: it gets the placeholder page before it shows.
  void PrepareEmpty();
  // Leaves `row_id` if this window shows it: the window's next row shows (a row about to move to another profile).
  void LeaveRow(const std::string& row_id);
  // The fade a cross-profile switch runs into this window; it ends when either window goes.
  void SetFade(std::unique_ptr<CluWindowFade> fade);
  // Drops `fade` if it is still this window's fade.
  void ClearFade(CluWindowFade* fade);
  // A cross-profile switch away from this window is loading its profile: the window does not settle on a space of
  // its own meanwhile.
  void set_switch_pending(bool pending) { switch_pending_ = pending; }
  bool switch_pending() const { return switch_pending_; }
  // A cross-profile fade into this window is still running.
  bool FadeRunning() const;
  // Ends that fade now, so a newer switch starts from a settled window.
  void FinishFade();
  // Tests: the frame WebUI has subscribed and the window shows its space.
  bool frame_ready_for_testing() { return frame_subscribed_ && rows_ready(); }

  // A confirm dialog in the top overlay (the "confirm" surface). `done(true)` on its confirm button; false on Cancel,
  // Esc, a prompt that replaced it or the window closing.
  void OpenConfirm(base::DictValue props, base::OnceCallback<void(bool)> done);
  // overlay.respond: answers the prompt `instance_id` with `result`. False when no such prompt is open.
  bool RespondOverlay(std::string_view instance_id, const base::Value& result);
  // spaces.openEditor: the space editor (a new space without `space_id`), or its profile manager view.
  void OpenSpaceEditor(const std::optional<std::string>& space_id,
                       const std::string& view);
  // spaces.openIconMenu and spaces.openOverflow: native menus at `anchor` (frame WebUI coordinates).
  CluSpaceMenu& space_menu() { return *space_menu_; }
  // The chrome set of the WebUI that asked for the next sidebar menu (tabs.openMenu and spaces.openIconMenu `light`):
  // the native menu draws light on a light space. Unset keeps the window's own appearance.
  void set_menu_light(std::optional<bool> light) { menu_light_ = light; }
  std::optional<bool> menu_light() const { return menu_light_; }
  // The window profile's downloads (M9, docs/chromium/m9.md).
  CluDownloads& downloads() { return *downloads_; }

  // ---- overlay surfaces ------------------------------------------------------------------------------------------

  // The command bar: open sets windows[id].command
  // and opens the bar's overlay surface; the same mode again closes it, the other mode switches the session in place.
  void OpenCommandBar(CommandBarMode mode);
  void CloseCommandBar();
  const std::optional<CommandSessionSnapshot>& command_session() const {
    return command_;
  }
  // A modal prompt in the top overlay (tabs.pinnedUrl, tabs.setIcon).
  // `modality` is "modal" (the window dims under it) or "light-dismiss" (a click outside closes it, nothing dims).
  void OpenPrompt(const std::string& surface,
                  base::DictValue props,
                  std::string modality = "modal");
  // A light-dismiss surface at `rect` (frame coordinates) that answers with a value, as the "…" and "+" popovers do
  // with the picked item's id. `done` hears false when it closes without one, or when another prompt replaces it.
  void OpenAnchoredPrompt(const std::string& surface,
                          const gfx::Rect& rect,
                          base::DictValue props,
                          base::OnceCallback<void(base::Value)> done);
  // overlay.close: closes the surface with `instance_id`. False when no such surface is open.
  bool CloseOverlayInstance(std::string_view instance_id);
  // A toast; with `undo_command`, an action button that runs it ("Undo", or the share button for kShareToastAction).
  void ShowToast(const std::string& text,
                 const std::optional<std::string>& action_command,
                 base::TimeDelta duration);
  // tabs.hover: the card for `row_id` beside the row at `anchor` (window coordinates); nullopt hides it, with `fade` a
  // title pill over kHoverCardOut (the pointer left the row), otherwise at once.
  void Hover(const std::optional<std::string>& row_id,
             const std::optional<gfx::Rect>& anchor,
             bool fade = false);
  // tabs.hover held: the pointer is on `row_id`'s Gmail card, so the tile's leave keeps it open; false
  // fades it out as the leave does. A card for another row, or no card, ignores it.
  void HoldHover(const std::string& row_id, bool held);
  // tabs.openMail: a message row of `row_id`'s Gmail card opens `url` (a mail.google.com page) in that
  // row's own tab, waking it if it is asleep, and the card closes. False for a row that is not a Gmail row or a URL
  // that is not Gmail's.
  bool OpenMail(const std::string& row_id, const std::string& url);
  // shell.tooltip: the frame's tooltip pill at `rect` (window coordinates, the pill plus its shadow margin) in the hover
  // card's passive slot, so it draws above the page card. An empty label, a missing rect, or the tab hover
  // card or folder popover holding the slot leave it hidden.
  void Tooltip(const std::string& label,
               std::vector<std::string> keys,
               const std::optional<gfx::Rect>& rect,
               bool above,
               bool closing);
  // folders.peek: the collapsed-folder popover in the hover card's passive slot. `rect` opens it for `folder_id`
  // (clamped to the window); `held` records whether the pointer is over it (false closes it); no folder closes it
  // unless it is held. False when the folder does not exist.
  bool FolderPeek(const std::optional<std::string>& folder_id,
                  const std::optional<gfx::Rect>& rect,
                  std::optional<bool> held);
  // switcher.respond and the Ctrl+Tab keys.
  CluSwitcher& switcher() { return *switcher_; }
  // tabs.openMenu: the native menu for a row, a selection, the empty area or the + button. `done` gets the row the
  // sidebar puts into inline rename, if any.
  void OpenMenu(const std::string& menu,
                const std::optional<std::string>& node_id,
                const std::vector<std::string>& tab_ids,
                const gfx::Rect& anchor,
                base::OnceCallback<void(std::optional<std::string>)> done);
  CluTabMenu& tab_menu() { return *tab_menu_; }

  // Copy URL (04-command F9): the stripped URL of row `tab_id`'s page, or of the page the window shows, goes to the
  // clipboard as is or as a Markdown link, with a toast. Pages with nothing to share leave the clipboard alone and
  // toast "No URL to copy". Returns what was copied.
  // siteControls.share: macOS's share picker for the active page's URL at `anchor` (frame
  // coordinates), or at the toast without one. A page with nothing to share says so in a toast.
  void ShareUrl(const std::optional<gfx::Rect>& anchor);
  // siteControls.screenshot: Chromium's region capture (IDC_SHARING_HUB_SCREENSHOT) on the active page. False,
  // with a toast, when the window shows no page or Chromium has the command off.
  bool TakeScreenshot();
  // siteControls.captureText: Chromium's region selection on the active page; macOS reads the text in the
  // region, which goes to the clipboard with a toast. False, with a toast, when the window shows no web page.
  bool CaptureText();
  // Whether Capture text's region selection is up.
  bool IsCapturingText();
  // The selected region (public for clu_browsertests): reads its text and copies it, or toasts "No text found".
  void OnTextCaptured(const image_editor::ScreenshotCaptureResult& result);
  std::optional<std::string> CopyUrl(const std::optional<std::string>& tab_id,
                                     bool markdown);
  // The title the main menu shows for command `id` now (the space names of Ctrl+1..9), nullopt for the table's, or
  // empty for a space that isn't there (the Spaces menu hides its row).
  std::optional<std::u16string> CommandTitle(std::string_view id);
  // Whether the main menu checks command `id` now (View ▸ Appearance ▸ the mode in use), nullopt for no check.
  std::optional<bool> CommandChecked(std::string_view id);
  // The sidebar's last ask from a main menu command (Tabs ▸ New Folder…, Reveal Tab in Sidebar), for tests.
  const std::optional<SidebarRequestSnapshot>& sidebar_request_for_testing()
      const {
    return sidebar_request_;
  }

  // ---- split view, Peek, the media card and the Mini window (docs/chromium/m6.md) -----------------------------------

  CluWindowMultitask& multitask() { return *multitask_; }

  // The hover peek of a hidden sidebar (docs/chromium/sidebar-pip.md).
  CluWindowSidebarPeek& sidebar_peek() { return *sidebar_peek_; }

  // CluShell::Client (clu_window_commands.cc):
  bool RunCommand(std::string_view id) override;
  bool RunKeyCommand(std::string_view id) override;
  bool CanRunCommand(std::string_view id) override;
  std::optional<bool> PeekPageCommand(std::string_view id, bool run) override;
  void OpenSiteControls(const gfx::Rect& anchor) override;

  // CluShell::Observer:
  void OnShellChanged(bool urgent) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

  // CluModelService::Observer:
  void OnModelChanged() override;

  // CluModelService::WindowLink:
  const std::string& LinkWindowId() override;
  std::string LinkSpaceId() override;
  std::string LinkActiveNodeId() override;
  std::vector<std::string> LinkMru() override;
  content::WebContents* LinkContents(std::string_view node_id) override;
  void LinkActivate(const std::optional<std::string>& node_id) override;
  void LinkDropPage(const std::string& node_id) override;
  void LinkRowsRemoved(const std::vector<std::string>& ids) override;
  void LinkToast(const std::string& text,
                 const std::optional<std::string>& undo_command) override;
  void LinkDropToasts(const std::string& command) override;

  // CluSpacesService::Observer:
  void OnSpacesChanged() override;

  // TemplateURLServiceObserver: the profile's default engine is in state (settings and the bar's search label).
  void OnTemplateURLServiceChanged() override;
  void OnTemplateURLServiceShuttingDown() override;

  // CluSwitcher::Delegate:
  std::vector<std::string> RecentTabs() override;
  std::string CurrentTabId() override;
  void ShowSwitcher(const SwitcherView& view) override;
  void HideSwitcher() override;
  void CommitSwitch(const std::string& tab_id) override;

  // ui::EventObserver: the Control key going up while the switcher runs.
  void OnEvent(const ui::Event& event) override;

  // M8: the app-wide part of the window's state (appearance, onboarding, settings, themes, import) and its accent.
  CluWindowAppState& app_state() { return *app_state_; }
  // The internal page `url` (chrome://history, Clu's settings) in this window's space: an existing row of it shows,
  // else it opens as a Today row at the top. With `fragment`, the page shows that section. False without a model.
  bool OpenInternalPage(const GURL& url);
  // The app settings window (docs/chromium/parity-settings.md) at `tab` (empty: the tab it shows), bound to this
  // window, or to the last main window when this is a Mini window.
  bool OpenAppSettings(std::string_view tab);
  // Chromium's own settings (rio://settings) at `path` ("privacy", "content/notifications"; empty for its first page)
  // as a row of this window's space, and this window comes to the front. A private window keeps no rows: Chromium
  // opens the page in a regular window, as it does for its own Settings item.
  bool OpenBrowserSettings(std::string_view path);
  // The theme picker for `space_id` in the top overlay, at `anchor` (frame WebUI coordinates; the top of the sidebar
  // without one). Closing it without a commit puts back the space's committed theme (01-shell F9).
  void OpenThemePicker(const std::string& space_id,
                       const std::optional<gfx::Rect>& anchor);
  // The back or forward history menu of the active page at `anchor` (frame WebUI coordinates): Chromium's own
  // BackForwardMenuModel, so picking an entry, Cmd-clicking it and "Show Full History" behave as in Chrome.
  void OpenNavHistory(bool back, const gfx::Rect& anchor);
  // The menu OpenNavHistory() shows (tests run its items without the native menu's nested loop).
  std::unique_ptr<ui::MenuModel> NavHistoryModel(bool back);
  // The URL pill's and the toolbar field's right-click menu at `anchor` (frame coordinates), Show as Toolbar or
  // Hide Toolbar.
  void OpenToolbarMenu(const gfx::Point& anchor);
  // The menu OpenToolbarMenu() shows (tests run its item without the native menu's nested loop).
  std::unique_ptr<ui::MenuModel> ToolbarMenuModel();
  // The profile id state shows for this window (profiles[id], the clear-data dialog's default).
  const std::string& state_profile_id() const { return profile_id_; }
  // The page shows while the window shows a row and first-run onboarding, which fills the window, is not up.
  void UpdatePageShown();

  // ---- site, blocker, Site tweaks, extensions (M7, clu_window_site.h) --------------------------------------------

  CluWindowSite& site() { return *site_; }

  // Tests: the recent list, and whether the window keeps a placeholder page.
  const TabMru& mru_for_testing() const { return mru_; }
  bool HasPlaceholderForTesting();
  // The frame's trackpad lift watcher, so tests can report a native lift.
  CluWheelLiftWatcher* wheel_watcher_for_testing() {
    return wheel_watcher_.get();
  }
  // The window's screenshot watcher is off under a test run; tests point it at a temp folder.
  void WatchScreenshotsForTesting(const base::FilePath& folder);
  const CluScreenshotWatcher& screenshots_for_testing() const {
    return *screenshots_;
  }
  // The page a space switch made visible a frame before the swap, if any.
  content::WebContents* pre_shown_page_for_testing() { return pre_shown_.get(); }

 private:
  // Capture text's read: the text to the clipboard with a toast, or "No text found".
  void CopyCapturedText(std::u16string text);
  // Hides the hover slot's card, tooltip or folder popover now, ending a hover card's fade.
  void HideHoverNow();
  // The feed `row_id`'s card waits for has answered; the card opens (or redraws) while the row is hovered.
  void OnGmailFeed(const std::string& row_id);
  // Opens its popovers and the tweak editor among the window's prompts.
  friend class CluWindowSite;

  // What a page that closed left behind, read while it was still alive.
  struct ClosedPage {
    ClosedPage();
    ClosedPage(const ClosedPage&);
    ClosedPage& operator=(const ClosedPage&);
    ~ClosedPage();

    std::string row_id;
    CluModelService::PageCapture capture;
    // The window showed it: it activates the F9 next row, not the strip's neighbor.
    bool was_active = false;
  };

  // M6's parts reach the window's rows and pages (clu_window_multitask.cc).
  friend class CluWindowMultitask;
  // The hover peek publishes its surface and reads the window's surfaces.
  friend class CluWindowSidebarPeek;

  CluWindowState(BrowserWindowInterface* browser, CluShell& shell);

  // RunCommand() with `run`, CanRunCommand() without: one place decides both, so the main menu, the key table and
  // the command bar never disagree about a command.
  // `from_key`: the chord of the key table runs it (RunKeyCommand()).
  bool DispatchCommand(std::string_view id, bool run, bool from_key);

  WindowSnapshot TakeSnapshot();
  void FillModelSnapshot(WindowSnapshot& s);
  TabSnapshot PageSnapshot(content::WebContents* contents);
  // A loaded Google Calendar pinned row's next event ("Event at 4:30 PM"), read from that tab's own page
  // in an isolated world at most every kCalendarReadInterval, never by reloading it.
  void ReadCalendarSubtitle(const std::string& row_id,
                            content::WebContents* contents);
  void OnCalendarSubtitle(const std::string& row_id, base::Value value);
  void Publish();
  // Brings back the sidebar a menu ask goes to: the floating sidebar when it is open over a hidden sidebar (it stays,
  // with the Library closed), otherwise the docked sidebar. Then puts a "rename" or "reveal" ask for `node_id` in the
  // window's state, naming that sidebar.
  void AskSidebar(std::string_view kind, const std::string& node_id);
  void OnActiveChanged(BrowserWindowInterface* browser);

  // The model's rows are read: adopt the pages the strip already has, and in the first window show the space's
  // restored tab (only that one loads, PLAN M3).
  void OnModelLoaded();
  // A page entered the strip: its row, or a new Today row at the top for a page Clu did not open.
  void AdoptPage(content::WebContents* contents);
  // A page of this profile other than `except` shows the row `row_id`, in any window.
  bool RowShowsElsewhere(const std::string& row_id,
                         content::WebContents* except);
  // Once the strip settles: when Chromium shows the restored page `contents`, the window shows its row (and its
  // space).
  void ShowRestoredRowSoon(content::WebContents* contents);
  // A page Chromium restored for a row that already showed: the row's own page shows in its place, and the copy goes.
  void DropRestoredCopy(content::WebContents* copy, const std::string& row_id);
  void NoteActivation(content::WebContents* old_contents,
                      content::WebContents* new_contents);
  // Writes a live page's URL, title and (Today rows) history into its row.
  void SyncRowFromPage(content::WebContents* contents, bool with_history);
  // Pages that closed by themselves (window.close(), an extension, the strip's own close) follow F9.
  void HandleClosedPages(std::vector<ClosedPage> pages);
  // Makes the page for `row_id`: restored history for a Today row, the pinned URL for an anchored one.
  void LoadRowPage(const TabNode& row);
  // The placeholder page keeps the strip from emptying. It stays in the background while a row shows, so a page that
  // leaves on its own (window.close(), an extension's chrome.tabs.remove) never takes the strip, and with it the
  // window, to zero tabs: Browser::TabStripEmpty() would close the window before F9 runs.
  tabs::TabInterface* EnsurePlaceholder();
  // Placeholders beyond the one kept in the background go (a new-tab page Chromium opened becomes one).
  void RemovePlaceholderIfUnused();
  // The window is on `space_id` now: the last space in Local State, and with the sidebar hidden the space toast when
  // it changed. True when it changed. Callers check the space exists.
  bool EnterSpace(const std::string& space_id);
  // F12's row order: the sidebar's, or a private window's pages in strip order.
  std::vector<std::string> RowOrder();
  void ShowEmpty();
  // No row shows (startup's new-tab page took the window, or a window opened empty): the space's restored row in the
  // first window of the run, else this window's most recent row, else the empty state.
  void RestoreOrEmpty();
  // The row this window falls back to when `row_id` leaves its sidebar (moved away, taken by another window): its
  // most recent row in its space, else the first Today row, else the first pinned one.
  std::optional<std::string> NextRowAfter(const std::string& row_id);
  // The F9 next row when this window closes `row_id`, which it shows.
  std::optional<std::string> NextRowAfterClose(const std::string& row_id);
  // Activates `row_id` in this window from another window's strip.
  bool TakePageFrom(content::WebContents* contents);
  // The space this window shows once its rows are read: the initial space it was given, the space of the row it
  // shows, the app's last space, else the profile's first. A profile with no space left sends the window to the first
  // space of another profile.
  void SettleSpace();
  // Shows the space's target row (its last active tab, else the first Today, else the first pinned), else the empty
  // state.
  void ActivateSpaceTarget();
  // The row ActivateSpaceTarget() shows, if any.
  std::optional<std::string> SpaceTarget();
  // Whether the page half of a switch from `before` shows the space's target: no row of the new space shows yet.
  bool SpacePageMoves(const std::string& before);
  // The first page half of ShowSpace(page_after_frame), at once: the incoming page (already in this window's
  // tab strip) becomes visible, so ShowSpacePage() a frame later only swaps the view and hides the old page.
  void PrepareSpacePage(const std::string& space_id, const std::string& before);
  // The page PrepareSpacePage() showed goes back to hidden unless it is the window's page now.
  void HidePreShownPage();
  // The page half of ShowSpace(page_after_frame): shows `space_id`'s page unless the window moved on since. `before`
  // is the row that showed when the switch began.
  void ShowSpacePage(const std::string& space_id, const std::string& before);
  // The space the window shows is gone (deleted, moved to another profile): it settles on another.
  void ValidateSpace();
  void OnPrivateWindowReady();
  // Rows are known: a model's, or a private window's strip.
  bool rows_ready();
  void NotifyFrameReady();
  void ClearSwipe();
  // A wheel event on the frame WebUI: `phased` when it had a real trackpad phase (false for a mouse), `lifted` when the
  // fingers left.
  void OnWheelPhase(bool phased, bool lifted);
  void FailPrompts();
  void UpdateToastSurface();
  void ExpireToasts();
  void UpdateEventMonitor();
  std::vector<OverlaySurfaceSnapshot> Surfaces();
  // The part of the window the top overlay covers for the open prompts, or none for all of it
  // (PassiveTopOverlayRect(): the tweak editor leaves the page beside it clickable).
  std::optional<gfx::Rect> TopOverlayRect();
  void UpdateTopOverlay();

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<CluShell> shell_;
  const std::string window_id_;
  // Without a model: one profile id and one space per window, as in M2.
  std::string profile_id_;
  std::string space_id_;
  // The page PrepareSpacePage() made visible ahead of its switch's ShowSpacePage().
  base::WeakPtr<content::WebContents> pre_shown_;
  // ShowSpacePage() tasks posted and not run yet.
  int pending_space_pages_ = 0;
  raw_ptr<CluModelService> model_ = nullptr;
  bool model_ready_ = false;
  bool private_ready_ = false;

  std::string group_id_;
  std::optional<std::string> initial_space_;
  bool hidden_by_switch_ = false;
  bool switch_pending_ = false;
  bool frame_subscribed_ = false;
  std::vector<base::OnceClosure> on_frame_ready_;
  std::unique_ptr<CluWindowFade> fade_;
  std::optional<SwipeState> swipe_;
  base::OneShotTimer swipe_stale_timer_;
  // windows[id].wheel: the frame's trackpad phases, so the sidebar swipe ends when the fingers lift.
  std::unique_ptr<CluWheelLiftWatcher> wheel_watcher_;
  bool wheel_phased_ = false;
  int wheel_lifts_ = 0;
  std::map<std::string, base::OnceCallback<void(base::Value)>, std::less<>>
      prompt_callbacks_;
  std::unique_ptr<CluSpaceMenu> space_menu_;
  std::unique_ptr<CluDownloads> downloads_;
  std::unique_ptr<CluScreenshotWatcher> screenshots_;
  std::unique_ptr<image_editor::ScreenshotFlow> capture_text_flow_;

  base::DictValue published_;
  int rev_ = 0;
  bool dirty_ = false;
  base::OneShotTimer publish_timer_;
  mojo::RemoteSet<mojom::StateObserver> observers_;

  std::optional<CommandSessionSnapshot> command_;
  // windows[id].sidebarRequest: AskSidebar() sets it, with the next seq.
  std::optional<SidebarRequestSnapshot> sidebar_request_;
  std::string command_instance_id_;
  std::vector<OverlaySurfaceSnapshot> prompts_;
  std::vector<ToastSnapshot> toasts_;
  std::string toast_instance_id_;
  // The content size the passive overlays were placed for: a resize places them again.
  gfx::Size passive_content_size_;
  // The page card the toast was placed in: toolbar mode or the sidebar moves the card without a resize.
  gfx::Rect passive_page_rect_;
  base::OneShotTimer toast_timer_;
  // While a link is under the pointer: the link, and since when Shift has been held (null while it is up).
  // After a hover opens a preview, hover_preview_timer_ reads the cursor until it leaves the link. Its own
  // timer: the page's next target URL (the Peek covering the link) restarts hover_link_timer_.
  void WatchHoverPreview(const gfx::Point& origin);
  base::RepeatingTimer hover_preview_timer_;
  base::RepeatingTimer hover_link_timer_;
  GURL hover_link_url_;
  base::TimeTicks hover_shift_since_;
  std::optional<OverlaySurfaceSnapshot> switcher_surface_;
  std::optional<OverlaySurfaceSnapshot> hover_surface_;
  std::string hover_row_id_;
  // Runs while the tab hover card fades out (kHoverCardOut); HideHoverNow ends it.
  base::OneShotTimer hover_close_timer_;
  // Where the tab hover card hangs from, so a title change while it shows redraws it there.
  gfx::Rect hover_anchor_;
  // Gmail rows' unread mail, and the row (and where its card hangs from) whose card waits for its feed.
  CluGmailFeed gmail_feed_;
  std::string gmail_wait_row_id_;
  gfx::Rect gmail_wait_anchor_;
  // The pointer is over the folder popover that hover_surface_ shows.
  bool folder_peek_held_ = false;
  // The pointer is over the Gmail card that hover_surface_ shows.
  bool mail_card_held_ = false;
  std::optional<bool> menu_light_;
  bool archive_panel_ = false;

  TabMru mru_;
  std::unique_ptr<CluSwitcher> switcher_;
  std::unique_ptr<views::EventMonitor> event_monitor_;
  // Presses on the browser window while the top overlay covers only its own surfaces (the tweak editor).
  std::unique_ptr<views::EventMonitor> page_click_monitor_;
  std::unique_ptr<CluTabMenu> tab_menu_;
  std::unique_ptr<CluWindowAppState> app_state_;
  std::unique_ptr<ui::MenuModel> nav_menu_model_;
  std::unique_ptr<views::MenuRunner> nav_menu_runner_;
  std::unique_ptr<CluWindowSite> site_;
  // Rows whose page this window is closing itself: their removal from the strip is no close.
  std::set<std::string> dropping_;
  // Without a model: when each tab was last active (epoch ms), by Clu tab id.
  std::map<std::string, double, std::less<>> last_active_at_;

  std::unique_ptr<CluWindowMultitask> multitask_;
  std::unique_ptr<CluWindowSidebarPeek> sidebar_peek_;

  std::vector<base::CallbackListSubscription> subscriptions_;
  base::ScopedObservation<CluShell, CluShell::Observer> shell_observation_{
      this};
  base::ScopedObservation<CluModelService, CluModelService::Observer>
      model_observation_{this};
  base::ScopedObservation<CluSpacesService, CluSpacesService::Observer>
      spaces_observation_{this};
  base::ScopedObservation<TemplateURLService, TemplateURLServiceObserver>
      engines_observation_{this};
  ui::ScopedUnownedUserData<CluWindowState> scoped_unowned_user_data_;
  // Icons for favorites and pinned rows never opened in this profile, made on first use.
  std::unique_ptr<CluIconBackfill> icon_backfill_;
  // Row id to its Calendar subtitle, and the page entry (its unique id) last read and when.
  std::map<std::string, std::string> calendar_subtitles_;
  std::map<std::string, std::pair<int, base::TimeTicks>> calendar_read_at_;
  base::WeakPtrFactory<CluWindowState> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WINDOW_STATE_H_
