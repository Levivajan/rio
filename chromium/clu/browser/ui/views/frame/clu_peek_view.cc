// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_peek_view.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "clu/browser/ui/views/frame/clu_look_tokens.h"
#include "clu/browser/ui/views/frame/clu_ui_zoom.h"
#include "clu/common/clu_urls.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"

namespace clu {

CluPeekView::CluPeekView(BrowserWindowInterface* browser,
                         content::WebContentsDelegate* overlay_delegate) {
  auto under = std::make_unique<views::WebView>(browser->GetProfile());
  under_view_ = AddChildView(std::move(under));
  content::WebContents* contents = under_view_->GetWebContents();
  contents->SetDelegate(overlay_delegate);
  contents->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
  webui::SetBrowserWindowInterface(contents, browser);
  CluUiZoomLock::Lock(contents);
  under_view_->LoadInitialURL(GURL(kCluUnderPeekOverlayUrl));

  page_view_ = AddChildView(std::make_unique<views::WebView>());
  page_view_->SetVisible(false);
}

CluPeekView::~CluPeekView() {
  page_view_->SetWebContents(nullptr);
}

content::WebContents* CluPeekView::under_contents() {
  return under_view_->GetWebContents();
}

void CluPeekView::SetPage(content::WebContents* page) {
  if (page_view_->GetWebContents() == page) {
    return;
  }
  page_view_->SetWebContents(page);
  if (!page) {
    SetPageVisible(false);
  }
}

content::WebContents* CluPeekView::page() {
  return page_view_->GetWebContents();
}

void CluPeekView::SetCard(const gfx::Rect& card) {
  if (card == card_) {
    return;
  }
  card_ = card;
  InvalidateLayout();
}

void CluPeekView::SetPageVisible(bool visible) {
  visible = visible && page_view_->GetWebContents();
  if (visible == page_visible_) {
    return;
  }
  page_visible_ = visible;
  page_view_->SetVisible(visible);
  if (visible) {
    // Only once the view is in a widget does the native host exist to round.
    page_view_->holder()->SetNativeViewCornerRadii(
        gfx::RoundedCornersF(look::kPeekRadius));
  }
  InvalidateLayout();
}

void CluPeekView::Layout(PassKey) {
  under_view_->SetBoundsRect(GetLocalBounds());
  page_view_->SetBoundsRect(card_);
}

BEGIN_METADATA(CluPeekView)
END_METADATA

}  // namespace clu
