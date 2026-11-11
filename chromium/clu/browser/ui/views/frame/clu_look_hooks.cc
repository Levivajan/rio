// Copyright 2026 The Rio Authors. All rights reserved.

// The look's hooks (clu_shell_hooks.h), in :frame because they reach the window's CluShell; the color supplier they
// hand out is in :look (clu_look.cc), which links without chrome.

#include "build/build_config.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "clu/browser/multitask/clu_mini_rules.h"
#include "clu/browser/ui/views/frame/clu_look.h"
#include "clu/browser/ui/views/frame/clu_look_tokens.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"

namespace clu {

namespace {

bool IsCluWindow(BrowserWindowInterface* browser) {
  // Asked while the window is built too, before its shell exists: every normal window gets the Clu layout.
  return browser && browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

}  // namespace

bool GetTitlebarHeight(BrowserWindowInterface* browser, float* height) {
  if (!browser || !height) {
    return false;
  }
  // The Mini window's buttons sit in its toolbar row (asked while it is built too, before its shell exists).
  if (browser->GetType() == BrowserWindowInterface::TYPE_POPUP) {
    const CluShell* shell = CluShell::From(browser);
    if (shell ? !shell->is_mini() : !IsCreatingMiniWindow()) {
      return false;
    }
    *height = kMiniToolbarHeight;
    return true;
  }
  if (!IsCluWindow(browser)) {
    return false;
  }
  // macOS centers the buttons vertically in the titlebar (BrowserWindowFrame's _shouldCenterTrafficLights), so twice
  // the center y puts them on the top row. Their x comes from macOS 26's 13 pt inset plus half a 14 pt button: 20.
  *height = look::kTrafficLightCenterY * 2;
  return true;
}

bool IsWindowDragArea(BrowserWindowInterface* browser,
                      views::View* browser_view,
                      const gfx::Point& point) {
  CluShell* shell = CluShell::From(browser);
  if (!shell || !browser_view || !browser_view->parent() ||
      IsSecondaryClickEvent()) {
    return false;
  }
  return shell->IsWindowDragArea(views::View::ConvertPointToTarget(
      browser_view->parent(), browser_view, point));
}

bool GetTabModalDialogTop(BrowserWindowInterface* browser, int* y) {
  CluShell* shell = CluShell::From(browser);
  if (!shell || !shell->frame_view() || !y) {
    return false;
  }
  // The frame WebUI fills the client area, so the card's rect in its coordinates is the shown page rect.
  *y = shell->frame_view()->ConvertRectToWidget(shell->shown_page_rect()).y();
  return true;
}

void AdjustColorProviderKey(BrowserWindowInterface* browser,
                            ui::ColorProviderKey& key) {
  if (IsCluWindow(browser) && !key.app_controller) {
    key.app_controller = CluColorSupplier::Get();
  }
}

#if !BUILDFLAG(IS_MAC)
void SetWindowButtonsVisible(views::Widget* widget, bool visible) {}
void SetWidgetIgnoresMouse(views::Widget* widget, bool ignore) {}
std::unique_ptr<TitlebarRevealWatch> WatchTitlebarReveal(
    views::Widget* widget,
    base::RepeatingCallback<void(int)> on_reveal) {
  return nullptr;
}
bool IsSecondaryClickEvent() {
  return false;
}
#endif

}  // namespace clu
