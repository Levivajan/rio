// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_SPACE_ACTIONS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_SPACE_ACTIONS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/spaces/clu_space_list.h"
#include "clu/browser/spaces/clu_space_rules.h"

class Profile;

// What moves rows, pages and windows when spaces change across profiles (docs/chromium/m4.md). Each step runs in the
// order that keeps every model and window consistent: windows leave a space before its rows go, rows leave one
// profile's model before CluSpacesService moves the space, and the other profile's model takes them right after.
// Profiles load lazily here: an action that needs a profile's rows loads it first.
namespace clu {

class CluWindowState;

// The 120 ms cross-fade of a cross-profile switch: the target window takes the source window's bounds
// and shows at opacity 0; once its frame WebUI shows the space and has painted it, it fades in over the source, which
// stays fully visible underneath and hides at the end. Reduced motion shows it at once. It belongs to the target window
// and stops when either window goes.
class CluWindowFade {
 public:
  static constexpr base::TimeDelta kDuration = base::Milliseconds(120);
  // The fade steps on the target window's compositor frames, at the display's rate (120 Hz on ProMotion);
  // a fade whose frames never come lands this long after its end.
  static constexpr base::TimeDelta kLandSlack = base::Milliseconds(100);

  // `close_source` closes the source window at the end instead of hiding it (a window whose profile has no space
  // left).
  CluWindowFade(base::WeakPtr<CluWindowState> from,
                base::WeakPtr<CluWindowState> to,
                bool close_source);
  CluWindowFade(const CluWindowFade&) = delete;
  CluWindowFade& operator=(const CluWindowFade&) = delete;
  ~CluWindowFade();

  void Start();
  // Ends the fade now: the target fully shown, the source hidden (or closed).
  void Finish();
  bool finished() const { return finished_; }

  // Tests: called on every frame with the target's opacity, whether the source is still visible, and whether the
  // target's frame showed its space when it became visible.
  using TickObserver =
      base::RepeatingCallback<void(float opacity,
                                   bool source_visible,
                                   bool target_frame_ready)>;
  static void SetTickObserverForTesting(TickObserver observer);
  // Tests: how many fade steps came from a compositor frame since the process started.
  static int compositor_steps_for_testing();
  // Tests: runs at the target frame's first paint with the closure that starts the rise, so a test can paint late.
  using PaintGate = base::RepeatingCallback<void(base::OnceClosure rise)>;
  static void SetPaintGateForTesting(PaintGate gate);

  // A frame that never paints (a load failure, a crash) must not leave an invisible window over the source. Only a
  // cap: the fade waits for the paint, which a new profile's first window on a busy Mac can take seconds to reach.
  static constexpr base::TimeDelta kPaintCap = base::Seconds(5);

 private:
  void OnFrameReady();
  void OnFramePainted();
  void Rise();
  void Step();

  class FrameDriver;

  base::WeakPtr<CluWindowState> from_;
  base::WeakPtr<CluWindowState> to_;
  const bool close_source_;
  base::TimeTicks start_;
  std::unique_ptr<FrameDriver> frame_driver_;
  base::OneShotTimer timer_;
  base::OneShotTimer paint_cap_;
  bool finished_ = false;
  base::WeakPtrFactory<CluWindowFade> weak_ptr_factory_{this};
};

namespace space_actions {

// Every Clu window now (one CluWindowState per Browser with the Clu layout).
std::vector<CluWindowState*> AllWindows();

// spaces.switch from `window`: a space of its own profile switches in place (CluWindowState::ShowSpace()); a space
// of another profile shows the group's window of that profile, made at the same bounds when the group has none, and
// fades it in over `window`, which hides. `done(true)` once the switch is under way; false for a no-op (an index past
// the end, a step past either end, the space already shown, a private window).
void SwitchWindow(CluWindowState* window,
                  const SwitchTarget& target,
                  base::OnceCallback<void(bool)> done = base::NullCallback());
void SwitchWindowToSpace(
    CluWindowState* window,
    const std::string& space_id,
    bool close_source,
    base::OnceCallback<void(bool)> done = base::NullCallback());

// spaces.create: the space joins the list and `window` switches to it.
base::expected<std::string, SpacesError> CreateSpace(
    CluWindowState* window,
    const std::optional<std::string>& name,
    const std::optional<SpaceIconInput>& icon,
    const std::optional<std::string>& color,
    const std::optional<std::string>& profile_id);

// spaces.delete (docs/plan/03-spaces.md "Delete space"): the last space is refused. Windows showing the space switch
// to the previous space (else the next), its rows go to the archive with reason delete, then it leaves the list.
void DeleteSpace(
    const std::string& space_id,
    base::OnceCallback<void(base::expected<void, SpacesError>)> done);
// The switcher icon menu's Delete Space…: asks first with the pinned and Today counts, then deletes.
void ConfirmDeleteSpace(CluWindowState* window, const std::string& space_id);

// spaces.setProfile: a space with tabs answers needs-confirmation with their count until `confirmed`. Its rows move
// to the other profile's model (pages reload there without history when shown) and windows showing it follow it.
struct SetProfileResult {
  bool needs_confirmation = false;
  int tab_count = 0;
};
void SetSpaceProfile(
    const std::string& space_id,
    const std::string& profile_id,
    bool confirmed,
    base::OnceCallback<void(base::expected<SetProfileResult, SpacesError>)>
        done);

// tabs.move and tabs.bulk with a target, and Move To: a target in a space of another profile moves the rows across
// profiles. Without `confirmed` such a move answers kNeedsConfirmation and changes nothing; with `ask` the window asks
// in its overlay first ("Move to <profile>?"). A target in the window's own profile moves in place.
enum class MoveStatus { kMoved, kNeedsConfirmation };
void MoveRows(
    CluWindowState* window,
    std::vector<std::string> ids,
    DropTarget target,
    bool confirmed,
    bool ask,
    base::OnceCallback<void(base::expected<MoveStatus, TreeError>)> done);

// True when `target` is a space of another profile than `window`'s.
bool IsCrossProfile(CluWindowState* window, const DropTarget& target);

// window.newPrivate and Cmd+Shift+N: Chromium's incognito window of the profile, with Clu's private shell.
void OpenPrivateWindow(Profile* profile);

}  // namespace space_actions

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_SPACE_ACTIONS_H_
