// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_nav_state.h"

#include <algorithm>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "clu/browser/model/clu_tab_rules.h"

namespace clu {

namespace {

constexpr size_t kMaxEntryTitle = 1024;

base::DictValue ToValue(const NavState& state) {
  base::ListValue entries;
  for (const NavEntry& entry : state.entries) {
    base::DictValue e;
    e.Set("url", entry.url);
    e.Set("title", entry.title);
    if (entry.page_state) {
      e.Set("pageState", *entry.page_state);
    }
    entries.Append(std::move(e));
  }
  return base::DictValue()
      .Set("index", state.index)
      .Set("entries", std::move(entries));
}

size_t ByteSize(const NavState& state) {
  return SerializeNavState(state).size();
}

}  // namespace

NavEntry::NavEntry() = default;
NavEntry::NavEntry(const NavEntry&) = default;
NavEntry& NavEntry::operator=(const NavEntry&) = default;
NavEntry::NavEntry(NavEntry&&) = default;
NavEntry& NavEntry::operator=(NavEntry&&) = default;
NavEntry::~NavEntry() = default;

NavState::NavState() = default;
NavState::NavState(const NavState&) = default;
NavState& NavState::operator=(const NavState&) = default;
NavState::NavState(NavState&&) = default;
NavState& NavState::operator=(NavState&&) = default;
NavState::~NavState() = default;

std::optional<NavState> CapNavState(NavState input) {
  NavState state;
  int index = -1;
  const int raw_index =
      input.index >= 0 && static_cast<size_t>(input.index) < input.entries.size()
          ? input.index
          : static_cast<int>(input.entries.size()) - 1;
  for (size_t i = 0; i < input.entries.size(); ++i) {
    NavEntry& entry = input.entries[i];
    if (!IsRestorableTabUrl(entry.url)) {
      continue;
    }
    if (static_cast<int>(i) <= raw_index) {
      index = static_cast<int>(state.entries.size());
    }
    std::string title;
    base::TruncateUTF8ToByteSize(entry.title, kMaxEntryTitle, &title);
    entry.title = std::move(title);
    if (entry.page_state && entry.page_state->size() > kNavMaxBytes) {
      entry.page_state.reset();
    }
    state.entries.push_back(std::move(entry));
  }
  if (state.entries.empty()) {
    return std::nullopt;
  }
  state.index = std::max(index, 0);
  // A window of at most 50 entries that holds the current one.
  if (state.entries.size() > kNavMaxEntries) {
    const int half = static_cast<int>(kNavMaxEntries / 2);
    const int start = std::min(
        std::max(0, state.index - half),
        static_cast<int>(state.entries.size() - kNavMaxEntries));
    state.entries.erase(state.entries.begin(), state.entries.begin() + start);
    state.entries.resize(kNavMaxEntries);
    state.index -= start;
  }
  if (ByteSize(state) <= kNavMaxBytes) {
    return state;
  }
  for (NavEntry& entry : state.entries) {
    entry.page_state.reset();
  }
  // Drop the entries farthest from the current one until it fits.
  while (ByteSize(state) > kNavMaxBytes && state.entries.size() > 1) {
    const int after = static_cast<int>(state.entries.size()) - 1 - state.index;
    if (state.index > after) {
      state.entries.erase(state.entries.begin());
      --state.index;
    } else {
      state.entries.pop_back();
    }
  }
  if (ByteSize(state) > kNavMaxBytes) {
    return std::nullopt;
  }
  return state;
}

std::optional<NavState> ParseNavState(std::string_view json) {
  if (json.empty() || json.size() > 4 * kNavMaxBytes) {
    return std::nullopt;
  }
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  const base::ListValue* entries = dict->FindList("entries");
  if (!entries) {
    return std::nullopt;
  }
  NavState state;
  const std::optional<int> index = dict->FindInt("index");
  state.index = index.value_or(static_cast<int>(entries->size()) - 1);
  for (const base::Value& raw : *entries) {
    const base::DictValue* e = raw.GetIfDict();
    const std::string* url = e ? e->FindString("url") : nullptr;
    if (!url) {
      // A junk entry still holds its place, so the current index keeps pointing at the same page.
      NavEntry junk;
      state.entries.push_back(std::move(junk));
      continue;
    }
    NavEntry entry;
    entry.url = *url;
    if (const std::string* title = e->FindString("title")) {
      entry.title = *title;
    }
    if (const std::string* page_state = e->FindString("pageState")) {
      entry.page_state = *page_state;
    }
    state.entries.push_back(std::move(entry));
  }
  return CapNavState(std::move(state));
}

std::string SerializeNavState(const NavState& state) {
  std::optional<std::string> json = base::WriteJson(ToValue(state));
  return json.value_or(std::string());
}

std::optional<std::string> CappedNavStateJson(NavState state) {
  std::optional<NavState> capped = CapNavState(std::move(state));
  if (!capped) {
    return std::nullopt;
  }
  return SerializeNavState(*capped);
}

std::optional<std::string> PushNavEntry(std::string_view json,
                                        const std::string& url) {
  std::optional<NavState> state = ParseNavState(json);
  if (!state) {
    return std::nullopt;
  }
  state->entries.resize(static_cast<size_t>(state->index) + 1);
  NavEntry entry;
  entry.url = url;
  state->entries.push_back(std::move(entry));
  state->index = static_cast<int>(state->entries.size()) - 1;
  return CappedNavStateJson(std::move(*state));
}

}  // namespace clu
