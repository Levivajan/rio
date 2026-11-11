// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_STATE_BUILDER_H_
#define CLU_BROWSER_UI_WEBUI_CLU_STATE_BUILDER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/values.h"
#include "clu/browser/capabilities/clu_download_rows.h"
#include "clu/browser/theme/clu_theme_math.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/updater/clu_update_rules.h"
#include "clu/browser/ui/webui/clu_site_state.h"
#include "ui/gfx/geometry/rect.h"

// The CluState JSON (src/shared/state.ts) one window publishes, built from plain snapshots so it is testable without
// a browser, and the patch ops between two states. Every field the TS type has is present: lanes this milestone does
// not implement get their defaults from src/shared/state/<lane>.ts.
namespace clu {

// Caps on page text in state. A tab row the command bar sends back (CommandResultTab) must pass the same caps.
inline constexpr size_t kMaxStateTitleLength = 1024;

struct TabSnapshot {
  TabSnapshot();
  TabSnapshot(const TabSnapshot&);
  TabSnapshot& operator=(const TabSnapshot&);
  ~TabSnapshot();

  std::string id;
  std::string url;
  std::optional<std::string> pending_url;
  std::u16string title;
  std::optional<std::string> favicon;
  // The live page's icon color, #rrggbb (src/shared/state/tabs.ts iconColor).
  std::optional<std::string> icon_color;
  // A favorite's Show Unread Badge (tabs.ts unreadBadge).
  bool unread_badge = true;
  bool loading = false;
  double progress = 0;
  bool can_go_back = false;
  bool can_go_forward = false;
  bool audible = false;
  bool muted = false;
  bool discarded = false;
  bool secure = false;
  // The page's zoom, 1 at 100%.
  double zoom_factor = 1;
  // Milliseconds since the Unix epoch.
  double last_active_at = 0;

  // The tab row (src/shared/state/tabs.ts). Without a model (a private window) every tab is a Today row of the
  // window's space, as in M2.
  std::string section = "today";
  // Empty: the window's space. Ignored for favorites, whose spaceId is null.
  std::string space_id;
  std::optional<std::string> parent_id;
  std::optional<std::string> pinned_url;
  bool off_pinned = false;
  std::optional<std::string> title_override;
  // A loaded Google Calendar pinned row's next event, "Event at 4:30 PM" (tabs.ts subtitle).
  std::optional<std::string> subtitle;
  std::optional<std::string> custom_icon;
  std::optional<std::string> split_id;
  // False for a row without a page: lifecycle "unloaded".
  bool live = true;
  // Site, blocker and Site tweaks fields (M7).
  SiteTabSnapshot site;

  // src/shared/state/split.ts (M6): media the page plays now, and its
  // picture-in-picture ("auto", "manual").
  bool media_playing = false;
  std::optional<std::string> pip;
};

// One entry of a pinned list or a folder's children (NodeRef).
struct NodeRefSnapshot {
  bool folder = false;
  std::string id;
};

struct SpaceSnapshot {
  SpaceSnapshot();
  SpaceSnapshot(const SpaceSnapshot&);
  SpaceSnapshot& operator=(const SpaceSnapshot&);
  ~SpaceSnapshot();

  std::string id;
  std::string name;
  std::string icon_kind = "letter";
  std::string icon_value;
  std::string color = "blue";
  // Empty: the window's profile.
  std::string profile_id;
  // Lists of spaces on the window's own profile; a space of another profile shows in the switcher only (its rows
  // live in that profile's model, and switching to it swaps windows).
  std::vector<NodeRefSnapshot> pinned;
  std::vector<std::string> today;
  bool pinned_collapsed = false;
  // M8: the theme the space shows (a picker preview, else its committed theme), or none: its named color.
  std::optional<SpaceTheme> theme;
};

struct FolderSnapshot {
  FolderSnapshot();
  FolderSnapshot(const FolderSnapshot&);
  FolderSnapshot& operator=(const FolderSnapshot&);
  ~FolderSnapshot();

  std::string id;
  std::string space_id;
  std::string name;
  std::optional<std::string> icon;
  bool collapsed = false;
  std::vector<NodeRefSnapshot> children;
};

// profiles[id] (src/shared/state/spaces.ts).
struct ProfileSnapshot {
  ProfileSnapshot();
  ProfileSnapshot(const ProfileSnapshot&);
  ProfileSnapshot& operator=(const ProfileSnapshot&);
  ~ProfileSnapshot();

  std::string id;
  std::string name;
  bool is_default = false;
};

// windows[id].swipe (src/shared/state/spaces.ts).
struct SwipeSnapshot {
  double fraction = 0;
  std::optional<std::string> target_space_id;
};

// windows[id].wheel (src/shared/state/spaces.ts): the frame's trackpad phases.
struct WheelSnapshot {
  bool phased = false;
  int lifts = 0;
};

// windows[id].toasts (src/shared/state/core.ts).
struct ToastSnapshot {
  ToastSnapshot();
  ToastSnapshot(const ToastSnapshot&);
  ToastSnapshot& operator=(const ToastSnapshot&);
  ~ToastSnapshot();

  std::string id;
  std::string text;
  std::optional<std::string> action_label;
  std::optional<std::string> action_command;
  // Epoch ms.
  double until = 0;
};

// windows[id].sidebarRequest (src/shared/state/tabs.ts): a main menu command's ask of the sidebar. `kind` is "rename"
// (Tabs ▸ New Folder…) or "reveal" (Tabs ▸ Reveal Tab in Sidebar); `seq` grows with each ask. `floating`: the floating
// sidebar was open, so its list answers rather than the docked one's.
struct SidebarRequestSnapshot {
  int seq = 0;
  std::string kind;
  std::string node_id;
  bool floating = false;
};

// windows[id].command (src/shared/state/command.ts).
struct CommandSessionSnapshot {
  CommandSessionSnapshot();
  CommandSessionSnapshot(const CommandSessionSnapshot&);
  CommandSessionSnapshot& operator=(const CommandSessionSnapshot&);
  ~CommandSessionSnapshot();

  std::string session_id;
  std::string mode;
  std::optional<std::string> target_tab_id;
  std::string initial_text;
  // The profile's default search engine's name.
  std::string engine_name;
};

// One entry of windows[id].overlay.surfaces (src/shared/state/core.ts): the command bar and the tab prompts in the top
// layer, the toast stack, the switcher and the hover card in passive slots sized to their rect.
struct OverlaySurfaceSnapshot {
  OverlaySurfaceSnapshot();
  OverlaySurfaceSnapshot(const OverlaySurfaceSnapshot&);
  OverlaySurfaceSnapshot& operator=(const OverlaySurfaceSnapshot&);
  ~OverlaySurfaceSnapshot();

  std::string instance_id;
  std::string surface;
  // "top" or "passive".
  std::string layer = "top";
  std::string modality = "modal";
  bool focus = true;
  // Passive slots 0..3.
  std::optional<int> slot;
  // In window coordinates; null for a full-window top surface.
  std::optional<gfx::Rect> rect;
  // The surface this one was opened from (the hover peek's hover card): the overlay keeps the parent open for it.
  std::optional<std::string> opened_from;
  base::DictValue props;
};

// app.import (src/shared/state/data.ts) while an import runs.
struct ImportProgressSnapshot {
  std::string source;
  std::string phase;
  int done = 0;
  int total = 0;
  int skipped = 0;
};

// settings.global (M8): every global key of the settings registry, resolved. A dict that copies by Clone(), so the
// snapshot stays copyable.
struct GlobalSettingsSnapshot {
  GlobalSettingsSnapshot();
  explicit GlobalSettingsSnapshot(base::DictValue values);
  GlobalSettingsSnapshot(const GlobalSettingsSnapshot&);
  GlobalSettingsSnapshot& operator=(const GlobalSettingsSnapshot&);
  ~GlobalSettingsSnapshot();

  base::DictValue values;
};

// windows[id].splitView (src/shared/state/split.ts): the panes of the split the
// window shows, in window coordinates. Chromium draws the bar, divider and
// focus outline of each pane, so the bars are empty and there are no dividers.
struct SplitViewSnapshot {
  SplitViewSnapshot();
  SplitViewSnapshot(const SplitViewSnapshot&);
  SplitViewSnapshot& operator=(const SplitViewSnapshot&);
  ~SplitViewSnapshot();

  std::string split_id;
  std::vector<std::pair<std::string, gfx::Rect>> panes;
  // 0 while Chromium's split draws the pane chrome; the bar the frame draws
  // above each page for a split of 3 or 4.
  int bar_height = 0;
  // A split of 3 or 4: the root's direction ("row" or "col"), its shares and
  // the room they divide, for the dividers the frame draws in the gaps.
  std::string dir;
  std::vector<double> sizes;
  int avail = 0;
};

// windows[id].splitDrop: a sidebar tab dragged over the page area.
struct SplitDropSnapshot {
  SplitDropSnapshot();
  SplitDropSnapshot(const SplitDropSnapshot&);
  SplitDropSnapshot& operator=(const SplitDropSnapshot&);
  ~SplitDropSnapshot();

  std::string tab_id;
  std::optional<std::string> target_tab_id;
  // "left", "right", "top" or "bottom".
  std::optional<std::string> edge;
  std::optional<gfx::Rect> zone;
};

// windows[id].peek (src/shared/state/multitask.ts).
struct PeekSnapshot {
  PeekSnapshot();
  PeekSnapshot(const PeekSnapshot&);
  PeekSnapshot& operator=(const PeekSnapshot&);
  ~PeekSnapshot();

  int seq = 0;
  std::string url;
  std::string host;
  std::u16string title;
  // The peeked page's icon (a chrome://favicon2 URL), shown at the end of the row that opened the Peek.
  std::optional<std::string> favicon;
  bool can_go_back = false;
  gfx::Rect origin_rect;
  gfx::Rect card_rect;
  // A hover preview's summary from the on-device model.
  std::optional<std::string> summary;
};

// windows[id].mini (MiniWindowState in src/shared/state/multitask.ts).
struct MiniSnapshot {
  MiniSnapshot();
  MiniSnapshot(const MiniSnapshot&);
  MiniSnapshot& operator=(const MiniSnapshot&);
  ~MiniSnapshot();

  std::string tab_id;
  std::string target_space_id;
  std::string target_space_name;
  bool profile_mismatch = false;
  bool confirming = false;
  std::string url;
  std::u16string title;
  std::optional<std::string> favicon;
  bool loading = false;
  double progress = 0;
  bool can_go_back = false;
  // The page's site, blocker count and switch (windows[id].miniPage, M7).
  SiteTabSnapshot site;
};

struct WindowSnapshot {
  WindowSnapshot();
  WindowSnapshot(const WindowSnapshot&);
  WindowSnapshot& operator=(const WindowSnapshot&);
  ~WindowSnapshot();

  std::string version;
  std::string window_id;
  std::string profile_id;
  std::string profile_name;
  std::string space_id;
  bool off_the_record = false;
  bool focused = false;
  bool fullscreen = false;
  // app.appearance.resolved: the mode Clu shows (appearance.mode, or the system's for auto).
  bool dark = false;
  bool reduce_motion = false;
  // M8: app.appearance, app.onboarding, app.import and settings.global.
  std::string appearance_mode = "auto";
  bool reduce_transparency = false;
  bool onboarding_show = false;
  std::optional<ImportProgressSnapshot> import_progress;
  GlobalSettingsSnapshot global_settings;
  // In tab strip order.
  std::vector<TabSnapshot> tabs;
  std::optional<std::string> active_tab_id;
  std::optional<SidebarRequestSnapshot> sidebar_request;
  ChromeState chrome;
  ShellLayout layout;
  std::optional<SidebarAnim> anim;
  // The hidden sidebar shows as the hover peek (windows[id].shell.sidebar.peeking).
  bool peeking = false;
  // What the revealed full-screen titlebar covers at the window's top (windows[id].shell.titlebarReveal).
  int titlebar_reveal = 0;
  std::optional<CommandSessionSnapshot> command;
  std::vector<OverlaySurfaceSnapshot> surfaces;
  // windows[id].overlay.topRect: the part of the window the top overlay covers, none for all of it.
  std::optional<gfx::Rect> top_overlay_rect;
  std::vector<ToastSnapshot> toasts;

  std::optional<SwipeSnapshot> swipe;
  WheelSnapshot wheel;

  // The profile's Clu model (M3). Without one (a private window, or before the rows are read) state shows one space
  // with the tab strip as its Today list: "Private" in a private window.
  bool has_model = false;
  bool persistent = true;
  // app.dataHealth.recovered: "backup" or "fresh" after an unreadable clu.db (M8).
  std::optional<std::string> recovered;
  // Every space in switcher order, across profiles (M4).
  std::vector<SpaceSnapshot> spaces;
  // Every Clu profile. Empty: just the window's own.
  std::vector<ProfileSnapshot> profiles;
  std::vector<std::string> favorites;
  std::vector<FolderSnapshot> folders;
  int archive_revision = 0;
  bool archive_panel = false;
  int archive_after_hours = 12;
  // The profile's default search engine as settings.byProfile[id]['search.engineId'] reads it (SETTINGS in
  // src/shared/settings.ts: google, duckduckgo, bing, brave or ecosia; any other engine reads as google).
  std::string search_engine_id = "google";
  // app.update (M12): the browser's one updater, the same in every window.
  UpdateSnapshot update;
  // app.defaultBrowser: "default", "not-default" or "unavailable" (CluDefaultBrowser), the same in every window.
  std::string default_browser = "unavailable";
  // The window profile's downloads (M9); state carries them as DownloadRows() of the window's profile id.
  std::vector<DownloadFacts> downloads;
  // Screenshots that landed while Rio runs; state carries them as ScreenshotArrivalRows().
  std::vector<ScreenshotArrival> screenshot_arrivals;

  // The action row of the window's profile, null without an extension; the profile's extensions (M7).
  std::optional<ActionRowSnapshot> action_row;
  std::vector<ExtensionInfoSnapshot> extensions;
  // M6 (docs/chromium/m6.md): "main" or "mini".
  std::string kind = "main";
  // The profile's splits (state.splits), and the one this window shows.
  std::vector<SplitRow> splits;
  std::optional<std::string> active_split_id;
  std::optional<SplitViewSnapshot> split_view;
  std::optional<SplitDropSnapshot> split_drop;
  std::optional<std::string> media_tab_id;
  std::optional<PeekSnapshot> peek;
  std::optional<MiniSnapshot> mini;
};

// The CluState of one window, without its "rev" (the message carries it, so a rev bump is not a patch op).
base::DictValue BuildCluState(const WindowSnapshot& snapshot);

// Patch ops (PatchOp in src/shared/state.ts) that turn `before` into `after`: objects are compared key by key,
// anything else (arrays included) is replaced whole.
base::ListValue DiffState(const base::DictValue& before,
                          const base::DictValue& after);

// StateMessage JSON: {kind: 'snapshot', rev, state} with state.rev = rev, and {kind: 'patch', rev, ops}.
std::string SnapshotMessage(const base::DictValue& state, int rev);
std::string PatchMessage(base::ListValue ops, int rev);

// A title as state carries it: at most kMaxStateTitleLength UTF-16 code units, never a split surrogate pair.
std::u16string CapTitle(std::u16string_view title);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_STATE_BUILDER_H_
