// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_LIBRARY_DRAG_H_
#define CLU_BROWSER_UI_WEBUI_CLU_LIBRARY_DRAG_H_

#include <string_view>

#include "ui/gfx/geometry/size.h"

namespace base {
class FilePath;
}  // namespace base

namespace content {
class WebContents;
}  // namespace content

// A Library file (Media tile, Downloads row, the hover peek and the bottom-left arrivals) drags out
// as the file itself, as chrome://downloads' rows do through DragDownloadItem: a native file drag under the pointer,
// so Finder and the Desktop copy it, Slack, Mail and Messages attach it and a page's drop zone or file input takes it.
// The drag offers copy (and inside Rio link), never move or delete, so no drop (Finder, the Trash, Rio's sidebar or
// page) moves or deletes the file. The WebUI names the file by source and name or download id; the handler resolves it.
namespace clu {

// The drag image for LibraryThumbnail's PNG data URL: its pixels at 2x, fit in kLibraryDragPoints. Empty for "" or
// junk, and macOS's icon for the file shows then.
inline constexpr int kLibraryDragPoints = 96;
gfx::Size LibraryDragImageSize(std::string_view png_data_url);

// The operations the drag offers, as ui::DragDropTypes bits. Outside Rio copy only: offered copy and link without
// move, Finder and the Desktop pick link and make an alias, so they get nothing else to pick. Inside Rio
// copy and link, which a page's drop zone or file input takes. Never move, so no drop moves or deletes the file.
int LibraryDragOperations(bool outside_application);

// Starts the drag of `path` from `frame`'s view at the pointer. False when no mouse button is down any more (the
// press ended before the file resolved) or `frame` has no window; nothing starts then.
bool BeginLibraryFileDrag(content::WebContents* frame,
                          const base::FilePath& path,
                          std::string_view png_data_url);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_LIBRARY_DRAG_H_
