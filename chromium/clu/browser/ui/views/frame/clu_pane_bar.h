// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_PANE_BAR_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_PANE_BAR_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace views {
class ImageButton;
class ImageView;
class Label;
class MenuRunner;
class WebView;
}  // namespace views

namespace clu {

// A split pane's own toolbar: 32 tall over the pane's page, back and forward (and reload on the focused pane) on a 37
// pitch from 21 in, the page's URL with a link glyph centered, then the site controls (right - 105), split menu
// (right - 69) and close (right - 37) buttons, over a 1 pt divider. It takes the place of Chromium's mini toolbar in
// a Clu window (ContentsContainerView asks clu_shell_hooks.h) and acts on the pane's own page; a press on it focuses
// the pane.
class CluPaneBar : public views::View,
                   public content::WebContentsObserver,
                   public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(CluPaneBar, views::View)

 public:
  static constexpr int kHeight = 32;

  CluPaneBar(BrowserWindowInterface* browser, views::WebView* web_view);
  CluPaneBar(const CluPaneBar&) = delete;
  CluPaneBar& operator=(const CluPaneBar&) = delete;
  ~CluPaneBar() override;

  void SetActive(bool active);
  bool active() const { return active_; }
  // The URL text the bar shows (host then port and path), and its host in the darker color, for tests.
  std::u16string url_text() const;
  std::u16string host_text() const;
  // The drawn size of each glyph at 1x, for tests: back, forward, reload, site controls, split menu, close, link.
  std::vector<gfx::Size> glyph_sizes() const;
  bool reload_visible() const;
  // Site controls and the split menu: the focused pane's only.
  bool site_controls_visible() const;
  bool split_menu_visible() const;
  // Button bounds in the bar, for tests.
  gfx::Rect back_bounds() const;
  gfx::Rect close_bounds() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DidStartLoading() override;
  void DidStopLoading() override;

  // ui::SimpleMenuModel::Delegate (the split menu):
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;

 private:
  void Attached(views::WebView* web_view);
  void Detached(views::WebView* web_view);
  void Update();
  void UpdateColors();
  bool Dark() const;
  void Back();
  void Forward();
  void Reload();
  void Close();
  void Focus();
  void SiteControls();
  void SplitMenu();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::ImageButton> back_ = nullptr;
  raw_ptr<views::ImageButton> forward_ = nullptr;
  raw_ptr<views::ImageButton> reload_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
  raw_ptr<views::ImageButton> site_ = nullptr;
  raw_ptr<views::ImageButton> menu_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  raw_ptr<views::ImageView> link_ = nullptr;
  // The host, then its port, path and query in a lighter grey.
  raw_ptr<views::Label> url_ = nullptr;
  raw_ptr<views::Label> path_ = nullptr;
  bool active_ = false;
  base::CallbackListSubscription attached_;
  base::CallbackListSubscription detached_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_PANE_BAR_H_
