// Copyright 2026 The Rio Authors. All rights reserved.

#include <memory>
#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/test/run_until.h"
#include "base/test/simple_test_clock.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/site/clu_site_page.h"
#include "clu/browser/site/clu_site_profile.h"
#include "clu/browser/site/clu_site_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_site_handlers.h"
#include "clu/browser/ui/webui/clu_window_site.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/base_window.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluTweaks* checks of docs/PLAN.md 12.15 on Chromium: Site tweaks CSS, JS and no-code controls as user-origin
// sheets and isolated-world script through Clu's component extension, live previews, the switch, zap mode with real
// mouse and key events, and the per-profile list the settings page reads.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kTweak[] = "/tweaks/tweak.html";
constexpr char kCsp[] = "/tweaks/csp.html";
constexpr char kRed[] = "rgb(255, 0, 0)";
constexpr char kRedCss[] = "h1{color:rgb(255,0,0)}";

class CluTweaksBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'800'000'000'000));
    SetCluSiteClockForTesting(&clock_);
  }

  void TearDownInProcessBrowserTestFixture() override {
    SetCluSiteClockForTesting(nullptr);
    InProcessBrowserTest::TearDownInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(base::test::RunUntil([&] { return site().loaded(); }));
    handler_ = std::make_unique<CluTweaksHandler>(window().GetWeakPtr());
  }

  void TearDownOnMainThread() override {
    handler_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluSiteService& site() { return *GetCluSiteService(browser()->GetProfile()); }
  CluTweaksHandler& tweaks() { return *handler_; }

  std::string Open(const std::string& path, const std::string& host = "") {
    const GURL url = host.empty() ? embedded_test_server()->GetURL(path)
                                  : embedded_test_server()->GetURL(host, path);
    base::expected<std::string, std::string> id =
        window().CreateRow(url, std::nullopt, /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return std::string();
    }
    EXPECT_TRUE(content::WaitForLoadStop(Contents(id.value())));
    return id.value();
  }

  content::WebContents* Contents(const std::string& row_id) {
    tabs::TabInterface* tab = window().FindTab(row_id);
    return tab ? tab->GetContents() : nullptr;
  }

  std::string Style(const std::string& row_id,
                    const char* selector,
                    const char* property) {
    return content::EvalJs(
               Contents(row_id),
               content::JsReplace(
                   "getComputedStyle(document.querySelector($1))[$2]",
                   selector, property))
        .ExtractString();
  }

  bool StyleIs(const std::string& row_id,
               const char* selector,
               const char* property,
               const std::string& value) {
    return base::test::RunUntil(
        [&] { return Style(row_id, selector, property) == value; });
  }

  base::Value TabField(const std::string& row_id, const char* key) {
    const base::DictValue state = window().CurrentState();
    const base::DictValue* tab = state.FindDict("tabs")->FindDict(row_id);
    const base::Value* value = tab ? tab->Find(key) : nullptr;
    return value ? value->Clone() : base::Value();
  }

  // A mark in the page's own world: a reload takes it away.
  void Mark(const std::string& row_id) {
    ASSERT_TRUE(content::ExecJs(Contents(row_id), "window.__cluMark = 1"));
  }
  bool StillMarked(const std::string& row_id) {
    return content::EvalJs(Contents(row_id), "window.__cluMark === 1")
        .ExtractBool();
  }

  mojom::CallResultPtr Save(const std::string& row_id,
                            const std::string& css,
                            const std::string& js,
                            mojom::TweakControlsPtr controls = nullptr) {
    CallFuture done;
    tweaks().Save(
        mojom::TweaksSaveInput::New(row_id, css, js, std::move(controls)),
        done.GetCallback());
    return done.Take();
  }

  // Saves JS and waits for the reload the save causes.
  void SaveJsAndWaitForReload(const std::string& row_id, const std::string& js) {
    content::TestNavigationObserver reload(Contents(row_id));
    ASSERT_TRUE(Save(row_id, "", js)->is_value_json());
    reload.Wait();
  }

  mojom::CallResultPtr Call(
      base::OnceCallback<void(base::OnceCallback<void(mojom::CallResultPtr)>)>
          call) {
    CallFuture done;
    std::move(call).Run(done.GetCallback());
    return done.Take();
  }

  static mojom::TweakControlsPtr Invert() {
    return mojom::TweakControls::New(0, true, 1, 1, 1, std::nullopt, 1);
  }

  gfx::Point CenterOf(const std::string& row_id, const char* selector) {
    const base::ListValue rect =
        content::EvalJs(Contents(row_id),
                        content::JsReplace(
                            "(() => { const r = document.querySelector($1)"
                            ".getBoundingClientRect(); return [r.x + r.width / "
                            "2, r.y + r.height / 2]; })()",
                            selector))
            .ExtractList()
            .Clone();
    return gfx::Point(static_cast<int>(rect[0].GetDouble()),
                      static_cast<int>(rect[1].GetDouble()));
  }

  void Click(const std::string& row_id, const char* selector,
             bool shift = false) {
    content::SimulateMouseClickAt(
        Contents(row_id),
        shift ? blink::WebInputEvent::kShiftKey : 0,
        blink::WebMouseEvent::Button::kLeft, CenterOf(row_id, selector));
  }

  // The picker's host: one extra element after body under the document element (closed shadow root).
  bool PickerShown(const std::string& row_id) {
    return content::EvalJs(Contents(row_id),
                           "document.documentElement.lastElementChild !== "
                           "document.body && "
                           "document.documentElement.lastElementChild."
                           "tagName === 'DIV'")
        .ExtractBool();
  }

  base::SimpleTestClock clock_;
  std::unique_ptr<CluTweaksHandler> handler_;
};

// 12.15: "Site tweaks CSS ... apply on commit, survive CSP": red at once, after a reload, in a second tab
// on the site, never on another site.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksCssAppliesInEveryPageOfTheSiteAndSurvivesCsp) {
  const std::string row = Open(kTweak);
  ASSERT_TRUE(Save(row, kRedCss, "")->is_value_json());
  EXPECT_TRUE(StyleIs(row, "h1", "color", kRed));
  EXPECT_EQ(TabField(row, "tweak"),
            base::JSONReader::Read(R"({"enabled":true,"hasCss":true,
                "hasJs":false,"hasControls":false,"zapCount":0})",
                                   base::JSON_PARSE_RFC)
                .value());

  content::TestNavigationObserver reload(Contents(row));
  Contents(row)->GetController().Reload(content::ReloadType::NORMAL, false);
  reload.Wait();
  EXPECT_TRUE(StyleIs(row, "h1", "color", kRed));

  const std::string second = Open(kTweak);
  EXPECT_TRUE(StyleIs(second, "h1", "color", kRed));
  const std::string locked = Open(kCsp);
  EXPECT_TRUE(StyleIs(locked, "h1", "color", kRed));

  const std::string other = Open(kTweak, "tracker.localhost");
  EXPECT_NE(Style(other, "h1", "color"), kRed);
  EXPECT_TRUE(TabField(other, "tweak").is_none());
}

// Advanced, "Enable Site tweaks on websites you visit": off, every page drops its saved CSS at once and a new page
// shows none; on brings them back. A page whose tweak has JS reloads each time, without the JS and then with it.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksTheAdvancedSwitchTurnsTweaksOffEverywhere) {
  const std::string row = Open(kTweak);
  ASSERT_TRUE(Save(row, kRedCss, "")->is_value_json());
  EXPECT_TRUE(StyleIs(row, "h1", "color", kRed));

  CluAppSettings::Get()->SetGlobal(kSiteTweaksEnabledKey, base::Value(false));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Style(row, "h1", "color") != kRed; }));
  const std::string second = Open(kTweak);
  EXPECT_NE(Style(second, "h1", "color"), kRed);

  CluAppSettings::Get()->SetGlobal(kSiteTweaksEnabledKey, base::Value(true));
  EXPECT_TRUE(StyleIs(row, "h1", "color", kRed));
  EXPECT_TRUE(StyleIs(second, "h1", "color", kRed));

  const std::string js_row = Open(kTweak, "js.localhost");
  SaveJsAndWaitForReload(js_row, "document.body.dataset.tweaked = '1'");
  auto tweaked = [&] {
    return content::EvalJs(Contents(js_row),
                           "document.body.dataset.tweaked || ''")
        .ExtractString();
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return tweaked() == "1"; }));
  {
    content::TestNavigationObserver reload(Contents(js_row));
    CluAppSettings::Get()->SetGlobal(kSiteTweaksEnabledKey,
                                     base::Value(false));
    reload.Wait();
  }
  EXPECT_EQ(tweaked(), "");
  {
    content::TestNavigationObserver reload(Contents(js_row));
    CluAppSettings::Get()->SetGlobal(kSiteTweaksEnabledKey, base::Value(true));
    reload.Wait();
  }
  EXPECT_TRUE(base::test::RunUntil([&] { return tweaked() == "1"; }));
}

// Site controls' switch turns a site without a tweak on (an empty tweak, on) and off again; off on such a site
// changes nothing.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksTheSwitchTurnsASiteWithoutATweakOn) {
  const std::string row = Open(kTweak);
  ASSERT_FALSE(site().Tweak("127.0.0.1"));
  auto set_enabled = [&](bool enabled) {
    return Call(base::BindOnce(&CluTweaksHandler::SetEnabled,
                               base::Unretained(&tweaks()),
                               mojom::TweaksSetEnabledInput::New(row, enabled)));
  };
  ASSERT_TRUE(set_enabled(false)->is_value_json());
  EXPECT_FALSE(site().Tweak("127.0.0.1"));

  ASSERT_TRUE(set_enabled(true)->is_value_json());
  ASSERT_TRUE(site().Tweak("127.0.0.1"));
  EXPECT_TRUE(site().Tweak("127.0.0.1")->enabled);
  EXPECT_TRUE(base::test::RunUntil([&] {
    const base::Value tweak = TabField(row, "tweak");
    return tweak.is_dict() && tweak.GetDict().FindBool("enabled") == true;
  }));
  // Nothing in it yet, so the page looks as it did.
  EXPECT_NE(Style(row, "h1", "color"), kRed);

  ASSERT_TRUE(set_enabled(false)->is_value_json());
  EXPECT_FALSE(site().Tweak("127.0.0.1")->enabled);
  EXPECT_TRUE(base::test::RunUntil([&] {
    const base::Value tweak = TabField(row, "tweak");
    return tweak.is_dict() && tweak.GetDict().FindBool("enabled") == false;
  }));
}

// In a short window, All Site Settings… keeps the panel inside the window: the panel ends above the window's
// bottom, nothing in it runs past its own bottom, and its settings scroll down to the zoom row.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksAllSiteSettingsStaysInsideAShortWindow) {
  browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1000, 560));
  Open(kTweak);
  CluShell& shell = *CluShell::From(browser());
  window().OpenSiteControls(gfx::Rect(200, 40, 28, 28));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell.IsOverlayVisible(); }));
  EXPECT_EQ(true, content::EvalJs(shell.overlay_contents(), R"(
      new Promise(resolve => {
        const wait = (find, then) => {
          const el = find();
          if (el) then(el); else setTimeout(() => wait(find, then), 20);
        };
        const panel = () => document.querySelector('.clu-site-controls');
        wait(() => panel()?.querySelector('[aria-label="More site controls"]'), (more) => {
          more.click();
          wait(() => [...document.querySelectorAll('[role="menuitem"]')]
                         .find((m) => m.textContent.trim() === 'All Site Settings…'), (item) => {
            item.click();
            wait(() => panel()?.querySelector('[data-more] .clu-zoom'), async (zoom) => {
              const el = panel();
              await Promise.all(el.getAnimations().map((a) => a.finished));
              const scroller = el.querySelector('[data-more]');
              scroller.scrollTop = scroller.scrollHeight;
              const box = el.getBoundingClientRect();
              const spill = [...el.children].some((c) => c.getBoundingClientRect().bottom > box.bottom + 0.5);
              const reset = [...zoom.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Reset');
              resolve(box.bottom <= innerHeight && !spill && !!reset &&
                      reset.getBoundingClientRect().bottom <= scroller.getBoundingClientRect().bottom + 0.5);
            });
          });
        });
      }))"));
}

// The Site controls panel is opaque, so the page never shows through it (the overlay widget has
// nothing behind it to blur), and All Site Settings… takes the popover's place instead of drawing a second panel
// over it; its back button brings the popover back.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksAllSiteSettingsReplacesAnOpaquePopover) {
  Open(kTweak);
  CluShell& shell = *CluShell::From(browser());
  window().OpenSiteControls(gfx::Rect(200, 40, 28, 28));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell.IsOverlayVisible(); }));
  EXPECT_EQ("opaque,replaced,back", content::EvalJs(shell.overlay_contents(), R"(
      new Promise(resolve => {
        const wait = (find, then) => {
          const el = find();
          if (el) then(el); else setTimeout(() => wait(find, then), 20);
        };
        const panel = () => document.querySelector('.clu-site-controls');
        const out = [];
        wait(() => panel()?.querySelector('[aria-label="More site controls"]'), async (more) => {
          const el = panel();
          await Promise.all(el.getAnimations().map((a) => a.finished));
          const c = getComputedStyle(el).backgroundColor;
          const see = /\/\s*(0?\.\d+|0)\s*\)$/.test(c) || /^rgba\(.*,\s*(0?\.\d+|0)\)$/.test(c);
          out.push(see || getComputedStyle(el).opacity !== '1' ? 'see-through ' + c : 'opaque');
          more.click();
          wait(() => [...document.querySelectorAll('[role="menuitem"]')]
                         .find((m) => m.textContent.trim() === 'All Site Settings…'), (item) => {
            item.click();
            wait(() => panel()?.querySelector('[data-more]'), () => {
              const p = panel();
              out.push(!p.querySelector('[aria-label="Page actions"]') &&
                       !p.querySelector('[aria-label="More site controls"]') ? 'replaced' : 'overlaps');
              p.querySelector('[aria-label="Back to site controls"]').click();
              wait(() => panel()?.querySelector('[aria-label="Page actions"]'), () => {
                out.push(panel().querySelector('[data-more]') ? 'both' : 'back');
                resolve(out.join(','));
              });
            });
          });
        });
      }))"));
}

// A page restored from the back/forward cache still has the sheet of its first load: switching the tweak off while
// away removes it on the way back, and a later switch works on it.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksAPageBackFromTheCacheFollowsTheTweak) {
  const std::string row = Open(kTweak);
  ASSERT_TRUE(Save(row, kRedCss, "")->is_value_json());
  ASSERT_TRUE(StyleIs(row, "h1", "color", kRed));
  Mark(row);
  ASSERT_TRUE(content::NavigateToURL(
      Contents(row),
      embedded_test_server()->GetURL(base::StrCat({kTweak, "?b"}))));
  ASSERT_TRUE(StyleIs(row, "h1", "color", kRed));
  auto set_enabled = [&](bool enabled) {
    return Call(base::BindOnce(&CluTweaksHandler::SetEnabled,
                               base::Unretained(&tweaks()),
                               mojom::TweaksSetEnabledInput::New(row, enabled)));
  };
  ASSERT_TRUE(set_enabled(false)->is_value_json());
  ASSERT_TRUE(StyleIs(row, "h1", "color", "rgb(20, 20, 20)"));

  content::TestNavigationObserver back(Contents(row));
  Contents(row)->GetController().GoBack();
  back.Wait();
  // The first page itself, out of the cache.
  ASSERT_TRUE(StillMarked(row));
  EXPECT_TRUE(StyleIs(row, "h1", "color", "rgb(20, 20, 20)"));
  ASSERT_TRUE(set_enabled(true)->is_value_json());
  EXPECT_TRUE(StyleIs(row, "h1", "color", kRed));
  ASSERT_TRUE(set_enabled(false)->is_value_json());
  EXPECT_TRUE(StyleIs(row, "h1", "color", "rgb(20, 20, 20)"));
}

// 12.15: JS runs once per document in an isolated world (the page cannot see its globals), after the reload a JS
// change causes, even under a CSP that allows nothing.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksJsRunsInAnIsolatedWorldAfterTheReload) {
  const std::string row = Open(kTweak);
  SaveJsAndWaitForReload(
      row, "document.body.dataset.tweaked = '1'; var tweakGlobal = 1;");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(Contents(row), "document.body.dataset.tweaked")
               .ExtractString() == "1";
  }));
  EXPECT_EQ(content::EvalJs(Contents(row), "typeof window.tweakGlobal")
                .ExtractString(),
            "undefined");
  const std::string locked = Open(kCsp);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(Contents(locked), "document.body.dataset.tweaked")
               .ExtractString() == "1";
  }));
}

// 12.15: "toggle without reload for CSS and controls"; a tweak with JS reloads.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksToggleWithoutReloadForCssAndControls) {
  const std::string row = Open(kTweak);
  ASSERT_TRUE(Save(row, kRedCss, "", Invert())->is_value_json());
  ASSERT_TRUE(StyleIs(row, "h1", "color", kRed));
  ASSERT_TRUE(StyleIs(row, "html", "filter", "invert(1)"));
  Mark(row);
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::SetEnabled,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksSetEnabledInput::New(row, false)))
                  ->is_value_json());
  EXPECT_TRUE(StyleIs(row, "h1", "color", "rgb(20, 20, 20)"));
  EXPECT_TRUE(StyleIs(row, "html", "filter", "none"));
  EXPECT_TRUE(StillMarked(row));
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::SetEnabled,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksSetEnabledInput::New(row, true)))
                  ->is_value_json());
  EXPECT_TRUE(StyleIs(row, "h1", "color", kRed));

  // With JS, off reloads and the script's mark is gone.
  SaveJsAndWaitForReload(row, "document.body.dataset.tweaked = '1'");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(Contents(row), "document.body.dataset.tweaked")
               .ExtractString() == "1";
  }));
  content::TestNavigationObserver reload(Contents(row));
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::SetEnabled,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksSetEnabledInput::New(row, false)))
                  ->is_value_json());
  reload.Wait();
  EXPECT_EQ(content::EvalJs(Contents(row),
                            "document.body.dataset.tweaked === undefined")
                .ExtractBool(),
            true);
}

// 12.15: "preview live"; closing the editor without saving restores the saved CSS.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksTheEditorPreviewsLiveAndCloseRestores) {
  const std::string row = Open(kTweak);
  ASSERT_TRUE(Save(row, kRedCss, "")->is_value_json());
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::OpenEditor,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksOpenEditorInput::New(row)))
                  ->is_value_json());
  std::string instance;
  {
    const base::DictValue state = window().CurrentState();
    for (const base::Value& surface :
         *state.FindDict("windows")
              ->FindDict(window().window_id())
              ->FindListByDottedPath("overlay.surfaces")) {
      if (*surface.GetDict().FindString("surface") == "tweak-editor") {
        instance = *surface.GetDict().FindString("instanceId");
        EXPECT_EQ(*surface.GetDict().FindStringByDottedPath("props.site"),
                  "127.0.0.1");
      }
    }
  }
  ASSERT_FALSE(instance.empty());
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::PreviewCss,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksPreviewCssInput::New(
                                      row, "h1{color:rgb(0,0,255)}")))
                  ->is_value_json());
  EXPECT_TRUE(StyleIs(row, "h1", "color", "rgb(0, 0, 255)"));
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::PreviewControls,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksPreviewControlsInput::New(
                                      row, Invert())))
                  ->is_value_json());
  EXPECT_TRUE(StyleIs(row, "html", "filter", "invert(1)"));
  // Nothing was stored.
  EXPECT_EQ(site().Tweak("127.0.0.1")->css, kRedCss);
  EXPECT_FALSE(site().Tweak("127.0.0.1")->controls);
  ASSERT_TRUE(window().CloseOverlayInstance(instance));
  EXPECT_TRUE(StyleIs(row, "h1", "color", kRed));
  EXPECT_TRUE(StyleIs(row, "html", "filter", "none"));
}

// docs/chromium/fixes.md (m7.md sections 5 and 7): the top overlay shrinks to the docked editor, so the page beside it
// takes clicks; the editor draws from that smaller overlay's origin, and anything else in the top layer covers the
// window again.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksTheEditorLeavesThePageBesideItClickable) {
  const std::string row = Open(kTweak);
  CluShell& shell = *CluShell::From(browser());
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::OpenEditor,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksOpenEditorInput::New(row)))
                  ->is_value_json());
  ASSERT_TRUE(shell.IsOverlayVisible());
  const base::DictValue state = window().CurrentState();
  const base::DictValue& win =
      *state.FindDict("windows")->FindDict(window().window_id());
  std::optional<gfx::Rect> editor;
  for (const base::Value& surface :
       *win.FindListByDottedPath("overlay.surfaces")) {
    const base::DictValue& dict = surface.GetDict();
    if (*dict.FindString("surface") == "tweak-editor") {
      editor = gfx::Rect(*dict.FindIntByDottedPath("rect.x"),
                         *dict.FindIntByDottedPath("rect.y"),
                         *dict.FindIntByDottedPath("rect.width"),
                         *dict.FindIntByDottedPath("rect.height"));
    }
  }
  ASSERT_TRUE(editor && !editor->IsEmpty());
  // The overlay covers the editor only, and state says so.
  EXPECT_EQ(shell.top_overlay_rect(), *editor);
  EXPECT_EQ(win.FindIntByDottedPath("overlay.topRect.x"), editor->x());
  EXPECT_EQ(win.FindIntByDottedPath("overlay.topRect.width"),
            editor->width());
  const gfx::Point origin =
      BrowserView::GetBrowserViewForBrowser(browser())->GetBoundsInScreen().origin();
  const gfx::Rect on_screen =
      shell.overlay_widget_for_testing()->GetWindowBoundsInScreen();
  EXPECT_EQ(on_screen,
            gfx::Rect(origin + editor->OffsetFromOrigin(), editor->size()));
  // The page card left of the editor, and the sidebar, are outside it.
  const gfx::Rect card = shell.layout().page_rect;
  EXPECT_FALSE(on_screen.Contains(
      origin + gfx::Vector2d(card.x() + 40, card.CenterPoint().y())));
  EXPECT_FALSE(on_screen.Contains(origin + gfx::Vector2d(40, 300)));
  // The editor fills the smaller overlay from its origin.
  EXPECT_EQ(true, content::EvalJs(shell.overlay_contents(), content::JsReplace(R"(
      new Promise(resolve => {
        const check = () => {
          const el = document.querySelector('[data-tweak-editor]');
          const r = el && el.getBoundingClientRect();
          if (r && r.x === 0 && r.y === 0 && Math.round(r.width) === $1) {
            resolve(true);
          } else {
            setTimeout(check, 20);
          }
        };
        check();
      }))",
                                                                   editor->width())));

  // A prompt replaces it (one prompt at a time) and covers the whole window again.
  window().OpenPrompt("pinned-url", base::DictValue());
  EXPECT_EQ(shell.top_overlay_rect(), std::nullopt);
  EXPECT_EQ(shell.overlay_widget_for_testing()->GetWindowBoundsInScreen(),
            BrowserView::GetBrowserViewForBrowser(browser())->GetBoundsInScreen());
}

// 12.15: zap hides with a real click (the page's own handler never fires), Cmd+Z undoes, Esc ends and the zap stays
// after a reload, Shift+click hides the related cards, a page-made click does nothing, Restore all brings them back,
// and the bar shows in the page with the sidebar hidden.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksZapHidesUndoesRestoresAndIgnoresSyntheticClicks) {
  const std::string row = Open(kTweak);
  ASSERT_TRUE(window().RunCommand("sidebar.toggle"));
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::StartZap,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksStartZapInput::New(row)))
                  ->is_value_json());
  ASSERT_TRUE(base::test::RunUntil([&] { return PickerShown(row); }));
  EXPECT_EQ(*TabField(row, "zapMode").GetDict().FindBool("active"), true);

  // A click the page makes is not trusted: nothing is zapped, and the page sees its own click.
  content::ExecuteScriptAsync(Contents(row),
                              "document.getElementById('promo').click()");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(Contents(row), "window.pageClicks").ExtractInt() == 1;
  }));
  EXPECT_EQ(Style(row, "#promo", "display"), "block");

  Click(row, "#promo");
  EXPECT_TRUE(StyleIs(row, "#promo", "display", "none"));
  EXPECT_EQ(content::EvalJs(Contents(row), "window.pageClicks").ExtractInt(), 1);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return site().Tweak("127.0.0.1") &&
           site().Tweak("127.0.0.1")->zaps.size() == 1u;
  }));
  EXPECT_EQ(site().Tweak("127.0.0.1")->zaps[0].created_at, 1'800'000'000'000);

  content::SimulateKeyPress(Contents(row), ui::DomKey::FromCharacter('z'),
                            ui::DomCode::US_Z, ui::VKEY_Z, false, false, false,
                            /*command=*/true);
  EXPECT_TRUE(StyleIs(row, "#promo", "display", "block"));

  Click(row, "#promo");
  EXPECT_TRUE(StyleIs(row, "#promo", "display", "none"));
  Click(row, ".card", /*shift=*/true);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(Contents(row),
                           "[...document.querySelectorAll('.card')].every(c => "
                           "getComputedStyle(c).display === 'none')")
        .ExtractBool();
  }));
  EXPECT_EQ(*TabField(row, "zapMode").GetDict().FindString("lastResult"),
            "Hid 3 related elements");

  content::SimulateKeyPress(Contents(row), ui::DomKey::ESCAPE,
                            ui::DomCode::ESCAPE, ui::VKEY_ESCAPE, false, false,
                            false, false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabField(row, "zapMode").is_none(); }));
  EXPECT_FALSE(PickerShown(row));
  content::TestNavigationObserver reload(Contents(row));
  Contents(row)->GetController().Reload(content::ReloadType::NORMAL, false);
  reload.Wait();
  EXPECT_TRUE(StyleIs(row, "#promo", "display", "none"));

  // Restore all (Site controls).
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::ClearZaps,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksClearZapsInput::New(row)))
                  ->is_value_json());
  EXPECT_TRUE(StyleIs(row, "#promo", "display", "block"));
}

// Navigating the tab ends zap mode, and the banner goes with it.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest, CluTweaksNavigatingEndsZapMode) {
  const std::string row = Open(kTweak);
  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::StartZap,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksStartZapInput::New(row)))
                  ->is_value_json());
  ASSERT_TRUE(base::test::RunUntil([&] { return PickerShown(row); }));
  ASSERT_TRUE(content::NavigateToURL(
      Contents(row), embedded_test_server()->GetURL("/tabs/a.html")));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return TabField(row, "zapMode").is_none(); }));
}

// 12.15: "Settings lists every site's tweaks per profile with toggle and delete" (the settings page itself is M8;
// these are the commands it calls).
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest,
                       CluTweaksTheSettingsListTogglesAndDeletes) {
  const std::string a = Open(kTweak);
  ASSERT_TRUE(Save(a, kRedCss, "")->is_value_json());
  const std::string b = Open(kTweak, "tracker.localhost");
  SaveJsAndWaitForReload(b, "document.body.dataset.tweaked = '1'");
  const std::string profile_id = window().site().profile_id();
  auto list = [&] {
    mojom::CallResultPtr r = Call(base::BindOnce(
        &CluTweaksHandler::List, base::Unretained(&tweaks()),
        mojom::TweaksListInput::New(profile_id)));
    return base::JSONReader::Read(r->get_value_json(), base::JSON_PARSE_RFC)
        .value();
  };
  base::Value rows = list();
  ASSERT_EQ(rows.GetList().size(), 2u);
  EXPECT_EQ(*rows.GetList()[0].GetDict().FindString("site"), "127.0.0.1");
  EXPECT_EQ(*rows.GetList()[1].GetDict().FindString("site"),
            "tracker.localhost");
  EXPECT_EQ(rows.GetList()[1].GetDict().FindBool("hasJs"), true);
  EXPECT_EQ(rows.GetList()[0].GetDict().FindDouble("updatedAt"),
            1'800'000'000'000);

  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::SetEnabledForSite,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksSetEnabledForSiteInput::New(
                                      profile_id, "127.0.0.1", false)))
                  ->is_value_json());
  EXPECT_TRUE(StyleIs(a, "h1", "color", "rgb(20, 20, 20)"));
  EXPECT_EQ(list().GetList()[0].GetDict().FindBool("enabled"), false);

  ASSERT_TRUE(Call(base::BindOnce(&CluTweaksHandler::DeleteForSite,
                                  base::Unretained(&tweaks()),
                                  mojom::TweaksDeleteForSiteInput::New(
                                      profile_id, "127.0.0.1")))
                  ->is_value_json());
  EXPECT_EQ(list().GetList().size(), 1u);
  // Another profile's id names nothing here.
  mojom::CallResultPtr other = Call(base::BindOnce(
      &CluTweaksHandler::List, base::Unretained(&tweaks()),
      mojom::TweaksListInput::New("00000000-0000-4000-8000-0000000000bb")));
  ASSERT_TRUE(other->is_error());
  EXPECT_EQ(other->get_error()->code, mojom::ErrorCode::kNotFound);
}

// A tweak over 256 KiB is refused with nothing stored; a page with no site has no tweak.
IN_PROC_BROWSER_TEST_F(CluTweaksBrowserTest, CluTweaksJunkIsRefused) {
  const std::string row = Open(kTweak);
  mojom::CallResultPtr big = Save(row, std::string(256 * 1024 + 1, 'a'), "");
  ASSERT_TRUE(big->is_error());
  EXPECT_EQ(big->get_error()->code, mojom::ErrorCode::kBadPayload);
  EXPECT_FALSE(site().Tweak("127.0.0.1"));
  const std::string blank =
      window().CreateRow(GURL("about:blank"), std::nullopt, true).value_or("");
  mojom::CallResultPtr no_site = Save(blank, kRedCss, "");
  ASSERT_TRUE(no_site->is_error());
  EXPECT_EQ(no_site->get_error()->reason, "no-site");
}

}  // namespace
}  // namespace clu
