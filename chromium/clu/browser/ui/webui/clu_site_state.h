// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_SITE_STATE_H_
#define CLU_BROWSER_UI_WEBUI_CLU_SITE_STATE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/values.h"

// The M7 fields of CluState (docs/chromium/m7.md) as plain snapshots and their JSON: a tab's site, blocked count,
// blocking switch, tweak summary and zap mode (src/shared/state/extensions.ts, tweaks.ts), a window's action row
// and a profile's extension list.
namespace clu {

// TabState.tweak.
struct TweakSummarySnapshot {
  bool enabled = true;
  bool has_css = false;
  bool has_js = false;
  bool has_controls = false;
  int zap_count = 0;

  friend bool operator==(const TweakSummarySnapshot&,
                         const TweakSummarySnapshot&) = default;
};

struct SiteTabSnapshot {
  SiteTabSnapshot();
  SiteTabSnapshot(const SiteTabSnapshot&);
  SiteTabSnapshot& operator=(const SiteTabSnapshot&);
  ~SiteTabSnapshot();

  std::optional<std::string> site;
  int blocked_count = 0;
  bool blocking_enabled = true;
  // The site is in Developer Mode.
  bool dev_mode = false;
  std::optional<TweakSummarySnapshot> tweak;
  bool zap_active = false;
  std::optional<std::string> zap_last_result;

  // Field by field: a publish skips a tab whose snapshot is unchanged (UpdateCluState).
  friend bool operator==(const SiteTabSnapshot&,
                         const SiteTabSnapshot&) = default;
};

// One entry of windows[id].actionRow.actions (ActionButton).
struct ActionButtonSnapshot {
  ActionButtonSnapshot();
  ActionButtonSnapshot(const ActionButtonSnapshot&);
  ActionButtonSnapshot& operator=(const ActionButtonSnapshot&);
  ~ActionButtonSnapshot();

  std::string id;
  std::string name;
  std::string title;
  // A PNG data URL, or null.
  std::optional<std::string> icon;
  std::string badge;
  bool enabled = true;
  bool pinned = false;
};

struct ActionRowSnapshot {
  ActionRowSnapshot();
  ActionRowSnapshot(const ActionRowSnapshot&);
  ActionRowSnapshot& operator=(const ActionRowSnapshot&);
  ~ActionRowSnapshot();

  std::string profile_id;
  std::vector<ActionButtonSnapshot> actions;
};

// One entry of profiles[id].extensions (ExtensionInfo in src/shared/ipc/extensions.ts).
struct ExtensionInfoSnapshot {
  ExtensionInfoSnapshot();
  ExtensionInfoSnapshot(const ExtensionInfoSnapshot&);
  ExtensionInfoSnapshot& operator=(const ExtensionInfoSnapshot&);
  ~ExtensionInfoSnapshot();

  std::string id;
  std::string name;
  std::string version;
  bool enabled = true;
  std::optional<std::string> icon;
  bool has_action = true;
  bool has_options = false;
  std::vector<std::string> permissions;
  bool pinned = false;
};

// Caps on what an extension puts in state: its name and title, its badge, its icon data URL.
inline constexpr size_t kMaxActionText = 256;
inline constexpr size_t kMaxBadgeText = 16;
inline constexpr size_t kMaxIconDataUrl = 64 * 1024;
inline constexpr size_t kMaxPermissionLines = 32;

// The tab fields: site, blockedCount, blockingEnabled, tweak, zapMode.
base::DictValue SiteTabValue(const SiteTabSnapshot& site);
base::Value ActionRowValue(const std::optional<ActionRowSnapshot>& row);
base::DictValue ExtensionInfoValue(const ExtensionInfoSnapshot& info);
base::ListValue ExtensionsValue(const std::vector<ExtensionInfoSnapshot>& list);

// `text` cut to at most `max` bytes on a UTF-8 boundary.
std::string CapUtf8(std::string_view text, size_t max);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_SITE_STATE_H_
