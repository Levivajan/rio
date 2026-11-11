// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_BROWSER_VIEW_CLU_LAYOUT_IMPL_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_BROWSER_VIEW_CLU_LAYOUT_IMPL_H_

#include <memory>

#include "chrome/browser/ui/views/frame/layout/browser_view_layout.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_delegate.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_impl.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"

class BrowserWindowInterface;

namespace clu {

class CluShell;

// The layout of a normal Clu window (docs/chromium/PLAN.md 1.3 "Layout"), next to Chromium's tabbed, popup and app
// layouts. The frame WebUI fills the client area; the page contents sit in the page card at the rect CluShell
// computes (the frame WebUI draws the card there); infobars sit at the top of the card and a visible side panel at
// its trailing edge. The tab strips, the toolbar and its top container are hidden. CluAnchorView sits at the card's
// top leading corner for native bubbles.
class BrowserViewCluLayoutImpl : public BrowserViewLayoutImpl {
 public:
  BrowserViewCluLayoutImpl(std::unique_ptr<BrowserViewLayoutDelegate> delegate,
                           BrowserWindowInterface* browser,
                           BrowserViewLayoutViews views);
  BrowserViewCluLayoutImpl(const BrowserViewCluLayoutImpl&) = delete;
  BrowserViewCluLayoutImpl& operator=(const BrowserViewCluLayoutImpl&) =
      delete;
  ~BrowserViewCluLayoutImpl() override;

  CluShell& shell() { return *shell_; }

 protected:
  // BrowserViewLayoutImpl:
  gfx::Size GetMinimumSize(const views::View* host) const override;
  ProposedLayout CalculateProposedLayout(
      const BrowserLayoutParams& params) const override;
  gfx::Rect CalculateTopContainerLayout(ProposedLayout& layout,
                                        BrowserLayoutParams params,
                                        bool needs_exclusion) const override;
  void DoPreLayoutComputations(const BrowserLayoutParams& params) override;
  // Rounds the page to the card's radius (M11: 6.5), square at the top while an infobar sits on the card.
  void DoPostLayoutVisualAdjustments(const BrowserLayoutParams& params) override;

 private:
  // BrowserViewLayoutImpl (virtual through chrome-browser-ui-views-frame-layout-browser_view_layout_impl.h.patch):
  // browser-modal dialogs and sheets drop from the page card's top edge, not from a toolbar Clu does not show.
  int GetDialogTop(const ProposedLayout& layout) const override;

  // Hides `view` if it is a child of the browser view.
  void HideChild(ProposedLayout& layout, views::View* view) const;

  std::unique_ptr<CluShell> shell_;
};

// BrowserViewLayout::CreateLayout() calls this for TYPE_NORMAL windows (patch
// chrome-browser-ui-views-frame-layout-browser_view_layout.cc.patch).
std::unique_ptr<BrowserViewLayout> CreateBrowserViewCluLayout(
    std::unique_ptr<BrowserViewLayoutDelegate> delegate,
    BrowserWindowInterface* browser,
    BrowserViewLayoutViews views);

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_BROWSER_VIEW_CLU_LAYOUT_IMPL_H_
