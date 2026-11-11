// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_ui_zoom.h"

#include "base/functional/bind.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/page/page_zoom.h"

namespace clu {

namespace {

// Zoom level 0 is a zoom factor of 1.0, whatever the profile's default zoom is.
constexpr double kUnzoomed = 0.0;

}  // namespace

// static
void CluUiZoomLock::Lock(content::WebContents* contents) {
  if (!contents || FromWebContents(contents)) {
    return;
  }
  CreateForWebContents(contents);
}

CluUiZoomLock::CluUiZoomLock(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<CluUiZoomLock>(*contents) {
  // A pinch over the sidebar or a menu would scale the WebUI's visual viewport; the page under it keeps its own.
  contents->SetIgnoreZoomGestures(true);
  Apply();
}

CluUiZoomLock::~CluUiZoomLock() = default;

void CluUiZoomLock::PrimaryPageChanged(content::Page& page) {
  Apply();
}

void CluUiZoomLock::Apply() {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return;
  }
  content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
  content::HostZoomMap* map = content::HostZoomMap::GetForWebContents(contents);
  // A navigation to another storage partition brings another zoom map; follow the one the frame uses.
  if (map != zoom_map_) {
    zoom_map_ = map;
    zoom_subscription_ =
        map ? map->AddZoomLevelChangedCallback(
                  base::BindRepeating(&CluUiZoomLock::OnZoomLevelChanged,
                                      base::Unretained(this)))
            : base::CallbackListSubscription();
  }
  if (!frame || !map) {
    return;
  }
  const content::GlobalRenderFrameHostId id = frame->GetGlobalId();
  if (map->UsesTemporaryZoomLevel(id) &&
      blink::ZoomValuesEqual(content::HostZoomMap::GetZoomLevel(contents, id),
                             kUnzoomed)) {
    return;
  }
  map->SetTemporaryZoomLevel(id, kUnzoomed);
}

void CluUiZoomLock::OnZoomLevelChanged(
    const content::HostZoomMap::ZoomLevelChange& change) {
  // Any change may be one to this frame (HostZoomMap::SetZoomLevel on it sets its temporary level); Apply() returns
  // at once when the frame is still at 100%.
  Apply();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(CluUiZoomLock);

}  // namespace clu
