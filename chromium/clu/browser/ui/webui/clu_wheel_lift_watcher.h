// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WHEEL_LIFT_WATCHER_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WHEEL_LIFT_WATCHER_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"

namespace blink {
class WebMouseWheelEvent;
}

namespace clu {

// The trackpad phases of the wheel events sent to the frame WebUI. Blink drops the phase-ended event before
// the DOM sees it (MouseWheelEventQueue sends it, MouseWheelEventManager returns early), and DOM wheel events carry no
// phase, so the sidebar's swipe cannot tell the fingers lifting from the fingers resting. This watcher sees every wheel
// event on the frame's widget and reports `phased` (the current gesture reports real phases) and `lifted` (the fingers
// left the trackpad: phase ended or cancelled, or momentum began). An event with no real phase (a mouse wheel, Shift+
// wheel, a thumb wheel) reports phased false, so a mouse drag after a trackpad gesture still ends on idle.
// It stops at the widget's end.
//
// On macOS RenderWidgetHostViewMac holds a phase-ended event for 100 ms in case momentum begins (MouseWheelPhaseHandler)
// and only then sends a synthetic one, so a slow swipe's lift reached the sidebar 100 to 116 ms late.
// The watcher also watches the app's scroll NSEvents and reports the lift the moment the fingers leave. Each gesture
// lifts once: whichever of the native end, Chromium's late end or the first momentum event comes first.
class CluWheelLiftWatcher : public content::RenderWidgetHost::InputEventObserver,
                            public content::RenderWidgetHostObserver {
 public:
  using Callback = base::RepeatingCallback<void(bool phased, bool lifted)>;

  // What a wheel event says: no real phase (a mouse), a phased event, or a lift.
  enum class Signal { kNone, kPhased, kLifted };
  static Signal Classify(const blink::WebMouseWheelEvent& event);

  CluWheelLiftWatcher(content::RenderWidgetHost* host, Callback callback);
  CluWheelLiftWatcher(const CluWheelLiftWatcher&) = delete;
  CluWheelLiftWatcher& operator=(const CluWheelLiftWatcher&) = delete;
  ~CluWheelLiftWatcher() override;

  // content::RenderWidgetHost::InputEventObserver:
  void OnInputEvent(const content::RenderWidgetHost& host,
                    const blink::WebInputEvent& event,
                    InputEventSource source) override;

  // content::RenderWidgetHostObserver:
  void RenderWidgetHostDestroyed(content::RenderWidgetHost* host) override;

  // A wheel event the frame's widget received (OnInputEvent's work, public for tests).
  void OnWheel(const blink::WebMouseWheelEvent& event);
  // The fingers left the trackpad, seen on the native event before Chromium's 100 ms momentum wait (public for tests).
  void OnFingersLifted();

 private:
  void Stop();

  raw_ptr<content::RenderWidgetHost> host_;
  Callback callback_;
  // A phased gesture reached the widget and has not lifted yet.
  bool fingers_down_ = false;
#if BUILDFLAG(IS_MAC)
  // The NSEvent local monitor (an id, retained), from clu_wheel_lift_watcher_mac.mm. An Objective-C object, not a
  // PartitionAlloc allocation, so raw_ptr has nothing to protect.
  RAW_PTR_EXCLUSION void* native_monitor_ = nullptr;
#endif
  base::WeakPtrFactory<CluWheelLiftWatcher> weak_factory_{this};
};

#if BUILDFLAG(IS_MAC)
// Runs `on_lift` when a scroll NSEvent ends or cancels its finger phase; returns the monitor for StopNativeLiftMonitor.
void* StartNativeLiftMonitor(base::RepeatingClosure on_lift);
void StopNativeLiftMonitor(void* monitor);
#endif

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WHEEL_LIFT_WATCHER_H_
