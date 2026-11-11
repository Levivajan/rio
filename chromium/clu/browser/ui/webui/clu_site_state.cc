// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_site_state.h"

#include <utility>

#include "base/strings/string_util.h"

namespace clu {

namespace {

base::Value OptionalString(const std::optional<std::string>& value) {
  return value ? base::Value(*value) : base::Value();
}

}  // namespace

SiteTabSnapshot::SiteTabSnapshot() = default;
SiteTabSnapshot::SiteTabSnapshot(const SiteTabSnapshot&) = default;
SiteTabSnapshot& SiteTabSnapshot::operator=(const SiteTabSnapshot&) = default;
SiteTabSnapshot::~SiteTabSnapshot() = default;

ActionButtonSnapshot::ActionButtonSnapshot() = default;
ActionButtonSnapshot::ActionButtonSnapshot(const ActionButtonSnapshot&) =
    default;
ActionButtonSnapshot& ActionButtonSnapshot::operator=(
    const ActionButtonSnapshot&) = default;
ActionButtonSnapshot::~ActionButtonSnapshot() = default;

ActionRowSnapshot::ActionRowSnapshot() = default;
ActionRowSnapshot::ActionRowSnapshot(const ActionRowSnapshot&) = default;
ActionRowSnapshot& ActionRowSnapshot::operator=(const ActionRowSnapshot&) =
    default;
ActionRowSnapshot::~ActionRowSnapshot() = default;

ExtensionInfoSnapshot::ExtensionInfoSnapshot() = default;
ExtensionInfoSnapshot::ExtensionInfoSnapshot(const ExtensionInfoSnapshot&) =
    default;
ExtensionInfoSnapshot& ExtensionInfoSnapshot::operator=(
    const ExtensionInfoSnapshot&) = default;
ExtensionInfoSnapshot::~ExtensionInfoSnapshot() = default;

std::string CapUtf8(std::string_view text, size_t max) {
  std::string out;
  base::TruncateUTF8ToByteSize(text, max, &out);
  return out;
}

base::DictValue SiteTabValue(const SiteTabSnapshot& s) {
  base::Value tweak;
  if (s.tweak) {
    tweak = base::Value(base::DictValue()
                            .Set("enabled", s.tweak->enabled)
                            .Set("hasCss", s.tweak->has_css)
                            .Set("hasJs", s.tweak->has_js)
                            .Set("hasControls", s.tweak->has_controls)
                            .Set("zapCount", s.tweak->zap_count));
  }
  base::Value zap;
  if (s.zap_active) {
    zap = base::Value(
        base::DictValue()
            .Set("active", true)
            .Set("lastResult", OptionalString(s.zap_last_result)));
  }
  return base::DictValue()
      // src/shared/state/extensions.ts
      .Set("site", OptionalString(s.site))
      .Set("blockedCount", s.blocked_count)
      .Set("blockingEnabled", s.blocking_enabled)
      .Set("devMode", s.dev_mode)
      // src/shared/state/tweaks.ts
      .Set("tweak", std::move(tweak))
      .Set("zapMode", std::move(zap));
}

base::Value ActionRowValue(const std::optional<ActionRowSnapshot>& row) {
  if (!row) {
    return base::Value();
  }
  base::ListValue actions;
  for (const ActionButtonSnapshot& a : row->actions) {
    actions.Append(
        base::DictValue()
            .Set("id", a.id)
            .Set("name", CapUtf8(a.name, kMaxActionText))
            .Set("title", CapUtf8(a.title, kMaxActionText))
            .Set("icon", a.icon && a.icon->size() <= kMaxIconDataUrl
                             ? base::Value(*a.icon)
                             : base::Value())
            .Set("badge", CapUtf8(a.badge, kMaxBadgeText))
            .Set("enabled", a.enabled)
            .Set("pinned", a.pinned));
  }
  return base::Value(base::DictValue()
                         .Set("profileId", row->profile_id)
                         .Set("actions", std::move(actions)));
}

base::DictValue ExtensionInfoValue(const ExtensionInfoSnapshot& info) {
  base::ListValue permissions;
  for (size_t i = 0; i < info.permissions.size() && i < kMaxPermissionLines;
       ++i) {
    permissions.Append(CapUtf8(info.permissions[i], kMaxActionText));
  }
  return base::DictValue()
      .Set("id", info.id)
      .Set("name", CapUtf8(info.name, kMaxActionText))
      .Set("version", CapUtf8(info.version, 64))
      .Set("enabled", info.enabled)
      .Set("icon", info.icon && info.icon->size() <= kMaxIconDataUrl
                       ? base::Value(*info.icon)
                       : base::Value())
      .Set("hasAction", info.has_action)
      .Set("hasOptions", info.has_options)
      .Set("permissions", std::move(permissions))
      .Set("pinned", info.pinned);
}

base::ListValue ExtensionsValue(const std::vector<ExtensionInfoSnapshot>& list) {
  base::ListValue out;
  for (const ExtensionInfoSnapshot& info : list) {
    out.Append(ExtensionInfoValue(info));
  }
  return out;
}

}  // namespace clu
