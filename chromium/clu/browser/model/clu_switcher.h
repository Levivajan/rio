// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_SWITCHER_H_
#define CLU_BROWSER_MODEL_CLU_SWITCHER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace base {
class TickClock;
}  // namespace base

// The Ctrl+Tab recent-tab switcher of one window (02-tabs F13). Ctrl+Tab moves the selection to the next most recent
// tab and shows nothing; a Ctrl release within 180 ms commits it. Still held at 180 ms, the overlay shows the 5 most
// recent tabs; Ctrl+Tab and Ctrl+Shift+Tab step through them with wrap, and the Ctrl release commits. Esc and window
// blur cancel; a key event without Ctrl commits (it covers a lost key-up).
namespace clu {

inline constexpr base::TimeDelta kSwitcherHold = base::Milliseconds(180);
inline constexpr size_t kSwitcherMax = 5;

struct SwitcherView {
  SwitcherView();
  SwitcherView(const SwitcherView&);
  SwitcherView& operator=(const SwitcherView&);
  ~SwitcherView();

  std::vector<std::string> tab_ids;
  int selected = 0;
};

class CluSwitcher {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // The window's recent tabs, most recent first.
    virtual std::vector<std::string> RecentTabs() = 0;
    virtual std::string CurrentTabId() = 0;
    // The overlay shows, updates or hides the cards.
    virtual void ShowSwitcher(const SwitcherView& view) = 0;
    virtual void HideSwitcher() = 0;
    virtual void CommitSwitch(const std::string& tab_id) = 0;
  };

  // `tick_clock` drives the 180 ms hold; null means the default clock.
  CluSwitcher(Delegate& delegate, const base::TickClock* tick_clock);
  CluSwitcher(const CluSwitcher&) = delete;
  CluSwitcher& operator=(const CluSwitcher&) = delete;
  ~CluSwitcher();

  // Ctrl+Tab (1) or Ctrl+Shift+Tab (-1). A second call while a session runs steps it.
  void Start(int direction);
  // Switches at once without a session (a menu pick, where no Ctrl key-up follows).
  void Jump(int direction);
  // The Control key went up, or a key arrived without Control held: commits.
  void ControlReleased();
  // Esc while Ctrl is held: cancels.
  void Cancel();
  // Window blur: cancels.
  void Blur();
  // From the overlay: a card click commits that card, cancel ends the session.
  void Respond(bool commit, const std::optional<std::string>& tab_id);
  // A tab closed or archived.
  void TabRemoved(std::string_view tab_id);

  // True while a session runs, shown or not.
  bool active() const { return session_.has_value(); }
  // The cards while the overlay shows.
  std::optional<SwitcherView> view() const;

 private:
  struct Session {
    Session();
    Session(const Session&);
    Session& operator=(const Session&);
    ~Session();

    std::string current;
    std::vector<std::string> tab_ids;
    int selected = 0;
    bool shown = false;
  };

  std::optional<Session> Plan(int direction);
  void Show();
  void Step(int direction);
  void End();
  void Commit();
  SwitcherView ViewOf(const Session& session) const;

  const raw_ref<Delegate> delegate_;
  std::optional<Session> session_;
  base::OneShotTimer hold_timer_;
};

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_SWITCHER_H_
