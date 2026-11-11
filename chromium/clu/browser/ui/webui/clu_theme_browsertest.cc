// Copyright 2026 The Rio Authors. All rights reserved.

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/theme/clu_overlay_scrollbars.h"
#include "clu/browser/theme/clu_theme_math.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_data_handlers.h"
#include "clu/browser/ui/webui/clu_default_browser.h"
#include "clu/browser/ui/webui/clu_platform_handler.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_app_state.h"
#include "clu/browser/ui/webui/clu_settings_window.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "ui/base/models/menu_model.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/native_theme/mock_os_settings_provider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/os_settings_provider.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluTheme* checks of docs/PLAN.md 12.4 (M8, docs/chromium/m8.md) and the CluShellRestore* checks M8 adds for
// 12.3: appearance modes in pages and Chromium's UI, Reduce transparency, space themes with the native accent,
// first-run onboarding, the sidebar's remembered width and hidden state, and the back and forward history menu. The
// boxes each test covers are in docs/chromium/acceptance-map.md.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

mojom::SpaceThemePtr MojoTheme(const SpaceTheme& theme) {
  std::vector<mojom::ThemeColorPtr> colors;
  for (const ThemeColor& c : theme.colors) {
    colors.push_back(mojom::ThemeColor::New(c.h, c.s));
  }
  return mojom::SpaceTheme::New(std::move(colors), theme.intensity, theme.grain);
}

const SpaceTheme& Preset(const std::string& id) {
  for (const ThemePreset& p : ThemePresets()) {
    if (p.id == id) {
      return p.theme;
    }
  }
  NOTREACHED();
}

// shell_integration with the LaunchServices work stubbed out: checks read `state`, a set only counts. The tests never
// change this Mac's default browser.
class FakeDefaultBrowserWorker
    : public shell_integration::DefaultWebClientWorker {
 public:
  struct Log {
    std::atomic<shell_integration::DefaultWebClientState> state{
        shell_integration::NOT_DEFAULT};
    std::atomic<int> sets{0};
  };

  explicit FakeDefaultBrowserWorker(Log* log)
      : DefaultWebClientWorker("DefaultBrowser"), log_(log) {}

 private:
  ~FakeDefaultBrowserWorker() override = default;

  shell_integration::DefaultWebClientState CheckIsDefaultImpl() override {
    return log_->state.load();
  }
  void SetAsDefaultImpl(base::OnceClosure on_finished_callback) override {
    ++log_->sets;
    std::move(on_finished_callback).Run();
  }

  raw_ptr<Log> log_;
};

class CluThemeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // --window-size overrides the saved placement (UpdateWindowBoundsAndShowStateFromCommandLine), so the relaunch
    // that checks it runs without; --window-position still keeps the window where the runner put it.
    if (std::string_view(testing::UnitTest::GetInstance()->current_test_info()->name()) ==
        "CluShellRestoreBoundsWidthAndHiddenSurviveARelaunch") {
      command_line->RemoveSwitch(switches::kWindowSize);
    }
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(base::test::RunUntil([&] { return model().loaded(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
  }

  void TearDownOnMainThread() override {
    if (CluAppSettings* settings = CluAppSettings::Get()) {
      settings->SetFirstRunForTesting(std::nullopt);
    }
    CluDefaultBrowser::Get().ResetForTesting();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  // Every default browser check and request from here on goes to the fake worker over `default_browser_`.
  void UseFakeDefaultBrowser() {
    // Belt and braces: a real worker made by mistake still never sets anything.
    shell_integration::DefaultBrowserWorker::DisableSetAsDefaultForTesting();
    // Drops the answer of the real check the window started when it opened.
    CluDefaultBrowser::Get().ResetForTesting();
    CluDefaultBrowser::Get().SetWorkerFactoryForTesting(
        base::BindLambdaForTesting([this] {
          return scoped_refptr<shell_integration::DefaultWebClientWorker>(
              base::MakeRefCounted<FakeDefaultBrowserWorker>(
                  &default_browser_));
        }));
  }

  std::string DefaultBrowserState() {
    const base::DictValue state = State();
    const std::string* value = state.FindStringByDottedPath("app.defaultBrowser");
    return value ? *value : std::string();
  }

  std::string OnboardingStep() {
    return content::EvalJs(
               frame(), "document.querySelector('.clu-ob')?.dataset.step ?? ''")
        .ExtractString();
  }

  // Clicks the onboarding button whose text is `text`; false when there is none.
  bool ClickOnboarding(std::string_view text) {
    return content::EvalJs(frame(), content::JsReplace(R"(
      (() => {
        const b = [...document.querySelectorAll('.clu-ob button')]
            .find(b => b.textContent.trim() === $1 && !b.disabled);
        if (b) b.click();
        return !!b;
      })())",
                                                       text))
        .ExtractBool();
  }

  std::string RequestDefaultBrowserStatus() {
    CluPlatformHandler handler(window().GetWeakPtr());
    CallFuture future;
    handler.RequestDefaultBrowser(future.GetCallback());
    mojom::CallResultPtr result = future.Take();
    if (!IsOk(result)) {
      return "error";
    }
    const std::optional<base::Value> value =
        base::JSONReader::Read(result->get_value_json(), base::JSON_PARSE_RFC);
    const std::string* status =
        value && value->is_dict() ? value->GetDict().FindString("status")
                                  : nullptr;
    return status ? *status : std::string();
  }

  FakeDefaultBrowserWorker::Log default_browser_;

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluModelService& model() { return *window().model(); }
  CluAppSettings& settings() { return *CluAppSettings::Get(); }
  std::string space() { return window().space_id(); }
  Profile* profile() { return browser()->GetProfile(); }
  content::WebContents* frame() { return shell().frame_contents(); }
  views::Widget* widget() { return shell().frame_view()->GetWidget(); }

  GURL Url(const std::string& path) {
    return embedded_test_server()->GetURL(path);
  }

  content::WebContents* Open(const GURL& url) {
    base::expected<std::string, std::string> id =
        window().CreateRow(url, std::nullopt, /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    content::WebContents* contents =
        id.has_value() ? window().FindTab(id.value())->GetContents() : nullptr;
    EXPECT_TRUE(contents && content::WaitForLoadStop(contents));
    return contents;
  }

  base::DictValue State() {
    window().Flush();
    return window().CurrentState();
  }

  void SetMode(mojom::ThemeSetModeInputMode mode) {
    CluThemeHandler handler(window().GetWeakPtr());
    CallFuture done;
    handler.SetMode(mojom::ThemeSetModeInput::New(mode), done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
  }

  std::string SchemeOf(content::WebContents* page) {
    return content::EvalJs(page, "document.getElementById('scheme').textContent")
        .ExtractString();
  }

  // The space's id by name (spaces survive a relaunch, their ids too).
  std::string SpaceNamed(const std::string& name) {
    for (const SpaceEntry& s : CluSpacesService::Get()->list().spaces()) {
      if (s.name == name) {
        return s.id;
      }
    }
    return std::string();
  }
};

// 12.4 box 1: light, dark and auto ---------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeModesDrivePagesAndChromiumsColorScheme) {
  content::WebContents* page = Open(Url("/theme/scheme.html"));
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile());

  SetMode(mojom::ThemeSetModeInputMode::kDark);
  EXPECT_EQ(theme->GetBrowserColorScheme(), ThemeService::BrowserColorScheme::kDark);
  ASSERT_TRUE(base::test::RunUntil([&] { return SchemeOf(page) == "dark"; }));
  // Clu's own WebUIs follow too, and state says what Clu resolved.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), "matchMedia('(prefers-color-scheme: dark)').matches")
               .ExtractBool();
  }));
  base::DictValue state = State();
  EXPECT_EQ(*state.FindStringByDottedPath("app.appearance.mode"), "dark");
  EXPECT_EQ(*state.FindStringByDottedPath("app.appearance.resolved"), "dark");
  EXPECT_EQ(*state.FindDict("settings")->FindDict("global")->FindString("appearance.mode"),
            "dark");

  SetMode(mojom::ThemeSetModeInputMode::kLight);
  EXPECT_EQ(theme->GetBrowserColorScheme(), ThemeService::BrowserColorScheme::kLight);
  ASSERT_TRUE(base::test::RunUntil([&] { return SchemeOf(page) == "light"; }));
  EXPECT_EQ(*State().FindStringByDottedPath("app.appearance.resolved"), "light");

  SetMode(mojom::ThemeSetModeInputMode::kAuto);
  EXPECT_EQ(theme->GetBrowserColorScheme(), ThemeService::BrowserColorScheme::kSystem);
  EXPECT_EQ(*State().FindStringByDottedPath("app.appearance.mode"), "auto");

  // A window of another profile opened later follows the mode too.
  SetMode(mojom::ThemeSetModeInputMode::kDark);
  BrowserWindowInterface* second = CreateBrowser(profile());
  ASSERT_TRUE(CluWindowState::GetOrCreate(second));
  EXPECT_EQ(theme->GetBrowserColorScheme(), ThemeService::BrowserColorScheme::kDark);
}

class CluThemeTransparencyBrowserTest : public CluThemeBrowserTest {
 protected:
  // Overrides the Mac's own settings for the test (NativeTheme reads it).
  ui::MockOsSettingsProvider os_settings_;
};

IN_PROC_BROWSER_TEST_F(CluThemeTransparencyBrowserTest,
                       CluThemeReduceTransparencyReachesState) {
  EXPECT_EQ(State().FindBoolByDottedPath("app.appearance.reduceTransparency"), false);
  os_settings_.SetPrefersReducedTransparency(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return State().FindBoolByDottedPath("app.appearance.reduceTransparency") == true;
  }));
  // The frame paints the opaque layer from it (translucencyAlpha, layer.test.ts); focus is windows[id].focused.
  EXPECT_TRUE(State().FindDict("windows")->FindDict(window().window_id())->Find("focused"));
}

// A Mac with a mouse (or "Show scroll bars: Always") asks for legacy scrollbars, which Chromium draws in a grey
// or near-white track column the full height of the page. Rio answers overlay anyway: a thin thumb
// over the page that takes no width, in pages and in Rio's own WebUIs.
class CluThemeScrollbarBrowserTest : public CluThemeBrowserTest {
 protected:
  ui::MockOsSettingsProvider legacy_mac_;
};

IN_PROC_BROWSER_TEST_F(CluThemeScrollbarBrowserTest, CluThemeOverlayScrollbarsOnAMouseMac) {
  legacy_mac_.SetPrefersOverlayScrollbars(false);
  ASSERT_TRUE(base::test::RunUntil(
      [] { return !ui::NativeTheme::GetInstanceForWeb()->use_overlay_scrollbar(); }));

  // The provider EnsureCluServiceFactoriesBuilt() installs, over the test's legacy Mac.
  std::unique_ptr<ui::OsSettingsProvider> rio =
      CreateOverlayScrollbarsProviderForTesting(ui::OsSettingsProvider::PriorityLevel::kTesting);
  ASSERT_TRUE(base::test::RunUntil(
      [] { return ui::NativeTheme::GetInstanceForWeb()->use_overlay_scrollbar(); }));

  constexpr char kScrollbarWidth[] = "innerWidth - document.documentElement.clientWidth";
  content::WebContents* page = Open(Url("/theme/tall.html"));
  ASSERT_TRUE(content::EvalJs(page, "document.documentElement.scrollHeight > innerHeight").ExtractBool());
  ASSERT_TRUE(base::test::RunUntil([&] { return content::EvalJs(page, kScrollbarWidth).ExtractInt() == 0; }));
  // Rio's frame WebUI, already open before the switch, follows too: a tall box of its own reserves no gutter.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), R"((() => {
      const box = document.createElement('div')
      box.style.cssText = 'position:fixed;left:0;top:0;width:100px;height:50px;overflow:scroll'
      box.appendChild(document.createElement('div')).style.height = '400px'
      document.body.appendChild(box)
      const gutter = box.offsetWidth - box.clientWidth
      box.remove()
      return gutter
    })())").ExtractInt() == 0;
  }));
}

// 12.4 box 2: space themes -----------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       PRE_CluThemeSpaceThemesPersistSetTheAccentAndFollowASwitch) {
  // Creating a space switches the window to it, so the first space is kept here.
  const std::string home = space();
  base::expected<std::string, SpacesError> two = space_actions::CreateSpace(
      &window(), std::string("Tide side"), std::nullopt, std::string("teal"),
      std::nullopt);
  ASSERT_TRUE(two.has_value());
  CluThemeHandler handler(window().GetWeakPtr());
  CallFuture first;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(home, MojoTheme(Preset("ember"))),
                        first.GetCallback());
  ASSERT_TRUE(IsOk(first.Take()));
  CallFuture second;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(two.value(), MojoTheme(Preset("tide"))),
                        second.GetCallback());
  ASSERT_TRUE(IsOk(second.Take()));
  // Renamed so the relaunch finds it.
  ASSERT_TRUE(CluSpacesService::Get()
                  ->UpdateSpace(home, std::string("Ember home"), std::nullopt, std::nullopt)
                  .has_value());
}

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeSpaceThemesPersistSetTheAccentAndFollowASwitch) {
  const std::string home = SpaceNamed("Ember home");
  const std::string tide = SpaceNamed("Tide side");
  ASSERT_FALSE(home.empty());
  ASSERT_FALSE(tide.empty());
  ASSERT_TRUE(window().ShowSpace(home));

  // Survived the relaunch, in state with its accent, and on the window's widget.
  base::DictValue state = State();
  const base::DictValue* home_state = state.FindDict("spaces")->FindDict(home);
  EXPECT_EQ(ParseSpaceTheme(*home_state->Find("theme")), Preset("ember"));
  EXPECT_EQ(*home_state->FindString("accent"), SpaceAccentHex(Preset("ember"), "blue"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return widget()->user_color_override() == SpaceAccentColor(Preset("ember"), "blue");
  }));

  // The accent follows a switch (the swipe ends in the same switch).
  ASSERT_TRUE(window().ShowSpace(tide));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return widget()->user_color_override() == SpaceAccentColor(Preset("tide"), "teal");
  }));

  // The picker previews without persisting and puts the committed theme back when it closes.
  window().OpenThemePicker(tide, std::nullopt);
  state = State();
  std::string picker;
  for (const base::Value& s : *state.FindDict("windows")
                                   ->FindDict(window().window_id())
                                   ->FindDict("overlay")
                                   ->FindList("surfaces")) {
    if (*s.GetDict().FindString("surface") == "theme-picker") {
      picker = *s.GetDict().FindString("instanceId");
      EXPECT_EQ(*s.GetDict().FindStringByDottedPath("props.spaceId"), tide);
    }
  }
  ASSERT_FALSE(picker.empty());
  CluThemeHandler handler(window().GetWeakPtr());
  CallFuture preview;
  handler.PreviewSpaceTheme(
      mojom::ThemePreviewSpaceThemeInput::New(tide, MojoTheme(Preset("moss"))),
      preview.GetCallback());
  ASSERT_TRUE(IsOk(preview.Take()));
  EXPECT_EQ(ParseSpaceTheme(*State().FindDict("spaces")->FindDict(tide)->Find("theme")),
            Preset("moss"));
  EXPECT_EQ(ParseSpaceTheme(*g_browser_process->local_state()
                                 ->GetDict(kCluSpaceThemesPref)
                                 .Find(tide)),
            Preset("tide"));
  ASSERT_TRUE(window().CloseOverlayInstance(picker));
  EXPECT_EQ(ParseSpaceTheme(*State().FindDict("spaces")->FindDict(tide)->Find("theme")),
            Preset("tide"));

  // Reset: the space shows its color again; an unknown space is refused.
  CallFuture reset;
  handler.ResetSpaceTheme(mojom::ThemeResetSpaceThemeInput::New(tide), reset.GetCallback());
  ASSERT_TRUE(IsOk(reset.Take()));
  EXPECT_TRUE(State().FindDict("spaces")->FindDict(tide)->Find("theme")->is_none());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return widget()->user_color_override() == SpaceAccentColor(std::nullopt, "teal");
  }));
  CallFuture unknown;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(
                            "00000000-0000-4000-8000-00000000dead", MojoTheme(Preset("ink"))),
                        unknown.GetCallback());
  EXPECT_EQ(unknown.Take()->get_error()->code, mojom::ErrorCode::kNotFound);
}

// The theme editor (docs/chromium/parity-theme.md): a drag sends a preview per frame, each one repaints
// the space in state at once while Local State keeps the committed theme; the release commits the editor's exact
// values (three colors, a continuous intensity, grain on the dial's 0.05 steps), and closing the editor keeps them.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeEditorDragPreviewsLiveAndCommitsOnRelease) {
  const std::string home = space();
  window().OpenThemePicker(home, std::nullopt);
  std::string picker;
  base::DictValue state = State();
  for (const base::Value& s : *state.FindDict("windows")
                                   ->FindDict(window().window_id())
                                   ->FindDict("overlay")
                                   ->FindList("surfaces")) {
    if (*s.GetDict().FindString("surface") == "theme-picker") {
      picker = *s.GetDict().FindString("instanceId");
      // Nothing dims while the editor is open (the sidebar shows the edit as it is), and with no anchor
      // its inner group sits at y 133.
      EXPECT_EQ(*s.GetDict().FindString("modality"), "light-dismiss");
      EXPECT_EQ(*s.GetDict().FindIntByDottedPath("props.anchor.y"), 133);
    }
  }
  ASSERT_FALSE(picker.empty());

  CluThemeHandler handler(window().GetWeakPtr());
  SpaceTheme drag;
  drag.colors = {{357, 0.617}, {40, 0.4}, {200, 0.3}};
  drag.intensity = 0.628;
  drag.grain = 0.3;
  for (const double h : {357.0, 20.0, 60.0, 120.0}) {
    drag.colors[0].h = h;
    CallFuture preview;
    handler.PreviewSpaceTheme(mojom::ThemePreviewSpaceThemeInput::New(home, MojoTheme(drag)),
                              preview.GetCallback());
    ASSERT_TRUE(IsOk(preview.Take()));
    base::DictValue now = State();
    EXPECT_EQ(ParseSpaceTheme(*now.FindDict("spaces")->FindDict(home)->Find("theme")), drag);
    EXPECT_EQ(*now.FindDict("spaces")->FindDict(home)->FindString("accent"),
              SpaceAccentHex(drag, "blue"));
    EXPECT_EQ(g_browser_process->local_state()->GetDict(kCluSpaceThemesPref).Find(home),
              nullptr);
  }

  CallFuture commit;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(home, MojoTheme(drag)),
                        commit.GetCallback());
  ASSERT_TRUE(IsOk(commit.Take()));
  const base::Value* stored =
      g_browser_process->local_state()->GetDict(kCluSpaceThemesPref).Find(home);
  ASSERT_TRUE(stored);
  EXPECT_EQ(ParseSpaceTheme(*stored), drag);
  EXPECT_DOUBLE_EQ(*stored->GetDict().FindDouble("intensity"), 0.628);
  ASSERT_TRUE(window().CloseOverlayInstance(picker));
  EXPECT_EQ(ParseSpaceTheme(*State().FindDict("spaces")->FindDict(home)->Find("theme")), drag);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return widget()->user_color_override() == SpaceAccentColor(drag, "blue");
  }));
}

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeJunkStoredThemesReadAsDefaults) {
  {
    ScopedDictPrefUpdate update(g_browser_process->local_state(), kCluSpaceThemesPref);
    update->Set(space(), base::DictValue().Set("colors", "junk"));
    update->Set("00000000-0000-4000-8000-00000000dead",
                SpaceThemeValue(Preset("ember")));
  }
  settings().ResetForTesting();
  EXPECT_EQ(settings().ShownTheme(space()), std::nullopt);
  // The repaired value is written back once.
  EXPECT_TRUE(g_browser_process->local_state()->GetDict(kCluSpaceThemesPref).empty());
  EXPECT_TRUE(State().FindDict("spaces")->FindDict(space())->Find("theme")->is_none());
}

// 12.4 box 3: onboarding -------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeOnboardingShowsOnceOnAFreshProfileAndCanBeSkipped) {
  // The test runner passes --no-first-run; a fresh install is Chromium's first run of the directory.
  settings().SetFirstRunForTesting(true);
  window().MarkDirty();
  EXPECT_EQ(State().FindBoolByDottedPath("app.onboarding.show"), true);
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(kWaitForSelector, ".clu-ob")));
  EXPECT_EQ(content::EvalJs(frame(), "document.querySelector('.clu-ob').dataset.step"),
            "welcome");
  // Onboarding fills the window, so the page view (which sits above the frame WebUI) hides under it.
  EXPECT_FALSE(shell().page_shown());
  // A private window never shows it.
  BrowserWindowInterface* incognito = CreateIncognitoBrowser(profile());
  CluWindowState* private_window = CluWindowState::GetOrCreate(incognito);
  private_window->Flush();
  EXPECT_EQ(private_window->CurrentState().FindBoolByDottedPath("app.onboarding.show"), false);

  // Skip setup.
  EXPECT_TRUE(content::ExecJs(frame(), R"(
    [...document.querySelectorAll('.clu-ob button')]
        .find(b => b.textContent.includes('Skip setup')).click())"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return State().FindBoolByDottedPath("app.onboarding.show") == false; }));
  EXPECT_TRUE(shell().page_shown());
  const std::string* completed = g_browser_process->local_state()
                                     ->GetDict(kCluSettingsPref)
                                     .FindString(kOnboardingCompletedAtKey);
  ASSERT_TRUE(completed);
  EXPECT_TRUE(completed->ends_with("Z"));

  // It never comes back: not on the next first run, not with junk stored.
  settings().SetFirstRunForTesting(true);
  EXPECT_FALSE(settings().onboarding_show());
  {
    ScopedDictPrefUpdate update(g_browser_process->local_state(), kCluSettingsPref);
    update->Set(kOnboardingCompletedAtKey, 42);
  }
  settings().SetFirstRunForTesting(true);
  EXPECT_FALSE(settings().onboarding_show());
  // A returning user without completedAt (a profile from before onboarding) is not first run.
  {
    ScopedDictPrefUpdate update(g_browser_process->local_state(), kCluSettingsPref);
    update->Remove(kOnboardingCompletedAtKey);
  }
  settings().SetFirstRunForTesting(false);
  EXPECT_FALSE(settings().onboarding_show());
}

// Blink collects drag areas in document order and the frame follows the onboarding, so the frame's window
// wide drag area landed over the card and the Mac took a press on its buttons ("Bring everything over", "Not now") as
// a window drag. While onboarding shows, its own background moves the window and its buttons take the click.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeOnboardingButtonsAreNotAWindowDragArea) {
  settings().SetFirstRunForTesting(true);
  window().MarkDirty();
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(kWaitForSelector,
                                                              ".clu-ob-actions-end button")));
  // The action button's center and the middle of the top row, in frame coordinates.
  const base::ListValue points =
      content::EvalJs(frame(), R"(
      (() => {
        const b = document.querySelector('.clu-ob-actions-end button')
                      .getBoundingClientRect();
        const t = document.querySelector('.clu-ob-top').getBoundingClientRect();
        return [Math.round(b.x + b.width / 2), Math.round(b.y + b.height / 2),
                Math.round(t.x + t.width / 2), Math.round(t.y + t.height / 2)];
      })())")
          .ExtractList()
          .Clone();
  ASSERT_EQ(points.size(), 4u);
  views::WebView* frame_view = shell().frame_view();
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(frame_view && browser_view);
  const auto in_window = [&](int x, int y) {
    return views::View::ConvertPointToTarget(frame_view, browser_view,
                                             gfx::Point(x, y));
  };
  const gfx::Point top = in_window(points[2].GetInt(), points[3].GetInt());
  const gfx::Point button = in_window(points[0].GetInt(), points[1].GetInt());
  ASSERT_TRUE(base::test::RunUntil([&] { return shell().IsWindowDragArea(top); }));
  EXPECT_FALSE(shell().IsWindowDragArea(button));
}

// On "Pick a look" every color control themes the space the window shows, which need not be the first one:
// a swatch by mouse and by keyboard, and the regular editor shown in the card (its color field and intensity) on that
// space. The background repaints at once and the choice is kept after Continue.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeOnboardingLookThemesTheWindowsSpace) {
  ASSERT_TRUE(space_actions::CreateSpace(&window(), std::string("Two"),
                                         std::nullopt, std::nullopt,
                                         std::nullopt)
                  .has_value());
  const std::vector<std::string> order =
      CluSpacesService::Get()->list().Order();
  ASSERT_EQ(order.size(), 2u);
  ASSERT_TRUE(window().ShowSpace(order[1]));
  const std::string shown = space();
  ASSERT_EQ(shown, order[1]);
  const std::optional<SpaceTheme> first_before = settings().ShownTheme(order[0]);

  settings().SetFirstRunForTesting(true);
  window().MarkDirty();
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(kWaitForSelector, ".clu-ob")));
  ASSERT_TRUE(ClickOnboarding("Get started"));
  ASSERT_TRUE(base::test::RunUntil([&] { return OnboardingStep() == "look"; }));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(kWaitForSelector,
                                                              ".clu-ob .clu-tp-swatch")));
  const auto background = [&] {
    return content::EvalJs(frame(), R"(
        (() => { const s = getComputedStyle(document.querySelector('.clu-ob'));
                 return s.backgroundImage + s.backgroundColor; })())")
        .ExtractString();
  };

  // Mouse.
  const std::string before = background();
  ASSERT_EQ(true, content::EvalJs(frame(), R"(
      (() => { document.querySelectorAll('.clu-ob .clu-tp-swatch')[2].click();
               return true; })())"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return g_browser_process->local_state()->GetDict(kCluSpaceThemesPref).Find(shown) != nullptr;
  }));
  const std::optional<SpaceTheme> by_mouse = settings().ShownTheme(shown);
  ASSERT_TRUE(by_mouse);
  ASSERT_TRUE(base::test::RunUntil([&] { return background() != before; }));

  // Keyboard: Enter on the next swatch.
  ASSERT_EQ(true, content::EvalJs(frame(), R"(
      (() => { document.querySelectorAll('.clu-ob .clu-tp-swatch')[3].focus();
               return document.activeElement?.dataset.swatch !== undefined; })())"));
  content::SimulateKeyPress(frame(), ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, false, false, false, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return settings().ShownTheme(shown) != by_mouse; }));

  // The regular editor is in the card on the same space, no popover over onboarding; its commit, as
  // its color field sends it on release.
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(R"(
      !!document.querySelector('.clu-ob .clu-theme-picker[data-inline]') &&
      document.querySelector('.clu-ob .clu-theme-picker').getAttribute('aria-label') === 'Theme for ' + $1)",
                                                             "Two")));
  CluThemeHandler handler(window().GetWeakPtr());
  CallFuture commit;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(shown, MojoTheme(Preset("ember"))),
                        commit.GetCallback());
  ASSERT_TRUE(IsOk(commit.Take()));

  ASSERT_TRUE(ClickOnboarding("Continue"));
  ASSERT_TRUE(base::test::RunUntil([&] { return OnboardingStep() == "moves"; }));
  EXPECT_EQ(settings().ShownTheme(shown), Preset("ember"));
  const base::Value* stored =
      g_browser_process->local_state()->GetDict(kCluSpaceThemesPref).Find(shown);
  ASSERT_TRUE(stored);
  EXPECT_EQ(ParseSpaceTheme(*stored), Preset("ember"));
  EXPECT_EQ(settings().ShownTheme(order[0]), first_before);
}

// At the smallest window onboarding allows, "Pick a look" is the whole space theme editor, every control of
// it inside the card: the canvas with the mode buttons, minus and plus, the swatch row with all 5 pages and both
// arrows, the intensity slider and the grain dial.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeOnboardingLookIsTheWholeEditorAtTheSmallestWindow) {
  const gfx::Rect bounds = widget()->GetWindowBoundsInScreen();
  widget()->SetBounds(gfx::Rect(bounds.origin(), gfx::Size(kMinWindowWidth, kMinWindowHeight)));
  settings().SetFirstRunForTesting(true);
  window().MarkDirty();
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(kWaitForSelector, ".clu-ob")));
  ASSERT_TRUE(ClickOnboarding("Get started"));
  ASSERT_TRUE(base::test::RunUntil([&] { return OnboardingStep() == "look"; }));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(kWaitForSelector,
                                                              ".clu-ob .clu-theme-picker")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), content::JsReplace(
                                        "window.innerWidth <= $1 && window.innerHeight <= $2",
                                        kMinWindowWidth, kMinWindowHeight))
        .ExtractBool();
  }));
  // Every part of the editor lies inside the card and the card inside the window; one string per failure.
  const std::string problems =
      content::EvalJs(frame(), R"(
      new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => {
        const ob = document.querySelector('.clu-ob');
        const card = ob.querySelector('.clu-ob-card').getBoundingClientRect();
        const editor = ob.querySelector('.clu-theme-picker');
        const out = [];
        const inside = (el, name) => {
          const r = el.getBoundingClientRect();
          if (r.width <= 0 || r.height <= 0) out.push(name + ' has no size');
          if (r.left < card.left - 0.5 || r.top < card.top - 0.5 ||
              r.right > card.right + 0.5 || r.bottom > card.bottom + 0.5) out.push(name + ' outside the card');
        };
        inside(editor, 'editor');
        inside(editor.querySelector('[data-testid="theme-field"]'), 'canvas');
        for (const name of ['Auto', 'Light', 'Dark'])
          inside(editor.querySelector(`[role="radio"][aria-label="${name}"]`), name);
        for (const name of ['Add color', 'Remove color', 'Previous presets', 'Next presets'])
          inside(editor.querySelector(`button[aria-label="${name}"]`), name);
        for (const name of ['Intensity', 'Grain'])
          inside(editor.querySelector(`[role="slider"][aria-label="${name}"]`), name);
        for (const s of editor.querySelectorAll('.clu-tp-swatch[data-page="0"]')) inside(s, 'swatch');
        if (editor.querySelectorAll('.clu-tp-swatch').length !== 45) out.push('not every swatch page');
        if (card.bottom > window.innerHeight || card.right > window.innerWidth) out.push('card outside the window');
        resolve(out.join(', '));
      }))))")
          .ExtractString();
  EXPECT_EQ(problems, "");
}

// With three colors, moving one dot moves only that dot. The editor sends the theme with the one dot moved;
// the browser keeps every dot exactly as sent, in the preview and in the stored theme, never re-placing the others
// around a main one, even with two dots right next to each other.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeEachColorDotMovesOnItsOwn) {
  const std::string home = space();
  CluThemeHandler handler(window().GetWeakPtr());
  SpaceTheme theme;
  theme.colors = {{0, 0.5}, {210, 0.5}, {150, 0.5}};
  theme.intensity = 0.6;
  theme.grain = 0.2;
  CallFuture start;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(home, MojoTheme(theme)),
                        start.GetCallback());
  ASSERT_TRUE(IsOk(start.Take()));
  // Drag the third dot next to the main one: only the third changes, frame by frame.
  for (const double h : {120.0, 60.0, 10.0}) {
    theme.colors[2] = {h, 0.55};
    CallFuture preview;
    handler.PreviewSpaceTheme(mojom::ThemePreviewSpaceThemeInput::New(home, MojoTheme(theme)),
                              preview.GetCallback());
    ASSERT_TRUE(IsOk(preview.Take()));
    const std::optional<SpaceTheme> shown =
        ParseSpaceTheme(*State().FindDict("spaces")->FindDict(home)->Find("theme"));
    ASSERT_TRUE(shown);
    ASSERT_EQ(shown->colors.size(), 3u);
    EXPECT_EQ(shown->colors[0], (ThemeColor{0, 0.5}));
    EXPECT_EQ(shown->colors[1], (ThemeColor{210, 0.5}));
    EXPECT_EQ(shown->colors[2], (ThemeColor{h, 0.55}));
  }
  CallFuture commit;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(home, MojoTheme(theme)),
                        commit.GetCallback());
  ASSERT_TRUE(IsOk(commit.Take()));
  const base::Value* stored =
      g_browser_process->local_state()->GetDict(kCluSpaceThemesPref).Find(home);
  ASSERT_TRUE(stored);
  EXPECT_EQ(ParseSpaceTheme(*stored), theme);
  EXPECT_EQ(settings().ShownTheme(home), theme);
}

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeOnboardingOffersAThemeAndEndsWhenATabOpens) {
  settings().SetFirstRunForTesting(true);
  window().MarkDirty();
  ASSERT_EQ(State().FindBoolByDottedPath("app.onboarding.show"), true);
  // The look step commits a preset to the first space and the mode (the same commands the step's buttons send).
  CluThemeHandler handler(window().GetWeakPtr());
  CallFuture look;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(space(), MojoTheme(Preset("ember"))),
                        look.GetCallback());
  ASSERT_TRUE(IsOk(look.Take()));
  SetMode(mojom::ThemeSetModeInputMode::kDark);
  EXPECT_EQ(settings().ShownTheme(space()), Preset("ember"));
  // The import step opens the app settings window at General (its import), then ends onboarding as the step does
  // once the window opened (docs/chromium/parity-settings.md).
  CluSettingsHandler settings_handler(window().GetWeakPtr());
  CallFuture opened;
  settings_handler.Open(mojom::SettingsOpenInput::New(mojom::SettingsOpenInputSection::kGeneral),
                        opened.GetCallback());
  ASSERT_TRUE(IsOk(opened.Take()));
  ASSERT_TRUE(CluSettingsWindow::Get());
  EXPECT_TRUE(settings().CompleteOnboarding(/*skipped=*/false));
  window().MarkDirty();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return State().FindBoolByDottedPath("app.onboarding.show") == false; }));
  CluSettingsWindow::Get()->Close();
}

// Onboarding's default browser step (docs/chromium/fixes.md, leftovers 1): "Set as default browser" asks macOS through
// shell_integration and moves on; the step and app.defaultBrowser show the answer once the user comes back.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeOnboardingAsksMacOSToMakeRioTheDefaultBrowser) {
  UseFakeDefaultBrowser();
  CluDefaultBrowser::Get().Refresh();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return DefaultBrowserState() == "not-default"; }));

  settings().SetFirstRunForTesting(true);
  window().MarkDirty();
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(kWaitForSelector, ".clu-ob")));
  for (const char* button : {"Get started", "Continue", "Continue"}) {
    const std::string before = OnboardingStep();
    ASSERT_TRUE(ClickOnboarding(button)) << before;
    ASSERT_TRUE(base::test::RunUntil([&] { return OnboardingStep() != before; }));
  }
  ASSERT_EQ(OnboardingStep(), "default-browser");

  // The button asks once, and the step moves on while macOS shows its own prompt.
  ASSERT_TRUE(ClickOnboarding("Set as default browser"));
  ASSERT_TRUE(base::test::RunUntil([&] { return OnboardingStep() == "import"; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return default_browser_.sets.load() == 1; }));
  EXPECT_EQ(DefaultBrowserState(), "not-default");

  // The user says yes and comes back to Rio: the window's next check reads it, and the step says so.
  default_browser_.state = shell_integration::IS_DEFAULT;
  CluDefaultBrowser::Get().Refresh();
  ASSERT_TRUE(base::test::RunUntil([&] { return DefaultBrowserState() == "default"; }));
  ASSERT_TRUE(ClickOnboarding("Back"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), "document.getElementById('clu-ob-title')?.textContent ?? ''")
               .ExtractString() == "Rio is your default browser";
  }));

  // Asking again changes nothing; neither does a build macOS cannot name.
  EXPECT_EQ(RequestDefaultBrowserStatus(), "already-default");
  default_browser_.state = shell_integration::UNKNOWN_DEFAULT;
  EXPECT_EQ(RequestDefaultBrowserStatus(), "not-packaged");
  EXPECT_EQ(DefaultBrowserState(), "unavailable");
  EXPECT_EQ(default_browser_.sets.load(), 1);
}

// Rio ▸ Set as Default Browser asks macOS once through the same request as onboarding; when Rio
// already is the default it only says so in a toast.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluThemeSetAsDefaultBrowserMenuItemAsksMacOS) {
  UseFakeDefaultBrowser();
  auto last_toast = [&] {
    const base::DictValue state = State();
    const base::ListValue* toasts = state.FindDict("windows")
                                        ->FindDict(window().window_id())
                                        ->FindList("toasts");
    return toasts && !toasts->empty() &&
                   toasts->back().GetDict().FindString("text")
               ? *toasts->back().GetDict().FindString("text")
               : std::string();
  };
  ASSERT_TRUE(window().CanRunCommand("platform.setDefaultBrowser"));
  ASSERT_TRUE(window().RunCommand("platform.setDefaultBrowser"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return default_browser_.sets.load() == 1; }));

  default_browser_.state = shell_integration::IS_DEFAULT;
  ASSERT_TRUE(window().RunCommand("platform.setDefaultBrowser"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return last_toast() == "Rio is your default browser"; }));
  EXPECT_EQ(default_browser_.sets.load(), 1);
}

// 12.3: the sidebar's width and hidden state, and the history menu ---------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluShellRestoreResizeClampsPersistsAndResets) {
  CluShellHandler handler(window().GetWeakPtr());
  CallFuture wide;
  // Past 500 the width clamps.
  handler.SetSidebarWidth(mojom::ShellSetSidebarWidthInput::New(900, true),
                          wide.GetCallback());
  ASSERT_TRUE(IsOk(wide.Take()));
  EXPECT_EQ(shell().chrome_state().sidebar_width, kSidebarMaxWidth);
  EXPECT_EQ(settings().last_sidebar_width(), kSidebarMaxWidth);
  // A drag that has not ended persists nothing.
  CallFuture dragging;
  handler.SetSidebarWidth(mojom::ShellSetSidebarWidthInput::New(300, std::nullopt),
                          dragging.GetCallback());
  ASSERT_TRUE(IsOk(dragging.Take()));
  EXPECT_EQ(settings().last_sidebar_width(), kSidebarMaxWidth);
  shell().ToggleSidebar();
  window().Flush();
  EXPECT_TRUE(settings().last_sidebar_hidden());

  // A new window starts as the last one left the sidebar.
  CallFuture done;
  handler.SetSidebarWidth(mojom::ShellSetSidebarWidthInput::New(300, true), done.GetCallback());
  ASSERT_TRUE(IsOk(done.Take()));
  BrowserWindowInterface* second = CreateBrowser(profile());
  CluShell* second_shell = CluShell::From(second);
  ASSERT_TRUE(second_shell);
  EXPECT_EQ(second_shell->chrome_state().sidebar_width, 300);
  EXPECT_TRUE(second_shell->chrome_state().sidebar_hidden);

  // Double-click resets to 214, for the next window too.
  CallFuture reset;
  handler.ResetSidebarWidth(reset.GetCallback());
  ASSERT_TRUE(IsOk(reset.Take()));
  EXPECT_EQ(shell().chrome_state().sidebar_width, 214);
  EXPECT_EQ(settings().last_sidebar_width(), 214);
}

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       PRE_CluShellRestoreBoundsWidthAndHiddenSurviveARelaunch) {
  CluShellHandler handler(window().GetWeakPtr());
  CallFuture done;
  handler.SetSidebarWidth(mojom::ShellSetSidebarWidthInput::New(333, true), done.GetCallback());
  ASSERT_TRUE(IsOk(done.Take()));
  shell().ToggleSidebar();
  window().Flush();
  // Only the size changes, so the window stays on the display it opened on.
  const gfx::Rect bounds = browser()->GetWindow()->GetBounds();
  browser()->GetWindow()->SetBounds(gfx::Rect(bounds.origin(), gfx::Size(1100, 760)));
}

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluShellRestoreBoundsWidthAndHiddenSurviveARelaunch) {
  EXPECT_EQ(shell().chrome_state().sidebar_width, 333);
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  // Chromium's own window placement (browser.window_placement) brings the bounds back.
  EXPECT_EQ(browser()->GetWindow()->GetBounds().size(), gfx::Size(1100, 760));
}

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluShellRestoreJunkSidebarSettingsGiveTheDefaults) {
  {
    ScopedDictPrefUpdate update(g_browser_process->local_state(), kCluSettingsPref);
    update->Set(kLastSidebarWidthKey, "very wide");
  }
  BrowserWindowInterface* second = CreateBrowser(profile());
  EXPECT_EQ(CluShell::From(second)->chrome_state().sidebar_width, 214);
  // One more window, no dialog.
  CluWindowState* state = CluWindowState::GetOrCreate(second);
  state->Flush();
  for (const base::Value& s : *state->CurrentState()
                                   .FindDict("windows")
                                   ->FindDict(state->window_id())
                                   ->FindDict("overlay")
                                   ->FindList("surfaces")) {
    EXPECT_NE(*s.GetDict().FindString("layer"), "top");
  }
}

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest,
                       CluShellRestoreBackAndForwardListTheirHistory) {
  content::WebContents* page = Open(Url("/shell/a.html"));
  ASSERT_TRUE(content::NavigateToURL(page, Url("/shell/b.html")));
  std::unique_ptr<ui::MenuModel> back = window().NavHistoryModel(/*back=*/true);
  ASSERT_GE(back->GetItemCount(), 1u);
  EXPECT_EQ(back->GetLabelAt(0), u"Alpha");
  // Picking the entry goes there, as the long-press menu's pick does.
  content::TestNavigationObserver went(page);
  // The native menu calls MenuWillShow() before any pick; BackForwardMenuModel CHECKs it.
  back->MenuWillShow();
  back->ActivatedAt(0, /*event_flags=*/0);
  went.Wait();
  EXPECT_EQ(page->GetLastCommittedURL(), Url("/shell/a.html"));
  std::unique_ptr<ui::MenuModel> forward = window().NavHistoryModel(/*back=*/false);
  ASSERT_GE(forward->GetItemCount(), 1u);
  EXPECT_EQ(forward->GetLabelAt(0), u"Bravo");
}

// The theme: the frame's chrome follows the tint, not only the mode; three colors
// paint corners; grain never reaches zero; the editor is neutral gray glass that grows out of the sidebar, and a
// swatch it picks stays marked after the browser's round trip.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeChromeFollowsTheTintAndTheEditorIsGrayGlass) {
  const std::string home = space();
  CluThemeHandler handler(window().GetWeakPtr());
  const auto set = [&](std::vector<ThemeColor> colors, double grain) {
    SpaceTheme t;
    t.colors = std::move(colors);
    t.intensity = 0.5;
    t.grain = grain;
    CallFuture done;
    handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(home, MojoTheme(t)), done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
  };
  const auto frame_js = [&](const std::string& script) {
    return content::EvalJs(frame(), script).ExtractString();
  };
  const std::string kChrome = "document.documentElement.dataset.theme ?? ''";

  // A pastel in dark mode turns the whole frame to the light set; a mid color keeps it dark.
  SetMode(mojom::ThemeSetModeInputMode::kDark);
  set({{330, 0.95}}, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return frame_js(kChrome) == "light"; }));
  set({{0, 0.4}}, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return frame_js(kChrome) == "dark"; }));
  // The dark empty card is a near-black, darker than the sidebar.
  EXPECT_EQ(content::EvalJs(frame(),
                            "(() => { const el = document.createElement('div'); el.className = 'clu-card';"
                            " el.dataset.empty = 'true'; document.body.append(el);"
                            " const v = getComputedStyle(el).backgroundColor.match(/[\\d.]+/g).slice(0, 3)"
                            ".map(Number); el.remove(); return Math.max(...v) < 40; })()"),
            true);
  // Grain 0 still draws the least grain.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(),
                           "Number(getComputedStyle(document.querySelector('.clu-theme-grain') ?? "
                           "document.body).opacity) > 0 && !!document.querySelector('.clu-theme-grain')")
               .ExtractBool();
  }));
  // In light mode the wheel's center is a dark color and keeps the dark set.
  SetMode(mojom::ThemeSetModeInputMode::kLight);
  set({{0, 0}}, 0.25);
  ASSERT_TRUE(base::test::RunUntil([&] { return frame_js(kChrome) == "dark"; }));

  // Three colors take the corner layout (two gradients) on the frame's theme layer.
  set({{0, 0.5}, {120, 0.5}, {230, 0.5}}, 0.25);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return frame_js("document.querySelector('.clu-theme-bg')?.dataset.colors ?? ''") == "3";
  }));
  EXPECT_EQ(content::EvalJs(frame(),
                            "(getComputedStyle(document.querySelector('.clu-theme-bg')).backgroundImage"
                            ".match(/linear-gradient/g) ?? []).length"),
            2);

  // The editor is solid #f3eae8 in light mode (the page never shows through it). Motion
  // (docs/chromium/motion.md, the theme editor): it opens once with a spring scale from its left middle and a fade,
  // as transitions on transform and opacity, and rests at full size with nothing running.
  window().OpenThemePicker(home, std::nullopt);
  content::WebContents* overlay = shell().overlay_contents();
  ASSERT_TRUE(content::WaitForLoadStop(overlay));
  const std::string kPicker =
      "(() => { const el = document.querySelector('.clu-theme-picker');"
      " return el ? getComputedStyle(el).backgroundColor : ''; })()";
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay, kPicker).ExtractString() == "rgb(243, 234, 232)";
  }));
  EXPECT_EQ(content::EvalJs(overlay,
                            "(() => { const cs = getComputedStyle(document.querySelector('.clu-theme-picker'));"
                            " return [cs.transitionProperty, cs.transitionDuration, cs.transformOrigin]"
                            ".join('|'); })()"),
            "transform, opacity|0.44s, 0.18s|0px 254px");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay,
                           "(() => { const el = document.querySelector('.clu-theme-picker');"
                           " return el.hasAttribute('data-shown') && getComputedStyle(el).transform === 'none' &&"
                           " el.getAnimations({ subtree: true }).every((a) => a.playState !== 'running'); })()")
               .ExtractBool();
  }));
  // Nothing replays at rest: the mode already on plays no icon twinkle.
  EXPECT_EQ(content::EvalJs(overlay, "document.querySelectorAll('.clu-tp-mode[data-picked]').length"), 0);
  // A swatch commits one color and stays marked after the browser's round trip.
  ASSERT_TRUE(content::ExecJs(overlay, "document.querySelector('[aria-label=\"Brand 2\"]').click()"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::optional<SpaceTheme> t =
        ParseSpaceTheme(*State().FindDict("spaces")->FindDict(home)->Find("theme"));
    return t && t->colors.size() == 1u &&
           content::EvalJs(overlay,
                           "document.querySelector('[aria-label=\"Brand 2\"]')?.getAttribute('aria-pressed')"
                           " ?? ''")
                   .ExtractString() == "true";
  }));
  SetMode(mojom::ThemeSetModeInputMode::kDark);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay, kPicker).ExtractString() == "rgb(95, 95, 95)";
  }));
}

// Motion (docs/chromium/motion.md, the theme editor): a mode click twinkles that icon's stars once, after the state
// confirms the mode, and Esc shrinks the editor toward the sidebar in 150 ms before the surface goes.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeEditorMotionRunsOncePerTrigger) {
  const std::string home = space();
  window().OpenThemePicker(home, std::nullopt);
  content::WebContents* overlay = shell().overlay_contents();
  ASSERT_TRUE(content::WaitForLoadStop(overlay));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay,
                           "(() => { const el = document.querySelector('.clu-theme-picker');"
                           " return !!el && el.hasAttribute('data-shown') &&"
                           " getComputedStyle(el).transform === 'none'; })()")
        .ExtractBool();
  }));

  ASSERT_TRUE(content::ExecJs(overlay, "document.querySelector('.clu-tp-mode[data-mode=\"dark\"]').click()"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay,
                           "(() => { const b = document.querySelector('.clu-tp-mode[data-mode=\"dark\"]');"
                           " return b.hasAttribute('data-picked') && b.getAnimations({ subtree: true })"
                           ".some((a) => a.animationName === 'clu-tp-twinkle'); })()")
        .ExtractBool();
  }));
  EXPECT_EQ(content::EvalJs(overlay, "document.querySelectorAll('.clu-tp-mode[data-picked]').length"), 1);

  EXPECT_EQ(content::EvalJs(overlay, R"(
    (async () => {
      const el = document.querySelector('.clu-theme-picker');
      const flagged = new Promise((done) => {
        const mo = new MutationObserver(() => {
          if (!el.hasAttribute('data-closing')) return;
          mo.disconnect();
          done();
        });
        mo.observe(el, { attributes: true, attributeFilter: ['data-closing'] });
      });
      el.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
      await flagged;
      const cs = getComputedStyle(el);
      return [cs.transitionProperty, cs.transitionDuration].join('|');
    })())"),
            "transform, opacity|0.15s, 0.15s");
  const auto picker_open = [&] {
    base::DictValue state = State();
    for (const base::Value& s : *state.FindDict("windows")
                                     ->FindDict(window().window_id())
                                     ->FindDict("overlay")
                                     ->FindList("surfaces")) {
      if (*s.GetDict().FindString("surface") == "theme-picker") {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return !picker_open(); }));
  SetMode(mojom::ThemeSetModeInputMode::kAuto);
}

// The WebUIs tint (tooltips, popovers, menus, the empty page) with the space's MAIN color, the theme's first
// stop, never a gradient's later stops. Returns "<tint is the first stop>,<tint is the last stop>", or "none" before
// the theme layer draws.
constexpr char kTintIsMainStop[] = R"(
  (() => {
    const bg = document.querySelector('.clu-theme-bg');
    if (!bg) return 'none';
    const css = (el, n) => getComputedStyle(el).getPropertyValue(n).trim();
    const paint = c => {
      const d = document.createElement('div');
      d.style.color = c;
      document.body.append(d);
      const v = getComputedStyle(d).color;
      d.remove();
      return v;
    };
    const tint = paint(css(document.documentElement, '--tint'));
    return [tint === paint(css(bg, '--clu-theme-c0')),
            tint === paint(css(bg, '--clu-theme-c2'))].join();
  })())";

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeTintIsTheMainColorOfATwoColorTheme) {
  CluThemeHandler handler(window().GetWeakPtr());
  SpaceTheme theme;
  theme.colors = {{100, 0.5}, {25, 0.62}};  // green main, orange second
  theme.intensity = 1;
  theme.grain = 0;
  CallFuture done;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(space(), MojoTheme(theme)),
                        done.GetCallback());
  ASSERT_TRUE(IsOk(done.Take()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), kTintIsMainStop).ExtractString() == "true,false";
  }));

  // Making orange the main color moves the tint with it.
  std::swap(theme.colors[0], theme.colors[1]);
  CallFuture swapped;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(space(), MojoTheme(theme)),
                        swapped.GetCallback());
  ASSERT_TRUE(IsOk(swapped.Take()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), kTintIsMainStop).ExtractString() == "true,false";
  }));
  const std::string tint =
      content::EvalJs(frame(),
                      "getComputedStyle(document.documentElement).getPropertyValue('--tint').trim()")
          .ExtractString();
  // The overlay (tooltips, menus, hover cards) publishes the same tint once it shows a layer (a hidden overlay
  // applies state when it next shows).
  window().OpenThemePicker(space(), std::nullopt);
  content::WebContents* overlay = shell().overlay_contents();
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(content::WaitForLoadStop(overlay));
  std::string overlay_tint;
  EXPECT_TRUE(base::test::RunUntil([&] {
    overlay_tint =
        content::EvalJs(overlay,
                        "getComputedStyle(document.documentElement).getPropertyValue('--tint').trim()")
            .ExtractString();
    return overlay_tint == tint;
  })) << "frame " << tint << ", overlay " << overlay_tint;
}

// The intensity slider at its minimum turns the chosen color into a lighter pastel that keeps most of its
// color in the frame (lighter than the middle and about as colorful), and the
// frame follows a drag's previews live. Returns the first stop's "<chroma> <channel sum>" (chroma: largest minus
// smallest channel) once the layer draws, or "none".
constexpr char kFirstStopChroma[] = R"(
  (() => {
    const bg = document.querySelector('.clu-theme-bg');
    if (!bg) return 'none';
    const d = document.createElement('div');
    d.style.color = getComputedStyle(bg).getPropertyValue('--clu-theme-c0').trim();
    document.body.append(d);
    const rgb = (getComputedStyle(d).color.match(/\d+(\.\d+)?/g) ?? []).slice(0, 3).map(Number);
    d.remove();
    return rgb.length === 3
        ? `${Math.round(Math.max(...rgb) - Math.min(...rgb))} ${Math.round(rgb[0] + rgb[1] + rgb[2])}`
        : 'none';
  })())";

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeIntensityMinimumKeepsAPastelLive) {
  CluThemeHandler handler(window().GetWeakPtr());
  SpaceTheme drag;
  drag.colors = {{340, 0.6}};  // a pink pick
  drag.intensity = 0.5;
  drag.grain = 0;
  CallFuture middle;
  handler.PreviewSpaceTheme(mojom::ThemePreviewSpaceThemeInput::New(space(), MojoTheme(drag)),
                            middle.GetCallback());
  ASSERT_TRUE(IsOk(middle.Take()));
  std::string mid;
  ASSERT_TRUE(base::test::RunUntil([&] {
    mid = content::EvalJs(frame(), kFirstStopChroma).ExtractString();
    return mid != "none";
  }));

  // Dragging to the minimum repaints the frame without a commit, and the color stays.
  drag.intensity = 0;
  CallFuture minimum;
  handler.PreviewSpaceTheme(mojom::ThemePreviewSpaceThemeInput::New(space(), MojoTheme(drag)),
                            minimum.GetCallback());
  ASSERT_TRUE(IsOk(minimum.Take()));
  std::string low;
  ASSERT_TRUE(base::test::RunUntil([&] {
    low = content::EvalJs(frame(), kFirstStopChroma).ExtractString();
    return low != "none" && low != mid;
  })) << "middle " << mid;
  const std::vector<std::string> mid_parts =
      base::SplitString(mid, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  const std::vector<std::string> low_parts =
      base::SplitString(low, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  ASSERT_EQ(mid_parts.size(), 2u) << mid;
  ASSERT_EQ(low_parts.size(), 2u) << low;
  int mid_chroma = 0, mid_sum = 0, low_chroma = 0, low_sum = 0;
  ASSERT_TRUE(base::StringToInt(mid_parts[0], &mid_chroma)) << mid;
  ASSERT_TRUE(base::StringToInt(mid_parts[1], &mid_sum)) << mid;
  ASSERT_TRUE(base::StringToInt(low_parts[0], &low_chroma)) << low;
  ASSERT_TRUE(base::StringToInt(low_parts[1], &low_sum)) << low;
  // Lighter than the middle, never a gray: light mode keeps about 0.9 of the color, dark about 0.4.
  EXPECT_GT(low_sum, mid_sum) << low << " vs " << mid;
  EXPECT_GE(low_chroma, 15) << "the minimum drained the color: " << low;
  EXPECT_LE(low_chroma, mid_chroma);
  EXPECT_GE(low_chroma * 10, mid_chroma * 3) << low << " of " << mid;
}

// The intensity slider at its maximum deepens the chosen color in the frame: darker than the middle (OKLab
// lightness down 0.050) and more colorful (chroma about 1.8 times), as #959ccf to #7d87e4.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeIntensityMaximumDeepensTheColorLive) {
  CluThemeHandler handler(window().GetWeakPtr());
  SpaceTheme drag;
  drag.colors = {{234, 0.87}};  // a periwinkle pick
  drag.intensity = 0.5;
  drag.grain = 0;
  CallFuture middle;
  handler.PreviewSpaceTheme(mojom::ThemePreviewSpaceThemeInput::New(space(), MojoTheme(drag)),
                            middle.GetCallback());
  ASSERT_TRUE(IsOk(middle.Take()));
  std::string mid;
  ASSERT_TRUE(base::test::RunUntil([&] {
    mid = content::EvalJs(frame(), kFirstStopChroma).ExtractString();
    return mid != "none";
  }));

  drag.intensity = 1;
  CallFuture maximum;
  handler.PreviewSpaceTheme(mojom::ThemePreviewSpaceThemeInput::New(space(), MojoTheme(drag)),
                            maximum.GetCallback());
  ASSERT_TRUE(IsOk(maximum.Take()));
  std::string high;
  ASSERT_TRUE(base::test::RunUntil([&] {
    high = content::EvalJs(frame(), kFirstStopChroma).ExtractString();
    return high != "none" && high != mid;
  })) << "middle " << mid;
  const std::vector<std::string> mid_parts =
      base::SplitString(mid, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  const std::vector<std::string> high_parts =
      base::SplitString(high, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  ASSERT_EQ(mid_parts.size(), 2u) << mid;
  ASSERT_EQ(high_parts.size(), 2u) << high;
  int mid_chroma = 0, mid_sum = 0, high_chroma = 0, high_sum = 0;
  ASSERT_TRUE(base::StringToInt(mid_parts[0], &mid_chroma)) << mid;
  ASSERT_TRUE(base::StringToInt(mid_parts[1], &mid_sum)) << mid;
  ASSERT_TRUE(base::StringToInt(high_parts[0], &high_chroma)) << high;
  ASSERT_TRUE(base::StringToInt(high_parts[1], &high_sum)) << high;
  // #959ce1 at the middle, #7f82ff at the top: darker, and the color grows by more than a third.
  EXPECT_LT(high_sum, mid_sum) << high << " vs " << mid;
  EXPECT_GE(high_chroma * 3, mid_chroma * 4) << high << " of " << mid;
}

// A space with no theme row (its color picked in New Space) opens the editor with the handles of the default
// theme its sidebar already shows, not the empty "Tap to pick" canvas, and opening it writes nothing.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeEditorOfASpaceWithNoThemeShowsItsHandles) {
  ASSERT_TRUE(State().FindDict("spaces")->FindDict(space())->Find("theme")->is_none());
  window().OpenThemePicker(space(), std::nullopt);
  content::WebContents* overlay = shell().overlay_contents();
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(content::WaitForLoadStop(overlay));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay,
                           "document.querySelector('.clu-theme-picker')?.dataset.colors ?? ''")
               .ExtractString() == "2";
  }));
  EXPECT_EQ(content::EvalJs(overlay, "document.querySelectorAll('.clu-tp-handle').length"), 2);
  EXPECT_EQ(content::EvalJs(overlay, "!!document.querySelector('.clu-tp-hint')"), false);
  EXPECT_TRUE(State().FindDict("spaces")->FindDict(space())->Find("theme")->is_none());
  EXPECT_EQ(g_browser_process->local_state()->GetDict(kCluSpaceThemesPref).Find(space()), nullptr);
}

// The selected favorite's ring is the space's accent (the theme's main color), never the site icon's color.
// Returns "<ring color>|<accent color>" for a selected tile drawn in the frame's favorites grid.
constexpr char kFavoriteRing[] = R"(
  (() => {
    const grid = document.createElement('div');
    grid.className = 'clu-favorites';
    grid.innerHTML = '<div class="clu-favorite" data-active="true"><button class="clu-favorite-tile"></button></div>';
    document.body.append(grid);
    const shadow = getComputedStyle(grid.querySelector('.clu-favorite-tile')).boxShadow;
    const d = document.createElement('div');
    d.style.color = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
    document.body.append(d);
    const accent = getComputedStyle(d).color;
    d.remove();
    grid.remove();
    const ring = (shadow.match(/(rgba?\([^)]*\)) 0px 0px 0px 2px inset/) ?? [])[1] ?? '';
    return ring + '|' + accent;
  })())";

IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeSelectedFavoriteRingIsTheSpaceAccent) {
  CluThemeHandler handler(window().GetWeakPtr());
  SpaceTheme theme;
  theme.colors = {{25, 0.62}, {100, 0.5}};  // orange main, green second
  theme.intensity = 0.5;
  theme.grain = 0;
  CallFuture done;
  handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(space(), MojoTheme(theme)),
                        done.GetCallback());
  ASSERT_TRUE(IsOk(done.Take()));
  const std::string accent = *State().FindDict("spaces")->FindDict(space())->FindString("accent");
  EXPECT_EQ(accent, SpaceAccentHex(theme, "blue"));
  // The ring is the main color as a soft tint, never neon (a mint space ringed in #00ff52).
  const std::array<double, 3> hsl = RgbToHsl(*HexToRgb(accent));
  EXPECT_LE(hsl[1], 0.585);
  EXPECT_NEAR(hsl[2], 0.68, 0.01);
  const std::string want = content::EvalJs(frame(), content::JsReplace(R"(
      (() => { const d = document.createElement('div'); d.style.color = $1; document.body.append(d);
               const c = getComputedStyle(d).color; d.remove(); return c; })())",
                                                                      accent))
                               .ExtractString();
  std::string got;
  EXPECT_TRUE(base::test::RunUntil([&] {
    got = content::EvalJs(frame(), kFavoriteRing).ExtractString();
    return got == want + "|" + want;
  })) << got << " want ring " << want;
}

// The overlay (menus, the command bar, hover cards) wears the frame's chrome set, so a pastel space
// in dark mode gets light menus and a light command bar, and a mid color keeps them dark. The
// command bar selects in the main color as a muted mid tone (HSL lightness 68%, saturation at most 58%), never neon.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeOverlayFollowsTheSpaceLookAndSelectsMuted) {
  const std::string home = space();
  CluThemeHandler handler(window().GetWeakPtr());
  const auto set = [&](std::vector<ThemeColor> colors) {
    SpaceTheme t;
    t.colors = std::move(colors);
    t.intensity = 0.5;
    t.grain = 0;
    CallFuture done;
    handler.SetSpaceTheme(mojom::ThemeSetSpaceThemeInput::New(home, MojoTheme(t)), done.GetCallback());
    ASSERT_TRUE(IsOk(done.Take()));
  };
  content::WebContents* overlay = shell().overlay_contents();
  ASSERT_TRUE(overlay);
  ASSERT_TRUE(content::WaitForLoadStop(overlay));
  const std::string kChrome = "document.documentElement.dataset.theme ?? ''";
  const auto overlay_js = [&](const std::string& script) {
    return content::EvalJs(overlay, script).ExtractString();
  };

  SetMode(mojom::ThemeSetModeInputMode::kDark);
  set({{330, 0.95}});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), kChrome).ExtractString() == "light" && overlay_js(kChrome) == "light";
  }));
  // The selection: the accent's hue at lightness 68%, saturation at most 58%. Returns "ok", or "<l>,<s>" in percent.
  constexpr char kSelect[] = R"(
    (() => {
      const v = getComputedStyle(document.documentElement).getPropertyValue('--accent-select').trim();
      const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(v);
      if (!m) return 'none';
      const [r, g, b] = m.slice(1).map(x => parseInt(x, 16) / 255);
      const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2;
      const s = max === min ? 0 : (max - min) / (1 - Math.abs(2 * l - 1));
      const [lp, sp] = [Math.round(l * 100), Math.round(s * 100)];
      return lp === 68 && sp <= 59 ? 'ok' : [lp, sp].join();
    })())";
  ASSERT_TRUE(base::test::RunUntil([&] { return overlay_js(kSelect) != "none"; }));
  EXPECT_EQ(overlay_js(kSelect), "ok");
  // On the light chrome the command bar is frosted (86% of #f8f9f9), not opaque.
  constexpr char kPanel[] = R"(
    (() => {
      const d = document.createElement('div');
      d.className = 'clu-cmd';
      document.body.append(d);
      const c = getComputedStyle(d).backgroundColor;
      d.remove();
      return /[\/,] ?0\.86\)$/.test(c) ? 'translucent' : c;
    })())";
  EXPECT_EQ(overlay_js(kPanel), "translucent");

  // A mid color keeps the frame and the overlay dark in dark mode.
  set({{0, 0.4}});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), kChrome).ExtractString() == "dark" && overlay_js(kChrome) == "dark";
  }));
}

// The top row's toggle, back, forward and reload draw at absolute sizes, never scaled by the
// traffic lights: their svg boxes are 20, 16, 16 and 18 pt.
IN_PROC_BROWSER_TEST_F(CluThemeBrowserTest, CluThemeTopRowGlyphsAtAbsoluteSizes) {
  constexpr char kBoxes[] = R"(
    (() => ['sidebar-toggle', 'nav-back', 'nav-forward', 'nav-reload'].map(id => {
      const svg = document.querySelector(`[data-shell="${id}"] svg`);
      return svg ? Number(svg.getAttribute('width')).toFixed(2) : 'none';
    }).join())())";
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(), kBoxes).ExtractString() == "20.00,16.00,16.00,18.00";
  })) << content::EvalJs(frame(), kBoxes).ExtractString();
}

}  // namespace
}  // namespace clu
