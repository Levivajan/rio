// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/theme/clu_space_themes.h"

#include <utility>

namespace clu {

CluSpaceThemes::CluSpaceThemes() = default;
CluSpaceThemes::~CluSpaceThemes() = default;

int CluSpaceThemes::Load(const base::DictValue& stored,
                         const std::set<std::string>& space_ids) {
  committed_.clear();
  previews_.clear();
  int dropped = 0;
  for (const auto [space_id, value] : stored) {
    std::optional<SpaceTheme> theme = ParseSpaceTheme(value);
    if (!theme || !space_ids.contains(space_id)) {
      ++dropped;
      continue;
    }
    committed_.emplace(space_id, std::move(*theme));
  }
  return dropped;
}

base::DictValue CluSpaceThemes::Value() const {
  base::DictValue out;
  for (const auto& [space_id, theme] : committed_) {
    out.Set(space_id, SpaceThemeValue(theme));
  }
  return out;
}

std::optional<SpaceTheme> CluSpaceThemes::Shown(
    std::string_view space_id) const {
  if (const auto it = previews_.find(space_id); it != previews_.end()) {
    return it->second;
  }
  return Committed(space_id);
}

std::optional<SpaceTheme> CluSpaceThemes::Committed(
    std::string_view space_id) const {
  if (const auto it = committed_.find(space_id); it != committed_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool CluSpaceThemes::Set(const std::string& space_id,
                         const SpaceTheme& theme) {
  previews_.erase(space_id);
  const auto it = committed_.find(space_id);
  if (it != committed_.end() && it->second == theme) {
    return false;
  }
  committed_.insert_or_assign(space_id, theme);
  return true;
}

bool CluSpaceThemes::Reset(std::string_view space_id) {
  bool changed = false;
  if (const auto it = previews_.find(space_id); it != previews_.end()) {
    previews_.erase(it);
    changed = true;
  }
  if (const auto it = committed_.find(space_id); it != committed_.end()) {
    committed_.erase(it);
    changed = true;
  }
  return changed;
}

bool CluSpaceThemes::Preview(const std::string& space_id,
                             const SpaceTheme& theme) {
  if (Shown(space_id) == theme) {
    return false;
  }
  previews_.insert_or_assign(space_id, theme);
  return true;
}

bool CluSpaceThemes::RevertPreview(std::string_view space_id) {
  const auto it = previews_.find(space_id);
  if (it == previews_.end()) {
    return false;
  }
  const bool changed = it->second != Committed(space_id);
  previews_.erase(it);
  return changed;
}

bool CluSpaceThemes::Retain(const std::set<std::string>& space_ids) {
  bool changed = false;
  for (auto* map : {&committed_, &previews_}) {
    for (auto it = map->begin(); it != map->end();) {
      if (space_ids.contains(it->first)) {
        ++it;
      } else {
        it = map->erase(it);
        changed = true;
      }
    }
  }
  return changed;
}

}  // namespace clu
