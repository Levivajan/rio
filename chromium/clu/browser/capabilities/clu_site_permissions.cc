// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_site_permissions.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/time/time.h"
#include "components/content_settings/core/browser/content_settings_info.h"
#include "components/content_settings/core/browser/content_settings_registry.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "url/origin.h"

namespace clu {

namespace {

// The contract's six first (SITE_PERMISSIONS), then the other kinds a site can have a decision for in Chromium's
// site settings. Location is GEOLOCATION: the approximate-location variant (GEOLOCATION_WITH_OPTIONS) is off on Mac
// (content_settings::GeolocationContentSettingsType()).
constexpr SitePermissionKind kKinds[] = {
    {"camera", ContentSettingsType::MEDIASTREAM_CAMERA},
    {"microphone", ContentSettingsType::MEDIASTREAM_MIC},
    {"location", ContentSettingsType::GEOLOCATION},
    {"notifications", ContentSettingsType::NOTIFICATIONS},
    {"clipboard", ContentSettingsType::CLIPBOARD_READ_WRITE},
    {"popups", ContentSettingsType::POPUPS},
    {"sound", ContentSettingsType::SOUND},
    {"automaticDownloads", ContentSettingsType::AUTOMATIC_DOWNLOADS},
    {"midi", ContentSettingsType::MIDI_SYSEX},
    {"sensors", ContentSettingsType::SENSORS},
    {"cameraPanTiltZoom", ContentSettingsType::CAMERA_PAN_TILT_ZOOM},
    {"windowManagement", ContentSettingsType::WINDOW_MANAGEMENT},
    {"localFonts", ContentSettingsType::LOCAL_FONTS},
    {"idleDetection", ContentSettingsType::IDLE_DETECTION},
    {"backgroundSync", ContentSettingsType::BACKGROUND_SYNC},
    {"javascript", ContentSettingsType::JAVASCRIPT},
    {"images", ContentSettingsType::IMAGES},
    {"vr", ContentSettingsType::VR},
    {"ar", ContentSettingsType::AR},
    {"handTracking", ContentSettingsType::HAND_TRACKING},
    {"keyboardLock", ContentSettingsType::KEYBOARD_LOCK},
    {"pointerLock", ContentSettingsType::POINTER_LOCK},
    {"capturedSurfaceControl", ContentSettingsType::CAPTURED_SURFACE_CONTROL},
    {"localNetworkAccess", ContentSettingsType::LOCAL_NETWORK_ACCESS},
    {"autoPictureInPicture", ContentSettingsType::AUTO_PICTURE_IN_PICTURE},
};

// A setting rule's resolution can name the same pattern again when a provider other than the prefs keeps it; the
// loop that removes the rules applying to an origin stops after this many.
constexpr int kMaxRulesPerOrigin = 16;

// Registered as a content setting in this build (VR, AR and hand tracking only with XR, for example).
bool IsRegistered(ContentSettingsType type) {
  return content_settings::ContentSettingsRegistry::GetInstance()->Get(type) !=
         nullptr;
}

bool IsSiteRule(const ContentSettingsPattern& primary,
                const ContentSettingsPattern& secondary) {
  return primary != ContentSettingsPattern::Wildcard() ||
         secondary != ContentSettingsPattern::Wildcard();
}

// A rule the user made for a site (a prompt's answer, Site controls, chrome://settings/content), not the default and
// not a policy's or an extension's.
bool IsUserSiteRule(const content_settings::SettingInfo& info) {
  return info.source == content_settings::SettingSource::kUser &&
         IsSiteRule(info.primary_pattern, info.secondary_pattern);
}

bool IsUserSiteRule(const ContentSettingPatternSource& rule) {
  return (rule.source == content_settings::mojom::ProviderType::kPrefProvider ||
          rule.source ==
              content_settings::mojom::ProviderType::kOneTimePermissionProvider) &&
         !rule.IsExpired() &&
         IsSiteRule(rule.primary_pattern, rule.secondary_pattern);
}

int64_t Millis(base::Time time) {
  return time.is_null() ? 0 : time.InMillisecondsSinceUnixEpoch();
}

// The origin a site rule shows as: its primary pattern's representative URL ("[*.]example.com" shows as
// https://example.com), else none (a rule for every site embedded in one).
std::optional<std::string> RuleOrigin(const ContentSettingPatternSource& rule) {
  if (rule.primary_pattern == ContentSettingsPattern::Wildcard()) {
    return std::nullopt;
  }
  const std::optional<GURL> origin =
      SitePermissionOrigin(rule.primary_pattern.ToRepresentativeUrl().spec());
  if (!origin) {
    return std::nullopt;
  }
  return url::Origin::Create(*origin).Serialize();
}

void ResetRule(HostContentSettingsMap& map,
               const ContentSettingsPattern& primary,
               const ContentSettingsPattern& secondary,
               ContentSettingsType type) {
  map.SetContentSettingCustomScope(primary, secondary, type,
                                   CONTENT_SETTING_DEFAULT);
}

void ClearKind(HostContentSettingsMap& map,
               const std::optional<GURL>& origin,
               ContentSettingsType type) {
  if (!IsRegistered(type)) {
    return;
  }
  if (origin) {
    // The rules that apply to the origin, most specific first, as Chromium resolves them.
    for (int i = 0; i < kMaxRulesPerOrigin; ++i) {
      content_settings::SettingInfo info;
      map.GetContentSetting(*origin, *origin, type, &info);
      if (!IsUserSiteRule(info)) {
        break;
      }
      ResetRule(map, info.primary_pattern, info.secondary_pattern, type);
    }
  }
  // And the rules listed under the origin (or every rule without one).
  const std::string serialized =
      origin ? url::Origin::Create(*origin).Serialize() : std::string();
  for (const ContentSettingPatternSource& rule :
       map.GetSettingsForOneType(type)) {
    if (!IsUserSiteRule(rule)) {
      continue;
    }
    if (origin && RuleOrigin(rule) != serialized) {
      continue;
    }
    ResetRule(map, rule.primary_pattern, rule.secondary_pattern, type);
  }
}

}  // namespace

base::span<const SitePermissionKind> SitePermissionKinds() {
  return kKinds;
}

const SitePermissionKind* FindSitePermission(std::string_view name) {
  const auto it = std::ranges::find(kKinds, name, &SitePermissionKind::name);
  return it == std::end(kKinds) ? nullptr : &*it;
}

std::optional<GURL> SitePermissionOrigin(std::string_view url) {
  const GURL parsed(url);
  if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS() ||
      parsed.host().empty()) {
    return std::nullopt;
  }
  const url::Origin origin = url::Origin::Create(parsed);
  if (origin.opaque()) {
    return std::nullopt;
  }
  return origin.GetURL();
}

std::vector<SitePermissionRow> ListSitePermissions(
    const HostContentSettingsMap& map,
    const std::optional<GURL>& origin) {
  std::vector<SitePermissionRow> rows;
  for (const SitePermissionKind& kind : kKinds) {
    if (!IsRegistered(kind.type)) {
      continue;
    }
    if (origin) {
      content_settings::SettingInfo info;
      const ContentSetting setting =
          map.GetContentSetting(*origin, *origin, kind.type, &info);
      if (IsUserSiteRule(info) && (setting == CONTENT_SETTING_ALLOW ||
                                   setting == CONTENT_SETTING_BLOCK)) {
        rows.push_back({.origin = url::Origin::Create(*origin).Serialize(),
                        .permission = std::string(kind.name),
                        .allow = setting == CONTENT_SETTING_ALLOW,
                        .updated_at_ms = Millis(info.metadata.last_modified())});
      }
      continue;
    }
    // Chromium's precedence order: the first rule for an origin is the one that counts.
    std::set<std::string> seen;
    for (const ContentSettingPatternSource& rule :
         map.GetSettingsForOneType(kind.type)) {
      if (!IsUserSiteRule(rule)) {
        continue;
      }
      const ContentSetting setting = rule.GetContentSetting();
      if (setting != CONTENT_SETTING_ALLOW && setting != CONTENT_SETTING_BLOCK) {
        continue;
      }
      std::optional<std::string> rule_origin = RuleOrigin(rule);
      if (!rule_origin || !seen.insert(*rule_origin).second) {
        continue;
      }
      rows.push_back({.origin = std::move(*rule_origin),
                      .permission = std::string(kind.name),
                      .allow = setting == CONTENT_SETTING_ALLOW,
                      .updated_at_ms = Millis(rule.metadata.last_modified())});
    }
  }
  return rows;
}

bool SetSitePermission(HostContentSettingsMap& map,
                       const GURL& origin,
                       const SitePermissionKind& kind,
                       SitePermissionDecision decision) {
  const content_settings::ContentSettingsInfo* info =
      content_settings::ContentSettingsRegistry::GetInstance()->Get(kind.type);
  if (!info) {
    return false;
  }
  if (decision == SitePermissionDecision::kAsk) {
    ClearKind(map, origin, kind.type);
    // Chromium's default decides again; a policy or an extension that still does is a refusal.
    content_settings::SettingInfo after;
    map.GetContentSetting(origin, origin, kind.type, &after);
    return after.source == content_settings::SettingSource::kUser &&
           !IsSiteRule(after.primary_pattern, after.secondary_pattern);
  }
  const ContentSetting setting = decision == SitePermissionDecision::kAllow
                                     ? CONTENT_SETTING_ALLOW
                                     : CONTENT_SETTING_BLOCK;
  if (!info->IsSettingValid(setting)) {
    return false;
  }
  // The scope Chromium's prompts write with (PermissionContextBase::UpdateContentSetting).
  map.SetContentSettingDefaultScope(origin, origin, kind.type, setting);
  content_settings::SettingInfo after;
  return map.GetContentSetting(origin, origin, kind.type, &after) == setting &&
         after.source == content_settings::SettingSource::kUser;
}

void ClearSitePermissions(HostContentSettingsMap& map,
                          const std::optional<GURL>& origin,
                          const SitePermissionKind* kind) {
  if (kind) {
    ClearKind(map, origin, kind->type);
    return;
  }
  for (const SitePermissionKind& each : kKinds) {
    ClearKind(map, origin, each.type);
  }
}

}  // namespace clu
