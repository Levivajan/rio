// Copyright 2026 The Rio Authors. All rights reserved.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_look.h"
#include "clu/browser/ui/views/frame/clu_look_tokens.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/base_window.h"
#include "ui/base/hit_test.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/gfx/scrollbar_size.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// CluLook*: the M11 "Done when" row (docs/chromium/PLAN.md section 6) in the real browser. The native side (traffic
// lights, the page view's inset and radius, dialogs, the find bar, Chromium's colors) is read from Chromium; the
// frame and overlay WebUIs run the same probes as the WebUI harness (clu/test/data/look/measure.js), with pixels from
// a copy of their surfaces. Every reading is judged here against clu_look_tokens.h, and written with its screenshot to
// $CLU_SHOTS_DIR/chromium-m11/ for the TS comparator (scripts/chromium/look-compare.ts) to judge the same way.

// AppKit's private reveal amount, which it sets on each step of the full-screen titlebar's slide.
@interface NSTitlebarAccessoryViewController ()
- (void)setRevealAmount:(double)revealAmount;
@end

namespace clu {
namespace {

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

int ChannelDelta(SkColor a, SkColor b) {
  auto channel = [](unsigned v) { return static_cast<int>(v); };
  return std::max({std::abs(channel(SkColorGetR(a)) - channel(SkColorGetR(b))),
                   std::abs(channel(SkColorGetG(a)) - channel(SkColorGetG(b))),
                   std::abs(channel(SkColorGetB(a)) - channel(SkColorGetB(b)))});
}

// `over` (ARGB) composited onto the opaque `base`, as derive.ts blend() does.
SkColor Blend(uint32_t over, SkColor base) {
  const double a = SkColorGetA(over) / 255.0;
  auto mix = [a](unsigned o, unsigned b) {
    return static_cast<U8CPU>(std::lround(b * (1 - a) + o * a));
  };
  return SkColorSetRGB(mix(SkColorGetR(over), SkColorGetR(base)),
                       mix(SkColorGetG(over), SkColorGetG(base)),
                       mix(SkColorGetB(over), SkColorGetB(base)));
}

// `share` of the tint over white (the empty page, tooltips).
SkColor TintOverWhite(SkColor tint, double share) {
  return Blend(SkColorSetA(tint, static_cast<U8CPU>(std::lround(share * 255))),
               SK_ColorWHITE);
}

base::ListValue ColorList(SkColor c) {
  base::ListValue list;
  list.Append(static_cast<int>(SkColorGetR(c)));
  list.Append(static_cast<int>(SkColorGetG(c)));
  list.Append(static_cast<int>(SkColorGetB(c)));
  return list;
}

std::optional<SkColor> ColorOf(const base::Value* v) {
  if (!v || !v->is_list() || v->GetList().size() != 3) {
    return std::nullopt;
  }
  // A channel read back from JSON may be an int or a double; anything else is no color.
  std::array<U8CPU, 3> c;
  for (size_t i = 0; i < 3; ++i) {
    const base::Value& ch = v->GetList()[i];
    if (!ch.is_int() && !ch.is_double()) {
      return std::nullopt;
    }
    c[i] = static_cast<U8CPU>(std::clamp<long>(std::lround(ch.GetDouble()), 0, 255));
  }
  return SkColorSetRGB(c[0], c[1], c[2]);
}

// The mean of a 3 x 3 device-pixel patch around the DIP point, like png.ts colorAt().
std::optional<SkColor> Sample(const SkBitmap& bitmap, double scale, double x,
                              double y) {
  const int cx = static_cast<int>(std::lround(x * scale));
  const int cy = static_cast<int>(std::lround(y * scale));
  int r = 0, g = 0, b = 0, n = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int px = cx + dx, py = cy + dy;
      if (px < 0 || py < 0 || px >= bitmap.width() || py >= bitmap.height()) {
        continue;
      }
      const SkColor c = bitmap.getColor(px, py);
      r += SkColorGetR(c);
      g += SkColorGetG(c);
      b += SkColorGetB(c);
      ++n;
    }
  }
  if (!n) {
    return std::nullopt;
  }
  return SkColorSetRGB(r / n, g / n, b / n);
}

class CluLookBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // The targets are the light set; the Mac's own appearance must not change what the tests see.
    ThemeServiceFactory::GetForProfile(browser()->GetProfile())
        ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    // The targets are set for a 1280 x 800 window; the window-relative ones (the command bar's place) use it.
    browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1280, 800));
    views::test::RunScheduledLayout(&browser_view());
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().overlay_contents()));
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  BrowserView& browser_view() {
    return *BrowserView::GetBrowserViewForBrowser(browser());
  }
  NSWindow* ns_window() {
    return browser_view().GetWidget()->GetNativeWindow().GetNativeNSWindow();
  }

  void OpenTabs() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/tabs/a.html")));
    for (const char* path : {"/tabs/b.html", "/tabs/c.html"}) {
      ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
          browser(), embedded_test_server()->GetURL(path),
          WindowOpenDisposition::kNewForegroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    }
    browser()->GetTabStripModel()->ActivateTabAt(0);
    EXPECT_EQ(true, content::EvalJs(shell().frame_contents(),
                                    content::JsReplace(kWaitForSelector,
                                                       ".clu-tab-tree .clu-tab-row[data-active=\"false\"]")));
    views::test::RunScheduledLayout(&browser_view());
  }

  // The window buttons' frames in window points, top-left origin.
  std::vector<gfx::RectF> TrafficLights() {
    NSWindow* window = ns_window();
    const CGFloat height = NSHeight(window.frame);
    std::vector<gfx::RectF> rects;
    for (NSWindowButton kind :
         {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
      NSButton* button = [window standardWindowButton:kind];
      const NSRect r = [button convertRect:button.bounds toView:nil];
      rects.emplace_back(NSMinX(r), height - NSMaxY(r), NSWidth(r),
                         NSHeight(r));
    }
    return rects;
  }

  // Every native reading, keyed like the targets' probe ids (native|native|<name>).
  base::DictValue NativeReadings() {
    views::test::RunScheduledLayout(&browser_view());
    std::map<std::string, double> v;
    const std::vector<gfx::RectF> lights = TrafficLights();
    v["traffic-light-0-cx"] = lights[0].CenterPoint().x();
    v["traffic-light-0-cy"] = lights[0].CenterPoint().y();
    v["traffic-light-pitch"] =
        lights[1].CenterPoint().x() - lights[0].CenterPoint().x();
    v["traffic-light-diameter"] = lights[0].width();

    const gfx::Rect contents = browser_view().multi_contents_view()->bounds();
    v["page-view-top"] = contents.y();
    v["page-view-right"] = browser_view().width() - contents.right();
    v["page-view-bottom"] = browser_view().height() - contents.bottom();
    v["page-view-radius"] =
        browser_view().multi_contents_view()->GetBackgroundRadii().lower_right();

    v["dialog-top"] = browser_view()
                          .GetWebContentsModalDialogHost()
                          ->GetDialogPosition(gfx::Size(300, 200))
                          .y();
    const gfx::Rect find = browser_view().GetFindBarBoundingBox();
    v["find-bar-top"] = find.y();
    v["find-bar-right"] =
        browser_view().width() - find.right() - gfx::scrollbar_size();

    base::DictValue readings;
    for (const auto& [name, value] : v) {
      readings.Set(base::StrCat({"native|native|", name}), value);
    }
    const SkColor popover = browser_view().GetColorProvider()->GetColor(
        ui::kColorBubbleBackground);
    readings.Set("native|native|color-popover", ColorList(popover));
    return readings;
  }

  // Runs measure.js with `state`'s probes in the frame and overlay WebUIs and resolves pixel probes from copies of
  // their surfaces. Returns the readings; `shots` gets each document's bitmap.
  base::DictValue MeasureWebUIs(const std::string& state,
                                std::map<std::string, SkBitmap>* shots) {
    base::ScopedAllowBlockingForTesting allow;
    std::string script;
    EXPECT_TRUE(base::ReadFileToString(
        base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
            .AppendASCII("clu/test/data/look/measure.js"),
        &script));
    std::optional<base::Value> probes =
        base::JSONReader::Read(look::kProbesJson, base::JSON_PARSE_RFC);
    CHECK(probes && probes->is_dict());
    const base::DictValue* by_doc = probes->GetDict().FindDict(state);
    CHECK(by_doc) << "no probes for " << state;
    base::DictValue readings;
    for (const char* doc : {"frame", "overlay"}) {
      const base::ListValue* list = by_doc->FindList(doc);
      if (!list || list->empty()) {
        continue;
      }
      content::WebContents* contents = std::string(doc) == "frame"
                                           ? shell().frame_contents()
                                           : shell().overlay_contents();
      std::optional<std::string> json = base::WriteJson(*list);
      const content::EvalJsResult result = content::EvalJs(
          contents, base::StrCat({script, "\n;globalThis.cluLookMeasure(",
                                  *json, ")"}));
      EXPECT_TRUE(result.is_ok()) << result.ExtractError();
      const SkBitmap bitmap = Copy(contents);
      (*shots)[doc] = bitmap;
      const double scale =
          bitmap.width() /
          std::max(1.0, static_cast<double>(contents->GetContainerBounds().width()));
      const base::DictValue& measured = result.ExtractDict();
      for (const auto [id, value] : measured) {
        if (value.is_dict()) {
          const base::ListValue* at = value.GetDict().FindList("at");
          std::optional<SkColor> c =
              at && at->size() == 2
                  ? Sample(bitmap, scale, (*at)[0].GetDouble(),
                           (*at)[1].GetDouble())
                  : std::nullopt;
          readings.Set(id, c ? base::Value(ColorList(*c)) : base::Value());
        } else {
          readings.Set(id, value.Clone());
        }
      }
    }
    return readings;
  }

  SkBitmap Copy(content::WebContents* contents) {
    SkBitmap bitmap;
    base::RunLoop loop;
    contents->GetRenderWidgetHostView()->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::TimeDelta(),
        base::BindLambdaForTesting(
            [&](const content::CopyFromSurfaceResult& result) {
              if (result.has_value()) {
                bitmap = result->bitmap;
              }
              loop.Quit();
            }));
    loop.Run();
    return bitmap;
  }

  // The context the expected colors come from: the tint above the top row and the command bar panel.
  base::DictValue Context(const std::map<std::string, SkBitmap>& shots) {
    const int sidebar = shell().layout().effective_width;
    SkColor tint = SK_ColorWHITE;
    if (auto it = shots.find("frame"); it != shots.end() && !it->second.drawsNothing()) {
      const double scale = it->second.width() /
                           std::max(1.0, static_cast<double>(shell().frame_view()->width()));
      tint = Sample(it->second, scale, sidebar / 2.0, 3).value_or(SK_ColorWHITE);
    }
    base::DictValue ctx;
    ctx.Set("tint", ColorList(tint));
    ctx.Set("panel", ColorList(look::kLight.cmd_bg));
    ctx.Set("sidebarWidth", sidebar);
    ctx.Set("windowHeight", browser_view().height());
    return ctx;
  }

  // Judges `readings` against every target of `state`, as compare.ts does. Returns the failures.
  std::vector<std::string> Judge(const std::string& state,
                                 const base::DictValue& readings,
                                 const base::DictValue& context) {
    const SkColor tint = ColorOf(context.Find("tint")).value_or(SK_ColorWHITE);
    std::vector<std::string> failures;
    for (const look::Target& t : look::kTargets) {
      // A fresh profile has one space, so the switcher's pitch has no second icon to measure; the WebUI harness
      // (three spaces) covers it.
      if (t.state != state || t.key == "spaces.switcherPitch") {
        continue;
      }
      const base::Value* reading = readings.Find(t.probe_id);
      bool pass = false;
      std::string expected;
      if (t.unit == look::TargetUnit::kColor ||
          t.unit == look::TargetUnit::kTintShare) {
        const SkColor base = t.base == look::TargetBase::kTint    ? tint
                             : t.base == look::TargetBase::kPanel ? look::kLight.cmd_bg
                                                                  : SK_ColorWHITE;
        const SkColor want = t.unit == look::TargetUnit::kTintShare
                                 ? TintOverWhite(tint, t.value)
                                 : Blend(t.color, base);
        const std::optional<SkColor> got = ColorOf(reading);
        pass = got && ChannelDelta(*got, want) <= t.tolerance;
        expected = base::StringPrintf("#%06x", want & 0xFFFFFFu);
      } else {
        pass = reading && reading->is_double() &&
               std::abs(reading->GetDouble() - t.value) <= t.tolerance;
        pass |= reading && reading->is_int() &&
                std::abs(reading->GetInt() - t.value) <= t.tolerance;
        expected = base::NumberToString(t.value);
      }
      if (!pass) {
        failures.push_back(base::StrCat(
            {std::string(t.key), " [", std::string(t.probe_id), "] expected ",
             expected, " got ",
             reading ? reading->DebugString() : std::string("missing")}));
      }
    }
    return failures;
  }

  // Writes <state>.json (readings and context, the shape look.webui.ts writes) and the screenshots.
  void Write(const std::string& state, const base::DictValue& readings,
             const base::DictValue& context,
             const std::map<std::string, SkBitmap>& shots) {
    std::optional<std::string> dir =
        base::Environment::Create()->GetVar("CLU_SHOTS_DIR");
    if (!dir) {
      return;
    }
    base::ScopedAllowBlockingForTesting allow;
    const base::FilePath out =
        base::FilePath::FromUTF8Unsafe(*dir).AppendASCII("chromium-m11");
    base::CreateDirectory(out);
    base::DictValue report;
    report.Set("state", state);
    report.Set("context", context.Clone());
    report.Set("readings", readings.Clone());
    base::WriteFile(out.AppendASCII("native-" + state + ".json"),
                    base::WriteJson(report).value_or("{}"));
    for (const auto& [doc, bitmap] : shots) {
      if (std::optional<std::vector<uint8_t>> png =
              gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false)) {
        base::WriteFile(out.AppendASCII("native-" + state + "-" + doc + ".png"),
                        *png);
      }
    }
  }

  void CheckState(const std::string& state) {
    std::map<std::string, SkBitmap> shots;
    base::DictValue readings = state == "native" ? NativeReadings()
                                                 : MeasureWebUIs(state, &shots);
    const base::DictValue context = Context(shots);
    Write(state, readings, context, shots);
    const std::vector<std::string> failures = Judge(state, readings, context);
    EXPECT_TRUE(failures.empty()) << state << ":\n"
                                  << base::JoinString(failures, "\n");
  }
};

IN_PROC_BROWSER_TEST_F(CluLookBrowserTest,
                       CluLookPageViewIsTheInsetRoundedCard) {
  OpenTabs();
  const gfx::Rect contents = browser_view().multi_contents_view()->bounds();
  EXPECT_EQ(contents.y(), look::kCardInset);
  EXPECT_EQ(browser_view().width() - contents.right(), look::kCardInset);
  EXPECT_EQ(browser_view().height() - contents.bottom(), look::kCardInset);
  EXPECT_EQ(contents.x(), shell().layout().effective_width);
  const gfx::RoundedCornersF radii =
      browser_view().multi_contents_view()->GetBackgroundRadii();
  EXPECT_FLOAT_EQ(radii.upper_left(), look::kCardRadius);
  EXPECT_FLOAT_EQ(radii.lower_right(), look::kCardRadius);

  // With the sidebar hidden the card keeps the inset on all four sides; focus mode squares it.
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), FindCommand("sidebar.toggle")->idc));
  // Hiding tweens the card over 80 ms.
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  views::test::RunScheduledLayout(&browser_view());
  EXPECT_EQ(browser_view().multi_contents_view()->x(), look::kCardInset);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), FindCommand("sidebar.toggle")->idc));
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  shell().ToggleFocusMode();
  views::test::RunScheduledLayout(&browser_view());
  EXPECT_FLOAT_EQ(
      browser_view().multi_contents_view()->GetBackgroundRadii().upper_left(), 0);
  shell().ToggleFocusMode();
}

IN_PROC_BROWSER_TEST_F(CluLookBrowserTest,
                       CluLookTrafficLightsSitOnTheTopRowAndFollowTheSidebar) {
  // macOS lays the buttons out from the titlebar height Clu gives it (clu::GetTitlebarHeight).
  ASSERT_TRUE(base::test::RunUntil([&] {
    return std::abs(TrafficLights()[0].CenterPoint().y() -
                    look::kTrafficLightCenterY) <= 1;
  }));
  const std::vector<gfx::RectF> lights = TrafficLights();
  EXPECT_NEAR(lights[0].CenterPoint().x(), look::kTrafficLightFirstCenterX, 1);
  EXPECT_NEAR(lights[1].CenterPoint().x() - lights[0].CenterPoint().x(),
              look::kTrafficLightPitch, 1);
  EXPECT_NEAR(lights[2].CenterPoint().y(), look::kTrafficLightCenterY, 1);
  // The sidebar toggle's center shares the row (the frame WebUI's top row).
  EXPECT_EQ(true, content::EvalJs(shell().frame_contents(),
                                  content::JsReplace(kWaitForSelector,
                                                     "[data-shell=\"sidebar-toggle\"]")));
  const double toggle_cy =
      content::EvalJs(shell().frame_contents(),
                      "(() => { const r = document.querySelector("
                      "'[data-shell=\"sidebar-toggle\"]').getBoundingClientRect();"
                      " return r.top + r.height / 2; })()")
          .ExtractDouble();
  EXPECT_NEAR(toggle_cy, look::kTrafficLightCenterY, 1);

  // Hidden with the sidebar, back with it.
  NSButton* close = [ns_window() standardWindowButton:NSWindowCloseButton];
  shell().ToggleSidebar();
  EXPECT_TRUE(close.hidden);
  shell().ToggleSidebar();
  EXPECT_FALSE(close.hidden);
}

// In full screen macOS reveals the window buttons at the top when the pointer gets there, inside the
// window, where they sat before. The top row keeps its spacer, so the sidebar toggle (and its tooltip, which hangs
// right of the pointer) starts after the zoom button in both modes and does not move when the buttons hide.
IN_PROC_BROWSER_TEST_F(CluLookBrowserTest,
                       CluLookFullScreenKeepsTheTopRowAfterTheWindowButtons) {
  EXPECT_EQ(true, content::EvalJs(shell().frame_contents(),
                                  content::JsReplace(kWaitForSelector,
                                                     "[data-shell=\"sidebar-toggle\"]")));
  static constexpr char kToggleLeft[] =
      "document.querySelector('[data-shell=\"sidebar-toggle\"]')"
      ".getBoundingClientRect().left";
  const float zoom_right = look::kTrafficLightFirstCenterX +
                           2 * look::kTrafficLightPitch +
                           look::kTrafficLightDiameter / 2;
  const double windowed =
      content::EvalJs(shell().frame_contents(), kToggleLeft).ExtractDouble();
  EXPECT_GE(windowed, zoom_right);
  EXPECT_GE(windowed, TrafficLights()[2].right());

  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return shell().chrome_state().fullscreen; }));
  views::test::RunScheduledLayout(&browser_view());
  EXPECT_EQ(shell().layout().traffic_spacer, kTrafficSpacer);
  // The published layout reaches the frame within a patch; the toggle stays where it was.
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(shell().frame_contents(),
                           "document.querySelector('[data-shell=\"traffic-spacer\"]')"
                           ".getBoundingClientRect().width")
               .ExtractDouble() > 0;
  }));
  EXPECT_DOUBLE_EQ(
      content::EvalJs(shell().frame_contents(), kToggleLeft).ExtractDouble(),
      windowed);
  // Wherever macOS holds the buttons in full screen (the title bar it reveals), they end before the toggle.
  NSWindow* window = ns_window();
  NSButton* zoom = [window standardWindowButton:NSWindowZoomButton];
  if (zoom.window) {
    const NSRect on_screen = [zoom.window
        convertRectToScreen:[zoom convertRect:zoom.bounds toView:nil]];
    EXPECT_LE(NSMaxX(on_screen) - NSMinX(window.frame), windowed);
  }

  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().chrome_state().fullscreen; }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(shell().frame_contents(), kToggleLeft)
               .ExtractDouble() == windowed;
  }));
}

// In full screen, when the pointer at the top brings out macOS's bar (the menu bar and the titlebar with the
// window buttons), the top row moves down with it and stays usable, and goes back up as the bar hides. The window
// hears the slide through a titlebar accessory only while it is in its own full screen.
IN_PROC_BROWSER_TEST_F(CluLookBrowserTest,
                       CluLookFullScreenTopRowFollowsTheRevealedTitlebar) {
  EXPECT_EQ(true, content::EvalJs(shell().frame_contents(),
                                  content::JsReplace(kWaitForSelector,
                                                     "[data-shell=\"sidebar-toggle\"]")));
  static constexpr char kToggleTop[] =
      "document.querySelector('[data-shell=\"sidebar-toggle\"]')"
      ".getBoundingClientRect().top";
  auto watcher = [&]() -> NSTitlebarAccessoryViewController* {
    for (NSTitlebarAccessoryViewController* c in ns_window()
             .titlebarAccessoryViewControllers) {
      if ([NSStringFromClass([c class])
              isEqualToString:@"CluTitlebarRevealController"]) {
        return c;
      }
    }
    return nil;
  };
  EXPECT_FALSE(watcher());

  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return shell().chrome_state().fullscreen; }));
  views::test::RunScheduledLayout(&browser_view());
  NSTitlebarAccessoryViewController* reveal = watcher();
  ASSERT_TRUE(reveal);
  const double rest =
      content::EvalJs(shell().frame_contents(), kToggleTop).ExtractDouble();

  // AppKit's slide: fully out, the bar's window height is covered; back in, nothing.
  [reveal setRevealAmount:1];
  NSWindow* bar = reveal.view.window;
  if (bar && bar != ns_window()) {
    EXPECT_GT(shell().titlebar_reveal(), 0);
    EXPECT_NEAR(shell().titlebar_reveal(), NSHeight(bar.frame), 1);
  }
  [reveal setRevealAmount:0];
  EXPECT_EQ(shell().titlebar_reveal(), 0);

  // The frame follows the published height: 52 pt covered moves the row 52 down, where a click reaches the toggle.
  shell().SetTitlebarReveal(52);
  // Published as windows[id].shell.titlebarReveal, which the row reads.
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(shell().frame_contents(),
                           "document.querySelector('[data-surface=\"top-row\"]')"
                           "?.dataset.titlebarReveal ?? ''")
               .ExtractString() == "52";
  }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return std::abs(content::EvalJs(shell().frame_contents(), kToggleTop)
                        .ExtractDouble() -
                    (rest + 52)) < 0.5;
  }));
  EXPECT_EQ(true, content::EvalJs(shell().frame_contents(), R"((() => {
      const r = document.querySelector('[data-shell="sidebar-toggle"]').getBoundingClientRect();
      const at = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
      return !!at && !!at.closest('[data-shell="sidebar-toggle"]');
    })())"));
  // 1142: the whole column moves with the row, so the row never draws over the address pill or the favorites, and
  // the pill stays on top where it is.
  EXPECT_EQ(true, content::EvalJs(shell().frame_contents(), R"((() => {
      const box = (s) => document.querySelector(s)?.getBoundingClientRect();
      const row = box('[data-surface="top-row"]');
      const pill = box('[data-shell="url-pill"]');
      const tiles = box('.clu-favorites') ?? box('.clu-space-panel');
      if (!row || !pill) return false;
      const at = document.elementFromPoint(pill.left + pill.width / 2, pill.top + pill.height / 2);
      return row.bottom <= pill.top + 0.5 && (!tiles || pill.bottom <= tiles.top + 0.5) &&
          !!at && !!at.closest('[data-shell="url-pill"]');
    })())"));
  shell().SetTitlebarReveal(0);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return std::abs(content::EvalJs(shell().frame_contents(), kToggleTop)
                        .ExtractDouble() -
                    rest) < 0.5;
  }));

  // Out of full screen the accessory goes and nothing moves the row.
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().chrome_state().fullscreen; }));
  views::test::RunScheduledLayout(&browser_view());
  EXPECT_FALSE(watcher());
  shell().SetTitlebarReveal(30);
  EXPECT_EQ(shell().titlebar_reveal(), 0);
}

// The frame WebUI's empty space is the window's title bar (a drag moves the window, a double click zooms
// it), its controls and the page stay client. Points come from the frame's own layout, so the test follows the CSS.
IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookFrameDragAreasAreTheTitleBar) {
  OpenTabs();
  views::Widget* widget = browser_view().GetWidget();
  // The window's hit test at `p` in frame WebUI coordinates, as the Mac window asks it on a mouse down.
  auto hit = [&](const gfx::Point& p) {
    gfx::Point in_widget(p);
    views::View::ConvertPointToWidget(shell().frame_view(), &in_widget);
    return widget->GetNonClientComponent(in_widget);
  };
  // The center of the first match of `selector`, or of the first spot on its middle line no control covers
  // (`empty`): the gaps between the bottom bar's buttons, the top row's fill.
  auto point = [&](const std::string& selector, bool empty) -> gfx::Point {
    const content::EvalJsResult result = content::EvalJs(
        shell().frame_contents(),
        content::JsReplace(R"((() => {
          const el = document.querySelector($1);
          if (!el) return [];
          const r = el.getBoundingClientRect();
          const y = r.top + r.height / 2;
          if (!$2) return [r.left + r.width / 2, y];
          for (let x = r.left + 1; x < r.right - 1; x += 1) {
            const at = document.elementFromPoint(x, y);
            if (at && el.contains(at) && !at.closest('button, [role="button"], input, a')) return [x, y];
          }
          return [];
        })())",
                           selector, empty));
    const auto& xy = result.ExtractList();
    EXPECT_EQ(xy.size(), 2u) << selector;
    return xy.size() == 2u ? gfx::Point(std::lround(xy[0].GetDouble()),
                                        std::lround(xy[1].GetDouble()))
                           : gfx::Point();
  };

  // The regions reach the browser once the frame lays out (CluShell::DraggableRegionsChanged()).
  const gfx::Point top_fill = point(".clu-top-row-fill", false);
  ASSERT_TRUE(base::test::RunUntil([&] { return hit(top_fill) == HTCAPTION; }));

  // The sidebar's top row around its buttons.
  EXPECT_EQ(hit(point(".clu-top-row", true)), HTCAPTION);
  // The space under the tabs: below the last row of the list.
  const double list_bottom =
      content::EvalJs(shell().frame_contents(),
                      "document.querySelector('.clu-tab-list').getBoundingClientRect().bottom")
          .ExtractDouble();
  const double tree_bottom =
      content::EvalJs(shell().frame_contents(),
                      "document.querySelector('.clu-tab-tree').getBoundingClientRect().bottom")
          .ExtractDouble();
  ASSERT_GT(list_bottom - tree_bottom, 20);
  const gfx::Point under_tabs(point(".clu-tab-list", false).x(),
                              std::lround((tree_bottom + list_bottom) / 2));
  EXPECT_EQ(hit(under_tabs), HTCAPTION);
  // The bottom bar's gaps.
  EXPECT_EQ(hit(point(".clu-footer", true)), HTCAPTION);
  // The card's top edge: the gutter over the page card.
  const gfx::Rect card = shell().shown_page_rect();
  ASSERT_GT(card.y(), 2);
  EXPECT_EQ(hit(gfx::Point(card.CenterPoint().x(), card.y() / 2)), HTCAPTION);

  // The buttons, rows and the pill keep their clicks, and so does the page.
  for (const char* selector :
       {"[data-shell=\"sidebar-toggle\"]", "[data-shell=\"nav-reload\"]", "[data-shell=\"url-pill\"]",
        ".clu-tab-row", ".clu-new-tab", ".clu-footer .clu-icon-button", ".clu-space-icon"}) {
    EXPECT_EQ(hit(point(selector, false)), HTCLIENT) << selector;
  }
  EXPECT_EQ(hit(card.CenterPoint()), HTCLIENT);
  EXPECT_EQ(hit(gfx::Point(card.x() + 4, card.y() + 4)), HTCLIENT);
}

// The top-row glyphs are drawn at fixed extents (toggle 18.5 x 15, back and
// forward 15 x 12) within 0.5 pt. The extent is the SVG geometry's box plus its stroke, in the frame's CSS px.
IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookTopRowGlyphsKeepTheirExtents) {
  const auto extent = [&](const char* selector, const char* side, double stroke) {
    return content::EvalJs(shell().frame_contents(),
                           content::JsReplace("(() => { const s = document.querySelector($1 + ' svg');"
                                              " const k = s.width.baseVal.value / s.viewBox.baseVal.width;"
                                              " return (s.getBBox()[$2] + $3) * k; })()",
                                              selector, side, stroke))
        .ExtractDouble();
  };
  struct Glyph {
    const char* selector;
    double stroke;
    double width;
    double height;
  };
  for (const Glyph& glyph : {Glyph{"[data-shell=\"sidebar-toggle\"]", 1.5, 18.5, 15},
                             Glyph{"[data-shell=\"nav-back\"]", 1.6, 15, 12},
                             Glyph{"[data-shell=\"nav-forward\"]", 1.6, 15, 12}}) {
    EXPECT_EQ(true, content::EvalJs(shell().frame_contents(),
                                    content::JsReplace(kWaitForSelector, glyph.selector)));
    EXPECT_NEAR(extent(glyph.selector, "width", glyph.stroke), glyph.width, 0.5) << glyph.selector;
    EXPECT_NEAR(extent(glyph.selector, "height", glyph.stroke), glyph.height, 0.5) << glyph.selector;
  }
}

IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookDialogsDropFromTheCardTop) {
  OpenTabs();
  const int card_top = shell().shown_page_rect().y();
  EXPECT_EQ(card_top, look::kCardInset);
  // Browser-modal dialogs and sheets (BrowserViewCluLayoutImpl::GetDialogTop()).
  EXPECT_EQ(browser_view()
                .GetWebContentsModalDialogHost()
                ->GetDialogPosition(gfx::Size(300, 200))
                .y(),
            card_top);
  // Tab-modal dialogs (TabModalDialogHost through clu::GetTabModalDialogTop()).
  content::WebContents* page = browser()->GetTabStripModel()->GetActiveWebContents();
  EXPECT_EQ(browser_view()
                .GetWebContentsModalDialogHostFor(page)
                ->GetDialogPosition(gfx::Size(300, 200))
                .y(),
            card_top);
  // The find bar hangs from the card's top edge at its right, left of the page's scrollbar.
  const gfx::Rect find = browser_view().GetFindBarBoundingBox();
  EXPECT_EQ(find.y(), card_top);
  EXPECT_EQ(find.right(), shell().shown_page_rect().right() - gfx::scrollbar_size());
}

IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookHooksLeaveOtherWindowsAlone) {
  float height = -1;
  EXPECT_FALSE(GetTitlebarHeight(nullptr, &height));
  EXPECT_EQ(height, -1);
  int y = -1;
  EXPECT_FALSE(GetTabModalDialogTop(nullptr, &y));
  EXPECT_EQ(y, -1);
  ui::ColorProviderKey key;
  AdjustColorProviderKey(nullptr, key);
  EXPECT_FALSE(key.app_controller);
  SetWindowButtonsVisible(nullptr, false);
  // A Clu window answers twice the buttons' center.
  ASSERT_TRUE(GetTitlebarHeight(browser(), &height));
  EXPECT_FLOAT_EQ(height, look::kTrafficLightCenterY * 2);
}

IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookChromiumColorsFollowTheTokens) {
  // The window's key carries Clu's supplier, so its bubbles, dialogs and menus inherit Clu's surfaces.
  ui::ColorProviderKey key;
  AdjustColorProviderKey(browser(), key);
  EXPECT_EQ(key.app_controller, CluColorSupplier::Get());
  const ui::ColorProvider* provider = browser_view().GetColorProvider();
  ASSERT_TRUE(provider);
  const CluNativeColors light =
      NativeColorsFor(ui::ColorProviderKey::ColorMode::kLight);
  EXPECT_EQ(provider->GetColor(ui::kColorBubbleBackground), light.surface);
  EXPECT_EQ(provider->GetColor(ui::kColorDialogBackground), light.surface);
  EXPECT_EQ(provider->GetColor(ui::kColorFrameActive), light.frame);
  EXPECT_EQ(provider->GetColor(ui::kColorFocusableBorderFocused),
            light.focus_ring);
}

// The screenshot suite: every target of each state the real browser can show without a pointer (hover and press
// states run in the WebUI harness and, with real input, in clu_interactive_ui_tests).
IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookNativeMatchesTheDesign) {
  OpenTabs();
  CheckState("native");
}

IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookFrameMatchesTheDesign) {
  OpenTabs();
  CheckState("page-card");
  CheckState("rows-rest");
  CheckState("row-active");
}

IN_PROC_BROWSER_TEST_F(CluLookBrowserTest, CluLookCommandBarMatchesTheDesign) {
  OpenTabs();
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  EXPECT_EQ(true, content::EvalJs(shell().overlay_contents(),
                                  content::JsReplace(kWaitForSelector, ".clu-cmd")));
  CheckState("command-empty");
}

}  // namespace
}  // namespace clu
