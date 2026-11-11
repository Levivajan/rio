// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_anchor_view.h"

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"

namespace clu {

DEFINE_ELEMENT_IDENTIFIER_VALUE(kCluAnchorViewElementId);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kCluExtensionAnchorViewElementId);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kCluDownloadsAnchorViewElementId);

CluAnchorView::CluAnchorView() {
  SetProperty(views::kElementIdentifierKey, kCluAnchorViewElementId);
  SetCanProcessEventsWithinSubtree(false);
  SetFocusBehavior(FocusBehavior::NEVER);
  GetViewAccessibility().SetIsIgnored(true);
}

CluAnchorView::~CluAnchorView() = default;

BEGIN_METADATA(CluAnchorView)
END_METADATA

}  // namespace clu
