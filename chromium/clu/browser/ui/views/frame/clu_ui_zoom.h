// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_UI_ZOOM_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_UI_ZOOM_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class Page;
class WebContents;
}  // namespace content

namespace clu {

// Keeps one of Rio's own WebContents (the frame, overlay, passive and under-Peek WebUIs) at 100% (docs/chromium/
// ui-polish.md), so the sidebar never zooms. Each primary main frame gets a temporary zoom level of 0,
// which HostZoomMap prefers over a level saved for the WebUI's host and over the profile's default zoom, and any later
// change to it is put back. Pinch and double-tap zoom gestures are dropped before they reach the renderer. Page zoom
// (Cmd+plus, Cmd+minus, Cmd+0, pinch over the page) keeps acting on the page's own WebContents.
class CluUiZoomLock : public content::WebContentsObserver,
                      public content::WebContentsUserData<CluUiZoomLock> {
 public:
  // Locks `contents` at 100% for its lifetime. Calling it again does nothing.
  static void Lock(content::WebContents* contents);

  CluUiZoomLock(const CluUiZoomLock&) = delete;
  CluUiZoomLock& operator=(const CluUiZoomLock&) = delete;
  ~CluUiZoomLock() override;

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<CluUiZoomLock>;

  explicit CluUiZoomLock(content::WebContents* contents);

  // Puts the primary main frame back at zoom level 0 and follows the zoom map that frame uses.
  void Apply();
  void OnZoomLevelChanged(const content::HostZoomMap::ZoomLevelChange& change);

  raw_ptr<content::HostZoomMap> zoom_map_ = nullptr;
  base::CallbackListSubscription zoom_subscription_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_UI_ZOOM_H_
