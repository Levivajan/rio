// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_state_builder.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

constexpr char kWindow[] = "00000000-0000-4000-8000-000100000001";
constexpr char kProfile[] = "00000000-0000-4000-8000-000200000001";
constexpr char kSpace[] = "00000000-0000-4000-8000-000300000001";
constexpr char kTabA[] = "00000000-0000-4000-8000-000500000001";
constexpr char kTabB[] = "00000000-0000-4000-8000-000500000002";
constexpr char kTabC[] = "00000000-0000-4000-8000-000500000003";
constexpr char kSecondSpace[] = "00000000-0000-4000-8000-000300000002";

TabSnapshot Tab(const char* id, const char* url) {
  TabSnapshot tab;
  tab.id = id;
  tab.url = url;
  tab.title = u"Title";
  return tab;
}

WindowSnapshot Window() {
  WindowSnapshot s;
  s.version = "158.0.8127.18";
  s.window_id = kWindow;
  s.profile_id = kProfile;
  s.profile_name = "Personal";
  s.space_id = kSpace;
  s.tabs = {Tab(kTabA, "https://a.test/"), Tab(kTabB, "https://b.test/")};
  s.active_tab_id = kTabB;
  s.layout = ComputeShellLayout(gfx::Size(1280, 800), s.chrome);
  return s;
}

TEST(CluStateBuilderTest, HasEveryTopLevelFieldOfCluState) {
  const base::DictValue state = BuildCluState(Window());
  for (const char* key :
       {"app", "profiles", "spaces", "tabs", "windows", "splits", "folders",
        "spaceOrder", "favorites", "downloads", "screenshotArrivals", "archiveRevision",
        "archivePanel", "settings", "historyRevision"}) {
    EXPECT_TRUE(state.contains(key)) << key;
  }
  // rev travels on the message, not inside the diffed state.
  EXPECT_FALSE(state.contains("rev"));
}

TEST(CluStateBuilderTest, PutsTheTabsInTodayInStripOrder) {
  const base::DictValue state = BuildCluState(Window());
  const base::ListValue* today =
      state.FindDict("spaces")->FindDict(kSpace)->FindList("today");
  ASSERT_TRUE(today);
  ASSERT_EQ(today->size(), 2u);
  EXPECT_EQ((*today)[0].GetString(), kTabA);
  EXPECT_EQ((*today)[1].GetString(), kTabB);
  const base::DictValue* window = state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(*window->FindString("activeTabId"), kTabB);
  EXPECT_EQ(*window->FindString("activeSpaceId"), kSpace);
  const base::DictValue* page_rect =
      window->FindDictByDottedPath("shell.layout.pageRect");
  ASSERT_TRUE(page_rect);
  EXPECT_EQ(page_rect->FindInt("x"), 214);
  // 1280 less the 214 sidebar and the card inset 10 on the right (M11).
  EXPECT_EQ(page_rect->FindInt("width"), 1056);
  EXPECT_TRUE(window->FindByDottedPath("shell.sidebar.anim")->is_none());
}

// Tabs ▸ New Folder… and Reveal Tab in Sidebar ask the sidebar through windows[id].sidebarRequest.
TEST(CluStateBuilderTest, TheSidebarRequestReachesState) {
  WindowSnapshot s = Window();
  const base::DictValue none = BuildCluState(s);
  EXPECT_TRUE(none.FindDict("windows")
                  ->FindDict(kWindow)
                  ->Find("sidebarRequest")
                  ->is_none());
  s.sidebar_request = SidebarRequestSnapshot{
      .seq = 2, .kind = "rename", .node_id = kTabA};
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* window = state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(window->FindIntByDottedPath("sidebarRequest.seq"), 2);
  EXPECT_EQ(*window->FindStringByDottedPath("sidebarRequest.kind"), "rename");
  EXPECT_EQ(*window->FindStringByDottedPath("sidebarRequest.nodeId"), kTabA);
  EXPECT_EQ(window->FindBoolByDottedPath("sidebarRequest.floating"), false);
  // An ask of the open floating sidebar says so.
  s.sidebar_request->floating = true;
  EXPECT_EQ(BuildCluState(s)
                .FindDict("windows")
                ->FindDict(kWindow)
                ->FindBoolByDottedPath("sidebarRequest.floating"),
            true);
}

TEST(CluStateBuilderTest, CapsJunkTitlesWithoutSplittingAPair) {
  std::u16string title(1023, u'a');
  title += u"\U0001F600";  // a pair at 1023..1024
  EXPECT_EQ(CapTitle(title).size(), 1023u);
  EXPECT_EQ(CapTitle(std::u16string(5000, u'x')).size(), kMaxStateTitleLength);
  EXPECT_EQ(CapTitle(u""), u"");
}

TEST(CluStateBuilderTest, DiffSetsChangedLeavesAndDeletesRemovedKeys) {
  WindowSnapshot before = Window();
  WindowSnapshot after = Window();
  after.tabs[0].title = u"Renamed";
  after.tabs.pop_back();
  after.active_tab_id = kTabA;
  const base::ListValue ops =
      DiffState(BuildCluState(before), BuildCluState(after));

  bool renamed = false;
  bool removed = false;
  for (const base::Value& op : ops) {
    const base::DictValue& dict = op.GetDict();
    const base::ListValue& path = *dict.FindList("path");
    if (*dict.FindString("op") == "set" && path.size() == 3 &&
        path[0].GetString() == "tabs" && path[1].GetString() == kTabA &&
        path[2].GetString() == "title") {
      renamed = *dict.FindString("value") == "Renamed";
    }
    if (*dict.FindString("op") == "del" && path.size() == 2 &&
        path[1].GetString() == kTabB) {
      removed = true;
    }
  }
  EXPECT_TRUE(renamed);
  EXPECT_TRUE(removed);
  EXPECT_TRUE(DiffState(BuildCluState(before), BuildCluState(before)).empty());
}

// The ops as sorted JSON strings: UpdateCluState may order them differently from DiffState, never differ in them.
std::vector<std::string> OpSet(const base::ListValue& ops) {
  std::vector<std::string> out;
  for (const base::Value& op : ops) {
    out.push_back(*base::WriteJson(op));
  }
  std::ranges::sort(out);
  return out;
}

// Publishes `steps` in turn through UpdateCluState and checks each against BuildCluState plus DiffState.
void ExpectUpdatesMatchFullBuilds(const std::vector<WindowSnapshot>& steps) {
  base::DictValue published;
  PublishedTabs last;
  std::optional<base::DictValue> full;
  for (size_t i = 0; i < steps.size(); ++i) {
    SCOPED_TRACE(i);
    const base::ListValue ops = UpdateCluState(published, last, steps[i]);
    base::DictValue next = BuildCluState(steps[i]);
    EXPECT_EQ(published, next);
    if (full) {
      EXPECT_EQ(OpSet(ops), OpSet(DiffState(*full, next)));
    }
    full = std::move(next);
  }
}

TEST(CluStateBuilderTest, UpdateGivesTheOpsAndStateOfAFullBuild) {
  WindowSnapshot start = Window();
  // A title change, a page loading, a closed tab, a new tab, and the window's own fields at once.
  WindowSnapshot changed = start;
  changed.tabs[0].title = u"Renamed";
  changed.tabs[0].loading = true;
  changed.tabs[0].site.blocked_count = 3;
  changed.tabs.pop_back();
  changed.tabs.push_back(Tab(kTabC, "https://c.test/"));
  changed.active_tab_id = kTabA;
  changed.focused = true;
  // Nothing changed: no ops.
  WindowSnapshot same = changed;
  // A tab's value reads the window's space: every tab without its own takes the new one.
  WindowSnapshot moved = changed;
  moved.space_id = kSecondSpace;
  // Every tab closed, then one back.
  WindowSnapshot empty = moved;
  empty.tabs.clear();
  WindowSnapshot back = empty;
  back.tabs.push_back(Tab(kTabA, "https://a.test/"));
  ExpectUpdatesMatchFullBuilds({start, changed, same, moved, empty, back});
}

TEST(CluStateBuilderTest, UpdateSendsNothingForUnchangedTabs) {
  base::DictValue published;
  PublishedTabs last;
  WindowSnapshot s = Window();
  UpdateCluState(published, last, s);
  EXPECT_TRUE(UpdateCluState(published, last, s).empty());

  s.tabs[1].favicon = "chrome://favicon2/?size=16&pageUrl=b";
  const base::ListValue ops = UpdateCluState(published, last, s);
  ASSERT_EQ(ops.size(), 1u);
  const base::ListValue& path = *ops[0].GetDict().FindList("path");
  ASSERT_EQ(path.size(), 3u);
  EXPECT_EQ(path[0].GetString(), "tabs");
  EXPECT_EQ(path[1].GetString(), kTabB);
  EXPECT_EQ(path[2].GetString(), "favicon");
}

TEST(CluStateBuilderTest, ADuplicateTabIdKeepsTheLastSnapshot) {
  WindowSnapshot s = Window();
  s.tabs.push_back(Tab(kTabA, "https://a2.test/"));
  WindowSnapshot later = s;
  later.tabs.back().title = u"Later";
  ExpectUpdatesMatchFullBuilds({s, later});
}

TEST(CluStateBuilderTest, MessagesAreStateMessages) {
  const base::DictValue state = BuildCluState(Window());
  std::optional<base::Value> snapshot =
      base::JSONReader::Read(SnapshotMessage(state, 7), base::JSON_PARSE_RFC);
  ASSERT_TRUE(snapshot && snapshot->is_dict());
  EXPECT_EQ(*snapshot->GetDict().FindString("kind"), "snapshot");
  EXPECT_EQ(snapshot->GetDict().FindInt("rev"), 7);
  EXPECT_EQ(snapshot->GetDict().FindDict("state")->FindInt("rev"), 7);

  std::optional<base::Value> patch = base::JSONReader::Read(
      PatchMessage(base::ListValue(), 8), base::JSON_PARSE_RFC);
  ASSERT_TRUE(patch && patch->is_dict());
  EXPECT_EQ(*patch->GetDict().FindString("kind"), "patch");
  EXPECT_EQ(patch->GetDict().FindInt("rev"), 8);
  EXPECT_TRUE(patch->GetDict().FindList("ops"));
}

TEST(CluStateBuilderTest, CommandSessionOpensTheBarSurface) {
  WindowSnapshot s = Window();
  CommandSessionSnapshot session;
  session.session_id = "00000000-0000-4000-8000-000600000001";
  session.mode = "new-tab";
  s.command = session;
  OverlaySurfaceSnapshot surface;
  surface.instance_id = "00000000-0000-4000-8000-000700000001";
  surface.surface = "command";
  surface.props.Set("sessionId", session.session_id);
  s.surfaces.push_back(surface);
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* window = state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(*window->FindStringByDottedPath("command.mode"), "new-tab");
  EXPECT_TRUE(window->FindByDottedPath("command.targetTabId")->is_none());
  // No engine name known: the bar falls back to the engine registry's name.
  EXPECT_FALSE(window->FindByDottedPath("command.engineName"));
  s.command->engine_name = "Fixture";
  EXPECT_EQ(*BuildCluState(s)
                 .FindDict("windows")
                 ->FindDict(kWindow)
                 ->FindStringByDottedPath("command.engineName"),
            "Fixture");
  const base::ListValue* surfaces =
      window->FindListByDottedPath("overlay.surfaces");
  ASSERT_EQ(surfaces->size(), 1u);
  EXPECT_EQ(*(*surfaces)[0].GetDict().FindString("surface"), "command");
  EXPECT_EQ(*(*surfaces)[0].GetDict().FindString("layer"), "top");
  EXPECT_EQ(*(*surfaces)[0].GetDict().FindStringByDottedPath("props.sessionId"),
            session.session_id);
  // The command bar's overlay covers the window.
  EXPECT_TRUE(window->FindByDottedPath("overlay.topRect")->is_none());
}

// docs/chromium/fixes.md: while the top overlay covers only part of the window (the tweak editor), the overlay WebUI
// reads that part as its origin.
TEST(CluStateBuilderTest, TheTopOverlaysRectReachesState) {
  WindowSnapshot s = Window();
  s.top_overlay_rect = gfx::Rect(890, 10, 380, 780);
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* window = state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(window->FindIntByDottedPath("overlay.topRect.x"), 890);
  EXPECT_EQ(window->FindIntByDottedPath("overlay.topRect.y"), 10);
  EXPECT_EQ(window->FindIntByDottedPath("overlay.topRect.width"), 380);
  EXPECT_EQ(window->FindIntByDottedPath("overlay.topRect.height"), 780);
}

// The hover peek: shell.sidebar.peeking, the peek's passive surface in slot 3, and its hover card's openedFrom.
TEST(CluStateBuilderTest, TheHoverPeekAndItsHoverCardReachState) {
  WindowSnapshot s = Window();
  s.chrome.sidebar_hidden = true;
  s.layout = ComputeShellLayout(gfx::Size(1280, 800), s.chrome);
  const base::DictValue before_state = BuildCluState(s);
  const base::DictValue* before =
      before_state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(before->FindBoolByDottedPath("shell.sidebar.peeking"), false);

  s.peeking = true;
  OverlaySurfaceSnapshot peek;
  peek.instance_id = "00000000-0000-4000-8000-000800000001";
  peek.surface = "sidebar-peek";
  peek.layer = "passive";
  peek.modality = "passive";
  peek.focus = false;
  peek.slot = 3;
  peek.rect = gfx::Rect(0, 0, 276, 800);
  OverlaySurfaceSnapshot card;
  card.instance_id = "00000000-0000-4000-8000-000800000002";
  card.surface = "tab-hover";
  card.layer = "passive";
  card.modality = "passive";
  card.focus = false;
  card.slot = 2;
  card.rect = gfx::Rect(284, 40, 280, 120);
  card.opened_from = peek.instance_id;
  s.surfaces = {peek, card};
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* window = state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(window->FindBoolByDottedPath("shell.sidebar.peeking"), true);
  EXPECT_EQ(window->FindBoolByDottedPath("shell.sidebar.hidden"), true);
  const base::ListValue* surfaces =
      window->FindListByDottedPath("overlay.surfaces");
  ASSERT_EQ(surfaces->size(), 2u);
  const base::DictValue& p = (*surfaces)[0].GetDict();
  EXPECT_EQ(*p.FindString("surface"), "sidebar-peek");
  EXPECT_EQ(p.FindInt("slot"), 3);
  EXPECT_EQ(p.FindIntByDottedPath("rect.width"), 276);
  EXPECT_TRUE(p.Find("openedFrom")->is_none());
  EXPECT_EQ(*(*surfaces)[1].GetDict().FindString("openedFrom"),
            peek.instance_id);
}

// M3: with a model the state carries the tree: favorites, pinned refs and folders, Today, unloaded rows, the archive
// revision and panel, the profile's archive setting and the window's toasts.
TEST(CluStateBuilderTest, TheModelTreeReachesState) {
  constexpr char kFolder[] = "00000000-0000-4000-8000-000800000001";
  constexpr char kFav[] = "00000000-0000-4000-8000-000500000003";
  WindowSnapshot s = Window();
  s.has_model = true;
  s.persistent = false;
  TabSnapshot pinned = Tab(kTabA, "https://a.test/x");
  pinned.section = "pinned";
  pinned.space_id = kSpace;
  pinned.parent_id = kFolder;
  pinned.pinned_url = "https://a.test/";
  pinned.off_pinned = true;
  pinned.live = false;
  TabSnapshot favorite = Tab(kFav, "https://f.test/");
  favorite.section = "favorite";
  favorite.pinned_url = "https://f.test/";
  s.tabs = {pinned, Tab(kTabB, "https://b.test/"), favorite};
  SpaceSnapshot space;
  space.id = kSpace;
  space.name = "Home";
  space.pinned = {{.folder = true, .id = kFolder}};
  space.today = {kTabB};
  space.pinned_collapsed = true;
  s.spaces = {space};
  FolderSnapshot folder;
  folder.id = kFolder;
  folder.space_id = kSpace;
  folder.name = "Reading";
  folder.children = {{.folder = false, .id = kTabA}};
  s.folders = {folder};
  s.favorites = {kFav};
  s.archive_revision = 4;
  s.archive_panel = true;
  s.archive_after_hours = 168;
  ToastSnapshot toast;
  toast.id = "00000000-0000-4000-8000-000900000001";
  toast.text = "Cleared 3 tabs";
  toast.action_label = "Undo";
  toast.action_command = "archive.undoClear";
  s.toasts = {toast};

  const base::DictValue state = BuildCluState(s);
  const base::DictValue* tab = state.FindDict("tabs")->FindDict(kTabA);
  EXPECT_EQ(*tab->FindString("lifecycle"), "unloaded");
  EXPECT_EQ(*tab->FindString("section"), "pinned");
  EXPECT_EQ(*tab->FindString("parentId"), kFolder);
  EXPECT_EQ(tab->FindBool("offPinned"), true);
  EXPECT_TRUE(state.FindDict("tabs")->FindDict(kFav)->Find("spaceId")->is_none());
  const base::DictValue* space_value = state.FindDict("spaces")->FindDict(kSpace);
  EXPECT_EQ(space_value->FindBool("pinnedCollapsed"), true);
  EXPECT_EQ(*(*space_value->FindList("pinned"))[0].GetDict().FindString("kind"),
            "folder");
  EXPECT_EQ(space_value->FindList("today")->size(), 1u);
  EXPECT_EQ(*state.FindDict("folders")->FindDict(kFolder)->FindString("name"),
            "Reading");
  EXPECT_EQ(state.FindDict("favorites")->FindList(kProfile)->size(), 1u);
  EXPECT_EQ(state.FindInt("archiveRevision"), 4);
  EXPECT_EQ(state.FindDict("archivePanel")->FindBool(kWindow), true);
  EXPECT_EQ(state.FindBoolByDottedPath("app.dataHealth.persistent"), false);
  EXPECT_EQ(state.FindDict("settings")
                ->FindDict("byProfile")
                ->FindDict(kProfile)
                ->FindInt("tabs.archiveAfterHours"),
            168);
  // The profile's engine, as the bar's engine registry names it (M5).
  EXPECT_EQ(*state.FindDict("settings")
                 ->FindDict("byProfile")
                 ->FindDict(kProfile)
                 ->FindString("search.engineId"),
            "google");
  const base::ListValue* toasts =
      state.FindDict("windows")->FindDict(kWindow)->FindList("toasts");
  ASSERT_EQ(toasts->size(), 1u);
  EXPECT_EQ(*(*toasts)[0].GetDict().FindStringByDottedPath("action.commandId"),
            "archive.undoClear");
}

TEST(CluStateBuilderTest, ShowsEverySpaceAndProfileButListsOnlyItsOwn) {
  constexpr char kOtherProfile[] = "00000000-0000-4000-8000-000200000002";
  constexpr char kOtherSpace[] = "00000000-0000-4000-8000-000300000002";
  WindowSnapshot s = Window();
  s.has_model = true;
  SpaceSnapshot own;
  own.id = kSpace;
  own.name = "Home";
  own.today = {kTabA, kTabB};
  SpaceSnapshot other;
  other.id = kOtherSpace;
  other.name = "Work";
  other.color = "teal";
  other.icon_kind = "emoji";
  other.icon_value = "\U0001F4BC";
  other.profile_id = kOtherProfile;
  s.spaces = {own, other};
  s.profiles = {ProfileSnapshot(), ProfileSnapshot()};
  s.profiles[0].id = kProfile;
  s.profiles[0].name = "Default";
  s.profiles[0].is_default = true;
  s.profiles[1].id = kOtherProfile;
  s.profiles[1].name = "Client";
  s.swipe = SwipeSnapshot{.fraction = 0.25, .target_space_id = kOtherSpace};
  s.wheel = WheelSnapshot{.phased = true, .lifts = 3};

  const base::DictValue state = BuildCluState(s);
  const base::ListValue* order = state.FindList("spaceOrder");
  ASSERT_EQ(order->size(), 2u);
  EXPECT_EQ((*order)[1].GetString(), kOtherSpace);
  const base::DictValue* work = state.FindDict("spaces")->FindDict(kOtherSpace);
  EXPECT_EQ(*work->FindString("profileId"), kOtherProfile);
  EXPECT_EQ(*work->FindString("accent"), "#14b8a6");
  EXPECT_EQ(*work->FindStringByDottedPath("icon.value"), "\U0001F4BC");
  EXPECT_TRUE(work->FindList("today")->empty());
  EXPECT_EQ(*state.FindDict("spaces")->FindDict(kSpace)->FindString("profileId"),
            kProfile);
  const base::DictValue* profiles = state.FindDict("profiles");
  EXPECT_EQ(profiles->size(), 2u);
  EXPECT_EQ(*profiles->FindDict(kOtherProfile)->FindString("name"), "Client");
  EXPECT_EQ(profiles->FindDict(kOtherProfile)->FindBool("isDefault"), false);
  EXPECT_EQ(profiles->FindDict(kProfile)->FindBool("isDefault"), true);
  const base::DictValue* swipe =
      state.FindDict("windows")->FindDict(kWindow)->FindDict("swipe");
  ASSERT_TRUE(swipe);
  EXPECT_EQ(swipe->FindDouble("fraction"), 0.25);
  EXPECT_EQ(*swipe->FindString("targetSpaceId"), kOtherSpace);
  const base::DictValue* wheel =
      state.FindDict("windows")->FindDict(kWindow)->FindDict("wheel");
  ASSERT_TRUE(wheel);
  EXPECT_EQ(wheel->FindBool("phased"), true);
  EXPECT_EQ(wheel->FindInt("lifts"), 3);
}

TEST(CluStateBuilderTest, APrivateWindowShowsOneUnsavedPrivateSpace) {
  WindowSnapshot s = Window();
  s.off_the_record = true;
  s.profile_name = "Private";
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* space = state.FindDict("spaces")->FindDict(kSpace);
  EXPECT_EQ(*space->FindString("name"), "Private");
  EXPECT_EQ(*space->FindString("color"), "slate");
  EXPECT_EQ(space->FindBool("ephemeral"), true);
  const base::DictValue* profile = state.FindDict("profiles")->FindDict(kProfile);
  EXPECT_EQ(profile->FindBool("ephemeral"), true);
  EXPECT_EQ(profile->FindBool("isDefault"), false);
  EXPECT_EQ(state.FindDict("windows")->FindDict(kWindow)->FindBool("private"),
            true);
  EXPECT_EQ(state.FindBoolByDottedPath("app.dataHealth.persistent"), false);
  EXPECT_TRUE(
      state.FindDict("windows")->FindDict(kWindow)->Find("swipe")->is_none());
}

// M12: app.update is the updater's status, the version it found and when it last checked.
TEST(CluStateBuilderTest, CarriesTheUpdateStatus) {
  WindowSnapshot s = Window();
  const base::DictValue before = BuildCluState(s);
  const base::DictValue* update = before.FindDict("app")->FindDict("update");
  ASSERT_TRUE(update);
  EXPECT_EQ(*update->FindString("status"), "unavailable");
  EXPECT_TRUE(update->Find("version")->is_none());
  EXPECT_TRUE(update->Find("checkedAt")->is_none());
  EXPECT_EQ(update->FindBool("dismissed"), false);

  s.update.status = UpdateStatus::kReady;
  s.update.version = "158.0.8127.19";
  s.update.checked_at = 1'800'000'000'000.0;
  s.update.dismissed = true;
  const base::DictValue state = BuildCluState(s);
  update = state.FindDict("app")->FindDict("update");
  EXPECT_EQ(*update->FindString("status"), "ready");
  EXPECT_EQ(*update->FindString("version"), "158.0.8127.19");
  EXPECT_EQ(update->FindDouble("checkedAt"), 1'800'000'000'000.0);
  // The user closed the update card for this version.
  EXPECT_EQ(update->FindBool("dismissed"), true);
}

// app.defaultBrowser carries CluDefaultBrowser's answer; it was always "unavailable" before the fix pass.
TEST(CluStateBuilderTest, CarriesTheDefaultBrowserState) {
  WindowSnapshot s = Window();
  EXPECT_EQ(*BuildCluState(s).FindStringByDottedPath("app.defaultBrowser"),
            "unavailable");
  for (const char* value : {"default", "not-default"}) {
    s.default_browser = value;
    EXPECT_EQ(*BuildCluState(s).FindStringByDottedPath("app.defaultBrowser"),
              value);
  }
}

TEST(CluStateBuilderTest, DownloadsCarryTheWindowsProfileAndSkipJunk) {
  WindowSnapshot s = Window();
  DownloadFacts file;
  file.guid = "00000000-0000-4000-8000-000600000001";
  file.file_name = "report.pdf";
  file.phase = DownloadPhase::kCompleted;
  DownloadFacts junk;
  junk.guid = "not-a-guid";
  s.downloads = {file, junk};
  const base::DictValue state = BuildCluState(s);
  const base::ListValue* downloads = state.FindList("downloads");
  ASSERT_TRUE(downloads);
  ASSERT_EQ(downloads->size(), 1u);
  const base::DictValue& row = (*downloads)[0].GetDict();
  EXPECT_EQ(*row.FindString("id"), file.guid);
  // downloadsForWindow() in src/shared/capabilities.ts matches rows to the window by this id.
  EXPECT_EQ(*row.FindString("profileId"), kProfile);
  EXPECT_EQ(*row.FindString("state"), "completed");
  // No downloads: an empty list, never a missing field.
  EXPECT_TRUE(BuildCluState(Window()).FindList("downloads")->empty());
}

// M8: appearance, onboarding, settings.global, space themes and accents, import progress, the rio:// alias.
TEST(CluStateBuilderTest, AppFieldsComeFromTheSettings) {
  WindowSnapshot s = Window();
  s.appearance_mode = "dark";
  s.dark = true;
  s.reduce_transparency = true;
  s.onboarding_show = true;
  s.import_progress = ImportProgressSnapshot{
      .source = "chrome", .phase = "history", .done = 3, .total = 9, .skipped = 1};
  s.global_settings = GlobalSettingsSnapshot(
      base::DictValue().Set("appearance.mode", "dark").Set("shell.lastSidebarWidth", 300));
  const base::DictValue state = BuildCluState(s);
  EXPECT_EQ(*state.FindStringByDottedPath("app.appearance.mode"), "dark");
  EXPECT_EQ(*state.FindStringByDottedPath("app.appearance.resolved"), "dark");
  EXPECT_EQ(state.FindBoolByDottedPath("app.appearance.reduceTransparency"), true);
  EXPECT_EQ(state.FindBoolByDottedPath("app.onboarding.show"), true);
  EXPECT_EQ(*state.FindStringByDottedPath("app.import.phase"), "history");
  EXPECT_EQ(state.FindIntByDottedPath("app.import.total"), 9);
  EXPECT_EQ(state.FindDict("settings")->FindDict("global")->FindInt("shell.lastSidebarWidth"), 300);
  // The copy keeps the dict.
  const WindowSnapshot copy = s;
  EXPECT_EQ(copy.global_settings.values, s.global_settings.values);
  // A private window never shows onboarding.
  s.off_the_record = true;
  EXPECT_EQ(BuildCluState(s).FindBoolByDottedPath("app.onboarding.show"), false);
  s.import_progress.reset();
  EXPECT_TRUE(BuildCluState(s).FindByDottedPath("app.import")->is_none());
}

TEST(CluStateBuilderTest, SpaceThemesSetTheThemeAndTheAccent) {
  WindowSnapshot s = Window();
  s.has_model = true;
  SpaceSnapshot space;
  space.id = kSpace;
  space.name = "Home";
  space.color = "red";
  s.spaces = {space};
  const base::DictValue plain = BuildCluState(s);
  const base::DictValue* home = plain.FindDict("spaces")->FindDict(kSpace);
  EXPECT_TRUE(home->Find("theme")->is_none());
  EXPECT_EQ(*home->FindString("accent"), "#ef4444");

  SpaceTheme theme;
  theme.colors = {{150, 0.45}, {20, 0.9}};
  theme.intensity = 0.7;
  theme.grain = 0.2;
  s.spaces[0].theme = theme;
  const base::DictValue themed = BuildCluState(s);
  home = themed.FindDict("spaces")->FindDict(kSpace);
  EXPECT_EQ(ParseSpaceTheme(*home->Find("theme")), theme);
  EXPECT_EQ(*home->FindString("accent"), RgbToHex(HslToRgb(150, 0.45, 0.68)));
}

TEST(CluStateBuilderTest, CluPagesShowAsCluUrls) {
  WindowSnapshot s = Window();
  s.tabs = {Tab(kTabA, "chrome://history/"), Tab(kTabB, "chrome://settings/autofill")};
  s.tabs[0].pinned_url = "chrome://history/";
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* a = state.FindDict("tabs")->FindDict(kTabA);
  EXPECT_EQ(*a->FindString("url"), "rio://history/");
  EXPECT_EQ(*a->FindString("pinnedUrl"), "rio://history/");
  EXPECT_EQ(a->FindBool("internal"), true);
  EXPECT_EQ(*state.FindDict("tabs")->FindDict(kTabB)->FindString("url"),
            "rio://settings/autofill");
}

// M6 (docs/chromium/m6.md): the split row and the split on screen, the drop
// preview, the media card tab, the Peek and the Mini window, in the shapes of
// src/shared/state/split.ts and multitask.ts.
TEST(CluStateBuilderTest, SplitPeekMediaAndMiniReachState) {
  WindowSnapshot s = Window();
  SplitRow split;
  split.id = "11111111-1111-4111-8111-111111111111";
  split.space_id = kSpace;
  split.tab_ids = {kTabA, kTabB};
  split.ratio = 0.25;
  split.focused_tab_id = kTabB;
  s.splits = {split};
  s.active_split_id = split.id;
  SplitViewSnapshot view;
  view.split_id = split.id;
  view.panes = {{kTabA, gfx::Rect(260, 8, 248, 784)},
                {kTabB, gfx::Rect(518, 8, 754, 784)}};
  s.split_view = view;
  s.tabs[0].media_playing = true;
  s.tabs[0].pip = "auto";
  s.tabs[0].icon_color = "#f9cb35";
  s.tabs[0].unread_badge = false;
  s.media_tab_id = kTabA;
  PeekSnapshot peek;
  peek.seq = 3;
  peek.url = "https://c.test/";
  peek.host = "c.test";
  peek.favicon = "chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=https%3A%2F%2Fc.test%2F";
  peek.card_rect = gfx::Rect(330, 56, 870, 704);
  s.peek = peek;

  const base::DictValue state = BuildCluState(s);
  const base::DictValue* stored = state.FindDict("splits")->FindDict(split.id);
  ASSERT_TRUE(stored);
  EXPECT_EQ(*stored->FindString("focusedTabId"), kTabB);
  EXPECT_EQ(*stored->FindStringByDottedPath("root.kind"), "group");
  EXPECT_EQ(*stored->FindStringByDottedPath("root.dir"), "row");
  EXPECT_NEAR((*stored->FindListByDottedPath("root.sizes"))[0].GetDouble(),
              0.25, 1e-9);

  const base::DictValue* window = state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(*window->FindString("kind"), "main");
  EXPECT_EQ(*window->FindString("activeSplitId"), split.id);
  const base::ListValue* panes =
      window->FindListByDottedPath("splitView.panes");
  ASSERT_TRUE(panes);
  ASSERT_EQ(panes->size(), 2u);
  // Chromium draws the pane chrome: empty bars, no dividers.
  EXPECT_EQ((*panes)[1].GetDict().FindIntByDottedPath("rect.x"), 518);
  EXPECT_EQ((*panes)[1].GetDict().FindIntByDottedPath("bar.height"), 0);
  EXPECT_TRUE(window->FindListByDottedPath("splitView.dividers")->empty());
  EXPECT_EQ(*window->FindString("mediaTabId"), kTabA);
  EXPECT_EQ(window->FindIntByDottedPath("peek.seq"), 3);
  EXPECT_EQ(*window->FindStringByDottedPath("peek.host"), "c.test");
  // The row that opened the Peek shows its icon.
  EXPECT_EQ(*window->FindStringByDottedPath("peek.favicon"), *peek.favicon);
  EXPECT_EQ(window->FindIntByDottedPath("peek.cardRect.width"), 870);
  EXPECT_TRUE(window->Find("mini")->is_none());
  const base::DictValue* tab = state.FindDict("tabs")->FindDict(kTabA);
  EXPECT_EQ(tab->FindBool("mediaPlaying"), true);
  EXPECT_EQ(*tab->FindString("pip"), "auto");
  EXPECT_TRUE(state.FindDict("tabs")->FindDict(kTabB)->Find("pip")->is_none());
  // The selected favorite's ring color; a tab without a live icon has none.
  EXPECT_EQ(*tab->FindString("iconColor"), "#f9cb35");
  // Show Unread Badge turned off on a favorite; on everywhere else.
  EXPECT_EQ(tab->FindBool("unreadBadge"), false);
  EXPECT_EQ(state.FindDict("tabs")->FindDict(kTabB)->FindBool("unreadBadge"),
            true);
  EXPECT_TRUE(
      state.FindDict("tabs")->FindDict(kTabB)->Find("iconColor")->is_none());
}

TEST(CluStateBuilderTest, AMiniWindowShowsItsPageAndTarget) {
  WindowSnapshot s = Window();
  s.kind = "mini";
  s.tabs.clear();
  s.active_tab_id.reset();
  MiniSnapshot mini;
  mini.tab_id = kTabA;
  mini.target_space_id = kSpace;
  mini.target_space_name = "Work";
  mini.profile_mismatch = true;
  mini.confirming = true;
  mini.url = "https://a.test/";
  mini.title = u"A";
  mini.loading = true;
  mini.progress = 0.5;
  s.mini = mini;
  const base::DictValue state = BuildCluState(s);
  const base::DictValue* window = state.FindDict("windows")->FindDict(kWindow);
  EXPECT_EQ(*window->FindString("kind"), "mini");
  EXPECT_EQ(*window->FindStringByDottedPath("mini.targetSpaceName"), "Work");
  EXPECT_EQ(window->FindBoolByDottedPath("mini.profileMismatch"), true);
  EXPECT_EQ(window->FindBoolByDottedPath("mini.confirming"), true);
  EXPECT_EQ(window->FindDoubleByDottedPath("mini.progress"), 0.5);
  EXPECT_TRUE(window->FindByDottedPath("mini.favicon")->is_none());
  EXPECT_TRUE(state.FindDict("tabs")->empty());
}

}  // namespace
}  // namespace clu
