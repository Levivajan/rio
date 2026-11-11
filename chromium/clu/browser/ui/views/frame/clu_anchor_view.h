// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_ANCHOR_VIEW_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_ANCHOR_VIEW_H_

#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace clu {

DECLARE_ELEMENT_IDENTIFIER_VALUE(kCluAnchorViewElementId);
// The second anchor, at the extension button the user clicked (M7).
DECLARE_ELEMENT_IDENTIFIER_VALUE(kCluExtensionAnchorViewElementId);
// The third, at the sidebar's downloads button while it shows (docs/chromium/fixes.md).
DECLARE_ELEMENT_IDENTIFIER_VALUE(kCluDownloadsAnchorViewElementId);

// The point Chromium's native bubbles hang from while the toolbar is hidden (docs/chromium/PLAN.md 1.3 "Native
// anchors"): password, permission, page info and extension popups. It is a zero-height view the Clu layout lays
// along the page card's top edge, so a bubble with a leading arrow hangs from the card's top leading corner and one
// with a trailing arrow from its top trailing corner. It paints nothing and takes no events, but it is visible, so
// IsDrawn() checks in the bubble code pass.
class CluAnchorView : public views::View {
  METADATA_HEADER(CluAnchorView, views::View)

 public:
  CluAnchorView();
  CluAnchorView(const CluAnchorView&) = delete;
  CluAnchorView& operator=(const CluAnchorView&) = delete;
  ~CluAnchorView() override;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_ANCHOR_VIEW_H_
