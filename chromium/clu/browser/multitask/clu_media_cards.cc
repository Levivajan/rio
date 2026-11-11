// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_media_cards.h"

namespace clu {

CluMediaCards::CluMediaCards() = default;
CluMediaCards::~CluMediaCards() = default;

CluMediaCards::Media& CluMediaCards::Entry(const std::string& tab_id) {
  return media_[tab_id];
}

void CluMediaCards::SetShown(const std::vector<std::string>& tab_ids) {
  shown_.clear();
  for (const std::string& id : tab_ids) {
    shown_.insert(id);
    // Seen paused: the card lets go of it.
    if (const auto it = media_.find(id); it != media_.end()) {
      it->second.held = false;
    }
  }
}

void CluMediaCards::Started(const std::string& tab_id) {
  Media& m = Entry(tab_id);
  if (!m.held) {
    m.heard = m.audible_now;
  }
  m.started = ++next_start_;
  m.playing = true;
  m.held = false;
}

void CluMediaCards::Paused(const std::string& tab_id) {
  const auto it = media_.find(tab_id);
  if (it == media_.end()) {
    return;
  }
  it->second.playing = false;
  if (!it->second.held) {
    it->second.heard = false;
  }
}

void CluMediaCards::Audible(const std::string& tab_id, bool audible) {
  Media& m = Entry(tab_id);
  m.audible_now = audible;
  if (audible && m.playing) {
    m.heard = true;
  }
}

void CluMediaCards::Reset(const std::string& tab_id) {
  media_.erase(tab_id);
}

CluMediaCards::ToggleAction CluMediaCards::Toggle(const std::string& tab_id) {
  const auto it = media_.find(tab_id);
  if (it == media_.end() || (!it->second.playing && !it->second.held)) {
    return ToggleAction::kNone;
  }
  if (it->second.playing) {
    // Held first: the stop event arrives after the pause.
    it->second.held = true;
    return ToggleAction::kPause;
  }
  return ToggleAction::kPlay;
}

std::optional<std::string> CluMediaCards::Card(
    base::FunctionRef<bool(std::string_view)> muted) const {
  std::optional<std::string> best;
  int64_t best_started = -1;
  for (const auto& [id, m] : media_) {
    if (shown_.contains(id)) {
      continue;
    }
    if (!m.held && !(m.playing && (m.heard || muted(id)))) {
      continue;
    }
    if (m.started > best_started) {
      best = id;
      best_started = m.started;
    }
  }
  return best;
}

bool CluMediaCards::IsPlaying(std::string_view tab_id) const {
  const auto it = media_.find(tab_id);
  return it != media_.end() && it->second.playing;
}

}  // namespace clu
