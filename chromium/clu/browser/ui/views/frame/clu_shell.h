// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_SHELL_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_SHELL_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "content/public/browser/web_contents_delegate.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class MultiContentsView;

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace views {
class FocusManager;
class View;
class WebView;
}  // namespace views

namespace clu {

class TitlebarRevealWatch;

class CluAnchorView;
class CluPeekView;

DECLARE_ELEMENT_IDENTIFIER_VALUE(kCluFrameWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kCluOverlayWebViewElementId);

// One normal window's Clu shell (docs/chromium/PLAN.md 1.3): the sidebar and focus-mode state and the shell layout
// computed from it, the frame WebUI view under the page card, the top overlay widget over the window, and the anchor
// view native bubbles hang from. The window's BrowserViewCluLayoutImpl owns it, so it lives exactly as long as the
// Clu layout of that window. It is registered on the window, so CluShell::From(browser) finds it.
//
// The bridge side (//clu/browser/ui/webui) attaches a Client through the client factory when the shell is made. The
// shell owns the client and destroys the WebUI documents first, so a document never outlives the client it calls.
class CluShell : public content::WebContentsDelegate,
                 public views::WidgetObserver,
                 public views::ViewObserver,
                 public ui::AcceleratorTarget {
 public:
  DECLARE_USER_DATA(CluShell);

  // What the bridge side provides to the window.
  class Client {
   public:
    virtual ~Client() = default;

    // Runs the command table id `id` (clu/browser/commands/command_table.h). Returns false when the id has no
    // handler in this build, or none right now (no tab to act on), so the caller can fall back to Chromium's own
    // command or let the key go on.
    virtual bool RunCommand(std::string_view id) = 0;
    // RunCommand() for a chord of the key table, held as the user holds it: the recent-tab switcher waits for the
    // Control key to go up only then (a menu or palette pick switches at once).
    virtual bool RunKeyCommand(std::string_view id) = 0;
    // Whether RunCommand(id) would run something now: the main menu enables the item, and the command bar lists
    // the palette row, only then.
    virtual bool CanRunCommand(std::string_view id) = 0;
    // A page command (nav.*, find.*, zoom.*) for the window's Peek while it takes page commands: nullopt when there
    // is none, so Chromium runs it on the active tab; else whether it would run (`run` false) or ran.
    virtual std::optional<bool> PeekPageCommand(std::string_view id,
                                                bool run) = 0;
    // The site controls popover for the active page, hanging from `anchor` (frame WebUI coordinates): a split
    // pane's toolbar opens it as the URL pill's glyph does.
    virtual void OpenSiteControls(const gfx::Rect& anchor) = 0;
  };

  class Observer : public base::CheckedObserver {
   public:
    // The shell layout, the sidebar or focus-mode state, or the sidebar tween changed. `urgent` is set when a tween
    // starts: the frame WebUI starts its slide from that state, so it should not wait for the next 16 ms batch.
    virtual void OnShellChanged(bool urgent) = 0;
  };

  // `browser_view` is the window's BrowserView; the frame view is added to it as its bottom child.
  CluShell(BrowserWindowInterface* browser, views::View* browser_view);
  CluShell(const CluShell&) = delete;
  CluShell& operator=(const CluShell&) = delete;
  ~CluShell() override;

  // Null when `browser` is not a normal window with the Clu layout.
  static CluShell* From(BrowserWindowInterface* browser);

  // ---- Mini window and Peek (docs/chromium/m6.md) -------------------------------------------------------------------

  // While one lives, a popup window being made is Clu's Mini window: it gets
  // the Clu layout in Mini mode (its toolbar over one page, no sidebar), the
  // Mini variant of the key table and a hidden title bar.
  class ScopedCreatingMiniWindow {
   public:
    ScopedCreatingMiniWindow();
    ScopedCreatingMiniWindow(const ScopedCreatingMiniWindow&) = delete;
    ScopedCreatingMiniWindow& operator=(const ScopedCreatingMiniWindow&) =
        delete;
    ~ScopedCreatingMiniWindow();
  };
  bool is_mini() const { return mini_; }
  // The Mini window's confirm strip pushes its page down (MiniPageRect).
  void SetMiniConfirming(bool confirming);

  // The Peek layer, made at the first Peek and kept for the window's life
  // (CluPeekView). Shown only while a Peek is open or closing.
  CluPeekView* EnsurePeekView();
  CluPeekView* peek_view() { return peek_view_; }
  void SetPeekShown(bool shown);
  bool peek_shown() const { return peek_shown_; }
  // OverlaySlotOf() for the under-peek overlay's contents.
  static constexpr int kUnderPeekSlot = -2;

  // The two panes of Chromium's split view in frame WebUI coordinates, in pane
  // order, or none when the window shows one page (windows[id].splitView).
  // MultiContentsView lays them out; the shell reads its container views.
  struct PaneRect {
    raw_ptr<content::WebContents> contents;
    gfx::Rect rect;
  };
  std::vector<PaneRect> SplitPaneRects();
  // The layout hands over the window's MultiContentsView; pane moves notify the
  // observers.
  void SetMultiContentsView(MultiContentsView* view);

  // A split of 3 or 4 panes, which Chromium's two-pane split cannot
  // show: `panes` in pane order, `active` the index of the page in
  // MultiContentsView. Every other pane shows in a WebView the shell hosts. The
  // page card splits by `shares` (one per pane, summing to 1) with
  // kExtraPaneGap between the panes, stacked or side by side.
  void SetExtraPanes(std::vector<content::WebContents*> panes,
                     std::vector<double> shares,
                     size_t active,
                     bool stacked);
  static constexpr int kExtraPaneGap = 10;
  // The room the shares divide: the card's length less the gaps.
  int ExtraPaneRoom(const gfx::Rect& card) const;
  // Back to one page or Chromium's split. Also called before a tab change,
  // so a page never sits in two views at once.
  void ClearExtraPanes();
  // A divider drag: the same panes at new shares.
  void SetExtraShares(std::vector<double> shares);
  bool has_extra_panes() const { return !extra_panes_.empty(); }
  // Where each pane sits inside `card` (the layout's contents bounds), in pane
  // order; empty without extra panes.
  // Each pane's page sits under a kPaneBarHeight bar the frame WebUI draws
  // (PANE_BAR in src/shared/split.ts: the pane toolbar).
  std::vector<gfx::Rect> ExtraPaneRects(const gfx::Rect& card) const;
  static constexpr int kPaneBarHeight = 32;
  size_t extra_active() const { return extra_active_; }
  // The hosted view of pane `index`, or null for the active pane.
  views::WebView* ExtraPaneView(size_t index) const;

  BrowserWindowInterface* browser() { return browser_; }

  // Sidebar and focus mode. Each relays out the window.
  const ChromeState& chrome_state() const { return chrome_; }
  void ToggleSidebar();
  void ToggleFocusMode();
  // Toolbar mode (View > Show Toolbar, Cmd+Shift+D): the URL bar across the top of the page. A new window starts
  // with the mode the last window left (clu.shell.toolbar), without a tween.
  void ToggleToolbar();
  void SetToolbar(bool shown);
  // The front page's site is in Developer Mode: the strip's band shows above the card, without a tween.
  void SetDevStrip(bool shown);
  // `width` comes from a resize drag; junk falls back to the default and the 164..500 range applies.
  void SetSidebarWidth(double width);
  // The Library (archive panel) widens the docked sidebar while it is open.
  void SetLibraryOpen(bool open);
  // The width the Library's section asks for (archive.setWidth): at least kLibraryWidth, and the page keeps
  // its minimum room. 0 goes back to kLibraryWidth.
  void SetLibraryWidth(int width);
  void ResetSidebarWidth();
  // M8: a new window starts with the sidebar width and hidden state the last window left (shell.lastSidebarWidth,
  // F5 and F13), without a tween.
  void RestoreSidebar(double width, bool hidden);

  // Called by the layout before each pass with the browser view's client area size and whether the window is in full
  // screen. A size change ends a running tween at its target.
  void UpdateForLayout(const gfx::Size& content_size, bool fullscreen);

  // In the window's own full screen, the height the bar macOS slides down at the screen's top covers at the
  // window's top (windows[id].shell.titlebarReveal): the frame moves its top row down by it. 0 otherwise.
  int titlebar_reveal() const { return titlebar_reveal_; }
  void SetTitlebarReveal(int covered);

  // The layout the frame WebUI draws (windows[id].shell.layout).
  const ShellLayout& layout() const { return layout_; }
  // Where the page contents sit right now: the layout's page rect, or the current tween tick's rect.
  const gfx::Rect& shown_page_rect() const { return shown_page_rect_; }
  // The running sidebar tween (windows[id].shell.sidebar.anim), if any.
  const std::optional<SidebarAnim>& sidebar_anim() const { return anim_; }
  // Tests: whether the running tween steps on the compositor's frames, and how many frames the last tween drew.
  bool tween_on_compositor_for_testing() const { return !!tween_driver_; }
  int tween_steps_for_testing() const { return tween_steps_; }

  views::WebView* frame_view() { return frame_view_; }
  CluAnchorView* anchor_view() { return anchor_view_; }
  // Extension popups hang from the action button the user clicked (M7): `rect` in frame WebUI coordinates, the
  // client area's. Until a button is clicked they use anchor_view().
  CluAnchorView* extension_anchor_view() { return extension_anchor_view_; }
  const std::optional<gfx::Rect>& extension_anchor_rect() const {
    return extension_anchor_rect_;
  }
  // The click on `id`'s button at `rect`. Only `id`'s next popup hangs from it, and only until the layout changes
  // (the button may have moved or hidden with the sidebar); the anchor view stays where an open popup hangs.
  void SetExtensionAnchorRect(const std::string& id, const gfx::Rect& rect);
  // The view `id`'s popup hangs from now.
  views::View* ExtensionPopupAnchorFor(const std::string& id);
  // Chromium's downloads bubble hangs from the sidebar's downloads button while it shows (docs/chromium/fixes.md):
  // the frame reports the button's rect in frame WebUI coordinates, none while the button is not there.
  CluAnchorView* downloads_anchor_view() { return downloads_anchor_view_; }
  void SetDownloadsButtonRect(const std::optional<gfx::Rect>& rect);
  const std::optional<gfx::Rect>& downloads_button_rect() const {
    return downloads_button_rect_;
  }
  // The button's rect while the bubble may hang from it now (DownloadsButtonAnchor()).
  std::optional<gfx::Rect> DownloadsAnchorRect() const;
  // The view the downloads bubble hangs from now: the button's anchor, else anchor_view().
  views::View* DownloadBubbleAnchor();
  content::WebContents* frame_contents();
  content::WebContents* overlay_contents();

  // The top overlay (chrome://clu-overlay/?layer=top) covers the window while a
  // surface is open in it. `focus` false shows it without taking focus (the
  // split drop preview during a sidebar drag). `rect`, in window coordinates,
  // shrinks it to that part of the window, so the rest takes clicks (the tweak
  // editor beside the page, docs/chromium/fixes.md).
  void ShowOverlay(bool focus = true,
                   const std::optional<gfx::Rect>& rect = std::nullopt);
  void HideOverlay();
  bool IsOverlayVisible() const;
  // The part of the window the shown top overlay covers, or none for all of it.
  const std::optional<gfx::Rect>& top_overlay_rect() const {
    return top_overlay_rect_;
  }
  views::Widget* overlay_widget_for_testing() { return overlay_widget_.get(); }

  // The passive overlays (chrome://clu-overlay/?layer=passive&slot=N): toasts, the Ctrl+Tab switcher, the hover
  // card and the hover peek of a hidden sidebar. Each is a child widget sized to its surface's rect (window
  // coordinates) that takes no focus, and covers nothing but its card, unless ShowPassive asks for focus.
  static constexpr int kPassiveSlots = 4;
  // `click_through`: clicks go to the frame under it (the hover card); otherwise the surface takes them. `focus`: the
  // slot may activate and takes the keyboard (the folder popover's search field); hiding it or showing it
  // again without focus gives the keyboard back to the window.
  void ShowPassive(int slot,
                   const gfx::Rect& rect,
                   bool click_through = false,
                   bool focus = false);
  void HidePassive(int slot);
  bool IsPassiveVisible(int slot) const;
  content::WebContents* passive_contents(int slot);
  views::Widget* passive_widget(int slot) {
    return slot >= 0 && slot < kPassiveSlots ? passive_[slot].widget.get()
                                             : nullptr;
  }
  // Tests: where passive overlay `slot` sits, in content coordinates.
  gfx::Rect passive_rect_for_testing(int slot) const {
    return slot >= 0 && slot < kPassiveSlots ? passive_[slot].rect
                                             : gfx::Rect();
  }
  // The hover card's live view of a site's compact page (LivePreviewUrl()), laid over the card at `rect`
  // (window coordinates) with the card's corners. Its own page in `context`, the hovered tab's profile, so the tab's
  // sign-in applies and the tab itself never navigates. The same URL and profile only move it; hiding closes it.
  // `user_agent` (LivePreviewUserAgent()) applies to that page alone, so the site serves its narrow view.
  void ShowLivePreview(content::BrowserContext* context,
                       const GURL& url,
                       const gfx::Rect& rect,
                       const std::string& user_agent);
  void HideLivePreview();
  content::WebContents* live_preview_contents_for_testing();
  // The overlay slot `contents` hosts: -1 for the top overlay, kUnderPeekSlot
  // for the under-peek overlay, 0..3 for a passive one, nullopt for anything
  // else.
  std::optional<int> OverlaySlotOf(content::WebContents* contents);

  // The page area shows only while the window shows a tab row; with none (the empty state) the frame WebUI draws the
  // card alone.
  void SetPageShown(bool shown);
  bool page_shown() const { return page_shown_; }

  Client* client() { return client_.get(); }
  void SetClient(std::unique_ptr<Client> client);

  // Attaches the window's client (it calls SetClient()). //clu/browser/ui/webui sets it when it registers its WebUIs,
  // so every shell has its client from the start, and Cmd+T or Cmd+L never fall back to Chromium's commands while
  // the frame WebUI is still connecting.
  using ClientFactory = void (*)(BrowserWindowInterface* browser);
  static void SetClientFactory(ClientFactory factory);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // content::WebContentsDelegate (frame and overlay contents): reserved chords run before Clu's own WebUIs see the
  // key, as they do before a page (Mac's prePerformKeyEquivalent does the same for real key events); every other key
  // the WebUI leaves alone goes to the window's accelerators.
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;
  bool HandleContextMenu(content::RenderFrameHost& render_frame_host,
                         const content::ContextMenuParams& params) override;
  // The frame WebUI's app-region: drag areas (it opts in when it is made): the sidebar's empty space, the strip over
  // the page card. Kept in frame WebUI coordinates for IsWindowDragArea().
  void DraggableRegionsChanged(
      const std::vector<blink::mojom::DraggableRegionPtr>& regions,
      content::WebContents* contents) override;

  // Whether `point` (browser view coordinates) is a drag area of the frame WebUI that nothing of the window covers
  // (the page, a Peek, a split pane's bar), so the window answers title bar there: a drag moves the window and a
  // double click zooms it (the AppleActionOnDoubleClick setting), like a Mac title bar. Never in full
  // screen.
  bool IsWindowDragArea(const gfx::Point& point);

  // ui::AcceleratorTarget: every chord of the docs/PLAN.md 8 key table (main variant), registered on the window's
  // focus manager, which the overlay widgets share (docs/chromium/m5.md section 1).
  // - Reserved chords (route router, and Next and Previous Space) are high-priority accelerators. Mac runs them
  //   before a focused page sees the key (ChromeCommandDispatcherDelegate's prePerformKeyEquivalent), and
  //   BrowserView::PreHandleKeyboardEvent() does on every other path. They are always consumed, even when the
  //   command has nothing to do, so the page never sees them.
  // - Menu chords are normal-priority accelerators. The page gets the key first, because the main menu holds the
  //   same chord (BrowserNativeWidgetMac::PreHandleKeyboardEvent() then answers NOT_HANDLED_IS_SHORTCUT); a key the
  //   page leaves alone comes back through postPerformKeyEquivalent or BrowserView::HandleKeyboardEvent() and runs.
  //   A command with nothing to do lets the key go on.
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  struct KeyBinding {
    ui::Accelerator accelerator;
    std::string id;
    bool reserved = false;
    // The chord stands for a Chromium command (Cmd+Shift+T is IDC_RESTORE_TAB): when Clu declines it, the key goes
    // on to Chromium's own command instead of being consumed.
    bool chromium_command = false;
  };
  // The chords and their command table ids, from the generated table with the
  // user's custom shortcuts (clu_shortcuts.h): the main variant, or with `mini`
  // the Mini window's (docs/PLAN.md 8 "Mini variant").
  static std::vector<KeyBinding> KeyBindings(bool mini = false);
  // Tests: once RecordKeysForTesting() ran, the command ids the key table ran, in order. With `record_only`, a chord
  // records its id and answers as it would (handled when reserved or runnable) without running, so a test can walk
  // the whole table without its side effects.
  void RecordKeysForTesting() { record_keys_for_testing_ = true; }
  const std::vector<std::string>& key_commands_for_testing() const {
    return key_commands_for_testing_;
  }
  void ClearKeyCommandsForTesting() { key_commands_for_testing_.clear(); }
  void SetRecordOnlyForTesting(bool record_only) {
    record_only_for_testing_ = record_only;
    record_keys_for_testing_ |= record_only;
  }

  // views::WidgetObserver (the browser widget):
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  // views::ViewObserver (the split panes of MultiContentsView):
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  // The layout for `size` from the current chrome state. A Mini window has no sidebar whatever that state says: its
  // page sits at MiniPageRect, its window buttons show and it has no hot zone.
  ShellLayout LayoutFor(const gfx::Size& size) const;
  // Recomputes the layout from the current chrome state. With `animate`, a page rect change tweens; `library` makes
  // it the Library's kLibraryMs exponential tween.
  void Relayout(bool animate, bool library = false);
  void StartTween(const gfx::Rect& from,
                  const gfx::Rect& to,
                  int ms,
                  bool exponential);
  // Draws the tween at `now_ms` (fractional milliseconds since the Unix epoch, the frame WebUI's Date.now() clock).
  void StepTween(double now_ms);
  void StopTween();
  void NotifyChanged(bool urgent);
  void CreateOverlay();
  void CreatePassiveOverlays();
  void UpdateOverlayBounds();
  void RegisterKeyTable(views::Widget* widget);
  // The user's shortcuts changed (clu_shortcuts.h): the window's keys again.
  void RebindKeyTable();
  void UnregisterAccelerators();
  // ClearExtraPanes without relayout or notification (also the destructor's).
  void DropExtraViews();

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<views::View> browser_view_;

  const bool mini_;
  bool mini_confirming_ = false;
  raw_ptr<CluPeekView> peek_view_ = nullptr;
  bool peek_shown_ = false;
  raw_ptr<MultiContentsView> multi_contents_view_ = nullptr;
  // SetExtraPanes: the panes in order and one hosted view per pane (null at the
  // active index). The views are children of the browser view.
  std::vector<raw_ptr<content::WebContents>> extra_panes_;
  std::vector<raw_ptr<views::WebView>> extra_views_;
  std::vector<double> extra_shares_;
  size_t extra_active_ = 0;
  bool extra_stacked_ = false;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      pane_observations_{this};

  ChromeState chrome_;
  ShellLayout layout_;
  // Follows the full-screen titlebar's reveal while the window is in its own full screen.
  std::unique_ptr<TitlebarRevealWatch> titlebar_watch_;
  int titlebar_reveal_ = 0;
  gfx::Rect shown_page_rect_;
  std::optional<SidebarAnim> anim_;
  // Steps the tween on the window compositor's frames (clu_shell.cc).
  class TweenDriver;
  std::unique_ptr<TweenDriver> tween_driver_;
  // Lands the tween if no frame comes (a window with no compositor, or occluded so it begins no frames).
  base::OneShotTimer tween_timer_;
  int tween_steps_ = 0;

  // Destroyed after the WebUI documents below (see ~CluShell()).
  std::unique_ptr<Client> client_;

  raw_ptr<views::WebView> frame_view_ = nullptr;
  SkRegion drag_region_;
  raw_ptr<CluAnchorView> anchor_view_ = nullptr;
  raw_ptr<CluAnchorView> extension_anchor_view_ = nullptr;
  raw_ptr<CluAnchorView> downloads_anchor_view_ = nullptr;
  std::optional<gfx::Rect> downloads_button_rect_;
  std::optional<gfx::Rect> extension_anchor_rect_;
  // The action whose button extension_anchor_rect_ is; cleared by any layout change.
  std::string extension_anchor_id_;
  std::unique_ptr<views::Widget> overlay_widget_;
  std::optional<gfx::Rect> top_overlay_rect_;
  raw_ptr<views::WebView> overlay_view_ = nullptr;
  struct Passive {
    Passive();
    Passive(Passive&&);
    Passive& operator=(Passive&&);
    ~Passive();

    std::unique_ptr<views::Widget> widget;
    raw_ptr<views::WebView> view = nullptr;
    gfx::Rect rect;
  };
  std::array<Passive, kPassiveSlots> passive_;
  std::unique_ptr<views::Widget> live_preview_widget_;
  raw_ptr<views::WebView> live_preview_view_ = nullptr;
  gfx::Rect live_preview_rect_;
  GURL live_preview_url_;
  bool page_shown_ = true;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  // The chords this shell registered (one with a high-priority handler from an extension is left to it).
  std::vector<KeyBinding> bindings_;
  base::CallbackListSubscription shortcuts_subscription_;
  std::vector<std::string> key_commands_for_testing_;
  bool record_only_for_testing_ = false;
  bool record_keys_for_testing_ = false;

  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      browser_widget_observation_{this};
  ui::ScopedUnownedUserData<CluShell> scoped_unowned_user_data_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_SHELL_H_
