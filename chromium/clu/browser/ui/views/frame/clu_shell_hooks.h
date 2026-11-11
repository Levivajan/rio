// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_SHELL_HOOKS_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_SHELL_HOOKS_H_

#include <memory>
#include <string>

class BrowserWindowInterface;

namespace gfx {
class Point;
}  // namespace gfx

namespace ui {
struct ColorProviderKey;
}  // namespace ui

namespace views {
class View;
}  // namespace views

// The one-line hooks upstream patches call into the Clu shell (clu_shell.cc). This header has its own GN target with
// no dependencies, so the toolbar, bubble, extension and command controller targets can include it without a cycle
// through //clu/browser/ui/views/frame. Each answers false or null for a window without a Clu shell.
namespace clu {

// BrowserCommandController: runs Clu's command for the Chromium command `command_id` (IDC_NEW_TAB opens the command
// bar, IDC_FOCUS_LOCATION edits the URL, the tab ids run the tabs module's commands, IDC_CLU_* from Clu's main menu run
// their table entry; clu/browser/commands/clu_command_routes.h). True when Clu handled it.
bool HandleBrowserCommand(BrowserWindowInterface* browser, int command_id);

// BrowserCommandController::IsReservedCommandOrKey(): Clu's router keys that Chromium does not reserve itself
// (Cmd+1..9, Cmd+L, the IDC_CLU_* ids of router entries, docs/PLAN.md 8) never reach the page.
bool IsReservedCommand(BrowserWindowInterface* browser, int command_id);

// BrowserCommandController::SupportsCommand() and IsCommandEnabled(): Clu's IDC_CLU_* ids exist in every normal
// window (the main menu validates its items through them), and are enabled while the window's Clu command can run.
bool SupportsCluCommand(BrowserWindowInterface* browser, int command_id);
bool IsCluCommandEnabled(BrowserWindowInterface* browser, int command_id);

// BrowserCommandController::IsCommandEnabled(): while a Peek takes page commands (its page or its find bar has focus),
// Back, Forward, Reload, Stop, Find and Zoom follow the Peek page, not the tab under it (docs/chromium/m6.md). True
// when Clu decides, with the answer in `enabled`.
bool OverrideCommandEnabled(BrowserWindowInterface* browser,
                            int command_id,
                            bool* enabled);

// BrowserCommandController: commands Clu keeps enabled whatever Chromium's own state says (Cmd+Shift+T reopens from
// Clu's closed stack and archive, not only from TabRestoreService).
bool KeepsCommandEnabled(BrowserWindowInterface* browser, int command_id);

// Toolbar, location bar and extension anchors: the view bubbles anchor to while the toolbar is hidden
// (CluAnchorView).
views::View* GetBubbleAnchorView(BrowserWindowInterface* browser);

// The look (M11, clu_look.cc):
// BrowserNativeWidgetMac::GetWindowFrameTitlebarHeight(): the titlebar height that centers macOS's window buttons on
// the sidebar's top row (twice the buttons' center y). True for a normal Clu window.
bool GetTitlebarHeight(BrowserWindowInterface* browser, float* height);
// BrowserView::NonClientHitTest(): true where the frame WebUI marks app-region: drag and nothing covers it, so the
// window answers title bar (HTCAPTION) there and a drag moves it. `point` is in `browser_view`'s parent
// coordinates, as NonClientHitTest() gets it.
bool IsWindowDragArea(BrowserWindowInterface* browser,
                      views::View* browser_view,
                      const gfx::Point& point);
// TabModalDialogHost: the y, in widget coordinates, tab-modal dialogs drop from (the page card's top edge). True
// when `browser` has a Clu shell.
bool GetTabModalDialogTop(BrowserWindowInterface* browser, int* y);
// BrowserWidget::GetColorProviderKey(): gives a normal Clu window Clu's color initializer, which runs after
// Chromium's mixers and paints the surfaces Chromium draws in the window (frame, bubbles, dialogs, menus, side
// panel, find bar) in Clu's tokens. A window that already has an app controller keeps it.
void AdjustColorProviderKey(BrowserWindowInterface* browser,
                            ui::ColorProviderKey& key);
// DownloadToolbarUIController::ShowBubble(): the view Chromium's downloads bubble hangs from, the sidebar's downloads
// button while it shows, else CluAnchorView (docs/chromium/fixes.md). Null for a window without a Clu shell.
views::View* GetDownloadBubbleAnchorView(BrowserWindowInterface* browser);
// Extension popups (ExtensionsToolbarDesktop::GetReferenceButtonForPopup): for `action_id`, the action button the user
// just clicked in the sidebar's action row or the extensions menu, else CluAnchorView (M7). A popup from a keyboard
// shortcut, chrome.action.openPopup() or another extension never hangs from an old click's button.
views::View* GetExtensionPopupAnchorView(BrowserWindowInterface* browser,
                                         const std::string& action_id);

// BrowserViewLayout::CreateLayout() and BrowserNativeWidgetMac: the popup
// window being made right now is Clu's Mini window (docs/chromium/m6.md), so it
// gets the Clu layout and a hidden title bar with its toolbar in that row.
bool IsCreatingMiniWindow();

// ContentsContainerView (split view): each pane gets its own toolbar on top (CluPaneBar)
// in place of Chromium's mini toolbar. CreatePaneBar answers null outside a normal window;
// UpdatePaneBar shows the bar while the pane is in a split, marks the focused one, and answers true when `container`
// has a bar (the mini toolbar then stays hidden); PaneBarOf finds it for the layout.
std::unique_ptr<views::View> CreatePaneBar(BrowserWindowInterface* browser,
                                           views::View* contents_web_view);
bool UpdatePaneBar(views::View* container, bool in_split, bool active);
views::View* PaneBarOf(views::View* container);

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_SHELL_HOOKS_H_
