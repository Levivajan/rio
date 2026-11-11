// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_archive_rules.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "base/strings/strcat.h"

namespace clu {

namespace {

constexpr int kArchiveAfterChoices[] = {12, 24, 168, 720};
constexpr int64_t kHourMs = 60 * 60 * 1000;

}  // namespace

std::string_view ArchiveReasonName(ArchiveReason reason) {
  switch (reason) {
    case ArchiveReason::kClose:
      return "close";
    case ArchiveReason::kClear:
      return "clear";
    case ArchiveReason::kAuto:
      return "auto";
    case ArchiveReason::kDelete:
      return "delete";
    case ArchiveReason::kMini:
      return "mini";
  }
}

std::optional<ArchiveReason> ArchiveReasonFromName(std::string_view name) {
  for (ArchiveReason reason :
       {ArchiveReason::kClose, ArchiveReason::kClear, ArchiveReason::kAuto,
        ArchiveReason::kDelete, ArchiveReason::kMini}) {
    if (ArchiveReasonName(reason) == name) {
      return reason;
    }
  }
  return std::nullopt;
}

bool IsArchiveAfterHours(double hours) {
  return std::ranges::any_of(kArchiveAfterChoices, [&](int choice) {
    return hours == static_cast<double>(choice);
  });
}

int ArchiveAfterHours(std::optional<double> stored) {
  if (stored && IsArchiveAfterHours(*stored)) {
    return static_cast<int>(*stored);
  }
  return kDefaultArchiveAfterHours;
}

int64_t ArchiveAfterMs(int hours) {
  return static_cast<int64_t>(ArchiveAfterHours(hours)) * kHourMs;
}

SweepTab::SweepTab() = default;
SweepTab::SweepTab(const SweepTab&) = default;
SweepTab& SweepTab::operator=(const SweepTab&) = default;
SweepTab::~SweepTab() = default;

std::vector<std::string> SelectIdleTabs(
    const std::vector<SweepTab>& tabs,
    int64_t now,
    int64_t threshold_ms,
    std::map<std::string, int64_t>& first_seen) {
  // A split archives as one unit: every member must be a Today tab and none held.
  std::map<std::string, std::vector<const SweepTab*>> units;
  std::vector<std::string> order;
  for (const SweepTab& tab : tabs) {
    const std::string key = tab.split_id ? base::StrCat({"split:", *tab.split_id})
                                         : base::StrCat({"tab:", tab.id});
    if (!units.contains(key)) {
      order.push_back(key);
    }
    units[key].push_back(&tab);
  }
  std::set<std::string> seen;
  auto since = [&](const SweepTab& tab) -> int64_t {
    if (tab.last_active_at > 0) {
      return tab.last_active_at;
    }
    seen.insert(tab.id);
    auto [it, inserted] = first_seen.try_emplace(tab.id, now);
    return it->second;
  };
  std::vector<std::string> out;
  for (const std::string& key : order) {
    const std::vector<const SweepTab*>& members = units[key];
    if (!std::ranges::all_of(members, [](const SweepTab* t) {
          return t->section == Section::kToday;
        })) {
      continue;
    }
    int64_t newest = 0;
    for (const SweepTab* t : members) {
      newest = std::max(newest, since(*t));
    }
    const int64_t idle = now - newest;
    // A negative idle time comes from a clock change; the tab waits for the clock to catch up.
    if (idle < 0 || idle < threshold_ms) {
      continue;
    }
    if (std::ranges::any_of(members, [](const SweepTab* t) { return t->held; })) {
      continue;
    }
    for (const SweepTab* t : members) {
      out.push_back(t->id);
    }
  }
  std::erase_if(first_seen,
                [&](const auto& entry) { return !seen.contains(entry.first); });
  return out;
}

ClosedEntry::ClosedEntry() = default;
ClosedEntry::ClosedEntry(const ClosedEntry&) = default;
ClosedEntry& ClosedEntry::operator=(const ClosedEntry&) = default;
ClosedEntry::~ClosedEntry() = default;

ClosedStack::ClosedStack() = default;
ClosedStack::~ClosedStack() = default;

void ClosedStack::PushRows(std::vector<std::string> archive_ids,
                           ArchiveReason reason) {
  if (reason == ArchiveReason::kAuto || archive_ids.empty()) {
    return;
  }
  ClosedEntry entry;
  entry.kind = ClosedEntry::Kind::kRows;
  entry.archive_ids = std::move(archive_ids);
  entry.seq = ++seq_;
  entries_.push_back(std::move(entry));
  while (entries_.size() > kMaxEntries) {
    entries_.pop_front();
  }
}

void ClosedStack::PushAnchored(std::string tab_id,
                               std::string url,
                               std::optional<std::string> nav_state) {
  ClosedEntry entry;
  entry.kind = ClosedEntry::Kind::kAnchored;
  entry.tab_id = std::move(tab_id);
  entry.url = std::move(url);
  entry.nav_state = std::move(nav_state);
  entry.seq = ++seq_;
  entries_.push_back(std::move(entry));
  while (entries_.size() > kMaxEntries) {
    entries_.pop_front();
  }
}

std::optional<ClosedEntry> ClosedStack::Pop() {
  if (entries_.empty()) {
    return std::nullopt;
  }
  ClosedEntry entry = std::move(entries_.back());
  entries_.pop_back();
  return entry;
}

TabMru::TabMru() = default;
TabMru::~TabMru() = default;

void TabMru::SeedIfEmpty(
    const std::vector<std::pair<std::string, int64_t>>& tabs) {
  if (seeded_) {
    return;
  }
  seeded_ = true;
  std::vector<std::pair<std::string, int64_t>> recent;
  for (const auto& tab : tabs) {
    if (tab.second > 0) {
      recent.push_back(tab);
    }
  }
  std::ranges::sort(recent, [](const auto& a, const auto& b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  for (const auto& [id, at] : recent) {
    if (!std::ranges::contains(list_, id)) {
      list_.push_back(id);
    }
  }
}

void TabMru::Touch(std::string_view tab_id) {
  std::erase(list_, tab_id);
  list_.insert(list_.begin(), std::string(tab_id));
}

void TabMru::Remove(std::string_view tab_id) {
  std::erase(list_, tab_id);
}

}  // namespace clu
