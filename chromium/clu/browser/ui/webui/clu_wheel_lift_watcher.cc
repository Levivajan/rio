// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_wheel_lift_watcher.h"

#include <utility>

#include "base/functional/bind.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"

namespace clu {

// static
CluWheelLiftWatcher::Signal CluWheelLiftWatcher::Classify(
    const blink::WebMouseWheelEvent& event) {
  // Phases Chromium adds on its own timer (mouse wheels off macOS) say nothing about fingers.
  if (event.has_synthetic_phase) {
    return Signal::kNone;
  }
  constexpr int kEnded = blink::WebMouseWheelEvent::kPhaseEnded |
                         blink::WebMouseWheelEvent::kPhaseCancelled;
  if ((event.phase & kEnded) ||
      (event.momentum_phase & blink::WebMouseWheelEvent::kPhaseBegan)) {
    return Signal::kLifted;
  }
  if (event.phase != blink::WebMouseWheelEvent::kPhaseNone ||
      event.momentum_phase != blink::WebMouseWheelEvent::kPhaseNone) {
    return Signal::kPhased;
  }
  return Signal::kNone;
}

CluWheelLiftWatcher::CluWheelLiftWatcher(content::RenderWidgetHost* host,
                                         Callback callback)
    : host_(host), callback_(std::move(callback)) {
  if (host_) {
    host_->AddInputEventObserver(this);
    host_->AddObserver(this);
#if BUILDFLAG(IS_MAC)
    native_monitor_ = StartNativeLiftMonitor(base::BindRepeating(
        &CluWheelLiftWatcher::OnFingersLifted, weak_factory_.GetWeakPtr()));
#endif
  }
}

CluWheelLiftWatcher::~CluWheelLiftWatcher() {
  Stop();
}

void CluWheelLiftWatcher::OnInputEvent(const content::RenderWidgetHost& host,
                                       const blink::WebInputEvent& event,
                                       InputEventSource source) {
  if (event.GetType() != blink::WebInputEvent::Type::kMouseWheel) {
    return;
  }
  OnWheel(static_cast<const blink::WebMouseWheelEvent&>(event));
}

void CluWheelLiftWatcher::OnWheel(const blink::WebMouseWheelEvent& event) {
  const Signal signal = Classify(event);
  if (signal == Signal::kNone) {
    // A mouse's events: the gesture is unphased, and no trackpad gesture is still down.
    fingers_down_ = false;
    callback_.Run(false, false);
    return;
  }
  if (signal == Signal::kPhased &&
      event.phase != blink::WebMouseWheelEvent::kPhaseNone) {
    fingers_down_ = true;
  }
  // A lift the native monitor already reported (Chromium's late phase ended, or the momentum after it) is only phased.
  const bool lifted = signal == Signal::kLifted && fingers_down_;
  if (lifted) {
    fingers_down_ = false;
  }
  callback_.Run(true, lifted);
}

void CluWheelLiftWatcher::OnFingersLifted() {
  // Only a gesture on the frame's widget: a scroll over the page ends without the sidebar hearing of it.
  if (!fingers_down_) {
    return;
  }
  fingers_down_ = false;
  callback_.Run(true, true);
}

void CluWheelLiftWatcher::RenderWidgetHostDestroyed(
    content::RenderWidgetHost* host) {
  Stop();
}

void CluWheelLiftWatcher::Stop() {
  if (!host_) {
    return;
  }
#if BUILDFLAG(IS_MAC)
  StopNativeLiftMonitor(native_monitor_);
  native_monitor_ = nullptr;
#endif
  host_->RemoveInputEventObserver(this);
  host_->RemoveObserver(this);
  host_ = nullptr;
}

}  // namespace clu
