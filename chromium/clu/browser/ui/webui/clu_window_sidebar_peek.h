// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WINDOW_SIDEBAR_PEEK_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WINDOW_SIDEBAR_PEEK_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/scoped_observation.h"
#include "clu/browser/ui/webui/clu_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace base {
class TickClock;
}  // namespace base

namespace blink {
class WebMouseEvent;
}  // namespace blink

namespace clu {

class CluWindowState;

// One window's hover peek (docs/PLAN.md 12.3 third box): CluSidebarPeek's timing rules over the window's shell. The
// peek is the overlay's "sidebar-peek" surface in passive slot kPeekSlot, a non-activatable child widget at the
// sidebar rect plus the bleed, so it never takes focus and covers no more of the page than that. The pointer comes
// from the mouse events Chromium routes to the frame WebUI (the hot zone) and to the peek's WebUI, read in the
// browser process before the renderers see them. CluWindowState owns it and calls in from its surface, shell and
// blur paths.
class CluWindowSidebarPeek : public CluSidebarPeek::Delegate,
                             public views::WidgetObserver {
 public:
  // The passive overlay slot the peek lives in (CluShell::kPassiveSlots).
  static constexpr int kPeekSlot = 3;

  explicit CluWindowSidebarPeek(CluWindowState& window,
                                const base::TickClock* clock = nullptr);
  CluWindowSidebarPeek(const CluWindowSidebarPeek&) = delete;
  CluWindowSidebarPeek& operator=(const CluWindowSidebarPeek&) = delete;
  ~CluWindowSidebarPeek() override;

  CluSidebarPeek& controller() { return peek_; }
  bool is_open() const { return peek_.is_open(); }
  // The open peek's overlay instance, empty while closed: surfaces opened from it carry it as openedFrom.
  const std::string& instance_id() const { return instance_id_; }

  // shell.setSidebarPeek and shell.pointerAlive.
  void SetOpen(bool open);
  void Alive();
  // The window lost focus: the peek goes, unless a hold just ended with the pointer on the panel (the keyboard passing
  // back from the peek's widget after the Library's Back or Escape; CluSidebarPeek::CloseOnBlur).
  void Close();

  // The peek's surface for windows[id].overlay.surfaces, if it is open.
  void AddSurface(std::vector<OverlaySurfaceSnapshot>& surfaces);
  // Before each publish: a surface that opened since the peek did, other than its own children and toasts, closes it
  // (the command bar, a prompt, the switcher, the Peek of a link). `surfaces` is the window's list.
  void Reconcile(const std::vector<OverlaySurfaceSnapshot>& surfaces);
  // The shell layout changed (Cmd+S, focus mode, a resize).
  void OnShellChanged();

  // Children of the open peek: its hover card shows or hides, its native row menu opens or closes.
  void SetHoverCard(bool shown);
  void MenuOpened();
  void MenuClosed();
  // shell.holdSidebarPeek: the Library, or the bottom-left pop-up's row menu or Rename, is open in the peek. It
  // holds the peek open as a child does and takes the keyboard until released.
  void SetHeld(bool held);
  bool held() const { return held_; }
  // A main menu command asked the open peek's tab list (`kind` "rename" or "reveal", AskSidebar): it holds the peek
  // until the action ends.
  void HoldForAction(std::string_view kind);
  // The Library open in the peek is closing (its Back, Escape, its button). Before the panel narrows the hold
  // ends, so the keyboard goes back to the window and the narrow panel never takes it again; after, the pointer decides
  // whether it retracts.
  void LibraryClosing();
  void LibraryClosed();
  // The window took activation back. From a held peek that is a click on the page: the Library open in it closes
  // as Back closes it, and the peek retracts after the grace once the pointer is off it; held by anything else it goes.
  void WindowActivated();
  // A file drag began from the window's Library or a native menu opened: either takes the pointer with no leave event
  // to the peek's widget, so the last point it saw is forgotten and the system pointer decides again.
  void PointerTaken();
  // The peek's widget lost activation while held: unless the window or the widget is active once that settles (the
  // app went, Cmd+Tab), the hold ends and the peek goes, as a window blur takes an unheld one.
  void ReleaseIfInactive();

  // views::WidgetObserver (the peek's widget):
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  // The mouse taps, public for tests: events Chromium routes to the frame's and to the peek's WebUI. They never
  // consume the event.
  bool OnFrameMouseEvent(const blink::WebMouseEvent& event);
  bool OnPeekMouseEvent(const blink::WebMouseEvent& event);

  // CluSidebarPeek::Delegate:
  const ShellLayout& PeekLayout() override;
  bool CanOpen() override;
  void ShowPeek(const gfx::Rect& rect) override;
  void HidePeek() override;
  void SlidePeekOut(bool leaving) override;
  bool PointerInside(const gfx::Rect& rect) override;
  bool PointerOnPeek(const gfx::Rect& rect) override;

  // Browser tests cannot move the system pointer: this stands in for where it is over the peek's panel.
  void set_pointer_on_peek_for_testing(std::optional<bool> on) {
    pointer_on_peek_for_testing_ = on;
  }
  bool has_peek_pointer_for_testing() const {
    return peek_pointer_.has_value();
  }
  // Runs right after a released hold hands the keyboard back to the window, where the real hand-back's blur comes.
  void set_keyboard_handed_back_for_testing(base::RepeatingClosure callback) {
    keyboard_handed_back_for_testing_ = std::move(callback);
  }
  void set_window_active_for_testing(std::optional<bool> active) {
    window_active_for_testing_ = active;
  }
  bool PrefersReducedMotion() override;
  bool PinsPeek() override;
  bool WindowActive() override;

 private:
  class MouseTap;

  void UpdateChildren(bool pointer_taken);

  const raw_ref<CluWindowState> window_;
  CluSidebarPeek peek_;
  std::string instance_id_;
  // Surfaces open when the peek opened: only newer ones close it.
  std::set<std::string> before_;
  bool hover_card_ = false;
  // Whether the sidebar was hidden at the last shell change.
  bool sidebar_hidden_ = false;
  int menus_ = 0;
  bool held_ = false;
  // Where the pointer last was on the peek's own widget, from the events it got, until it leaves the widget or the
  // peek closes, or something takes the pointer (PointerTaken()). The panel decides by it: the system pointer is asked
  // only when there is none.
  std::optional<gfx::PointF> peek_pointer_;
  std::unique_ptr<MouseTap> frame_tap_;
  std::unique_ptr<MouseTap> peek_tap_;
  std::optional<bool> pointer_on_peek_for_testing_;
  base::RepeatingClosure keyboard_handed_back_for_testing_;
  std::optional<bool> window_active_for_testing_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WINDOW_SIDEBAR_PEEK_H_
