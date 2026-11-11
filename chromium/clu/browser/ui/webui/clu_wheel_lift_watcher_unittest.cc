// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_wheel_lift_watcher.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"

namespace clu {
namespace {

using Phase = blink::WebMouseWheelEvent::Phase;
using Signal = CluWheelLiftWatcher::Signal;

Signal Of(Phase phase, Phase momentum, bool synthetic = false) {
  blink::WebMouseWheelEvent event;
  event.phase = phase;
  event.momentum_phase = momentum;
  event.has_synthetic_phase = synthetic;
  return CluWheelLiftWatcher::Classify(event);
}

// The sidebar swipe ends when the fingers lift, never on a pause.
TEST(CluWheelLiftWatcherTest, ClassifiesTrackpadPhases) {
  constexpr Phase kNone = blink::WebMouseWheelEvent::kPhaseNone;
  // Fingers on the trackpad.
  EXPECT_EQ(Of(blink::WebMouseWheelEvent::kPhaseBegan, kNone), Signal::kPhased);
  EXPECT_EQ(Of(blink::WebMouseWheelEvent::kPhaseChanged, kNone),
            Signal::kPhased);
  EXPECT_EQ(Of(blink::WebMouseWheelEvent::kPhaseStationary, kNone),
            Signal::kPhased);
  // The fingers lift: ended, cancelled, or the first momentum event.
  EXPECT_EQ(Of(blink::WebMouseWheelEvent::kPhaseEnded, kNone), Signal::kLifted);
  EXPECT_EQ(Of(blink::WebMouseWheelEvent::kPhaseCancelled, kNone),
            Signal::kLifted);
  EXPECT_EQ(Of(kNone, blink::WebMouseWheelEvent::kPhaseBegan), Signal::kLifted);
  // The rest of the momentum is phased but lifts nothing.
  EXPECT_EQ(Of(kNone, blink::WebMouseWheelEvent::kPhaseChanged),
            Signal::kPhased);
  // A mouse wheel has no phases; Chromium's timer phases say nothing about fingers.
  EXPECT_EQ(Of(kNone, kNone), Signal::kNone);
  EXPECT_EQ(Of(blink::WebMouseWheelEvent::kPhaseEnded, kNone,
               /*synthetic=*/true),
            Signal::kNone);
}

// MacOS holds phase ended 100 ms for momentum, so the native lift comes first. Each gesture
// reports one lift, whichever signal arrives first, and a lift with no gesture on the widget reports nothing.
TEST(CluWheelLiftWatcherTest, ReportsOneLiftPerGesture) {
  std::vector<bool> runs;
  CluWheelLiftWatcher watcher(
      nullptr, base::BindRepeating(
                   [](std::vector<bool>* out, bool phased, bool lifted) {
                     EXPECT_TRUE(phased);
                     out->push_back(lifted);
                   },
                   base::Unretained(&runs)));
  auto wheel = [&](Phase phase, Phase momentum) {
    blink::WebMouseWheelEvent event;
    event.phase = phase;
    event.momentum_phase = momentum;
    watcher.OnWheel(event);
  };
  constexpr Phase kNone = blink::WebMouseWheelEvent::kPhaseNone;

  // A scroll over the page ended: the frame never saw it.
  watcher.OnFingersLifted();
  EXPECT_TRUE(runs.empty());

  // A slow swipe: the native lift ends it at once; Chromium's late phase ended is only phased.
  wheel(blink::WebMouseWheelEvent::kPhaseBegan, kNone);
  wheel(blink::WebMouseWheelEvent::kPhaseChanged, kNone);
  watcher.OnFingersLifted();
  wheel(blink::WebMouseWheelEvent::kPhaseEnded, kNone);
  EXPECT_EQ(runs, (std::vector<bool>{false, false, true, false}));

  // A flick: the native lift, then momentum began lifts nothing more.
  runs.clear();
  wheel(blink::WebMouseWheelEvent::kPhaseBegan, kNone);
  watcher.OnFingersLifted();
  wheel(kNone, blink::WebMouseWheelEvent::kPhaseBegan);
  wheel(kNone, blink::WebMouseWheelEvent::kPhaseChanged);
  EXPECT_EQ(runs, (std::vector<bool>{false, true, false, false}));

  // Without the native lift (another platform), momentum began still lifts.
  runs.clear();
  wheel(blink::WebMouseWheelEvent::kPhaseBegan, kNone);
  wheel(kNone, blink::WebMouseWheelEvent::kPhaseBegan);
  EXPECT_EQ(runs, (std::vector<bool>{false, true}));
}

// Phased belongs to the current gesture. A mouse's events after a trackpad gesture report unphased (and
// Chromium's synthetic phases for a mouse too), so the sidebar's drag ends on idle; the next trackpad event is phased.
TEST(CluWheelLiftWatcherTest, AMouseAfterATrackpadIsUnphased) {
  std::vector<std::pair<bool, bool>> runs;
  CluWheelLiftWatcher watcher(
      nullptr, base::BindRepeating(
                   [](std::vector<std::pair<bool, bool>>* out, bool phased,
                      bool lifted) { out->emplace_back(phased, lifted); },
                   base::Unretained(&runs)));
  auto wheel = [&](Phase phase, Phase momentum, bool synthetic = false) {
    blink::WebMouseWheelEvent event;
    event.phase = phase;
    event.momentum_phase = momentum;
    event.has_synthetic_phase = synthetic;
    watcher.OnWheel(event);
  };
  constexpr Phase kNone = blink::WebMouseWheelEvent::kPhaseNone;

  wheel(blink::WebMouseWheelEvent::kPhaseBegan, kNone);
  wheel(kNone, kNone);
  wheel(blink::WebMouseWheelEvent::kPhaseBegan, kNone, /*synthetic=*/true);
  wheel(blink::WebMouseWheelEvent::kPhaseEnded, kNone, /*synthetic=*/true);
  // The trackpad gesture was dropped by the mouse: a stray native lift reports nothing.
  watcher.OnFingersLifted();
  wheel(blink::WebMouseWheelEvent::kPhaseBegan, kNone);
  using Run = std::pair<bool, bool>;
  EXPECT_EQ(runs, (std::vector<Run>{Run(true, false), Run(false, false),
                                    Run(false, false), Run(false, false),
                                    Run(true, false)}));
}

}  // namespace
}  // namespace clu
