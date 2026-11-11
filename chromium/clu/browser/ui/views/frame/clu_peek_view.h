// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_PEEK_VIEW_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_PEEK_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
class WebContentsDelegate;
}  // namespace content

namespace views {
class WebView;
}  // namespace views

namespace clu {

// The Peek layer of a window (docs/chromium/PLAN.md 1.3 item 4,
// docs/chromium/m6.md): a view over the page area that holds, bottom to top,
// the under-peek overlay WebUI (chrome://clu-overlay/?layer=under-peek: the
// scrim, the card frame and its animation, the host and the button rail) and
// the Peek page itself, a tabless WebContents at the card rect with rounded
// corners. The overlay widgets are child widgets of the window, so toasts,
// prompts and the command bar always show above it.
class CluPeekView : public views::View {
  METADATA_HEADER(CluPeekView, views::View)

 public:
  CluPeekView(BrowserWindowInterface* browser,
              content::WebContentsDelegate* overlay_delegate);
  CluPeekView(const CluPeekView&) = delete;
  CluPeekView& operator=(const CluPeekView&) = delete;
  ~CluPeekView() override;

  content::WebContents* under_contents();
  // The page shown in the card, or null. The view never owns it.
  void SetPage(content::WebContents* page);
  content::WebContents* page();
  // The card in this view's coordinates (window coordinates: the view fills the
  // client area).
  void SetCard(const gfx::Rect& card);
  // The page shows once the overlay's open animation is done (05 "Opening
  // animation").
  void SetPageVisible(bool visible);
  bool page_visible() const { return page_visible_; }
  views::WebView* page_view() { return page_view_; }

  // views::View:
  void Layout(PassKey) override;

 private:
  raw_ptr<views::WebView> under_view_ = nullptr;
  raw_ptr<views::WebView> page_view_ = nullptr;
  gfx::Rect card_;
  bool page_visible_ = false;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_PEEK_VIEW_H_
