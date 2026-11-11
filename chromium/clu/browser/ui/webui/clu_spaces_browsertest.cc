// Copyright 2026 The Rio Authors. All rights reserved.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/base_paths.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/spaces/clu_spaces_prefs.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_space_menu.h"
#if BUILDFLAG(IS_MAC)
#include "clu/browser/ui/webui/clu_menu_appearance.h"
#endif
#include "ui/views/controls/webview/webview.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/url_loader_interceptor.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/base_window.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluSpaces* checks of the M4 "Done when" row (docs/chromium/PLAN.md section 6): docs/PLAN.md 12.7 on Chromium,
// with real Chromium profiles, real windows, the real frame WebUI and the handlers the bridge calls. The boxes each
// test covers are in docs/chromium/acceptance-map.md.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

std::string Reason(const mojom::CallResultPtr& result) {
  return result && result->is_error()
             ? result->get_error()->reason.value_or(std::string())
             : std::string();
}

base::DictValue ValueOf(const mojom::CallResultPtr& result) {
  std::optional<base::Value> value =
      base::JSONReader::Read(result->get_value_json(), base::JSON_PARSE_RFC);
  return value && value->is_dict() ? std::move(value->GetDict())
                                   : base::DictValue();
}

mojom::DropTargetPtr TodayOf(const std::string& space, int index) {
  return mojom::DropTarget::NewToday(
      mojom::DropTargetToday::New("today", space, index));
}

constexpr char kStateLog[] = R"(
  new Promise(resolve => {
    const slider = document.querySelector('.clu-space-slider');
    const seen = [];
    const start = performance.now();
    new MutationObserver(() => {
      seen.push(slider.dataset.state);
      if (slider.dataset.state === 'idle' && seen.includes('settling')) {
        resolve({seen, ms: Math.round(performance.now() - start),
                 reduced: document.documentElement.dataset.reducedMotion});
      }
    }).observe(slider, {attributes: true, attributeFilter: ['data-state']});
    window.__cluSliderReady = true;
  }))";

class CluSpacesBrowserTest : public InProcessBrowserTest {
 protected:
  static constexpr char kFixedOrigin[] = "http://probe.clu.test/";

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // The Mac's own reduce-motion setting must not change what the tests see.
    gfx::Animation::SetPrefersReducedMotionForTesting(false);
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    ASSERT_TRUE(spaces());
    // The registry adopted the startup profile with its Home space.
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !spaces()->list().spaces().empty() &&
             spaces()->LoadedModel(ProfileId());
    }));
  }

  void TearDownOnMainThread() override {
    fixed_origin_.reset();
    CluWindowFade::SetTickObserverForTesting({});
    CluWindowFade::SetPaintGateForTesting({});
    gfx::Animation::SetPrefersReducedMotionForTesting(false);
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluSpacesService* spaces() { return CluSpacesService::Get(); }
  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluModelService& model() { return *window().model(); }
  std::string ProfileId() { return spaces()->ProfileIdOf(browser()->GetProfile()); }
  std::vector<std::string> Order() { return spaces()->list().Order(); }

  GURL Url(const std::string& page) {
    return embedded_test_server()->GetURL(base::StrCat({"/", page}));
  }

  // localStorage is per origin, port included, and the test server takes a new port on every launch: data that must
  // survive a restart is stored at one fixed origin, served from clu/test/data.
  GURL FixedOriginUrl(const std::string& page) {
    if (!fixed_origin_) {
      fixed_origin_ =
          content::URLLoaderInterceptor::ServeFilesFromDirectoryAtOrigin(
              "clu/test/data", GURL(kFixedOrigin));
    }
    return GURL(base::StrCat({kFixedOrigin, page}));
  }

  // A space on `profile_id` (the window's profile when empty), made through the handler: the window switches to it.
  std::string Create(CluWindowState& w,
                     const std::string& name,
                     std::optional<std::string> profile_id = std::nullopt) {
    CluSpacesHandler handler(w.GetWeakPtr());
    CallFuture result;
    handler.Create(mojom::SpacesCreateInput::New(name, nullptr, std::nullopt,
                                                 std::move(profile_id)),
                   result.GetCallback());
    EXPECT_TRUE(IsOk(result.Get()));
    return IsOk(result.Get()) ? *ValueOf(result.Get()).FindString("spaceId")
                              : std::string();
  }

  // A Today row on `page` in the space `w` shows, loaded.
  std::string Open(CluWindowState& w, const std::string& page) {
    return OpenUrl(w, Url(page));
  }

  std::string OpenUrl(CluWindowState& w, const GURL& url) {
    base::expected<std::string, std::string> id =
        w.CreateRow(url, std::nullopt, /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return std::string();
    }
    EXPECT_TRUE(content::WaitForLoadStop(Contents(w, id.value())));
    return id.value();
  }

  content::WebContents* Contents(CluWindowState& w, const std::string& row) {
    tabs::TabInterface* tab = w.FindTab(row);
    return tab ? tab->GetContents() : nullptr;
  }

  // A key chord from the keyboard into the focused page: down, char, up through the page's RenderWidgetHost, which
  // asks the browser first (PreHandleKeyboardEvent) as for a key from the OS. content::SimulateKeyPress() marks its
  // events skip_if_unhandled, which skips the browser's pre-handling, so it cannot test browser keys.
  void PressKey(content::WebContents* contents,
                ui::DomKey key,
                ui::DomCode code,
                ui::KeyboardCode key_code,
                int modifiers) {
    content::RenderWidgetHost* widget =
        contents->GetPrimaryMainFrame()->GetRenderWidgetHost();
    for (blink::WebInputEvent::Type type :
         {blink::WebInputEvent::Type::kRawKeyDown,
          blink::WebInputEvent::Type::kChar,
          blink::WebInputEvent::Type::kKeyUp}) {
      input::NativeWebKeyboardEvent event(type, modifiers,
                                          base::TimeTicks::Now());
      event.dom_key = key;
      event.dom_code = static_cast<int>(code);
      event.windows_key_code = key_code;
      if (type != blink::WebInputEvent::Type::kKeyUp && key.IsCharacter()) {
        event.text[0] = key.ToCharacter();
        event.unmodified_text[0] = key.ToCharacter();
      }
      widget->ForwardKeyboardEvent(event);
    }
  }

  // A two-finger horizontal trackpad drag of 12 wheel events, with the phases a Mac trackpad sends (Chromium's wheel
  // queue requires a phase on Mac).
  void Wheel(content::WebContents* contents, const gfx::Point& at) {
    for (int i = 0; i < 12; ++i) {
      content::SimulateMouseWheelEvent(
          contents, at, gfx::Vector2d(-60, 0),
          i == 0 ? blink::WebMouseWheelEvent::kPhaseBegan
                 : blink::WebMouseWheelEvent::kPhaseChanged);
    }
    content::SimulateMouseWheelEvent(contents, at, gfx::Vector2d(),
                                     blink::WebMouseWheelEvent::kPhaseEnded);
  }

  std::string Probe(CluWindowState& w, const std::string& row) {
    return content::EvalJs(Contents(w, row),
                           "document.getElementById('cookie').textContent + '/' + "
                           "document.getElementById('storage').textContent")
        .ExtractString();
  }

  // A new Clu profile (a real Chromium profile directory).
  std::string NewProfile(const std::string& name) {
    CluProfilesHandler handler(window().GetWeakPtr());
    CallFuture result;
    handler.Create(mojom::ProfilesCreateInput::New(name), result.GetCallback());
    EXPECT_TRUE(IsOk(result.Get()));
    return IsOk(result.Get()) ? *ValueOf(result.Get()).FindString("profileId")
                              : std::string();
  }

  // The window of the group that shows `space_id` once a switch settled, else null.
  CluWindowState* WindowShowing(const std::string& space_id) {
    for (CluWindowState* w : space_actions::AllWindows()) {
      if (w->space_id() == space_id && !w->hidden_by_switch() &&
          w->browser()->GetWindow()->IsVisible() && w->model()) {
        return w;
      }
    }
    return nullptr;
  }

  // Screenshots go to $CLU_SHOTS_DIR when it is set (docs/chromium/PLAN.md 5.1: shots/<label>/).
  void Shot(content::WebContents* contents, const std::string& name) {
    std::unique_ptr<base::Environment> env = base::Environment::Create();
    std::optional<std::string> dir = env->GetVar("CLU_SHOTS_DIR");
    if (!dir || !contents || !contents->GetRenderWidgetHostView()) {
      return;
    }
    base::test::TestFuture<const content::CopyFromSurfaceResult&> copy;
    contents->GetRenderWidgetHostView()->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::TimeDelta(), copy.GetCallback());
    if (!copy.Get().has_value()) {
      return;
    }
    std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(copy.Get()->bitmap,
                                          /*discard_transparency=*/false);
    if (!png) {
      return;
    }
    base::ScopedAllowBlockingForTesting allow;
    const base::FilePath path =
        base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(name + ".png");
    base::CreateDirectory(path.DirName());
    base::WriteFile(path, *png);
  }

  std::unique_ptr<content::URLLoaderInterceptor> fixed_origin_;
};

// 12.7 box 1: create, rename, recolor, re-icon, reorder, delete; caps; names --------------------------------------------

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesCreateEditReorderAndDeleteThroughTheHandlers) {
  const std::string home = window().space_id();
  CluSpacesHandler handler(window().GetWeakPtr());

  // Create with every field: the window switches to the new space.
  CallFuture created;
  handler.Create(
      mojom::SpacesCreateInput::New(
          "Work",
          mojom::SpaceIcon::NewEmoji(
              mojom::SpaceIconEmoji::New("emoji", "\U0001F4BC")),
          mojom::SpacesCreateInputColor::kTeal, std::nullopt),
      created.GetCallback());
  ASSERT_TRUE(IsOk(created.Get()));
  const std::string work = *ValueOf(created.Get()).FindString("spaceId");
  EXPECT_EQ(window().space_id(), work);
  const SpaceEntry* entry = spaces()->list().FindSpace(work);
  EXPECT_EQ(entry->icon_value, "\U0001F4BC");
  EXPECT_EQ(entry->color, "teal");
  EXPECT_EQ(entry->profile_id, ProfileId());

  // Create without fields: "Space 3", the letter icon, the default profile.
  const std::string third = Create(window(), "");
  EXPECT_EQ(spaces()->list().FindSpace(third)->name, "Space 3");

  // Rename, recolor, re-icon; a 100-character name keeps 60 graphemes; an empty name and a bad emoji are refused.
  CallFuture updated;
  handler.Update(mojom::SpacesUpdateInput::New(
                     third, std::string(100, 'n'),
                     mojom::SpaceIcon::NewLetter(mojom::SpaceIconLetter::New("letter")),
                     mojom::SpacesUpdateInputColor::kPink),
                 updated.GetCallback());
  ASSERT_TRUE(IsOk(updated.Get()));
  EXPECT_EQ(spaces()->list().FindSpace(third)->name, std::string(60, 'n'));
  EXPECT_EQ(spaces()->list().FindSpace(third)->color, "pink");
  CallFuture empty_name;
  handler.Update(mojom::SpacesUpdateInput::New(third, " \t", nullptr,
                                               std::nullopt),
                 empty_name.GetCallback());
  EXPECT_EQ(empty_name.Get()->get_error()->code, mojom::ErrorCode::kBadPayload);
  CallFuture bad_icon;
  handler.Update(
      mojom::SpacesUpdateInput::New(
          third, std::nullopt,
          mojom::SpaceIcon::NewEmoji(mojom::SpaceIconEmoji::New("emoji", "abc")),
          std::nullopt),
      bad_icon.GetCallback());
  EXPECT_EQ(bad_icon.Get()->get_error()->code, mojom::ErrorCode::kBadPayload);
  // The state carries every change.
  const base::DictValue state = window().CurrentState();
  EXPECT_EQ(*state.FindDict("spaces")->FindDict(work)->FindString("name"),
            "Work");
  EXPECT_EQ(state.FindList("spaceOrder")->size(), 3u);

  // Reorder: the third space to -5 clamps to the front, and Ctrl+1 opens it.
  CallFuture reordered;
  handler.Reorder(mojom::SpacesReorderInput::New(third, -5),
                  reordered.GetCallback());
  ASSERT_TRUE(IsOk(reordered.Get()));
  EXPECT_EQ(Order(), (std::vector<std::string>{third, home, work}));
  ASSERT_TRUE(window().RunCommand("spaces.goto3"));
  EXPECT_EQ(window().space_id(), work);
  ASSERT_TRUE(window().RunCommand("spaces.goto1"));
  EXPECT_EQ(window().space_id(), third);

  // Delete a space with 2 pinned and 3 Today rows: the window shows the previous space and the archive has all 5.
  ASSERT_TRUE(window().RunCommand("spaces.goto3"));
  for (const char* page : {"tabs/a.html", "tabs/b.html"}) {
    const std::string row = Open(window(), page);
    ASSERT_TRUE(window()
                    .MoveRows({row}, DropTarget::Pinned(work, "", 0), false)
                    .has_value());
  }
  for (const char* page : {"tabs/a.html", "tabs/b.html", "tabs/c.html"}) {
    Open(window(), page);
  }
  // The space header's menu: its items in order, New Folder answers the folder to rename, Rename
  // Space… the space.
  std::vector<std::string> labels;
  for (const MenuItem& item : window().space_menu().BuildSpaceMenu(home)) {
    labels.push_back(item.separator ? "-" : item.label);
  }
  const std::string home_name = spaces()->list().FindSpace(home)->name;
  EXPECT_EQ(labels, (std::vector<std::string>{
                        "Change Space Icon…", "Rename Space…",
                        "Edit Theme Color…", "Set Profile", "-", "New Folder",
                        "Live Folders", "-", "Share Space…",
                        base::StrCat({"Export “", home_name, "” into Folder"}),
                        "-", "Manage Spaces…", "-", "Delete Space"}));
  // Share Space… stays pickable in a space with no pinned tabs.
  ASSERT_TRUE(model().tree().Pinned(third).empty());
  EXPECT_TRUE(std::ranges::contains(
      EnabledLeaves(window().space_menu().BuildSpaceMenu(third)),
      std::string(CluSpaceMenu::kShare)));
  // Set Profile ▸ checks the space's profile instead of writing a mark into its name.
  for (const MenuItem& item : window().space_menu().BuildSpaceMenu(home)) {
    if (item.id == CluSpaceMenu::kProfile) {
      ASSERT_FALSE(item.submenu.empty());
      EXPECT_TRUE(item.submenu[0].checked);
      EXPECT_EQ(item.submenu[0].label.find("✓"), std::string::npos);
    }
  }
  // Export "<name>" into Folder ▸ Home: a folder named after the space at the end of Home's pinned rows, holding
  // copies of its 2 pinned tabs; the space keeps its own.
  const std::string work_name = spaces()->list().FindSpace(work)->name;
  window().space_menu().RunSpaceItem(
      work, base::StrCat({CluSpaceMenu::kExportPrefix, home}));
  ASSERT_FALSE(model().tree().Pinned(home).empty());
  const TabNode* exported = model().tree().Get(model().tree().Pinned(home).back());
  ASSERT_TRUE(exported && exported->is_folder());
  EXPECT_EQ(exported->title_override, work_name);
  EXPECT_EQ(model().tree().Children(exported->id).size(), 2u);
  EXPECT_EQ(model().tree().Pinned(work).size(), 2u);
  EXPECT_EQ(window().space_menu().RunSpaceItem(home, CluSpaceMenu::kRename),
            home);
  const std::optional<std::string> folder =
      window().space_menu().RunSpaceItem(home, CluSpaceMenu::kNewFolder);
  ASSERT_TRUE(folder);
  EXPECT_EQ(model().tree().Get(*folder)->title_override, "Untitled");
  const std::vector<MenuItem> menu = window().space_menu().BuildIconMenu(work);
  ASSERT_EQ(menu.size(), 6u);
  EXPECT_FALSE(menu[3].enabled);  // Move Right at the end
  window().space_menu().RunIconItem(work, CluSpaceMenu::kDelete);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window().CurrentState().FindDict("windows")->FindDict(
               window().window_id())->FindListByDottedPath(
               "overlay.surfaces")->size() > 0;
  }));
  const base::DictValue confirm =
      (*window().CurrentState().FindDict("windows")->FindDict(
            window().window_id())->FindListByDottedPath("overlay.surfaces"))[0]
          .GetDict()
          .Clone();
  EXPECT_EQ(*confirm.FindString("surface"), "confirm");
  EXPECT_EQ(*confirm.FindStringByDottedPath("props.body"),
            "Its 2 pinned tabs close and its 3 Today tabs move to Archive.");
  ASSERT_TRUE(window().RespondOverlay(*confirm.FindString("instanceId"),
                                      base::Value(true)));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !spaces()->list().FindSpace(work); }));
  EXPECT_EQ(window().space_id(), home);
  base::test::TestFuture<ArchivePage> archive;
  model().ListArchive(std::string(), std::string(), 100, archive.GetCallback());
  EXPECT_EQ(archive.Get().rows.size(), 5u);

  // The last space cannot be deleted: the menu item is off and the handler refuses.
  CallFuture gone_third;
  handler.Delete(mojom::SpacesDeleteInput::New(third), gone_third.GetCallback());
  ASSERT_TRUE(IsOk(gone_third.Get()));
  EXPECT_FALSE(window().space_menu().BuildIconMenu(home)[5].enabled);
  CallFuture last;
  handler.Delete(mojom::SpacesDeleteInput::New(home), last.GetCallback());
  EXPECT_EQ(Reason(last.Get()), "last-space");
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest, CluSpacesCapsAtFiftySpacesAndTwentyProfiles) {
  CluSpacesHandler handler(window().GetWeakPtr());
  while (spaces()->list().spaces().size() < kMaxSpaces) {
    ASSERT_TRUE(spaces()
                    ->CreateSpace(std::nullopt, std::nullopt, std::nullopt,
                                  std::nullopt)
                    .has_value());
  }
  CallFuture more;
  handler.Create(mojom::SpacesCreateInput::New("51st", nullptr, std::nullopt,
                                               std::nullopt),
                 more.GetCallback());
  EXPECT_EQ(Reason(more.Get()), "max-spaces");
  EXPECT_EQ(window().CurrentState().FindList("spaceOrder")->size(), kMaxSpaces);

  // Twenty registry profiles (the directories need not exist for the cap): the next create is refused before
  // Chromium makes anything.
  while (spaces()->list().profiles().size() < kMaxProfiles) {
    spaces()->AddProfileForTesting(
        base::StrCat({"Profile ",
                      base::NumberToString(spaces()->list().profiles().size())}));
  }
  CluProfilesHandler profiles(window().GetWeakPtr());
  CallFuture twenty_first;
  profiles.Create(mojom::ProfilesCreateInput::New("21st"),
                  twenty_first.GetCallback());
  EXPECT_EQ(Reason(twenty_first.Get()), "max-profiles");
  // The default profile and a profile a space uses stay.
  CallFuture default_profile;
  profiles.Delete(mojom::ProfilesDeleteInput::New(ProfileId()),
                  default_profile.GetCallback());
  EXPECT_EQ(Reason(default_profile.Get()), "default-profile");
}

// Corrupt rows in Local State are repaired at launch.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       PRE_CluSpacesJunkInLocalStateIsRepairedAtLaunch) {
  base::ListValue junk_spaces = spaces()->list().SpacesValue();
  junk_spaces.Append(42);
  junk_spaces.Append(base::DictValue()
                         .Set("id", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa9")
                         .Set("name", std::string(5000, 'x'))
                         .Set("icon_kind", "emoji")
                         .Set("icon_value", "<script>")
                         .Set("color", "chartreuse")
                         .Set("profile_id", "no-such-profile"));
  junk_spaces.Append(base::DictValue().Set("id", "not-a-uuid"));
  base::ListValue junk_profiles = spaces()->list().ProfilesValue();
  junk_profiles.Append(base::DictValue()
                           .Set("id", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb9")
                           .Set("dir", "../../escape")
                           .Set("name", "Evil"));
  PrefService* local_state = g_browser_process->local_state();
  local_state->SetList(kCluSpacesPref, std::move(junk_spaces));
  local_state->SetList(kCluProfilesPref, std::move(junk_profiles));
  local_state->SetString(kCluLastSpacePref, "gone");
  local_state->CommitPendingWrite();
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesJunkInLocalStateIsRepairedAtLaunch) {
  const CluSpaceList& list = spaces()->list();
  ASSERT_EQ(list.spaces().size(), 2u);
  ASSERT_EQ(list.profiles().size(), 1u);
  const SpaceEntry* repaired =
      list.FindSpace("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa9");
  ASSERT_TRUE(repaired);
  EXPECT_EQ(repaired->name, std::string(60, 'x'));
  EXPECT_EQ(repaired->icon_kind, "letter");
  EXPECT_EQ(repaired->color, "slate");
  EXPECT_EQ(repaired->profile_id, ProfileId());
  // "gone" was dropped; the window then showed a real space, which became the last one.
  EXPECT_TRUE(list.FindSpace(window().space_id()));
  EXPECT_EQ(list.last_space(), window().space_id());
}

// A symbol icon set through the handler survives a relaunch, in Local State and in clu.db, beside an emoji icon set
// the same way.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       PRE_CluSpacesSymbolIconSurvivesARelaunch) {
  CluSpacesHandler handler(window().GetWeakPtr());
  CallFuture planet;
  handler.Create(
      mojom::SpacesCreateInput::New(
          "Planet",
          mojom::SpaceIcon::NewSymbol(mojom::SpaceIconSymbol::New(
              "symbol", mojom::SpaceIconSymbolValue::kPlanet)),
          std::nullopt, std::nullopt),
      planet.GetCallback());
  ASSERT_TRUE(IsOk(planet.Get()));
  CallFuture work;
  handler.Create(
      mojom::SpacesCreateInput::New(
          "Work",
          mojom::SpaceIcon::NewEmoji(
              mojom::SpaceIconEmoji::New("emoji", "\U0001F4BC")),
          std::nullopt, std::nullopt),
      work.GetCallback());
  ASSERT_TRUE(IsOk(work.Get()));
  const std::string planet_id = *ValueOf(planet.Get()).FindString("spaceId");
  EXPECT_EQ(spaces()->list().FindSpace(planet_id)->icon_kind, "symbol");
  g_browser_process->local_state()->CommitPendingWrite();
  base::RunLoop loop;
  model().FlushForTesting(loop.QuitClosure());
  loop.Run();
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesSymbolIconSurvivesARelaunch) {
  const SpaceEntry* planet = nullptr;
  const SpaceEntry* work = nullptr;
  for (const SpaceEntry& space : spaces()->list().spaces()) {
    if (space.name == "Planet") {
      planet = &space;
    } else if (space.name == "Work") {
      work = &space;
    }
  }
  ASSERT_TRUE(planet);
  ASSERT_TRUE(work);
  EXPECT_EQ(planet->icon_kind, "symbol");
  EXPECT_EQ(planet->icon_value, "planet");
  EXPECT_EQ(work->icon_kind, "emoji");
  EXPECT_EQ(work->icon_value, "\U0001F4BC");
  // The profile's clu.db mirror read the same icons back.
  const SpaceRow* row = model().FindSpace(planet->id);
  ASSERT_TRUE(row);
  EXPECT_EQ(row->icon_kind, "symbol");
  EXPECT_EQ(row->icon_value, "planet");
  const SpaceRow* work_row = model().FindSpace(work->id);
  ASSERT_TRUE(work_row);
  EXPECT_EQ(work_row->icon_value, "\U0001F4BC");
}

// The Create a Space panel's Create Space sends the name, a symbol icon, the theme color and the profile in one
// spaces.create; the space keeps all four.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesCreatePanelPayloadKeepsTheSymbolIcon) {
  CluSpacesHandler handler(window().GetWeakPtr());
  CallFuture moon;
  handler.Create(
      mojom::SpacesCreateInput::New(
          "Orbit",
          mojom::SpaceIcon::NewSymbol(mojom::SpaceIconSymbol::New(
              "symbol", mojom::SpaceIconSymbolValue::kMoon)),
          mojom::SpacesCreateInputColor::kIndigo, ProfileId()),
      moon.GetCallback());
  ASSERT_TRUE(IsOk(moon.Get()));
  const base::DictValue result = ValueOf(moon.Get());
  const std::string* id = result.FindString("spaceId");
  ASSERT_TRUE(id);
  const SpaceEntry* space = spaces()->list().FindSpace(*id);
  ASSERT_TRUE(space);
  EXPECT_EQ(space->name, "Orbit");
  EXPECT_EQ(space->icon_kind, "symbol");
  EXPECT_EQ(space->icon_value, "moon");
  EXPECT_EQ(space->color, "indigo");
  EXPECT_EQ(space->profile_id, ProfileId());
}

// A row of another space activated directly (the command bar's tab rows) switches the window as a switch does: Local
// State records the space, and with the sidebar hidden the space toast names it.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesActivatingAnotherSpacesRowSwitchesLikeASwitch) {
  const std::string home = window().space_id();
  const std::string work = Create(window(), "Work");
  const std::string row = Open(window(), "spaces/keylog.html");
  ASSERT_TRUE(window().ShowSpace(home));
  ASSERT_EQ(spaces()->list().last_space(), home);
  shell().ToggleSidebar();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shell().chrome_state().sidebar_hidden; }));

  ASSERT_TRUE(window().ActivateRow(row));
  EXPECT_EQ(window().space_id(), work);
  EXPECT_EQ(spaces()->list().last_space(), work);
  const base::DictValue state = window().CurrentState();
  const base::ListValue* toasts = state.FindDict("windows")
                                      ->FindDict(window().window_id())
                                      ->FindList("toasts");
  ASSERT_TRUE(toasts);
  ASSERT_FALSE(toasts->empty());
  EXPECT_EQ(*toasts->back().GetDict().FindString("text"), "Work");
}

// 12.7 box 2: keys from a focused page, the slide, the swipe ------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesKeysSwitchFromAFocusedPageAndThePageNeverSeesThem) {
  const std::string one = window().space_id();
  const std::string first_row = Open(window(), "spaces/keylog.html");
  const std::string two = Create(window(), "Two");
  const std::string second_row = Open(window(), "spaces/keylog.html");
  const std::string three = Create(window(), "Three");
  ASSERT_TRUE(window().ShowSpace(one));
  ASSERT_EQ(window().ActiveRowId(), first_row);

  // The keys are high-priority accelerators of the window: Mac runs them before the page.
  views::FocusManager* focus = views::Widget::GetWidgetForNativeWindow(
                                   browser()->GetWindow()->GetNativeWindow())
                                   ->GetFocusManager();
  EXPECT_TRUE(focus->HasPriorityHandler(
      ui::Accelerator(ui::VKEY_1, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(focus->HasPriorityHandler(
      ui::Accelerator(ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)));

  // Ctrl+2 from the focused page switches; the page logged no keydown for it.
  content::WebContents* page = Contents(window(), first_row);
  page->Focus();
  PressKey(page, ui::DomKey::FromCharacter('2'), ui::DomCode::DIGIT2,
           ui::VKEY_2, blink::WebInputEvent::kControlKey);
  ASSERT_TRUE(base::test::RunUntil([&] { return window().space_id() == two; }));
  // The page swaps in the task after the frame hears of the switch.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == second_row; }));
  EXPECT_EQ(content::EvalJs(page, "window.__keys.join(',')"), "");

  // Ctrl+9 with three spaces does nothing.
  content::WebContents* page_two = Contents(window(), second_row);
  page_two->Focus();
  PressKey(page_two, ui::DomKey::FromCharacter('9'), ui::DomCode::DIGIT9,
           ui::VKEY_9, blink::WebInputEvent::kControlKey);
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(window().space_id(), two);

  // Cmd+Option+Right and Left step without wrapping.
  PressKey(page_two, ui::DomKey::ARROW_RIGHT, ui::DomCode::ARROW_RIGHT,
           ui::VKEY_RIGHT,
           blink::WebInputEvent::kAltKey | blink::WebInputEvent::kMetaKey);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().space_id() == three; }));
  // An empty space shows the empty state with no page.
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().page_shown(); }));
  ASSERT_TRUE(window().RunCommand("spaces.next"));
  EXPECT_EQ(window().space_id(), three);
  ASSERT_TRUE(window().RunCommand("spaces.goto1"));
  ASSERT_TRUE(window().RunCommand("spaces.prev"));
  EXPECT_EQ(window().space_id(), one);
  // Switching back restores the tab the space showed last.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == first_row; }));
  EXPECT_TRUE(shell().page_shown());
}

// A switch the user asks for publishes the new space to the frame WebUI before the page view swaps, so the
// strip starts sliding on the frame's compositor while the browser hides one page and shows the other. The page
// follows in the next task; a quicker second switch wins over the first one's page.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesTheFrameHearsOfASwitchBeforeThePageSwaps) {
  const std::string one = window().space_id();
  const std::string first_row = Open(window(), "tabs/a.html");
  const std::string two = Create(window(), "Two");
  const std::string second_row = Open(window(), "tabs/b.html");
  const std::string three = Create(window(), "Three");
  const std::string third_row = Open(window(), "tabs/a.html");
  ASSERT_TRUE(window().ShowSpace(one));
  ASSERT_EQ(window().ActiveRowId(), first_row);

  bool switched = false;
  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = two},
                              base::BindLambdaForTesting(
                                  [&](bool done) { switched = done; }));
  EXPECT_TRUE(switched);
  // Published already: the state the frame got names Two while the page of One still shows.
  const base::DictValue state = window().CurrentState();
  EXPECT_EQ(*state.FindDict("windows")
                 ->FindDict(window().window_id())
                 ->FindString("activeSpaceId"),
            two);
  EXPECT_EQ(window().ActiveRowId(), first_row);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == second_row; }));
  EXPECT_TRUE(shell().page_shown());

  // Two presses before the page task runs: only the last space's page shows, and it stays.
  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = three});
  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = one});
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == first_row; }));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(window().space_id(), one);
  EXPECT_EQ(window().ActiveRowId(), first_row);

  // Other callers (a row opened into another space) still swap at once.
  ASSERT_TRUE(window().ShowSpace(three));
  EXPECT_EQ(window().ActiveRowId(), third_row);
}

// The page half of a switch is split over two frames. The incoming page turns visible first, while the
// old one still shows; the view swaps (hiding the old page and laying the window out) a frame later. A switch that
// moves on before its swap leaves no page visible behind the window's own.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesASwitchShowsTheIncomingPageAFrameBeforeTheSwap) {
  const std::string one = window().space_id();
  const std::string first_row = Open(window(), "tabs/a.html");
  const std::string two = Create(window(), "Two");
  const std::string second_row = Open(window(), "tabs/b.html");
  ASSERT_TRUE(window().ShowSpace(one));
  ASSERT_EQ(window().ActiveRowId(), first_row);
  content::WebContents* first = window().FindTab(first_row)->GetContents();
  content::WebContents* second = window().FindTab(second_row)->GetContents();
  ASSERT_EQ(second->GetVisibility(), content::Visibility::HIDDEN);

  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = two});
  // The first half: the incoming page is visible, the old page is still the window's page.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return second->GetVisibility() == content::Visibility::VISIBLE; }));
  EXPECT_EQ(window().ActiveRowId(), first_row);
  EXPECT_EQ(window().pre_shown_page_for_testing(), second);
  // The second half swaps the view and hides the old page.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == second_row; }));
  EXPECT_EQ(window().pre_shown_page_for_testing(), nullptr);
  EXPECT_EQ(second->GetVisibility(), content::Visibility::VISIBLE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return first->GetVisibility() == content::Visibility::HIDDEN; }));

  // Back to One, then at once a synchronous switch to Two: One's page, shown ahead, goes hidden again.
  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = one});
  // Queued behind the first half (shown ahead), before the swap.
  bool pre_shown = false;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] {
        pre_shown = window().pre_shown_page_for_testing() == first &&
                    first->GetVisibility() == content::Visibility::VISIBLE;
        window().ShowSpace(two);
      }));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().space_id() == two; }));
  EXPECT_TRUE(pre_shown);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window().pre_shown_page_for_testing() == nullptr &&
           first->GetVisibility() == content::Visibility::HIDDEN;
  }));
  EXPECT_EQ(window().ActiveRowId(), second_row);
  EXPECT_EQ(second->GetVisibility(), content::Visibility::VISIBLE);
}

// The page shown ahead of a switch that the user's pick put on screen as the other pane of a split stays visible
// after the swap: the swap hides only a page the window no longer draws.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesASwitchKeepsAPreShownSplitPaneVisible) {
  const std::string one = window().space_id();
  Open(window(), "tabs/a.html");
  const std::string two = Create(window(), "Two");
  const std::string x_row = Open(window(), "tabs/a.html");
  const std::string y_row = Open(window(), "tabs/b.html");
  ASSERT_TRUE(window()
                  .multitask()
                  .CreateSplit({y_row, x_row}, SplitDir::kRow, x_row)
                  .has_value());
  ASSERT_EQ(window().ActiveRowId(), x_row);
  ASSERT_TRUE(window().ShowSpace(one));
  content::WebContents* x = window().FindTab(x_row)->GetContents();
  content::WebContents* y = window().FindTab(y_row)->GetContents();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return x->GetVisibility() == content::Visibility::HIDDEN; }));

  // Space Two's target is X. Between the two halves of the switch the user picks Y, which shows X beside it.
  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = two});
  bool picked = false;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] {
        picked = window().pre_shown_page_for_testing() == x &&
                 window().ActivateRow(y_row);
      }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picked && window().pre_shown_page_for_testing() == nullptr;
  }));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(window().ActiveRowId(), y_row);
  EXPECT_EQ(y->GetVisibility(), content::Visibility::VISIBLE);
  EXPECT_EQ(x->GetVisibility(), content::Visibility::VISIBLE);
}

// A favorite counts as in every space, so the page half of a switch must not keep it. Switching
// away from a favorite shows the new space's last tab, by key and by every other path alike.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesASwitchFromAFavoriteShowsTheNewSpacesTab) {
  const std::string one = window().space_id();
  const std::string favorite = Open(window(), "tabs/a.html");
  ASSERT_TRUE(window()
                  .MoveRows({favorite}, DropTarget::Favorite(0), false)
                  .has_value());
  const std::string two = Create(window(), "Two");
  const std::string second_row = Open(window(), "tabs/b.html");
  ASSERT_TRUE(window().ShowSpace(one));
  ASSERT_TRUE(window().ActivateRow(favorite));
  ASSERT_EQ(window().ActiveRowId(), favorite);
  ASSERT_TRUE(model().IsInSpace(favorite, two));

  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = two});
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().ActiveRowId() == second_row; }));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(window().space_id(), two);
  EXPECT_EQ(window().ActiveRowId(), second_row);

  // A row picked in the new space after the switch began still wins over the target.
  ASSERT_TRUE(window().ShowSpace(one));
  ASSERT_TRUE(window().ActivateRow(favorite));
  space_actions::SwitchWindow(&window(), SwitchTarget{.space_id = two});
  const std::string picked = Open(window(), "tabs/a.html");
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(window().ActiveRowId(), picked);
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesTheSlideRunsAndReducedMotionCrossFades) {
  const std::string one = window().space_id();
  const std::string two = Create(window(), "Two");
  ASSERT_TRUE(window().ShowSpace(one));
  content::WebContents* frame = shell().frame_contents();
  ASSERT_TRUE(content::WaitForLoadStop(frame));
  // Creating Two and showing One again each slid; the log starts once those slides are over.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame,
                           "document.querySelector('.clu-space-slider')"
                           "?.dataset.state ?? ''")
               .ExtractString() == "idle";
  }));

  // The frame slides from the activeSpaceId patch: idle, settling, idle in about 260 ms.
  ASSERT_TRUE(content::ExecJs(frame,
                              "window.__cluSlide = " + std::string(kStateLog),
                              content::EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
  ASSERT_TRUE(window().ShowSpace(two));
  base::DictValue slide =
      content::EvalJs(frame, "window.__cluSlide").ExtractDict().Clone();
  EXPECT_LT(*slide.FindInt("ms"), 600);
  EXPECT_EQ(*slide.FindString("reduced"), "false");
  Shot(frame, "spaces-slide");

  // Reduced motion: the 120 ms cross-fade.
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  window().MarkDirty();
  window().Flush();
  ASSERT_TRUE(content::ExecJs(frame,
                              "window.__cluSlide = " + std::string(kStateLog),
                              content::EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
  ASSERT_TRUE(window().ShowSpace(one));
  slide = content::EvalJs(frame, "window.__cluSlide").ExtractDict().Clone();
  EXPECT_EQ(*slide.FindString("reduced"), "true");
  EXPECT_LT(*slide.FindInt("ms"), 400);
}

// The first switch to a space the window never showed, by a click on its switcher button, holds no frame (it
// held 183 and 124 ms frames while the new space's pane laid out cold). The frame lays every other space's pane out
// once while idle (SidebarWarmup), so the click finds it warm.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesFirstSwitchByItsButtonHoldsNoFrame) {
  const std::string one = window().space_id();
  Open(window(), "tabs/a.html");
  base::expected<std::string, SpacesError> made = spaces()->CreateSpace(
      std::string("Two"), std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(made.has_value());
  const std::string two = made.value();
  EXPECT_EQ(window().space_id(), one);
  content::WebContents* frame = shell().frame_contents();
  ASSERT_TRUE(content::WaitForLoadStop(frame));
  ASSERT_EQ(true, content::EvalJs(frame, content::JsReplace(R"(
      new Promise(resolve => {
        const check = () => document.querySelector(`[data-space-id="${$1}"]`)
            ? resolve(true) : setTimeout(check, 20);
        check();
      }))", two)));
  // Past the warm-up's delay (WARM_DELAY_MS, 600 ms in Sidebar.tsx).
  const base::TimeTicks loaded = base::TimeTicks::Now();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return base::TimeTicks::Now() - loaded > base::Milliseconds(1200);
  }));
  ASSERT_TRUE(content::ExecJs(frame, R"(
      window.cluB68 = [];
      let last = performance.now();
      const tick = (now) => {
        window.cluB68.push(now - last);
        last = now;
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);)"));
  ASSERT_TRUE(content::ExecJs(
      frame, content::JsReplace(
                 "document.querySelector(`[data-space-id=\"${$1}\"]`).click()",
                 two)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window().space_id() == two; }));
  const double longest = content::EvalJs(frame, R"(
      new Promise(resolve => setTimeout(() => {
        // The first sample spans the click itself.
        resolve(Math.max(0, ...window.cluB68.slice(1)));
      }, 800)))")
                             .ExtractDouble();
  EXPECT_LT(longest, 100);
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesSwipeOverTheSidebarSwitchesAndOverThePageDoesNot) {
  const std::string one = window().space_id();
  const std::string row = Open(window(), "tabs/a.html");
  const std::string two = Create(window(), "Two");
  ASSERT_TRUE(window().ShowSpace(one));

  // The drag publishes windows[id].swipe; momentum never moves it; the end switches one space.
  window().Swipe(CluWindowState::SwipePhase::kBegin, 40, false);
  window().Swipe(CluWindowState::SwipePhase::kUpdate, 120, false);
  window().Swipe(CluWindowState::SwipePhase::kUpdate, 900, true);
  const base::DictValue dragging = window().CurrentState();
  const base::DictValue* swipe = dragging.FindDict("windows")
                                     ->FindDict(window().window_id())
                                     ->FindDict("swipe");
  ASSERT_TRUE(swipe);
  EXPECT_GT(*swipe->FindDouble("fraction"), 0);
  EXPECT_LT(*swipe->FindDouble("fraction"), 1);
  EXPECT_EQ(*swipe->FindString("targetSpaceId"), two);
  window().Swipe(CluWindowState::SwipePhase::kEnd, 120, false);
  EXPECT_EQ(window().space_id(), two);
  ASSERT_TRUE(window().ShowSpace(one));

  // Real wheel events over the page belong to the page.
  content::WebContents* page = Contents(window(), row);
  Wheel(page, gfx::Point(200, 200));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(window().space_id(), one);

  // Over the sidebar they swipe: the frame's reducer reports the drag, and its end switches.
  content::WebContents* frame = shell().frame_contents();
  const gfx::Rect sidebar = shell().layout().sidebar_rect;
  const gfx::Point over(sidebar.x() + sidebar.width() / 2,
                        sidebar.y() + sidebar.height() / 2);
  Wheel(frame, over);
  ASSERT_TRUE(base::test::RunUntil([&] { return window().space_id() == two; }));
}

// One swipe moves one page. A two-finger stream on the sidebar (fingers, then macOS momentum that
// decays and then jitters for over a second) switches exactly one space, and the swipe that closes the Library
// lands on the space it was opened from; its momentum never carries the space list on. Events are paced by the
// clock and yield through MessageChannel tasks, so timer throttling cannot split the stream.
constexpr char kSwipeStream[] = R"(
  (async (fingers) => {
    const tick = () => new Promise((r) => {
      const c = new MessageChannel();
      c.port1.onmessage = () => r();
      c.port2.postMessage(0);
    });
    const jitter = [8, 8, 9, 7, 7, 8, 6, 6, 7, 5, 5, 6, 4, 4, 5, 3, 3, 4, 2, 2, 3];
    const tail = [10, 9, 8.1];
    for (let i = 0; i < 75; i++) tail.push(jitter[i % jitter.length]);
    const sign = Math.sign(fingers);
    const stream = [...Array(13).fill(8 * sign), ...tail.map((d) => d * sign)];
    for (const dx of stream) {
      const until = performance.now() + 16;
      while (performance.now() < until) await tick();
      document.querySelector('.clu-sidebar[data-mode=main]')?.dispatchEvent(
          new WheelEvent('wheel', {deltaX: dx, bubbles: true, cancelable: true}));
    }
    const settle = performance.now() + 600;
    while (performance.now() < settle) await tick();
    return true;
  })($1)
)";

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesSwipeMovesOnePageAndLibraryExitReturns) {
  const std::string one = window().space_id();
  const std::string two = Create(window(), "Two");
  const std::string three = Create(window(), "Three");
  ASSERT_TRUE(window().ShowSpace(one));
  content::WebContents* frame = shell().frame_contents();
  ASSERT_TRUE(content::WaitForLoadStop(frame));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame,
                           "document.querySelector('.clu-space-slider')"
                           "?.dataset.state ?? ''")
               .ExtractString() == "idle";
  }));

  // Fingers left, then a long momentum tail: Two, never Three.
  EXPECT_EQ(true, content::EvalJs(frame, content::JsReplace(kSwipeStream, 1)));
  EXPECT_EQ(window().space_id(), two);
  ASSERT_TRUE(window().ShowSpace(one));

  // Fingers right on the first space open the Library; its momentum is swallowed there.
  EXPECT_EQ(true, content::EvalJs(frame, content::JsReplace(kSwipeStream, -1)));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().archive_panel(); }));
  EXPECT_EQ(window().space_id(), one);
  // Fingers left close it, and the closing swipe's momentum reaches the space list without moving it.
  EXPECT_EQ(true, content::EvalJs(frame, content::JsReplace(kSwipeStream, 1)));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));
  EXPECT_EQ(window().space_id(), one);
  std::ignore = three;
}

// A slow swipe whose fingers rest mid-gesture, or whose frame stalls, must not settle until the
// fingers lift. The browser reports the trackpad's phases as windows[id].wheel (Blink never shows phase ended to the
// DOM), so the sidebar holds the drag through a 400 ms rest and ends it on phase ended. Every wheel event also carries
// the real phase, so the slider stays `dragging` and the space only switches after the lift.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesSwipeEndsOnlyWhenTheFingersLift) {
  const std::string one = window().space_id();
  const std::string two = Create(window(), "Two");
  ASSERT_TRUE(window().ShowSpace(one));
  content::WebContents* frame = shell().frame_contents();
  ASSERT_TRUE(content::WaitForLoadStop(frame));
  auto slider = [&] {
    return content::EvalJs(frame,
                           "document.querySelector('.clu-space-slider')"
                           "?.dataset.state ?? ''")
        .ExtractString();
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return slider() == "idle"; }));
  const gfx::Rect sidebar = shell().layout().sidebar_rect;
  const gfx::Point over(sidebar.x() + sidebar.width() / 2,
                        sidebar.y() + sidebar.height() / 2);
  base::DictValue state;
  auto wheel = [&]() -> const base::DictValue* {
    state = window().CurrentState();
    return state.FindDict("windows")
        ->FindDict(window().window_id())
        ->FindDict("wheel");
  };
  ASSERT_TRUE(wheel());
  EXPECT_EQ(wheel()->FindInt("lifts"), 0);

  // Fingers left, slowly: the browser sees real phases.
  for (int i = 0; i < 6; ++i) {
    content::SimulateMouseWheelEvent(
        frame, over, gfx::Vector2d(-12, 0),
        i == 0 ? blink::WebMouseWheelEvent::kPhaseBegan
               : blink::WebMouseWheelEvent::kPhaseChanged);
  }
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return wheel()->FindBool("phased") == true; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return slider() == "dragging"; }));

  // The fingers rest for 400 ms (no events): still dragging, no switch.
  base::RunLoop rest;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, rest.QuitClosure(), base::Milliseconds(400));
  rest.Run();
  EXPECT_EQ(slider(), "dragging");
  EXPECT_EQ(window().space_id(), one);

  // They move on past the commit share, then lift: one lift, one space on.
  for (int i = 0; i < 8; ++i) {
    content::SimulateMouseWheelEvent(frame, over, gfx::Vector2d(-12, 0),
                                     blink::WebMouseWheelEvent::kPhaseChanged);
  }
  EXPECT_EQ(window().space_id(), one);
  content::SimulateMouseWheelEvent(frame, over, gfx::Vector2d(),
                                   blink::WebMouseWheelEvent::kPhaseEnded);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return wheel()->FindInt("lifts") == 1; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return window().space_id() == two; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return slider() == "idle"; }));

  // A mouse wheel after the trackpad gesture (no phase) makes the window unphased again, so a Shift+wheel
  // drag ends on idle instead of waiting for a lift that never comes.
  content::SimulateMouseWheelEvent(frame, over, gfx::Vector2d(-12, 0),
                                   blink::WebMouseWheelEvent::kPhaseNone);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return wheel()->FindBool("phased") == false; }));
  EXPECT_EQ(wheel()->FindInt("lifts"), 1);
}

// Records every Animation.currentTime the frame sets, so a test can check that a settle starts
// one frame into its curve (a Web Animation paints its start time first, so starting at 0 held the strip a frame).
constexpr char kRecordSettleStarts[] = R"(
  window.cluStarts = [];
  const d = Object.getOwnPropertyDescriptor(Animation.prototype, 'currentTime');
  Object.defineProperty(Animation.prototype, 'currentTime', {
    configurable: true,
    get() { return d.get.call(this); },
    set(v) { window.cluStarts.push(v); d.set.call(this, v); },
  });
)";
constexpr char kSettleStartsMove[] =
    "window.cluStarts.length > 0 && Math.min(...window.cluStarts) >= 16";

// After a slow swipe the settle began 106 to 116 ms after the lift, because macOS holds phase
// ended 100 ms for momentum before Chromium sends it. The watcher's native monitor reports the lift at once
// (OnFingersLifted, what the NSEvent monitor calls), so the space switches with no phase ended ever sent, and
// Chromium's late one lifts nothing more. During the drag the neighbor sits one panel width away, the gap the
// commit's % transforms keep (it stepped from 214 to 198 px).
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest, CluSpacesSwipeSettlesOnTheNativeLift) {
  const std::string one = window().space_id();
  const std::string two = Create(window(), "Two");
  ASSERT_TRUE(window().ShowSpace(one));
  content::WebContents* frame = shell().frame_contents();
  ASSERT_TRUE(content::WaitForLoadStop(frame));
  auto slider = [&] {
    return content::EvalJs(frame,
                           "document.querySelector('.clu-space-slider')"
                           "?.dataset.state ?? ''")
        .ExtractString();
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return slider() == "idle"; }));
  const gfx::Rect sidebar = shell().layout().sidebar_rect;
  const gfx::Point over(sidebar.x() + sidebar.width() / 2,
                        sidebar.y() + sidebar.height() / 2);
  base::DictValue state;
  auto lifts = [&] {
    state = window().CurrentState();
    const base::DictValue* wheel = state.FindDict("windows")
                                       ->FindDict(window().window_id())
                                       ->FindDict("wheel");
    return wheel ? wheel->FindInt("lifts").value_or(-1) : -1;
  };
  ASSERT_TRUE(window().wheel_watcher_for_testing());
  const int before = lifts();

  for (int i = 0; i < 12; ++i) {
    content::SimulateMouseWheelEvent(
        frame, over, gfx::Vector2d(-12, 0),
        i == 0 ? blink::WebMouseWheelEvent::kPhaseBegan
               : blink::WebMouseWheelEvent::kPhaseChanged);
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return slider() == "dragging"; }));
  constexpr char kGap[] = R"((() => {
    const cur = document.querySelector(
        '.clu-space-panel:not([data-peer]):not([data-outgoing])');
    const peer = document.querySelector('.clu-space-panel[data-peer]');
    if (!cur || !peer) return -1;
    return Math.round(peer.getBoundingClientRect().x -
        cur.getBoundingClientRect().x - cur.getBoundingClientRect().width);
  })())";
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return content::EvalJs(frame, kGap).ExtractInt() == 0; }));
  EXPECT_EQ(window().space_id(), one);
  ASSERT_TRUE(content::ExecJs(frame, kRecordSettleStarts));

  // The fingers lift: no phase ended reaches the widget, and the space still switches.
  window().wheel_watcher_for_testing()->OnFingersLifted();
  EXPECT_EQ(lifts(), before + 1);
  ASSERT_TRUE(base::test::RunUntil([&] { return window().space_id() == two; }));
  // Every settle animation (the commit's, and main's switch picking it up) starts at least one frame into its curve,
  // so the settle never holds a frame or two and then steps.
  EXPECT_EQ(true, content::EvalJs(frame, kSettleStartsMove));
  // Chromium's own phase ended, 100 ms later, is not a second lift.
  content::SimulateMouseWheelEvent(frame, over, gfx::Vector2d(),
                                   blink::WebMouseWheelEvent::kPhaseEnded);
  ASSERT_TRUE(base::test::RunUntil([&] { return slider() == "idle"; }));
  EXPECT_EQ(lifts(), before + 1);
  EXPECT_EQ(window().space_id(), two);
}

// Leaving the Library by a swipe swapped it for the space list in one frame, the list
// appearing at x 79.8 or 158.4. Now, as the Library closes, the sidebar slides in from the Library's right edge on
// the exit's curve, with the Library drawn just left of it (.clu-library-exit), and lands on the space it was
// opened from.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesLibraryExitSlidesAsOneStrip) {
  const std::string one = window().space_id();
  content::WebContents* frame = shell().frame_contents();
  ASSERT_TRUE(content::WaitForLoadStop(frame));
  window().SetArchivePanel(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame,
                           "!!document.querySelector('.clu-sidebar-library')")
        .ExtractBool();
  }));
  ASSERT_TRUE(content::ExecJs(frame, R"(
    window.cluExit = null;
    // A fresh Library must not mount in the frame it closed (a one-frame swap, then a stall). The exit
    // must draw the open Library's own node.
    window.cluLib = document.querySelector('.clu-sidebar-library .clu-library');
    new MutationObserver(() => {
      const ghost = document.querySelector('.clu-library-exit');
      const nav = document.querySelector('nav.clu-sidebar');
      // The newest: the Library's own slide may still sit on the reused nav.
      const anim = nav && nav.getAnimations().at(-1);
      if (ghost && anim && !window.cluExit) {
        window.cluExit = {
          from: anim.effect.getKeyframes()[0].transform,
          right: Math.round(ghost.getBoundingClientRect().right -
                            nav.getBoundingClientRect().left),
          same: !!window.cluLib && ghost.contains(window.cluLib),
        };
      }
    }).observe(document.body, {subtree: true, childList: true});
  )"));
  ASSERT_TRUE(content::ExecJs(frame, kRecordSettleStarts));
  const gfx::Rect sidebar = shell().layout().sidebar_rect;
  const gfx::Point over(sidebar.x() + sidebar.width() / 2,
                        sidebar.y() + sidebar.height() / 2);
  // Fingers left past the commit share of the widened sidebar, then lift.
  for (int i = 0; i < 24; ++i) {
    content::SimulateMouseWheelEvent(
        frame, over, gfx::Vector2d(-12, 0),
        i == 0 ? blink::WebMouseWheelEvent::kPhaseBegan
               : blink::WebMouseWheelEvent::kPhaseChanged);
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame,
                           "document.querySelector('.clu-sidebar-library')"
                           "?.dataset.swipe ?? ''")
               .ExtractString() == "dragging";
  }));
  window().wheel_watcher_for_testing()->OnFingersLifted();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));
  // The sidebar started off to the right by what the Library still had to go, with the Library flush left of it.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame, "!!window.cluExit").ExtractBool();
  }));
  const std::string from =
      content::EvalJs(frame, "window.cluExit.from").ExtractString();
  EXPECT_TRUE(base::StartsWith(from, "translateX(")) << from;
  EXPECT_FALSE(base::StartsWith(from, "translateX(-")) << from;
  EXPECT_NE(from, "translateX(0px)");
  EXPECT_EQ(content::EvalJs(frame, "window.cluExit.right").ExtractInt(), 0);
  EXPECT_EQ(true, content::EvalJs(frame, "window.cluExit.same"));
  // The Library's slide and the sidebar's both start one frame in: no held frame at the lift or the close.
  EXPECT_EQ(true, content::EvalJs(frame, kSettleStartsMove));
  // It finishes: no Library left behind, on the space it was opened from.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame,
                           "!document.querySelector('.clu-library-exit')")
        .ExtractBool();
  }));
  EXPECT_EQ(window().space_id(), one);
}

// 12.7 box 3: profiles isolate cookies and storage, lazy sessions, changing a space's profile -------------------------

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesProfilesIsolateCookiesAndStorage) {
  // Spaces A and B on the default profile, C on a new profile.
  const std::string a = window().space_id();
  const std::string a_row = Open(window(), "spaces/profile-probe.html?set=one");
  EXPECT_EQ(Probe(window(), a_row), "one/one");
  const std::string b = Create(window(), "B");
  const std::string b_row = Open(window(), "spaces/profile-probe.html");
  EXPECT_EQ(Probe(window(), b_row), "one/one");

  const std::string client = NewProfile("Client");
  ASSERT_FALSE(client.empty());
  const std::string c = Create(window(), "C", client);
  // C lives in the other profile: its window took over at the same bounds.
  ASSERT_TRUE(base::test::RunUntil([&] { return WindowShowing(c); }));
  CluWindowState& other = *WindowShowing(c);
  EXPECT_NE(other.profile(), browser()->GetProfile());
  EXPECT_EQ(other.browser()->GetWindow()->GetBounds(),
            browser()->GetWindow()->GetBounds());
  ASSERT_TRUE(base::test::RunUntil([&] { return window().hidden_by_switch(); }));
  const std::string c_row = Open(other, "spaces/profile-probe.html");
  EXPECT_EQ(Probe(other, c_row), "none/none");

  // Moving C to the default profile asks first (it has a tab), then reloads its tab there.
  CluSpacesHandler handler(other.GetWeakPtr());
  CallFuture ask;
  handler.SetProfile(
      mojom::SpacesSetProfileInput::New(c, ProfileId(), std::nullopt),
      ask.GetCallback());
  ASSERT_TRUE(IsOk(ask.Get()));
  EXPECT_EQ(*ValueOf(ask.Get()).FindString("status"), "needs-confirmation");
  EXPECT_EQ(ValueOf(ask.Get()).FindInt("tabCount"), 1);
  EXPECT_EQ(spaces()->list().FindSpace(c)->profile_id, client);
  CallFuture moved;
  handler.SetProfile(mojom::SpacesSetProfileInput::New(c, ProfileId(), true),
                     moved.GetCallback());
  ASSERT_TRUE(IsOk(moved.Get()));
  EXPECT_EQ(spaces()->list().FindSpace(c)->profile_id, ProfileId());
  // The window follows C back into the default profile's window, where the probe sees the default profile's data.
  ASSERT_TRUE(base::test::RunUntil([&] {
    CluWindowState* w = WindowShowing(c);
    return w && w->profile() == browser()->GetProfile();
  }));
  ASSERT_TRUE(model().tree().Get(c_row));
  ASSERT_TRUE(window().ActivateRow(c_row));
  ASSERT_TRUE(content::WaitForLoadStop(Contents(window(), c_row)));
  EXPECT_EQ(Probe(window(), c_row), "one/one");
  // A profile a space uses stays; once unused it can go.
  CluProfilesHandler profiles(window().GetWeakPtr());
  CallFuture in_use;
  ASSERT_TRUE(spaces()->SetSpaceProfile(b, client).has_value());
  profiles.Delete(mojom::ProfilesDeleteInput::New(client), in_use.GetCallback());
  EXPECT_EQ(Reason(in_use.Get()), "in-use");
  std::ignore = a;
}

// Sessions are lazy: a profile loads only when a window shows one of its spaces. Data survives a restart.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       PRE_CluSpacesProfilesLoadLazilyAndKeepTheirData) {
  const std::string row =
      OpenUrl(window(), FixedOriginUrl("spaces/profile-probe.html?set=kept"));
  EXPECT_EQ(Probe(window(), row), "kept/kept");
  const std::string client = NewProfile("Client");
  std::ignore = spaces()->CreateSpace("Elsewhere", std::nullopt, std::nullopt,
                                      client);
  g_browser_process->local_state()->CommitPendingWrite();
  base::RunLoop loop;
  model().FlushForTesting(loop.QuitClosure());
  loop.Run();
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesProfilesLoadLazilyAndKeepTheirData) {
  ASSERT_EQ(spaces()->list().profiles().size(), 2u);
  const ProfileEntry& client = spaces()->list().profiles()[1];
  // Not loaded at launch.
  EXPECT_FALSE(g_browser_process->profile_manager()->GetProfileByPath(
      spaces()->ProfilePath(client.id)));
  const std::string elsewhere = spaces()->list().spaces().back().id;
  space_actions::SwitchWindowToSpace(&window(), elsewhere,
                                     /*close_source=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return WindowShowing(elsewhere); }));
  // Ready before the first page: the window and its pages are on the profile.
  CluWindowState& other = *WindowShowing(elsewhere);
  EXPECT_EQ(other.profile(), spaces()->LoadedProfile(client.id));
  const std::string row =
      OpenUrl(other, FixedOriginUrl("spaces/profile-probe.html"));
  EXPECT_EQ(Contents(other, row)->GetBrowserContext(),
            spaces()->LoadedProfile(client.id));
  // The default profile's cookie and storage survived the restart; the new profile sees neither.
  EXPECT_EQ(Probe(other, row), "none/none");
  ASSERT_TRUE(base::test::RunUntil([&] { return window().hidden_by_switch(); }));
  space_actions::SwitchWindowToSpace(&other, spaces()->list().spaces()[0].id,
                                     false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().hidden_by_switch(); }));
  const std::string home_row =
      OpenUrl(window(), FixedOriginUrl("spaces/profile-probe.html"));
  EXPECT_EQ(Probe(window(), home_row), "kept/kept");
}

// A deleted profile's directory is gone on the next start.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       PRE_CluSpacesADeletedProfilesDirectoryIsRemoved) {
  const std::string doomed = NewProfile("Doomed");
  const base::FilePath path = spaces()->ProfilePath(doomed);
  ASSERT_FALSE(path.empty());
  CluProfilesHandler profiles(window().GetWeakPtr());
  CallFuture deleted;
  profiles.Delete(mojom::ProfilesDeleteInput::New(doomed), deleted.GetCallback());
  ASSERT_TRUE(IsOk(deleted.Get()));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !spaces()->list().FindProfile(doomed); }));
  g_browser_process->local_state()->CommitPendingWrite();
}

// A page of the profile that answers beforeunload with Stay keeps the profile, in Chromium and so in Clu.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesAProfileKeptByBeforeUnloadStaysInClu) {
  const std::string kept = NewProfile("Kept");
  base::test::TestFuture<Profile*> loaded;
  spaces()->LoadProfile(kept, loaded.GetCallback());
  ASSERT_TRUE(loaded.Get());
  BrowserWindowInterface* private_window = CreateIncognitoBrowser(loaded.Get());
  ASSERT_TRUE(private_window);
  content::WebContents* page =
      private_window->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(page, Url("tabs/a.html")));
  ASSERT_TRUE(content::ExecJs(
      page,
      "window.keep = true; window.addEventListener('beforeunload', e => { "
      "if (window.keep) { e.preventDefault(); e.returnValue = ''; } });"));
  content::PrepContentsForBeforeUnloadTest(page);

  CluProfilesHandler profiles(window().GetWeakPtr());
  CallFuture deleted;
  profiles.Delete(mojom::ProfilesDeleteInput::New(kept), deleted.GetCallback());
  ASSERT_TRUE(IsOk(deleted.Get()));
  javascript_dialogs::AppModalDialogController* dialog =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  dialog->view()->CancelAppModalDialog();
  content::RunAllTasksUntilIdle();
  EXPECT_TRUE(spaces()->list().FindProfile(kept));
  // The test's teardown closes every window; the page lets it go.
  ASSERT_TRUE(content::ExecJs(page, "window.keep = false"));
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesADeletedProfilesDirectoryIsRemoved) {
  EXPECT_EQ(spaces()->list().profiles().size(), 1u);
  // Chromium's DeleteProfileHelper removes the directory at startup at the latest; the test looks for any profile
  // directory besides the default one.
  base::ScopedAllowBlockingForTesting allow;
  const base::FilePath user_data =
      g_browser_process->profile_manager()->user_data_dir();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !base::PathExists(user_data.AppendASCII("Profile 1"));
  }));
}

// Moving a space to another profile writes Local State at once, not after its 10 s commit interval: a crash right
// after the move must not leave the registry naming the old profile for rows that have left it.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesAProfileMoveReachesLocalStateAtOnce) {
  const std::string client = NewProfile("Client");
  const std::string moved =
      spaces()->CreateSpace("Moved", std::nullopt, std::nullopt, std::nullopt)
          .value();
  g_browser_process->local_state()->CommitPendingWrite();
  base::test::TestFuture<
      base::expected<space_actions::SetProfileResult, SpacesError>>
      done;
  space_actions::SetSpaceProfile(moved, client, /*confirmed=*/true,
                                 done.GetCallback());
  ASSERT_TRUE(done.Get().has_value());

  base::FilePath local_state_path;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &local_state_path));
  local_state_path = local_state_path.Append(chrome::kLocalStateFilename);
  auto on_disk = [&] {
    base::ScopedAllowBlockingForTesting allow;
    std::string text;
    if (!base::ReadFileToString(local_state_path, &text)) {
      return std::string();
    }
    std::optional<base::Value> value =
        base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    const base::ListValue* list =
        value && value->is_dict()
            ? value->GetDict().FindListByDottedPath(kCluSpacesPref)
            : nullptr;
    if (!list) {
      return std::string();
    }
    for (const base::Value& space : *list) {
      const std::string* id = space.GetDict().FindString("id");
      if (id && *id == moved) {
        const std::string* profile = space.GetDict().FindString("profile_id");
        return profile ? *profile : std::string();
      }
    }
    return std::string();
  };
  // Well inside the 10 s interval.
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(3);
  while (on_disk() != client && base::TimeTicks::Now() < deadline) {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(50));
    loop.Run();
  }
  EXPECT_EQ(on_disk(), client);
}

// The cross-profile switch swaps windows with no flash ----------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesTheCrossProfileSwitchFadesWithoutAFlash) {
  const std::string home = window().space_id();
  Open(window(), "tabs/a.html");
  const std::string client = NewProfile("Client");
  const std::string elsewhere = spaces()
                                    ->CreateSpace("Elsewhere", std::nullopt,
                                                  "violet", client)
                                    .value();
  struct Tick {
    float opacity;
    bool source_visible;
    bool frame_ready;
  };
  std::vector<Tick> ticks;
  const int compositor_steps = CluWindowFade::compositor_steps_for_testing();
  CluWindowFade::SetTickObserverForTesting(base::BindRepeating(
      [](std::vector<Tick>* ticks, float opacity, bool source_visible,
         bool frame_ready) {
        ticks->push_back({opacity, source_visible, frame_ready});
      },
      &ticks));
  Shot(shell().frame_contents(), "cross-profile-0-before");
  space_actions::SwitchWindowToSpace(&window(), elsewhere, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return window().hidden_by_switch(); }));
  CluWindowState* other = WindowShowing(elsewhere);
  ASSERT_TRUE(other);
  // The new profile's engines load with its first window: nothing else loads them, and the bar's engine rows and
  // engine name need them.
  EXPECT_TRUE(base::test::RunUntil([&] {
    return TemplateURLServiceFactory::GetForProfile(other->profile())->loaded();
  }));
  Shot(other->shell().frame_contents(), "cross-profile-1-after");
  ASSERT_GE(ticks.size(), 2u);
  for (size_t i = 0; i + 1 < ticks.size(); ++i) {
    // Whenever the new window is visible at all, its frame already shows its space, and the old window is still
    // fully there underneath: no frame of the sequence is blank.
    if (ticks[i].opacity > 0) {
      EXPECT_TRUE(ticks[i].frame_ready) << i;
      EXPECT_TRUE(ticks[i].source_visible) << i;
    }
    EXPECT_LE(ticks[i].opacity, ticks[i + 1].opacity) << i;
  }
  EXPECT_EQ(ticks.back().opacity, 1.f);
  // The fade steps on the new window's compositor frames (the display's rate, 120 Hz on ProMotion), not on
  // a 16 ms timer.
  EXPECT_GE(CluWindowFade::compositor_steps_for_testing() - compositor_steps, 2);
  EXPECT_FALSE(browser()->GetWindow()->IsVisible());
  EXPECT_TRUE(other->browser()->GetWindow()->IsVisible());
  EXPECT_EQ(other->browser()->GetWindow()->GetBounds(),
            browser()->GetWindow()->GetBounds());
  EXPECT_EQ(other->group_id(), window().group_id());

  // Back again: the hidden sibling shows, no third window is made.
  const size_t windows = space_actions::AllWindows().size();
  space_actions::SwitchWindowToSpace(other, home, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return other->hidden_by_switch(); }));
  EXPECT_EQ(space_actions::AllWindows().size(), windows);
  EXPECT_TRUE(browser()->GetWindow()->IsVisible());
  EXPECT_EQ(window().space_id(), home);

  // A second switch inside the fade (a quick second press, key repeat) settles the running fade first: one window
  // stays on screen, never none.
  space_actions::SwitchWindowToSpace(&window(), elsewhere, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return other->FadeRunning(); }));
  space_actions::SwitchWindowToSpace(other, home, false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !other->FadeRunning() && !window().FadeRunning(); }));
  content::RunAllTasksUntilIdle();
  EXPECT_TRUE(browser()->GetWindow()->IsVisible());
  EXPECT_FALSE(other->browser()->GetWindow()->IsVisible());
  EXPECT_EQ(window().space_id(), home);
  // While the source of a running switch, a window takes no other switch.
  space_actions::SwitchWindowToSpace(&window(), elsewhere, false);
  EXPECT_TRUE(window().switch_pending());
  base::test::TestFuture<bool> refused;
  space_actions::SwitchWindowToSpace(&window(), home, false,
                                     refused.GetCallback());
  EXPECT_FALSE(refused.Get());
  ASSERT_TRUE(base::test::RunUntil([&] { return window().hidden_by_switch(); }));

  CluWindowFade::SetTickObserverForTesting({});

  // Closing the window on screen closes its hidden sibling too.
  content::WebContentsDestroyedWatcher gone(window().shell().frame_contents());
  other->browser()->GetWindow()->Close();
  gone.Wait();
}

// Fix pass (docs/chromium/fixes.md, leftovers 7): a new profile's first window can take longer to paint than the old
// 1 s fallback on a busy Mac. The fade waits for that paint (up to CluWindowFade::kPaintCap): the new window stays at
// opacity 0 over the old one, then rises with its space already drawn.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesTheCrossProfileSwitchWaitsForALateFirstPaint) {
  Open(window(), "tabs/a.html");
  const std::string client = NewProfile("Client");
  const std::string elsewhere = spaces()
                                    ->CreateSpace("Elsewhere", std::nullopt,
                                                  "violet", client)
                                    .value();
  struct Tick {
    float opacity;
    bool source_visible;
    bool frame_ready;
  };
  std::vector<Tick> ticks;
  CluWindowFade::SetTickObserverForTesting(base::BindRepeating(
      [](std::vector<Tick>* ticks, float opacity, bool source_visible,
         bool frame_ready) {
        ticks->push_back({opacity, source_visible, frame_ready});
      },
      &ticks));
  base::OnceClosure rise;
  CluWindowFade::SetPaintGateForTesting(base::BindRepeating(
      [](base::OnceClosure* rise, base::OnceClosure release) {
        *rise = std::move(release);
      },
      &rise));
  space_actions::SwitchWindowToSpace(&window(), elsewhere, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !rise.is_null(); }));

  // The frame painted; the rise holds 1.5 s, past the old fallback and under the cap.
  ASSERT_GT(CluWindowFade::kPaintCap, base::Milliseconds(1500));
  base::RunLoop late;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, late.QuitClosure(), base::Milliseconds(1500));
  late.Run();
  CluWindowState* other = WindowShowing(elsewhere);
  ASSERT_TRUE(other);
  EXPECT_TRUE(other->FadeRunning());
  EXPECT_TRUE(ticks.empty());
  EXPECT_TRUE(browser()->GetWindow()->IsVisible());
  EXPECT_FALSE(window().hidden_by_switch());

  std::move(rise).Run();
  ASSERT_TRUE(base::test::RunUntil([&] { return window().hidden_by_switch(); }));
  ASSERT_GE(ticks.size(), 2u);
  for (size_t i = 0; i + 1 < ticks.size(); ++i) {
    if (ticks[i].opacity > 0) {
      EXPECT_TRUE(ticks[i].frame_ready) << i;
      EXPECT_TRUE(ticks[i].source_visible) << i;
    }
    EXPECT_LE(ticks[i].opacity, ticks[i + 1].opacity) << i;
  }
  EXPECT_EQ(ticks.back().opacity, 1.f);
  EXPECT_TRUE(other->browser()->GetWindow()->IsVisible());
}

// 12.7 box 4: a private window keeps nothing ---------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesAPrivateWindowKeepsNothingAfterClose) {
  Profile* regular = browser()->GetProfile();
  const size_t rows = model().tree().size();
  CluWindowHandler handler(window().GetWeakPtr());
  CallFuture opened;
  handler.NewPrivate(opened.GetCallback());
  ASSERT_TRUE(IsOk(opened.Get()));
  CluWindowState* incognito = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (CluWindowState* w : space_actions::AllWindows()) {
      if (w->is_private()) {
        incognito = w;
      }
    }
    return incognito && incognito->frame_ready_for_testing();
  }));
  // A private window opens on the command bar, in one unsaved "Private" space, with no switcher or swipe.
  EXPECT_TRUE(incognito->command_session());
  const base::DictValue state = incognito->CurrentState();
  EXPECT_EQ(state.FindDict("windows")->FindDict(incognito->window_id())->FindBool(
                "private"),
            true);
  EXPECT_EQ(*state.FindDict("spaces")->FindDict(incognito->space_id())->FindString(
                "name"),
            kPrivateSpaceName);
  EXPECT_EQ(state.FindList("spaceOrder")->size(), 1u);
  EXPECT_FALSE(incognito->RunCommand("spaces.goto1"));

  const GURL secret = Url("spaces/profile-probe.html?set=secret");
  base::expected<std::string, std::string> row =
      incognito->CreateRow(secret, std::nullopt, true);
  ASSERT_TRUE(row.has_value());
  ASSERT_TRUE(content::WaitForLoadStop(Contents(*incognito, row.value())));
  EXPECT_EQ(Probe(*incognito, row.value()), "secret/secret");
  // A link from it opens in it.
  base::expected<std::string, std::string> links = incognito->CreateRow(
      Url("spaces/private-links.html"), std::nullopt, true);
  ASSERT_TRUE(links.has_value());
  ASSERT_TRUE(content::WaitForLoadStop(Contents(*incognito, links.value())));
  const int strip_before = incognito->browser()->GetTabStripModel()->count();
  ASSERT_TRUE(content::ExecJs(Contents(*incognito, links.value()),
                              "document.getElementById('blank').click()"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return incognito->browser()->GetTabStripModel()->count() > strip_before;
  }));

  // Closing it discards everything: the off-the-record profile goes with its cookies.
  Profile* otr = incognito->profile();
  ASSERT_TRUE(otr->IsOffTheRecord());
  incognito->browser()->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !regular->HasPrimaryOTRProfile(); }));

  // No tab rows, no archive rows, no history, no cookie in the persistent profile.
  EXPECT_EQ(model().tree().size(), rows);
  base::test::TestFuture<ArchivePage> archive;
  model().ListArchive(std::string(), std::string(), 100, archive.GetCallback());
  EXPECT_TRUE(archive.Get().rows.empty());
  ui_test_utils::HistoryEnumerator history(regular);
  for (const GURL& url : history.urls()) {
    EXPECT_NE(url.path(), secret.path()) << url;
  }
  const std::string probe = Open(window(), "spaces/profile-probe.html");
  EXPECT_EQ(Probe(window(), probe), "none/none");
}

// Moving a tab to a space of another profile ---------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest,
                       CluSpacesMovingATabToAnotherProfileAsksAndReloadsThere) {
  const std::string row = Open(window(), "spaces/profile-probe.html?set=here");
  const std::string client = NewProfile("Client");
  const std::string elsewhere = spaces()
                                    ->CreateSpace("Elsewhere", std::nullopt,
                                                  std::nullopt, client)
                                    .value();
  // Move To lists the other profile's space.
  CluTabsHandler tabs(window().GetWeakPtr());
  CallFuture first;
  tabs.Move(mojom::TabsMoveInput::New(row, TodayOf(elsewhere, 0), std::nullopt,
                                      std::nullopt, std::nullopt),
            first.GetCallback());
  ASSERT_TRUE(IsOk(first.Get()));
  EXPECT_EQ(*ValueOf(first.Get()).FindString("status"), "needs-confirmation");
  EXPECT_EQ(*ValueOf(first.Get()).FindString("reason"), "cross-profile");
  EXPECT_TRUE(model().tree().Get(row));

  CallFuture confirmed;
  tabs.Move(mojom::TabsMoveInput::New(row, TodayOf(elsewhere, 0), true,
                                      std::nullopt, std::nullopt),
            confirmed.GetCallback());
  ASSERT_TRUE(IsOk(confirmed.Get()));
  EXPECT_EQ(*ValueOf(confirmed.Get()).FindString("status"), "moved");
  EXPECT_FALSE(model().tree().Get(row));
  CluModelService* target = spaces()->LoadedModel(client);
  ASSERT_TRUE(target);
  ASSERT_TRUE(target->tree().Get(row));
  EXPECT_EQ(target->tree().Today(elsewhere), std::vector<std::string>{row});

  // Shown there, the page loads in the other profile, which has no cookie of its own.
  space_actions::SwitchWindowToSpace(&window(), elsewhere, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return WindowShowing(elsewhere); }));
  CluWindowState& other = *WindowShowing(elsewhere);
  ASSERT_TRUE(base::test::RunUntil([&] { return other.ActiveRowId() == row; }));
  ASSERT_TRUE(content::WaitForLoadStop(Contents(other, row)));
  // The row's URL still carries ?set=here, so the probe sets it in this profile: the history did not come along.
  EXPECT_FALSE(Contents(other, row)->GetController().CanGoBack());
}

// The editor opens in the overlay; the overflow lists every space ------------------------------------------------------

// New Space opens the editor with no space: the overlay draws it as the panel over the sidebar, and the frame reads
// the same surface to show the bare sidebar and the bottom bar without the Library button and "+".
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest, CluSpacesNewSpaceOpensEditorWithoutSpace) {
  ASSERT_TRUE(window().RunCommand("spaces.newSpace"));
  EXPECT_TRUE(shell().IsOverlayVisible());
  const base::DictValue state = window().CurrentState();
  const base::ListValue* surfaces =
      state.FindDict("windows")
          ->FindDict(window().window_id())
          ->FindListByDottedPath("overlay.surfaces");
  ASSERT_EQ(surfaces->size(), 1u);
  const base::DictValue& editor = (*surfaces)[0].GetDict();
  EXPECT_EQ(*editor.FindString("surface"), "space-editor");
  EXPECT_EQ(*editor.FindString("modality"), "modal");
  const base::Value* space_id = editor.FindByDottedPath("props.spaceId");
  ASSERT_TRUE(space_id);
  EXPECT_TRUE(space_id->is_none());
  EXPECT_EQ(*editor.FindStringByDottedPath("props.view"), "space");
  // Cancel closes it and the space list is unchanged.
  const size_t count = spaces()->list().Order().size();
  ASSERT_TRUE(window().CloseOverlayInstance(*editor.FindString("instanceId")));
  EXPECT_FALSE(shell().IsOverlayVisible());
  EXPECT_EQ(spaces()->list().Order().size(), count);
}

// The Delete Space dialog in the real overlay WebUI wears "esc" on Cancel and "return" on the red Delete, and
// Return deletes the space while focus sits on Cancel.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest, CluSpacesDeleteDialogReturnDeletes) {
  const std::string home = window().space_id();
  const std::string work = Create(window(), "Work");
  content::WebContents* overlay = shell().overlay_contents();
  ASSERT_TRUE(content::WaitForLoadStop(overlay));
  window().space_menu().RunIconItem(work, CluSpaceMenu::kDelete);
  EXPECT_EQ("Delete your “Work” Space?|Cancel:esc|Delete:return|danger|Cancel",
            content::EvalJs(overlay, R"(
    new Promise(resolve => {
      const check = () => {
        const dialog = document.querySelector('[data-surface="confirm"] [role="dialog"]');
        const buttons = dialog ? [...dialog.querySelectorAll('button')] : [];
        if (buttons.length !== 2 || !dialog.contains(document.activeElement)) {
          return setTimeout(check, 20);
        }
        const name = b => b.firstChild.textContent + ':' + (b.querySelector('kbd')?.textContent ?? '');
        resolve([dialog.querySelector('h2').textContent, name(buttons[0]), name(buttons[1]),
                 buttons[1].dataset.variant, document.activeElement.textContent.replace('esc', '')].join('|'));
      };
      check();
    }))"));
  content::SimulateKeyPress(overlay, ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, false, false, false, false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !spaces()->list().FindSpace(work); }));
  EXPECT_EQ(window().space_id(), home);
}

IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest, CluSpacesEditorAndOverflowMenu) {
  const std::string home = window().space_id();
  const std::string two = Create(window(), "Two");
  CluSpacesHandler handler(window().GetWeakPtr());
  CallFuture opened;
  handler.OpenEditor(
      mojom::SpacesOpenEditorInput::New(home, std::nullopt),
      opened.GetCallback());
  ASSERT_TRUE(IsOk(opened.Get()));
  EXPECT_TRUE(shell().IsOverlayVisible());
  const base::DictValue editing = window().CurrentState();
  const base::ListValue* surfaces =
      editing.FindDict("windows")
          ->FindDict(window().window_id())
          ->FindListByDottedPath("overlay.surfaces");
  ASSERT_EQ(surfaces->size(), 1u);
  EXPECT_EQ(*(*surfaces)[0].GetDict().FindString("surface"), "space-editor");
  EXPECT_EQ(*(*surfaces)[0].GetDict().FindStringByDottedPath("props.spaceId"),
            home);
  // The editor closes through overlay.close, and the overlay hides.
  ASSERT_TRUE(window().CloseOverlayInstance(
      *(*surfaces)[0].GetDict().FindString("instanceId")));
  EXPECT_FALSE(shell().IsOverlayVisible());
  CallFuture unknown;
  handler.OpenEditor(mojom::SpacesOpenEditorInput::New(
                         "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", std::nullopt),
                     unknown.GetCallback());
  EXPECT_EQ(unknown.Get()->get_error()->code, mojom::ErrorCode::kNotFound);

  const std::vector<MenuItem> overflow = window().space_menu().BuildOverflow();
  ASSERT_EQ(overflow.size(), 2u);
  EXPECT_EQ(overflow[1].label, "✓  Two");
  window().space_menu().RunOverflowItem(home);
  EXPECT_EQ(window().space_id(), home);
  std::ignore = two;
}

#if BUILDFLAG(IS_MAC)
// The sidebar's native menus wear the calling WebUI's chrome set, so on a light space they are light even
// when macOS is dark. The window wears it only while the menu runs and gets its own appearance back.
IN_PROC_BROWSER_TEST_F(CluSpacesBrowserTest, CluSpacesMenuWearsTheCallersChromeSet) {
  views::Widget* widget = window().shell().frame_view()->GetWidget();
  ASSERT_TRUE(widget);
  const std::string before = ScopedMenuAppearance::WindowAppearanceForTesting(widget);
  {
    const ScopedMenuAppearance light(widget, true);
    EXPECT_STREQ(ScopedMenuAppearance::WindowAppearanceForTesting(widget), "light");
  }
  EXPECT_EQ(ScopedMenuAppearance::WindowAppearanceForTesting(widget), before);
  {
    const ScopedMenuAppearance dark(widget, false);
    EXPECT_STREQ(ScopedMenuAppearance::WindowAppearanceForTesting(widget), "dark");
  }
  {
    const ScopedMenuAppearance none(widget, std::nullopt);
    EXPECT_EQ(ScopedMenuAppearance::WindowAppearanceForTesting(widget), before);
  }
  EXPECT_EQ(ScopedMenuAppearance::WindowAppearanceForTesting(widget), before);
  // The frame's `light` reaches the window state the menus read.
  window().set_menu_light(true);
  EXPECT_EQ(window().menu_light(), std::optional<bool>(true));
  window().set_menu_light(std::nullopt);
}
#endif

}  // namespace
}  // namespace clu
