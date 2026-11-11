// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_state_builder.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/theme/clu_theme_math.h"
#include "clu/browser/ui/webui/clu_url_policy.h"

namespace clu {

namespace {


base::Value Null() {
  return base::Value();
}

base::Value OptionalString(const std::optional<std::string>& value) {
  return value ? base::Value(*value) : Null();
}

base::Value SidebarRequestValue(
    const std::optional<SidebarRequestSnapshot>& request) {
  if (!request) {
    return Null();
  }
  return base::Value(base::DictValue()
                         .Set("seq", request->seq)
                         .Set("kind", request->kind)
                         .Set("nodeId", request->node_id)
                         .Set("floating", request->floating));
}

base::DictValue RectValue(const gfx::Rect& rect) {
  return base::DictValue()
      .Set("x", rect.x())
      .Set("y", rect.y())
      .Set("width", rect.width())
      .Set("height", rect.height());
}

base::ListValue StringList(const std::vector<std::string>& values) {
  base::ListValue list;
  for (const std::string& value : values) {
    list.Append(value);
  }
  return list;
}

base::DictValue UpdateValue(const UpdateSnapshot& update) {
  base::DictValue value;
  value.Set("status", UpdateStatusName(update.status));
  value.Set("version", OptionalString(update.version));
  value.Set("checkedAt",
            update.checked_at ? base::Value(*update.checked_at) : Null());
  value.Set("dismissed", update.dismissed);
  return value;
}

base::Value ImportValue(const std::optional<ImportProgressSnapshot>& p) {
  if (!p) {
    return Null();
  }
  return base::Value(base::DictValue()
                         .Set("source", p->source)
                         .Set("phase", p->phase)
                         .Set("done", p->done)
                         .Set("total", p->total)
                         .Set("skipped", p->skipped));
}

base::Value ThemeValue(const std::optional<SpaceTheme>& theme) {
  return theme ? base::Value(SpaceThemeValue(*theme)) : Null();
}

base::DictValue AppValue(const WindowSnapshot& s) {
  return base::DictValue()
      .Set("version", s.version)
      .Set("isPackaged", true)
      .Set("versions", base::DictValue()
                           .Set("chrome", s.version)
                           .Set("node", ""))
      .Set("appearance", base::DictValue()
                             .Set("mode", s.appearance_mode)
                             .Set("resolved", s.dark ? "dark" : "light")
                             .Set("reduceTransparency", s.reduce_transparency)
                             .Set("reduceMotion", s.reduce_motion))
      // A private window never shows first-run onboarding.
      .Set("onboarding",
           base::DictValue().Set("show", s.onboarding_show && !s.off_the_record))
      .Set("dataHealth", base::DictValue()
                             .Set("recovered", OptionalString(s.recovered))
                             .Set("persistent",
                                  !s.off_the_record && s.persistent))
      .Set("import", ImportValue(s.import_progress))
      .Set("defaultBrowser", s.default_browser)
      .Set("update", UpdateValue(s.update));
}

base::DictValue TabValue(const WindowSnapshot& s, const TabSnapshot& tab) {
  // Clu's pages show as rio:// (the display alias, clu_url_policy.h); Chromium's other pages as chrome://.
  const std::string url = DisplayUrl(tab.url);
  const bool internal = url.starts_with("chrome:") || url.starts_with("rio:");
  const bool favorite = tab.section == "favorite";
  const std::string& space_id = tab.space_id.empty() ? s.space_id : tab.space_id;
  base::DictValue value = base::DictValue()
      // src/shared/state/engine.ts
      .Set("lifecycle", !tab.live || tab.discarded ? "unloaded" : "live")
      .Set("pendingUrl", OptionalString(tab.pending_url))
      .Set("loading", tab.loading)
      .Set("progress", tab.progress)
      .Set("canGoBack", tab.can_go_back)
      .Set("canGoForward", tab.can_go_forward)
      .Set("audible", tab.audible)
      .Set("zoomFactor", tab.zoom_factor)
      .Set("find", Null())
      .Set("issue", Null())
      .Set("security", tab.secure ? "secure" : "none")
      .Set("fullscreen", false)
      .Set("unresponsive", false)
      // src/shared/state/tabs.ts
      .Set("id", tab.id)
      .Set("profileId", s.profile_id)
      .Set("spaceId", favorite ? Null() : base::Value(space_id))
      .Set("section", tab.section)
      .Set("parentId", OptionalString(tab.parent_id))
      .Set("url", url)
      .Set("pinnedUrl", tab.pinned_url ? base::Value(DisplayUrl(*tab.pinned_url))
                                       : Null())
      .Set("offPinned", tab.off_pinned)
      .Set("title", CapTitle(tab.title))
      .Set("titleOverride", OptionalString(tab.title_override))
      .Set("subtitle", OptionalString(tab.subtitle))
      .Set("favicon", OptionalString(tab.favicon))
      .Set("iconColor", OptionalString(tab.icon_color))
      .Set("unreadBadge", tab.unread_badge)
      .Set("customIcon", OptionalString(tab.custom_icon))
      .Set("muted", tab.muted)
      .Set("lastActiveAt", tab.last_active_at)
      .Set("splitId", OptionalString(tab.split_id))
      .Set("internal", internal)
      // src/shared/state/split.ts
      .Set("mediaPlaying", tab.media_playing)
      .Set("pip", OptionalString(tab.pip));
  // src/shared/state/extensions.ts and tweaks.ts
  value.Merge(SiteTabValue(tab.site));
  return value;
}

base::ListValue RefsValue(const std::vector<NodeRefSnapshot>& refs) {
  base::ListValue list;
  for (const NodeRefSnapshot& ref : refs) {
    list.Append(base::DictValue()
                    .Set("kind", ref.folder ? "folder" : "tab")
                    .Set("id", ref.id));
  }
  return list;
}

base::DictValue SpaceValue(const WindowSnapshot& s, const SpaceSnapshot& space) {
  base::DictValue icon = base::DictValue().Set("kind", space.icon_kind);
  if (space.icon_kind == "emoji" || space.icon_kind == "symbol") {
    icon.Set("value", space.icon_value);
  }
  return base::DictValue()
      .Set("id", space.id)
      .Set("name", space.name)
      .Set("icon", std::move(icon))
      .Set("color", space.color)
      .Set("profileId", space.profile_id.empty() ? s.profile_id
                                                 : space.profile_id)
      .Set("ephemeral", false)
      .Set("theme", ThemeValue(space.theme))
      // The theme's first color, else the named color (spaceAccent in src/shared/theme/space.ts).
      .Set("accent", SpaceAccentHex(space.theme, space.color))
      .Set("pinned", RefsValue(space.pinned))
      .Set("today", StringList(space.today))
      .Set("pinnedCollapsed", space.pinned_collapsed);
}

base::DictValue FolderValue(const FolderSnapshot& folder) {
  return base::DictValue()
      .Set("id", folder.id)
      .Set("spaceId", folder.space_id)
      .Set("name", folder.name)
      .Set("icon", OptionalString(folder.icon))
      .Set("collapsed", folder.collapsed)
      .Set("children", RefsValue(folder.children));
}

base::ListValue ToastsValue(const std::vector<ToastSnapshot>& toasts) {
  base::ListValue list;
  for (const ToastSnapshot& toast : toasts) {
    base::DictValue value = base::DictValue()
                                .Set("id", toast.id)
                                .Set("text", toast.text)
                                .Set("until", toast.until);
    if (toast.action_label && toast.action_command) {
      value.Set("action", base::DictValue()
                              .Set("label", *toast.action_label)
                              .Set("commandId", *toast.action_command));
    }
    list.Append(std::move(value));
  }
  return list;
}

base::Value AnimValue(const std::optional<SidebarAnim>& anim) {
  if (!anim) {
    return Null();
  }
  return base::Value(
      base::DictValue()
          .Set("from", anim->from)
          .Set("to", anim->to)
          .Set("startAt", static_cast<double>(anim->start_at_ms))
          .Set("ms", anim->ms)
          .Set("fromRect", RectValue(anim->from_rect))
          .Set("toRect", RectValue(anim->to_rect))
          .Set("curve", anim->exponential ? "exp" : "cubic"));
}

base::DictValue ShellValue(const WindowSnapshot& s) {
  const ShellLayout& l = s.layout;
  return base::DictValue()
      .Set("sidebar", base::DictValue()
                          .Set("width", s.chrome.sidebar_width)
                          .Set("effectiveWidth", l.effective_width)
                          .Set("hidden", s.chrome.sidebar_hidden)
                          .Set("peeking", s.peeking)
                          .Set("anim", AnimValue(s.anim)))
      .Set("focusMode", s.chrome.focus_mode)
      .Set("toolbar", s.chrome.toolbar)
      .Set("titlebarReveal", s.titlebar_reveal)
      .Set("layout",
           base::DictValue()
               .Set("contentSize", base::DictValue()
                                       .Set("width", l.content_size.width())
                                       .Set("height", l.content_size.height()))
               .Set("pageRect", RectValue(l.page_rect))
               .Set("sidebarRect", RectValue(l.sidebar_rect))
               .Set("hotZone", l.hot_zone ? base::Value(RectValue(*l.hot_zone))
                                          : Null())
               .Set("radius", l.radius)
               .Set("effectiveWidth", l.effective_width)
               .Set("trafficSpacer", l.traffic_spacer)
               .Set("trafficLights", l.traffic_lights));
}

base::Value CommandValue(const std::optional<CommandSessionSnapshot>& c) {
  if (!c) {
    return Null();
  }
  base::DictValue command =
      base::DictValue()
          .Set("sessionId", c->session_id)
          .Set("mode", c->mode)
          .Set("targetTabId", OptionalString(c->target_tab_id))
          .Set("initialText", c->initial_text);
  if (!c->engine_name.empty()) {
    command.Set("engineName", c->engine_name);
  }
  return base::Value(std::move(command));
}

base::ListValue SurfacesValue(const std::vector<OverlaySurfaceSnapshot>& all) {
  base::ListValue list;
  for (const OverlaySurfaceSnapshot& surface : all) {
    list.Append(base::DictValue()
                    .Set("instanceId", surface.instance_id)
                    .Set("surface", surface.surface)
                    .Set("layer", surface.layer)
                    .Set("modality", surface.modality)
                    .Set("rect", surface.rect
                                     ? base::Value(RectValue(*surface.rect))
                                     : Null())
                    .Set("openedFrom", surface.opened_from
                                           ? base::Value(*surface.opened_from)
                                           : Null())
                    .Set("focus", surface.focus)
                    .Set("slot", surface.slot ? base::Value(*surface.slot)
                                              : Null())
                    .Set("props", surface.props.Clone()));
  }
  return list;
}

base::Value SwipeValue(const std::optional<SwipeSnapshot>& swipe) {
  if (!swipe) {
    return Null();
  }
  return base::Value(
      base::DictValue()
          .Set("fraction", swipe->fraction)
          .Set("targetSpaceId", OptionalString(swipe->target_space_id)));
}

base::DictValue ProfileValue(const std::string& id,
                             const std::string& name,
                             bool is_default,
                             bool ephemeral) {
  return base::DictValue()
      .Set("id", id)
      .Set("name", name)
      .Set("isDefault", is_default)
      .Set("ephemeral", ephemeral)
      .Set("extensions", base::ListValue());
}

base::Value SplitViewValue(const std::optional<SplitViewSnapshot>& view) {
  if (!view) {
    return Null();
  }
  base::ListValue panes;
  for (const auto& [tab_id, rect] : view->panes) {
    // Chromium draws each pane's own bar: the bar is empty, at the pane's top
    // edge. Panes of 3 or 4 get the frame's bar right above the page.
    panes.Append(base::DictValue()
                     .Set("tabId", tab_id)
                     .Set("rect", RectValue(rect))
                     .Set("bar", RectValue(gfx::Rect(
                                     rect.x(), rect.y() - view->bar_height,
                                     rect.width(), view->bar_height))));
  }
  // A split of 3 or 4: a divider in each gap, from the top of the bars to the
  // bottom of the pages (SplitDividerView in src/shared/state/split.ts).
  base::ListValue dividers;
  const bool row = view->dir == "row";
  if (view->bar_height > 0 && view->sizes.size() == view->panes.size()) {
    for (size_t i = 0; i + 1 < view->panes.size(); ++i) {
      const gfx::Rect& a = view->panes[i].second;
      const gfx::Rect& b = view->panes[i + 1].second;
      const gfx::Rect gap =
          row ? gfx::Rect(a.right(), a.y() - view->bar_height,
                          std::max(0, b.x() - a.right()),
                          a.height() + view->bar_height)
              : gfx::Rect(a.x(), a.bottom(), a.width(),
                          std::max(0, b.y() - view->bar_height - a.bottom()));
      base::ListValue sizes;
      for (double size : view->sizes) {
        sizes.Append(size);
      }
      dividers.Append(base::DictValue()
                          .Set("path", base::ListValue())
                          .Set("index", static_cast<int>(i))
                          .Set("dir", view->dir)
                          .Set("rect", RectValue(gap))
                          .Set("sizes", std::move(sizes))
                          .Set("avail", view->avail));
    }
  }
  return base::Value(base::DictValue()
                         .Set("splitId", view->split_id)
                         .Set("panes", std::move(panes))
                         .Set("dividers", std::move(dividers)));
}

base::Value SplitDropValue(const std::optional<SplitDropSnapshot>& drop) {
  if (!drop) {
    return Null();
  }
  return base::Value(
      base::DictValue()
          .Set("tabId", drop->tab_id)
          .Set("targetTabId", OptionalString(drop->target_tab_id))
          .Set("edge", OptionalString(drop->edge))
          .Set("zone",
               drop->zone ? base::Value(RectValue(*drop->zone)) : Null()));
}

base::Value PeekValue(const std::optional<PeekSnapshot>& peek) {
  if (!peek) {
    return Null();
  }
  return base::Value(base::DictValue()
                         .Set("seq", peek->seq)
                         .Set("url", peek->url)
                         .Set("host", peek->host)
                         .Set("title", CapTitle(peek->title))
                         .Set("favicon", OptionalString(peek->favicon))
                         .Set("summary", OptionalString(peek->summary))
                         .Set("canGoBack", peek->can_go_back)
                         .Set("originRect", RectValue(peek->origin_rect))
                         .Set("cardRect", RectValue(peek->card_rect)));
}

// src/shared/state/extensions.ts ExtensionsTabFields of the Mini window's page.
base::Value MiniPageValue(const std::optional<MiniSnapshot>& mini) {
  if (!mini) {
    return base::Value();
  }
  return base::Value(base::DictValue()
                         .Set("site", OptionalString(mini->site.site))
                         .Set("blockedCount", mini->site.blocked_count)
                         .Set("blockingEnabled", mini->site.blocking_enabled)
                         .Set("devMode", mini->site.dev_mode));
}

base::Value MiniValue(const std::optional<MiniSnapshot>& mini) {
  if (!mini) {
    return Null();
  }
  return base::Value(base::DictValue()
                         .Set("tabId", mini->tab_id)
                         .Set("targetSpaceId", mini->target_space_id)
                         .Set("targetSpaceName", mini->target_space_name)
                         .Set("profileMismatch", mini->profile_mismatch)
                         .Set("confirming", mini->confirming)
                         .Set("url", mini->url)
                         .Set("title", CapTitle(mini->title))
                         .Set("favicon", OptionalString(mini->favicon))
                         .Set("loading", mini->loading)
                         .Set("progress", mini->progress)
                         .Set("canGoBack", mini->can_go_back));
}

base::DictValue SplitsValue(const std::vector<SplitRow>& splits) {
  base::DictValue out;
  for (const SplitRow& split : splits) {
    out.Set(split.id, base::DictValue()
                          .Set("id", split.id)
                          .Set("spaceId", split.space_id)
                          .Set("root", SplitRootValue(split))
                          .Set("focusedTabId", split.focused_tab_id));
  }
  return out;
}

base::DictValue WindowValue(const WindowSnapshot& s) {
  return base::DictValue()
      // src/shared/state/core.ts
      .Set("id", s.window_id)
      .Set("kind", s.kind)
      .Set("private", s.off_the_record)
      .Set("focused", s.focused)
      .Set("fullscreen", s.fullscreen)
      .Set("overlay",
           base::DictValue()
               .Set("surfaces", SurfacesValue(s.surfaces))
               .Set("topRect", s.top_overlay_rect
                                   ? base::Value(RectValue(*s.top_overlay_rect))
                                   : base::Value()))
      .Set("toasts", ToastsValue(s.toasts))
      // spaces.ts
      .Set("activeSpaceId", s.space_id)
      .Set("swipe", SwipeValue(s.swipe))
      .Set("wheel", base::DictValue()
                        .Set("phased", s.wheel.phased)
                        .Set("lifts", s.wheel.lifts))
      // tabs.ts
      .Set("activeTabId", OptionalString(s.active_tab_id))
      .Set("sidebarRequest", SidebarRequestValue(s.sidebar_request))
      // shell.ts
      .Set("shell", ShellValue(s))
      // capabilities.ts
      .Set("prompts", base::ListValue())
      // command.ts
      .Set("command", CommandValue(s.command))
      // split.ts
      .Set("activeSplitId", OptionalString(s.active_split_id))
      .Set("focusedTabId", OptionalString(s.active_tab_id))
      .Set("splitDrop", SplitDropValue(s.split_drop))
      .Set("splitView", SplitViewValue(s.split_view))
      .Set("mediaTabId", OptionalString(s.media_tab_id))
      // multitask.ts
      .Set("peek", PeekValue(s.peek))
      .Set("mini", MiniValue(s.mini))
      // engine.ts
      .Set("peekPage", Null())
      // extensions.ts
      .Set("installPrompt", Null())
      .Set("actionRow", ActionRowValue(s.action_row))
      .Set("miniPage", MiniPageValue(s.mini));
}

base::ListValue PathValue(const std::vector<std::string>& path) {
  return StringList(path);
}

void DiffInto(const base::DictValue& before,
              const base::DictValue& after,
              std::vector<std::string>& path,
              base::ListValue& ops) {
  for (const auto [key, value] : after) {
    const base::Value* old = before.Find(key);
    if (old && *old == value) {
      continue;
    }
    path.push_back(key);
    if (old && old->is_dict() && value.is_dict()) {
      DiffInto(old->GetDict(), value.GetDict(), path, ops);
    } else {
      ops.Append(base::DictValue()
                     .Set("op", "set")
                     .Set("path", PathValue(path))
                     .Set("value", value.Clone()));
    }
    path.pop_back();
  }
  for (const auto [key, value] : before) {
    if (!after.contains(key)) {
      path.push_back(key);
      ops.Append(
          base::DictValue().Set("op", "del").Set("path", PathValue(path)));
      path.pop_back();
    }
  }
}

std::string Serialize(base::DictValue message) {
  std::optional<std::string> json = base::WriteJson(message);
  // State holds no binary values and stays far below the depth limit.
  CHECK(json);
  return std::move(*json);
}

}  // namespace

TabSnapshot::TabSnapshot() = default;
TabSnapshot::TabSnapshot(const TabSnapshot&) = default;
TabSnapshot& TabSnapshot::operator=(const TabSnapshot&) = default;
TabSnapshot::~TabSnapshot() = default;

GlobalSettingsSnapshot::GlobalSettingsSnapshot() = default;
GlobalSettingsSnapshot::GlobalSettingsSnapshot(base::DictValue values)
    : values(std::move(values)) {}
GlobalSettingsSnapshot::GlobalSettingsSnapshot(
    const GlobalSettingsSnapshot& other)
    : values(other.values.Clone()) {}
GlobalSettingsSnapshot& GlobalSettingsSnapshot::operator=(
    const GlobalSettingsSnapshot& other) {
  if (this != &other) {
    values = other.values.Clone();
  }
  return *this;
}
GlobalSettingsSnapshot::~GlobalSettingsSnapshot() = default;

ProfileSnapshot::ProfileSnapshot() = default;
ProfileSnapshot::ProfileSnapshot(const ProfileSnapshot&) = default;
ProfileSnapshot& ProfileSnapshot::operator=(const ProfileSnapshot&) = default;
ProfileSnapshot::~ProfileSnapshot() = default;

CommandSessionSnapshot::CommandSessionSnapshot() = default;
CommandSessionSnapshot::CommandSessionSnapshot(const CommandSessionSnapshot&) =
    default;
CommandSessionSnapshot& CommandSessionSnapshot::operator=(
    const CommandSessionSnapshot&) = default;
CommandSessionSnapshot::~CommandSessionSnapshot() = default;

OverlaySurfaceSnapshot::OverlaySurfaceSnapshot() = default;
OverlaySurfaceSnapshot::OverlaySurfaceSnapshot(
    const OverlaySurfaceSnapshot& other)
    : instance_id(other.instance_id),
      surface(other.surface),
      layer(other.layer),
      modality(other.modality),
      focus(other.focus),
      slot(other.slot),
      rect(other.rect),
      opened_from(other.opened_from),
      props(other.props.Clone()) {}
OverlaySurfaceSnapshot& OverlaySurfaceSnapshot::operator=(
    const OverlaySurfaceSnapshot& other) {
  if (this != &other) {
    instance_id = other.instance_id;
    surface = other.surface;
    layer = other.layer;
    modality = other.modality;
    focus = other.focus;
    slot = other.slot;
    rect = other.rect;
    opened_from = other.opened_from;
    props = other.props.Clone();
  }
  return *this;
}
OverlaySurfaceSnapshot::~OverlaySurfaceSnapshot() = default;

SpaceSnapshot::SpaceSnapshot() = default;
SpaceSnapshot::SpaceSnapshot(const SpaceSnapshot&) = default;
SpaceSnapshot& SpaceSnapshot::operator=(const SpaceSnapshot&) = default;
SpaceSnapshot::~SpaceSnapshot() = default;

FolderSnapshot::FolderSnapshot() = default;
FolderSnapshot::FolderSnapshot(const FolderSnapshot&) = default;
FolderSnapshot& FolderSnapshot::operator=(const FolderSnapshot&) = default;
FolderSnapshot::~FolderSnapshot() = default;

ToastSnapshot::ToastSnapshot() = default;
ToastSnapshot::ToastSnapshot(const ToastSnapshot&) = default;
ToastSnapshot& ToastSnapshot::operator=(const ToastSnapshot&) = default;
ToastSnapshot::~ToastSnapshot() = default;

WindowSnapshot::WindowSnapshot() = default;
WindowSnapshot::WindowSnapshot(const WindowSnapshot&) = default;
WindowSnapshot& WindowSnapshot::operator=(const WindowSnapshot&) = default;
WindowSnapshot::~WindowSnapshot() = default;

SplitViewSnapshot::SplitViewSnapshot() = default;
SplitViewSnapshot::SplitViewSnapshot(const SplitViewSnapshot&) = default;
SplitViewSnapshot& SplitViewSnapshot::operator=(const SplitViewSnapshot&) =
    default;
SplitViewSnapshot::~SplitViewSnapshot() = default;

SplitDropSnapshot::SplitDropSnapshot() = default;
SplitDropSnapshot::SplitDropSnapshot(const SplitDropSnapshot&) = default;
SplitDropSnapshot& SplitDropSnapshot::operator=(const SplitDropSnapshot&) =
    default;
SplitDropSnapshot::~SplitDropSnapshot() = default;

PeekSnapshot::PeekSnapshot() = default;
PeekSnapshot::PeekSnapshot(const PeekSnapshot&) = default;
PeekSnapshot& PeekSnapshot::operator=(const PeekSnapshot&) = default;
PeekSnapshot::~PeekSnapshot() = default;

MiniSnapshot::MiniSnapshot() = default;
MiniSnapshot::MiniSnapshot(const MiniSnapshot&) = default;
MiniSnapshot& MiniSnapshot::operator=(const MiniSnapshot&) = default;
MiniSnapshot::~MiniSnapshot() = default;

base::DictValue BuildCluState(const WindowSnapshot& s) {
  base::DictValue tabs;
  std::vector<std::string> strip_order;
  for (const TabSnapshot& tab : s.tabs) {
    strip_order.push_back(tab.id);
    tabs.Set(tab.id, TabValue(s, tab));
  }

  base::DictValue profiles;
  for (const ProfileSnapshot& profile : s.profiles) {
    profiles.Set(profile.id, ProfileValue(profile.id, profile.name,
                                          profile.is_default, false));
  }
  if (!profiles.contains(s.profile_id)) {
    profiles.Set(s.profile_id,
                 ProfileValue(s.profile_id, s.profile_name,
                              !s.off_the_record && s.profiles.empty(),
                              s.off_the_record));
  }
  // Only the window's own profile is loaded here, so only it lists its extensions.
  if (base::DictValue* own = profiles.FindDict(s.profile_id)) {
    own->Set("extensions", ExtensionsValue(s.extensions));
  }

  base::DictValue spaces;
  std::vector<std::string> space_order;
  base::DictValue folders;
  base::DictValue settings_by_profile;
  if (s.has_model) {
    for (const SpaceSnapshot& space : s.spaces) {
      space_order.push_back(space.id);
      spaces.Set(space.id, SpaceValue(s, space));
    }
    for (const FolderSnapshot& folder : s.folders) {
      folders.Set(folder.id, FolderValue(folder));
    }
    settings_by_profile.Set(
        s.profile_id,
        base::DictValue()
            .Set("tabs.archiveAfterHours", s.archive_after_hours)
            .Set("search.engineId", s.search_engine_id));
  } else {
    // One unsaved space: "Private" in a private window (PLAN 3.15), never in the switcher order of other windows.
    const char* name = s.off_the_record ? kPrivateSpaceName : kHomeSpaceName;
    const char* color = s.off_the_record ? kPrivateSpaceColor : kHomeSpaceColor;
    space_order.push_back(s.space_id);
    spaces.Set(s.space_id,
               base::DictValue()
                   .Set("id", s.space_id)
                   .Set("name", name)
                   .Set("icon", base::DictValue().Set("kind", "letter"))
                   .Set("color", color)
                   .Set("profileId", s.profile_id)
                   .Set("ephemeral", s.off_the_record)
                   .Set("theme", Null())
                   .Set("accent", SpaceColorHex(color))
                   .Set("pinned", base::ListValue())
                   .Set("today", StringList(strip_order))
                   .Set("pinnedCollapsed", false));
  }

  base::DictValue archive_panel;
  if (s.archive_panel) {
    archive_panel.Set(s.window_id, true);
  }

  return base::DictValue()
      .Set("app", AppValue(s))
      .Set("profiles", std::move(profiles))
      .Set("spaces", std::move(spaces))
      .Set("tabs", std::move(tabs))
      .Set("windows", base::DictValue().Set(s.window_id, WindowValue(s)))
      .Set("splits", SplitsValue(s.splits))
      .Set("folders", std::move(folders))
      .Set("spaceOrder", StringList(space_order))
      .Set("favorites",
           base::DictValue().Set(s.profile_id, StringList(s.favorites)))
      .Set("downloads", DownloadRows(s.downloads, s.profile_id))
      .Set("screenshotArrivals", ScreenshotArrivalRows(s.screenshot_arrivals))
      .Set("archiveRevision", s.archive_revision)
      .Set("archivePanel", std::move(archive_panel))
      .Set("settings", base::DictValue()
                           .Set("global", s.global_settings.values.Clone())
                           .Set("byProfile", std::move(settings_by_profile)))
      .Set("historyRevision", 0);
}

base::ListValue DiffState(const base::DictValue& before,
                          const base::DictValue& after) {
  base::ListValue ops;
  std::vector<std::string> path;
  DiffInto(before, after, path, ops);
  return ops;
}

std::string SnapshotMessage(const base::DictValue& state, int rev) {
  base::DictValue full = state.Clone();
  full.Set("rev", rev);
  return Serialize(base::DictValue()
                       .Set("kind", "snapshot")
                       .Set("rev", rev)
                       .Set("state", std::move(full)));
}

std::string PatchMessage(base::ListValue ops, int rev) {
  return Serialize(base::DictValue()
                       .Set("kind", "patch")
                       .Set("rev", rev)
                       .Set("ops", std::move(ops)));
}

std::u16string CapTitle(std::u16string_view title) {
  if (title.size() <= kMaxStateTitleLength) {
    return std::u16string(title);
  }
  size_t length = kMaxStateTitleLength;
  if (CBU16_IS_LEAD(title[length - 1])) {
    --length;
  }
  return std::u16string(title.substr(0, length));
}

}  // namespace clu
