// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_SETTINGS_WINDOW_H_
#define CLU_BROWSER_UI_WEBUI_CLU_SETTINGS_WINDOW_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;
class Profile;

namespace views {
class WebView;
}  // namespace views

// Rio's app settings window: a window of its own, 684 x 650
// and fixed, showing chrome://clu-settings (the internal:settings surface) under a native title bar that
// reads the selected tab's name. One for the app. It belongs to the Rio window that opened it: the bridge binds the
// page to that window's state (CluBridgeHost), and when that window closes the page moves to the last active Rio
// window, or the settings window closes with the last one. Cmd+W closes it.
namespace clu {

class CluSettingsWindow : public views::WidgetDelegate,
                          public views::WidgetObserver,
                          public content::WebContentsDelegate,
                          public content::WebContentsObserver,
                          public ui::AcceleratorTarget,
                          public CluAppSettings::Observer {
 public:
  // The window is 684 wide and 650 tall with the 28 pt title bar.
  static constexpr int kWidth = 684;
  static constexpr int kHeight = 650;

  // Opens the window for `browser` (a Rio window) at `tab` (one of the window's tabs, or empty for the one it shows),
  // or brings the open one to the front there. False when `browser` is not a Rio window.
  static bool Show(BrowserWindowInterface* browser, std::string_view tab);
  // The open window, or null.
  static CluSettingsWindow* Get();
  // The Rio window the settings page in `contents` belongs to, or null when `contents` is not that page.
  static BrowserWindowInterface* BrowserFor(content::WebContents* contents);

  CluSettingsWindow(const CluSettingsWindow&) = delete;
  CluSettingsWindow& operator=(const CluSettingsWindow&) = delete;
  ~CluSettingsWindow() override;

  views::Widget* widget() { return widget_.get(); }
  content::WebContents* contents();
  BrowserWindowInterface* browser() { return browser_; }
  void Close();

  // views::WidgetDelegate:
  std::u16string GetWindowTitle() const override;
  // The window's origin lives in Local State (kCluSettingsWindowOriginPref), not in a profile's window placements:
  // it is one window for the app, whichever profile's window it belongs to.
  bool ShouldSaveWindowPlacement() const override;
  void SaveWindowPlacement(const gfx::Rect& bounds,
                           ui::mojom::WindowShowState show_state) override;
  bool GetSavedWindowPlacement(
      const views::Widget* widget,
      gfx::Rect* bounds,
      ui::mojom::WindowShowState* show_state) const override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // content::WebContentsDelegate:
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;

  // content::WebContentsObserver:
  void TitleWasSet(content::NavigationEntry* entry) override;

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // CluAppSettings::Observer: the title bar follows Rio's appearance.
  void OnAppSettingsChanged() override;

 private:
  CluSettingsWindow(BrowserWindowInterface* browser, std::string_view tab);

  void Bind(BrowserWindowInterface* browser);
  void OnBrowserClosed(BrowserWindowInterface* browser);
  // Loads the page at `tab` in the web view's contents (made for the view's profile when it has none).
  void OpenPage(std::string_view tab);
  void Navigate(std::string_view tab);
  void Style();
  void OnCloseSynchronous(views::Widget::ClosedReason reason);
  void DeleteSoon();

  raw_ptr<BrowserWindowInterface> browser_ = nullptr;
  base::CallbackListSubscription browser_close_subscription_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  bool deleting_ = false;
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::ScopedObservation<CluAppSettings, CluAppSettings::Observer>
      settings_observation_{this};
  base::WeakPtrFactory<CluSettingsWindow> weak_ptr_factory_{this};
};

// clu_settings_window_mac.mm: the title bar takes the page's toolbar color (#ffffff, #25282a in dark), the window
// follows Rio's appearance `mode` ("auto", "light", "dark"), and its minimize and zoom buttons are off.
void StyleSettingsWindow(views::Widget* widget, std::string_view mode);
// The centered title follows the window's title (Mac; nothing elsewhere).
void SetSettingsWindowTitle(views::Widget* widget);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_SETTINGS_WINDOW_H_
