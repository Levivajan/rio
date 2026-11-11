// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_wheel_lift_watcher.h"

#import <AppKit/AppKit.h>

#include <utility>

namespace clu {

void* StartNativeLiftMonitor(base::RepeatingClosure on_lift) {
  // A local monitor sees the event as the app dequeues it, before RenderWidgetHostViewMac holds a phase ended for
  // 100 ms waiting for momentum. It never changes or swallows the event.
  id monitor = [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                   handler:^NSEvent*(NSEvent* event) {
                                     if (event.momentumPhase ==
                                             NSEventPhaseNone &&
                                         (event.phase &
                                          (NSEventPhaseEnded |
                                           NSEventPhaseCancelled))) {
                                       on_lift.Run();
                                     }
                                     return event;
                                   }];
  return (__bridge_retained void*)monitor;
}

void StopNativeLiftMonitor(void* monitor) {
  if (!monitor) {
    return;
  }
  [NSEvent removeMonitor:(__bridge_transfer id)monitor];
}

}  // namespace clu
