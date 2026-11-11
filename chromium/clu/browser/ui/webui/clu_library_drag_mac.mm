// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_library_drag.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/strings/string_util.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

// clu::LibraryDragOperations: Finder and the Desktop copy, the Trash refuses, and no drop moves the file. This is the
// one change from DragDownloadItem's BeginFileDrag, whose content view offers every operation, move included.
@interface CluLibraryDragSource : NSObject <NSDraggingSource>
@end

@implementation CluLibraryDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return ui::DragDropTypes::DragOperationToNSDragOperation(
      clu::LibraryDragOperations(context ==
                                 NSDraggingContextOutsideApplication));
}
@end

namespace clu {

namespace {

constexpr char kPngPrefix[] = "data:image/png;base64,";

// The thumbnail as an image sized in points, nil for "" or junk.
NSImage* DragImage(std::string_view png_data_url) {
  if (!base::StartsWith(png_data_url, kPngPrefix)) {
    return nil;
  }
  std::optional<std::vector<uint8_t>> png =
      base::Base64Decode(png_data_url.substr(sizeof(kPngPrefix) - 1));
  if (!png || png->empty()) {
    return nil;
  }
  NSImage* image = [[NSImage alloc]
      initWithData:[NSData dataWithBytes:png->data() length:png->size()]];
  NSBitmapImageRep* rep =
      base::apple::ObjCCast<NSBitmapImageRep>(image.representations.firstObject);
  if (!rep || rep.pixelsWide <= 0 || rep.pixelsHigh <= 0) {
    return nil;
  }
  const double scale =
      std::min(0.5, static_cast<double>(kLibraryDragPoints) /
                        std::max(rep.pixelsWide, rep.pixelsHigh));
  image.size = NSMakeSize(std::max(1.0, rep.pixelsWide * scale),
                          std::max(1.0, rep.pixelsHigh * scale));
  return image;
}

}  // namespace

int LibraryDragOperations(bool outside_application) {
  return outside_application
             ? ui::DragDropTypes::DRAG_COPY
             : ui::DragDropTypes::DRAG_COPY | ui::DragDropTypes::DRAG_LINK;
}

gfx::Size LibraryDragImageSize(std::string_view png_data_url) {
  NSImage* image = DragImage(png_data_url);
  return image ? gfx::Size(static_cast<int>(image.size.width),
                           static_cast<int>(image.size.height))
               : gfx::Size();
}

bool BeginLibraryFileDrag(content::WebContents* frame,
                          const base::FilePath& path,
                          std::string_view png_data_url) {
  if (!frame || ([NSEvent pressedMouseButtons] & 1) == 0) {
    return false;
  }
  NSView* view = frame->GetNativeView().GetNativeNSView();
  NSURL* url = base::apple::FilePathToNSURL(path);
  if (!view || !view.window || !url) {
    return false;
  }
  // As DragDownloadItem: the frame's widget may hold mouse capture from the press, and this drag bypasses
  // View::DoDrag(), which would release it.
  if (views::Widget* widget =
          views::Widget::GetWidgetForNativeView(frame->GetNativeView())) {
    widget->ReleaseCapture();
  }
  static CluLibraryDragSource* source = [[CluLibraryDragSource alloc] init];

  const NSPoint in_window = view.window.mouseLocationOutsideOfEventStream;
  const NSPoint at = [view convertPoint:in_window fromView:nil];
  NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];
  NSImage* image = DragImage(png_data_url);
  if (!image) {
    // As chrome://downloads does without an icon: macOS's file icon at 32 points.
    image = [NSWorkspace.sharedWorkspace iconForFile:base::apple::FilePathToNSString(path)];
    image.size = NSMakeSize(32, 32);
  }
  [item setDraggingFrame:NSMakeRect(at.x - image.size.width / 2,
                                    at.y - image.size.height / 2,
                                    image.size.width, image.size.height)
                contents:image];
  NSEvent* event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
                                      location:in_window
                                 modifierFlags:0
                                     timestamp:NSApp.currentEvent.timestamp
                                  windowNumber:view.window.windowNumber
                                       context:nil
                                   eventNumber:0
                                    clickCount:1
                                      pressure:1.0];
  [view beginDraggingSessionWithItems:@[ item ] event:event source:source];
  return true;
}

}  // namespace clu
