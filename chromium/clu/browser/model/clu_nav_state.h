// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_NAV_STATE_H_
#define CLU_BROWSER_MODEL_CLU_NAV_STATE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Back and forward history kept on Today rows and archive rows (src/shared/navState.ts, PLAN decision 16): at most 50
// entries around the current one and 256 KiB of JSON, URLs the tab policy allows only. The JSON keeps the TS shape,
// {index, entries: [{url, title, pageState?}]}; pageState is Chromium's encoded page state in base64.
namespace clu {

inline constexpr size_t kNavMaxEntries = 50;
inline constexpr size_t kNavMaxBytes = 256 * 1024;

struct NavEntry {
  NavEntry();
  NavEntry(const NavEntry&);
  NavEntry& operator=(const NavEntry&);
  NavEntry(NavEntry&&);
  NavEntry& operator=(NavEntry&&);
  ~NavEntry();

  std::string url;
  std::string title;
  std::optional<std::string> page_state;
};

struct NavState {
  NavState();
  NavState(const NavState&);
  NavState& operator=(const NavState&);
  NavState(NavState&&);
  NavState& operator=(NavState&&);
  ~NavState();

  int index = 0;
  std::vector<NavEntry> entries;
};

// capNavState(): drops entries whose URL the policy refuses, keeps a window of 50 around the current entry, then fits
// 256 KiB by dropping page states and then the entries farthest from the current one. nullopt when nothing usable is
// left.
std::optional<NavState> CapNavState(NavState state);

// parseNavState(): junk JSON, a wrong shape or nothing usable reads as nullopt.
std::optional<NavState> ParseNavState(std::string_view json);
std::string SerializeNavState(const NavState& state);

// The JSON text a row stores for `state` after capping, or nullopt.
std::optional<std::string> CappedNavStateJson(NavState state);

// The JSON of `json`'s history with `url` loaded on top as a link would: the forward entries go, and `url` becomes the
// current entry. nullopt when `json` holds no usable history or the result has none.
std::optional<std::string> PushNavEntry(std::string_view json,
                                        const std::string& url);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_NAV_STATE_H_
