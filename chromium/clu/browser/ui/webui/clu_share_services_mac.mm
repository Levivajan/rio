// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_share_services.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>

#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/strings/sys_string_conversions.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

namespace clu {

namespace {

constexpr size_t kMaxServices = 12;

NSArray<NSSharingService*>* ServicesFor(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return @[];
  }
  NSURL* ns_url = [NSURL URLWithString:base::SysUTF8ToNSString(url.spec())];
  if (!ns_url) {
    return @[];
  }
  // Chromium's own File > Share menu reads the same list (share_menu_controller.mm).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSArray<NSSharingService*>* services =
      [NSSharingService sharingServicesForItems:@[ ns_url ]];
#pragma clang diagnostic pop
  if (services.count > kMaxServices) {
    services = [services subarrayWithRange:NSMakeRange(0, kMaxServices)];
  }
  return services ?: @[];
}

}  // namespace

std::vector<std::string> ShareServiceNames(const GURL& url) {
  std::vector<std::string> names;
  for (NSSharingService* service in ServicesFor(url)) {
    names.push_back(base::SysNSStringToUTF8(service.menuItemTitle ?: service.title));
  }
  return names;
}

bool ShareWithService(size_t index, const GURL& url, const std::u16string& title) {
  NSArray<NSSharingService*>* services = ServicesFor(url);
  if (index >= services.count) {
    return false;
  }
  NSSharingService* service = services[index];
  NSURL* ns_url = [NSURL URLWithString:base::SysUTF8ToNSString(url.spec())];
  if (!title.empty()) {
    service.subject = base::SysUTF16ToNSString(title);
  }
  if (![service canPerformWithItems:@[ ns_url ]]) {
    return false;
  }
  [service performWithItems:@[ ns_url ]];
  return true;
}

namespace {

bool ShowPickerFor(NSView* view, const gfx::Rect& anchor, id item) {
  NSSharingServicePicker* picker =
      [[NSSharingServicePicker alloc] initWithItems:@[ item ]];
  // The page's view is flipped, so frame coordinates map straight onto it.
  NSRect rect = NSMakeRect(anchor.x(), anchor.y(), std::max(1, anchor.width()),
                           std::max(1, anchor.height()));
  if (!view.isFlipped) {
    rect.origin.y = NSHeight(view.bounds) - NSMaxY(rect);
  }
  [picker showRelativeToRect:rect ofView:view preferredEdge:NSMaxYEdge];
  return true;
}

}  // namespace

bool ShowSharePicker(content::WebContents* frame,
                     const gfx::Rect& anchor,
                     const std::u16string& text) {
  NSView* view = frame ? frame->GetNativeView().GetNativeNSView() : nil;
  if (!view || text.empty()) {
    return false;
  }
  // A page's link goes as a URL, so AirDrop and Messages share it as a link.
  const GURL url(text);
  id item = base::SysUTF16ToNSString(text);
  if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
    if (NSURL* ns_url =
            [NSURL URLWithString:base::SysUTF8ToNSString(url.spec())]) {
      item = ns_url;
    }
  }
  return ShowPickerFor(view, anchor, item);
}

bool ShowFileSharePicker(content::WebContents* frame,
                         const gfx::Rect& anchor,
                         const base::FilePath& path) {
  NSView* view = frame ? frame->GetNativeView().GetNativeNSView() : nil;
  NSURL* url = base::apple::FilePathToNSURL(path);
  return view && url && ShowPickerFor(view, anchor, url);
}

}  // namespace clu
