// Copyright 2026 The Rio Authors. All rights reserved.

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/settings/clu_settings_hooks.h"
#include "content/public/test/browser_test.h"

// Records what clu::QuitCameFromAppleEvent() says while AppKit dispatches an Apple event, the way it dispatches the
// Dock's or AppleScript's quit.
@interface CluQuitEventProbe : NSObject
@property(nonatomic) BOOL sawAppleEvent;
- (void)handleEvent:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation CluQuitEventProbe
@synthesize sawAppleEvent = _sawAppleEvent;

- (void)handleEvent:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply {
  _sawAppleEvent = clu::QuitCameFromAppleEvent();
}
@end

namespace clu {
namespace {

// A test-only event class and id, so AppKit's own quit handler stays installed.
constexpr FourCharCode Code(char a, char b, char c, char d) {
  return (static_cast<FourCharCode>(a) << 24) | (static_cast<FourCharCode>(b) << 16) |
         (static_cast<FourCharCode>(c) << 8) | static_cast<FourCharCode>(d);
}
constexpr AEEventClass kProbeClass = Code('C', 'l', 'u', 'T');
constexpr AEEventID kProbeId = Code('q', 'u', 'i', 't');

using CluQuitBrowserTest = InProcessBrowserTest;

// "Hold ⌘Q to quit" is for the Cmd+Q key only. A quit that arrives as an Apple event (the Dock, AppleScript,
// a logout or a restart) reads as one, so AppController quits at once; outside one (the key) it still asks.
IN_PROC_BROWSER_TEST_F(CluQuitBrowserTest, CluQuitFromAppleEventSkipsHoldToQuit) {
  EXPECT_FALSE(QuitCameFromAppleEvent());

  CluQuitEventProbe* probe = [[CluQuitEventProbe alloc] init];
  NSAppleEventManager* manager = NSAppleEventManager.sharedAppleEventManager;
  [manager setEventHandler:probe
               andSelector:@selector(handleEvent:withReplyEvent:)
             forEventClass:kProbeClass
                andEventID:kProbeId];
  NSAppleEventDescriptor* event =
      [NSAppleEventDescriptor appleEventWithEventClass:kProbeClass
                                               eventID:kProbeId
                                      targetDescriptor:NSAppleEventDescriptor.currentProcessDescriptor
                                              returnID:kAutoGenerateReturnID
                                         transactionID:kAnyTransactionID];
  AppleEvent reply = {typeNull, nullptr};
  // The SDK marks the refcon nonnull; the handler ignores it.
  int ref_con = 0;
  EXPECT_EQ([manager dispatchRawAppleEvent:event.aeDesc withRawReply:&reply handlerRefCon:&ref_con], noErr);
  AEDisposeDesc(&reply);
  [manager removeEventHandlerForEventClass:kProbeClass andEventID:kProbeId];

  EXPECT_TRUE(probe.sawAppleEvent);
  EXPECT_FALSE(QuitCameFromAppleEvent());
}

}  // namespace
}  // namespace clu
