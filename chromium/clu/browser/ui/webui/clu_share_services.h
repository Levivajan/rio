// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_SHARE_SERVICES_H_
#define CLU_BROWSER_UI_WEBUI_CLU_SHARE_SERVICES_H_

#include <string>
#include <vector>

class GURL;

namespace base {
class FilePath;
}  // namespace base

namespace content {
class WebContents;
}  // namespace content

namespace gfx {
class Rect;
}  // namespace gfx

namespace clu {

// A row menu's Share ▸ (docs/chromium/parity-sidebar.md): macOS's sharing services for a link (Mail, Messages,
// AirDrop, Notes…), by name, at most 12. Empty off macOS and for a link that is not http(s).
std::vector<std::string> ShareServiceNames(const GURL& url);
// Hands the link to the service at `index` of ShareServiceNames(url); false when that service is gone.
bool ShareWithService(size_t index, const GURL& url, const std::u16string& title);
// Share Space…: macOS's share picker for `text`, pointing at `anchor` in `frame`'s coordinates. False when
// the frame has no view.
bool ShowSharePicker(content::WebContents* frame,
                     const gfx::Rect& anchor,
                     const std::u16string& text);
// The Library menu's Share…: the same picker for a file, so AirDrop, Mail and Messages send the file itself.
bool ShowFileSharePicker(content::WebContents* frame,
                         const gfx::Rect& anchor,
                         const base::FilePath& path);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_SHARE_SERVICES_H_
