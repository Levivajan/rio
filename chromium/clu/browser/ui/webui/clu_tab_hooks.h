// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_TAB_HOOKS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_TAB_HOOKS_H_

namespace content {
class WebContents;
}  // namespace content

// The hook the extensions tabs and windows APIs call (docs/chromium/fixes.md,
// leftovers 4). This header has its own GN target with no dependencies, so
// //chrome/browser/extensions reaches no Clu code; the implementation lives in
// clu_tab_id.cc.
namespace clu {

// True for a window's hidden placeholder page (CluPlaceholder): no row shows
// it, so chrome.tabs, chrome.windows and the tab events leave it out. Null-safe.
bool IsPlaceholderTab(content::WebContents* contents);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_TAB_HOOKS_H_
