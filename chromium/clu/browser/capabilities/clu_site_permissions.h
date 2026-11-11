// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_SITE_PERMISSIONS_H_
#define CLU_BROWSER_CAPABILITIES_CLU_SITE_PERMISSIONS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "url/gurl.h"

class HostContentSettingsMap;

// Site permissions for Site controls and Rio's settings (permission.list, permission.set and permission.clear in
// src/shared/ipc/capabilities.ts; docs/chromium/fixes.md). Chromium keeps every decision in the profile's
// HostContentSettingsMap: its prompts write there, chrome://settings/content reads and writes there, and so do
// these. Nothing is stored twice.
namespace clu {

// A permission by its contract name (SITE_PERMISSIONS in src/shared/capabilities.ts, plus the other kinds a site can
// have a decision for) and its Chromium content setting.
struct SitePermissionKind {
  std::string_view name;
  ContentSettingsType type;
};

// Every kind, the contract's six first (camera, microphone, location, notifications, clipboard, popups).
base::span<const SitePermissionKind> SitePermissionKinds();
// The kind named `name`, or null for junk.
const SitePermissionKind* FindSitePermission(std::string_view name);

// The origin of `url` as a URL ("https://example.com"), for http and https only; nullopt for anything else (chrome://,
// file:, junk, an empty host).
std::optional<GURL> SitePermissionOrigin(std::string_view url);

// One user decision: Allow or Block for a kind on a site. Chromium's defaults, policy and extension settings are not
// rows: the user cannot change them here.
struct SitePermissionRow {
  std::string origin;
  std::string permission;
  bool allow = false;
  int64_t updated_at_ms = 0;

  bool operator==(const SitePermissionRow&) const = default;
};

// The user's decisions in `map`. With `origin`, the decisions that apply to that origin (one per kind, as Chromium
// resolves it); without, every site's, one row per origin and kind, sites in Chromium's precedence order.
std::vector<SitePermissionRow> ListSitePermissions(
    const HostContentSettingsMap& map,
    const std::optional<GURL>& origin);

enum class SitePermissionDecision { kAsk, kAllow, kBlock };

// Stores `decision` for `kind` on `origin` with the scope Chromium's own prompts use
// (SetContentSettingDefaultScope). kAsk removes the user's decisions that apply, so Chromium's default comes back.
// False when Chromium refuses the value for that kind, or a policy or an extension decides it.
bool SetSitePermission(HostContentSettingsMap& map,
                       const GURL& origin,
                       const SitePermissionKind& kind,
                       SitePermissionDecision decision);

// Removes user decisions: those that apply to `origin`, or every site's without it; for `kind`, or every kind
// without it.
void ClearSitePermissions(HostContentSettingsMap& map,
                          const std::optional<GURL>& origin,
                          const SitePermissionKind* kind);

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_SITE_PERMISSIONS_H_
