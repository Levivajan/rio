// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_AI_CLU_LINK_HOVER_H_
#define CLU_BROWSER_AI_CLU_LINK_HOVER_H_

class GURL;

namespace content {
class WebContents;
}  // namespace content

// The link under the pointer in a page (Chromium's status bubble URL), for hover previews (docs/chromium/parity-ai.md).
// BrowserWebContentsDelegate::UpdateTargetURL calls OnTargetUrl (patch
// chrome-browser-ui-browser_web_contents_delegate-browser_web_contents_delegate.cc); Clu's window side sets the
// handler (clu_link_hover.cc, in //clu/browser/ai, which the window side links into the same binary).
namespace clu {

using LinkHoverHandler = void (*)(content::WebContents* source, const GURL& url);

void SetLinkHoverHandler(LinkHoverHandler handler);
void OnTargetUrl(content::WebContents* source, const GURL& url);

}  // namespace clu

#endif  // CLU_BROWSER_AI_CLU_LINK_HOVER_H_
