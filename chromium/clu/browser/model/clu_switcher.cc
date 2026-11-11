// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_switcher.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/default_tick_clock.h"

namespace clu {

SwitcherView::SwitcherView() = default;
SwitcherView::SwitcherView(const SwitcherView&) = default;
SwitcherView& SwitcherView::operator=(const SwitcherView&) = default;
SwitcherView::~SwitcherView() = default;

CluSwitcher::Session::Session() = default;
CluSwitcher::Session::Session(const Session&) = default;
CluSwitcher::Session& CluSwitcher::Session::operator=(const Session&) =
    default;
CluSwitcher::Session::~Session() = default;

CluSwitcher::CluSwitcher(Delegate& delegate, const base::TickClock* tick_clock)
    : delegate_(delegate),
      hold_timer_(tick_clock ? tick_clock
                             : base::DefaultTickClock::GetInstance()) {}

CluSwitcher::~CluSwitcher() = default;

std::optional<CluSwitcher::Session> CluSwitcher::Plan(int direction) {
  Session s;
  s.current = delegate_->CurrentTabId();
  std::vector<std::string> others;
  for (std::string& id : delegate_->RecentTabs()) {
    if (id != s.current && !std::ranges::contains(others, id)) {
      others.push_back(std::move(id));
    }
  }
  if (others.empty()) {
    return std::nullopt;
  }
  if (!s.current.empty()) {
    s.tab_ids.push_back(s.current);
  }
  for (std::string& id : others) {
    if (s.tab_ids.size() >= kSwitcherMax) {
      break;
    }
    s.tab_ids.push_back(std::move(id));
  }
  const int first = s.current.empty() ? 0 : 1;
  s.selected =
      direction >= 0 ? first : static_cast<int>(s.tab_ids.size()) - 1;
  return s;
}

void CluSwitcher::Start(int direction) {
  if (session_) {
    Step(direction >= 0 ? 1 : -1);
    return;
  }
  session_ = Plan(direction);
  if (!session_) {
    return;
  }
  hold_timer_.Start(FROM_HERE, kSwitcherHold,
                    base::BindOnce(&CluSwitcher::Show, base::Unretained(this)));
}

void CluSwitcher::Jump(int direction) {
  if (session_) {
    return;
  }
  const std::optional<Session> plan = Plan(direction);
  if (plan) {
    delegate_->CommitSwitch(plan->tab_ids[plan->selected]);
  }
}

void CluSwitcher::ControlReleased() {
  if (session_) {
    Commit();
  }
}

void CluSwitcher::Cancel() {
  End();
}

void CluSwitcher::Blur() {
  End();
}

void CluSwitcher::Respond(bool commit,
                          const std::optional<std::string>& tab_id) {
  if (!session_) {
    return;
  }
  if (!commit) {
    End();
    return;
  }
  if (tab_id) {
    const auto it = std::ranges::find(session_->tab_ids, *tab_id);
    if (it == session_->tab_ids.end()) {
      return;
    }
    session_->selected = static_cast<int>(it - session_->tab_ids.begin());
  }
  Commit();
}

void CluSwitcher::TabRemoved(std::string_view tab_id) {
  if (!session_) {
    return;
  }
  Session& s = *session_;
  const auto it = std::ranges::find(s.tab_ids, tab_id);
  if (it == s.tab_ids.end()) {
    return;
  }
  const int index = static_cast<int>(it - s.tab_ids.begin());
  s.tab_ids.erase(it);
  if (s.current == tab_id) {
    s.current.clear();
  }
  if (index < s.selected) {
    --s.selected;
  }
  const bool any_other = std::ranges::any_of(
      s.tab_ids, [&](const std::string& id) { return id != s.current; });
  if (!any_other) {
    End();
    return;
  }
  s.selected = std::min(s.selected, static_cast<int>(s.tab_ids.size()) - 1);
  if (s.tab_ids[s.selected] == s.current) {
    s.selected = (s.selected + 1) % static_cast<int>(s.tab_ids.size());
  }
  if (s.shown) {
    delegate_->ShowSwitcher(ViewOf(s));
  }
}

std::optional<SwitcherView> CluSwitcher::view() const {
  if (!session_ || !session_->shown) {
    return std::nullopt;
  }
  return ViewOf(*session_);
}

void CluSwitcher::Show() {
  if (!session_) {
    return;
  }
  session_->shown = true;
  delegate_->ShowSwitcher(ViewOf(*session_));
}

void CluSwitcher::Step(int direction) {
  Session& s = *session_;
  const int n = static_cast<int>(s.tab_ids.size());
  s.selected = ((s.selected + direction) % n + n) % n;
  if (s.shown) {
    delegate_->ShowSwitcher(ViewOf(s));
  }
}

void CluSwitcher::End() {
  hold_timer_.Stop();
  if (!session_) {
    return;
  }
  const bool shown = session_->shown;
  session_.reset();
  if (shown) {
    delegate_->HideSwitcher();
  }
}

void CluSwitcher::Commit() {
  const Session s = *session_;
  End();
  const std::string& tab_id = s.tab_ids[s.selected];
  if (tab_id != s.current) {
    delegate_->CommitSwitch(tab_id);
  }
}

SwitcherView CluSwitcher::ViewOf(const Session& session) const {
  SwitcherView view;
  view.tab_ids = session.tab_ids;
  view.selected = session.selected;
  return view;
}

}  // namespace clu
