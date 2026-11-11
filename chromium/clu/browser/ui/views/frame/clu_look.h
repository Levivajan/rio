// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_LOOK_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_LOOK_H_

#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider_key.h"

namespace ui {
class ColorProvider;
}  // namespace ui

namespace views {
class Widget;
}  // namespace views

// The native half of Clu's look (milestone M11, docs/chromium/m11.md): the colors Chromium draws in a Clu window and
// the macOS window buttons. The numbers come from clu_look_tokens.h, generated from src/shared/look/tokens.ts.
namespace clu {

// Clu's color initializer: BrowserWidget::GetColorProviderKey() puts it in a normal Clu window's key as the app
// controller (clu::AdjustColorProviderKey), so it runs after every Chromium mixer and only for Clu windows; child
// widgets (bubbles, dialogs, the find bar, menus) inherit the window's key. It sets surfaces only: the accent stays
// whatever the key's user color gives (the space theme, M8).
class CluColorSupplier : public ui::ColorProviderKey::InitializerSupplier {
 public:
  static CluColorSupplier* Get();

  CluColorSupplier();
  CluColorSupplier(const CluColorSupplier&) = delete;
  CluColorSupplier& operator=(const CluColorSupplier&) = delete;
  ~CluColorSupplier() override;

  // ui::ColorProviderKey::InitializerSupplier:
  void AddColorMixers(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) const override;
};

// The colors the mixer sets, by mode, for the tests.
struct CluNativeColors {
  SkColor frame;
  SkColor surface;
  SkColor page;
  SkColor focus_ring;
  SkColor separator;
  // The focused split pane's outline: the space accent, else the default strong accent.
  SkColor split_focus;
};
CluNativeColors NativeColorsFor(ui::ColorProviderKey::ColorMode mode,
                                std::optional<SkColor> accent = std::nullopt);

// In :frame (clu_look_hooks.cc, clu_window_buttons_mac.mm):
// Shows or hides macOS's close, minimize and zoom buttons (they follow the sidebar: hidden while it is hidden or in
// focus mode). No-op off Mac or for a widget without a native window.
void SetWindowButtonsVisible(views::Widget* widget, bool visible);
// Lets clicks pass through a widget's window to the window under it (the hover card, which only shows a title).
void SetWidgetIgnoresMouse(views::Widget* widget, bool ignore);
// While `widget`'s window is in macOS full screen, follows the bar macOS slides down over the screen's top when
// the pointer reaches it (the menu bar and the titlebar with the window's buttons): `on_reveal` gets the height in
// points it covers at the window's top, 0 once it is gone. The watch ends when the returned object goes. Null off Mac
// or for a widget without a native window.
class TitlebarRevealWatch {
 public:
  virtual ~TitlebarRevealWatch() = default;
};
std::unique_ptr<TitlebarRevealWatch> WatchTitlebarReveal(
    views::Widget* widget,
    base::RepeatingCallback<void(int)> on_reveal);
// True while the app handles a right click or a Control-click: the drag area then gives the click to the frame WebUI
// so its menu opens, and only a left drag moves the window. False off Mac.
bool IsSecondaryClickEvent();

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_LOOK_H_
