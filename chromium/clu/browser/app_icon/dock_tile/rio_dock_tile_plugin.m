// Copyright 2026 The Rio Authors. All rights reserved.

// Rio's Dock tile plug-in (docs/chromium/icons.md), Contents/PlugIns/RioDockTile.docktileplugin, named by the app's
// NSDockTilePlugIn key. The Dock loads it into its own process and asks it for the tile while Rio is closed; while Rio
// runs, the browser's NSApp.applicationIconImage shows instead. It reads the alternate icon Rio saved under clu.appIcon
// in Rio's user defaults and shows that icon's PNG from its own Resources, or the bundle's icon when there is none.
// Rio posts <bundle id>.appIconChanged when the choice changes. Plain Objective-C on AppKit alone: nothing of Chromium
// loads into the Dock. It never writes anywhere.

#import <AppKit/AppKit.h>

static NSString* const kAppIconKey = @"clu.appIcon";

@interface RioDockTilePlugIn : NSObject <NSDockTilePlugIn>
@end

@implementation RioDockTilePlugIn {
  NSDockTile* _dockTile;
  NSString* _appID;
  id _observer;
}

- (void)dealloc {
  if (_observer) {
    [NSDistributedNotificationCenter.defaultCenter removeObserver:_observer];
  }
}

- (void)setDockTile:(NSDockTile*)dockTile {
  if (_observer) {
    [NSDistributedNotificationCenter.defaultCenter removeObserver:_observer];
    _observer = nil;
  }
  _dockTile = dockTile;
  if (!dockTile) {
    return;
  }
  // Rio.app/Contents/PlugIns/RioDockTile.docktileplugin: the app is three levels up.
  NSURL* app = [NSBundle bundleForClass:[self class]]
                   .bundleURL.URLByDeletingLastPathComponent
                   .URLByDeletingLastPathComponent.URLByDeletingLastPathComponent;
  _appID = [NSBundle bundleWithURL:app].bundleIdentifier;
  if (_appID.length == 0) {
    return;
  }
  __weak RioDockTilePlugIn* weakSelf = self;
  _observer = [NSDistributedNotificationCenter.defaultCenter
      addObserverForName:[_appID stringByAppendingString:@".appIconChanged"]
                  object:nil
                   queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification* notification) {
                [weakSelf update];
              }];
  [self update];
}

// The saved alternate's image, or nil for the default, a missing value or junk.
- (NSImage*)savedIcon {
  CFStringRef appID = (__bridge CFStringRef)_appID;
  CFPreferencesAppSynchronize(appID);
  id value = CFBridgingRelease(
      CFPreferencesCopyAppValue((__bridge CFStringRef)kAppIconKey, appID));
  if (![value isKindOfClass:[NSString class]]) {
    return nil;
  }
  NSString* name = value;
  // Only a plain resource name: no path separators, dots or anything else.
  NSCharacterSet* allowed = [NSCharacterSet
      characterSetWithCharactersInString:
          @"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"];
  if (name.length == 0 || name.length > 32 ||
      [name rangeOfCharacterFromSet:allowed.invertedSet].location != NSNotFound) {
    return nil;
  }
  NSURL* url = [[NSBundle bundleForClass:[self class]] URLForResource:name
                                                        withExtension:@"png"];
  NSImage* image = url ? [[NSImage alloc] initWithContentsOfURL:url] : nil;
  return image.valid ? image : nil;
}

- (void)update {
  NSDockTile* tile = _dockTile;
  if (!tile) {
    return;
  }
  NSImage* image = [self savedIcon];
  if (image) {
    NSImageView* view = [NSImageView imageViewWithImage:image];
    view.frame = NSMakeRect(0, 0, tile.size.width, tile.size.height);
    view.imageScaling = NSImageScaleProportionallyUpOrDown;
    tile.contentView = view;
  } else {
    tile.contentView = nil;
  }
  [tile display];
}

@end
