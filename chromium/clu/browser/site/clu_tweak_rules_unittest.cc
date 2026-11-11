// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_tweak_rules.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace clu {
namespace {

base::Value Json(std::string_view json) {
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC).value();
}

TweakControls Controls(std::string_view json) {
  return ClampControls(Json(json));
}

// The no-code controls as CSS.
TEST(CluTweakRulesTest, ControlsWriteNothingForTheDefaults) {
  EXPECT_EQ(ControlsToCss(TweakControls()), "");
  EXPECT_EQ(ControlsToCss(std::nullopt), "");
}

TEST(CluTweakRulesTest, ColorsBecomeOneFilterOnHtmlAndMediaIsInvertedBack) {
  const std::string css = ControlsToCss(Controls(
      R"({"hue":40,"invert":true,"contrast":1.2,"brightness":0.9,"saturation":1.5})"));
  EXPECT_NE(css.find("html { filter: hue-rotate(40deg) invert(1) contrast(1.2) "
                     "brightness(0.9) saturate(1.5) !important; }"),
            std::string::npos);
  EXPECT_NE(css.find("img, video, picture, canvas { filter: invert(1) "
                     "!important; }"),
            std::string::npos);
  EXPECT_NE(css.find("html { background-color: #fff; }"), std::string::npos);
  EXPECT_EQ(ControlsToCss(Controls(R"({"hue":-90})")),
            "html { filter: hue-rotate(-90deg) !important; }");
}

TEST(CluTweakRulesTest, FontFamilyAndScaleBecomeOneFontRule) {
  const std::string css =
      ControlsToCss(Controls(R"({"fontFamily":"Georgia","fontScale":1.25})"));
  EXPECT_EQ(css.find('\n'), std::string::npos);
  EXPECT_NE(css.find("html { font-size: 125% !important; }"), std::string::npos);
  EXPECT_NE(css.find("font-family: \"Georgia\", system-ui !important;"),
            std::string::npos);
  EXPECT_NE(ControlsToCss(Controls(R"({"fontFamily":"serif"})"))
                .find("font-family: serif !important;"),
            std::string::npos);
  EXPECT_EQ(ControlsToCss(Controls(R"({"fontScale":0.75})")),
            "html { font-size: 75% !important; }");
}

TEST(CluTweakRulesTest, ControlsClampJunkAndUnknownFonts) {
  const TweakControls wild =
      Controls(R"({"hue":9999,"brightness":50,"saturation":-3,"contrast":"x"})");
  EXPECT_EQ(ControlsToCss(wild),
            "html { filter: hue-rotate(180deg) brightness(2) saturate(0) "
            "!important; }");
  const TweakControls low =
      Controls(R"({"hue":-500,"contrast":0.1,"fontScale":0.2})");
  EXPECT_EQ(low.hue, -180);
  EXPECT_EQ(low.contrast, 0.5);
  EXPECT_EQ(low.font_scale, 0.75);
  EXPECT_TRUE(IsDefaultControls(ClampControls(base::Value("junk"))));
  EXPECT_FALSE(Controls(R"({"invert":"yes"})").invert);
  EXPECT_EQ(Controls(R"({"fontFamily":"Comic Sans MS"})").font_family,
            std::nullopt);
}

TEST(CluTweakRulesTest, ControlsRoundTripAndDefaultsStoreAsNone) {
  const TweakControls c = Controls(R"({"hue":10,"fontFamily":"Menlo"})");
  EXPECT_EQ(ParseControls(ControlsToJson(c)), c);
  EXPECT_EQ(ParseControls(ControlsToJson(TweakControls())), std::nullopt);
  EXPECT_EQ(ParseControls("{not json"), std::nullopt);
  EXPECT_EQ(ParseControls("[1,2]"), std::nullopt);
  EXPECT_EQ(ParseControls(""), std::nullopt);
}

// Zap selectors and their validation.
TEST(CluTweakRulesTest, SelectorsCannotLeaveTheirRule) {
  for (const std::string& bad : std::vector<std::string>{
           "", "   ", "a{", "a}", "a;b", "@media", "<b>", "a/*x*/",
        std::string(kMaxZapSelector + 1, 'a')}) {
    EXPECT_FALSE(IsSafeSelector(bad)) << bad.substr(0, 20);
    std::vector<Zap> zaps;
    EXPECT_EQ(AppendZap(zaps, Zap{.selector = bad}), AppendZapResult::kUnsafe);
    EXPECT_TRUE(zaps.empty());
  }
  EXPECT_TRUE(IsSafeSelector(std::string(kMaxZapSelector, 'a')));
  EXPECT_TRUE(IsSafeSelector("body > div.card:nth-of-type(2) a[href]"));
}

TEST(CluTweakRulesTest, AtMostTwoHundredZapsPerSite) {
  std::vector<Zap> zaps;
  for (size_t i = 0; i < kMaxZaps; ++i) {
    ASSERT_EQ(AppendZap(zaps, Zap{.selector = "#z" + std::to_string(i)}),
              AppendZapResult::kOk);
  }
  EXPECT_EQ(AppendZap(zaps, Zap{.selector = "#more"}), AppendZapResult::kFull);
  EXPECT_EQ(zaps.size(), kMaxZaps);
}

TEST(CluTweakRulesTest, LabelsArePrintableSingleLineAndShort) {
  EXPECT_EQ(CleanLabel("div\x01\n \xC2\xB7 Promo\tbanner"), "div \xC2\xB7 Promo banner");
  EXPECT_EQ(CleanLabel(std::string(200, 'x')).size(), kMaxZapLabel);
  std::vector<Zap> zaps;
  AppendZap(zaps, Zap{.selector = "#a", .label = "  two\nlines "});
  EXPECT_EQ(zaps[0].label, "two lines");
}

TEST(CluTweakRulesTest, ZapCssHasOneRulePerSafeSelector) {
  EXPECT_EQ(ZapCss({Zap{.selector = "#a"}, Zap{.selector = "b{"},
                    Zap{.selector = ".c"}}),
            "#a { display: none !important; }\n.c { display: none "
            "!important; }");
}

// The repair of stored rows.
TEST(CluTweakRulesTest, StoredZapsAreReadDefensively) {
  bool junk = false;
  EXPECT_TRUE(ParseZaps("garbage", &junk).empty());
  EXPECT_TRUE(junk);
  junk = false;
  EXPECT_TRUE(ParseZaps(R"({"selector":"#a"})", &junk).empty());
  EXPECT_TRUE(junk);
  junk = false;
  const std::vector<Zap> zaps = ParseZaps(
      R"(["#bare", {"selector":"#ok","label":"div\nx","related":true,"createdAt":5},
         {"selector":"a{"}, {"label":"no selector"}, 7, {"selector":".n","createdAt":"x"}])",
      &junk);
  EXPECT_FALSE(junk);
  ASSERT_EQ(zaps.size(), 3u);
  EXPECT_EQ(zaps[0], (Zap{.selector = "#bare"}));
  EXPECT_EQ(zaps[1], (Zap{.selector = "#ok", .label = "div x", .related = true,
                          .created_at = 5}));
  EXPECT_EQ(zaps[2].created_at, 0);
  EXPECT_EQ(ParseZaps(ZapsToJson(zaps), nullptr), zaps);
  EXPECT_TRUE(ParseZaps("", &junk).empty());
}

TEST(CluTweakRulesTest, CodeIsCappedAtTwoHundredFiftySixKibibytes) {
  EXPECT_TRUE(IsTweakCodeSizeOk(std::string(kMaxTweakCodeBytes, 'a'), ""));
  EXPECT_FALSE(IsTweakCodeSizeOk(std::string(kMaxTweakCodeBytes + 1, 'a'), ""));
  EXPECT_FALSE(IsTweakCodeSizeOk("", std::string(kMaxTweakCodeBytes + 1, 'a')));
}

TEST(CluTweakRulesTest, TheCssJoinsDraftsSavedControlsAndZaps) {
  SiteTweak t;
  t.site = "a.test";
  t.css = "h1{color:red}";
  t.controls = Controls(R"({"invert":true})");
  t.zaps = {Zap{.selector = "#promo"}};
  const std::string css = TweakCss(&t, std::nullopt, std::nullopt);
  EXPECT_EQ(css.find("h1{color:red}"), 0u);
  EXPECT_NE(css.find("invert(1)"), std::string::npos);
  EXPECT_NE(css.find("#promo { display: none !important; }"), std::string::npos);
  // A draft replaces the saved part it stands for, nothing else.
  const std::string draft =
      TweakCss(&t, std::string("h1{color:blue}"), TweakControls());
  EXPECT_EQ(draft, "h1{color:blue}\n#promo { display: none !important; }");
  t.enabled = false;
  EXPECT_EQ(TweakCss(&t, std::string("h1{}"), std::nullopt), "");
  EXPECT_EQ(TweakCss(nullptr, std::string("p{}"), std::nullopt), "p{}");
  EXPECT_EQ(TweakCss(nullptr, std::nullopt, std::nullopt), "");
}

TEST(CluTweakRulesTest, EmptinessSummaryAndValues) {
  SiteTweak t;
  t.site = "a.test";
  t.css = "  \n";
  EXPECT_TRUE(IsEmptyTweak(t));
  EXPECT_TRUE(TweakSummaryValue(nullptr).is_none());
  t.js = "x()";
  t.zaps = {Zap{.selector = "#a"}};
  EXPECT_FALSE(IsEmptyTweak(t));
  const base::Value summary = TweakSummaryValue(&t);
  EXPECT_EQ(summary.GetDict().FindBool("hasCss"), false);
  EXPECT_EQ(summary.GetDict().FindBool("hasJs"), true);
  EXPECT_EQ(summary.GetDict().FindInt("zapCount"), 1);
  const base::DictValue value = SiteTweakToValue(t);
  EXPECT_TRUE(value.Find("controls")->is_none());
  EXPECT_EQ(value.FindList("zaps")->size(), 1u);
  EXPECT_EQ(*SiteTweakRowValue(t).FindString("site"), "a.test");
}

TEST(CluTweakRulesTest, TweakJsReportsErrorsAndAnswersUndefined) {
  const std::string js = WrapTweakJs("document.body.dataset.tweaked = '1'");
  EXPECT_EQ(js.find("try {\n"), 0u);
  EXPECT_NE(js.find("console.error(\"[Rio site tweak]\", e)"), std::string::npos);
  EXPECT_TRUE(js.ends_with("\nvoid 0"));
}

}  // namespace
}  // namespace clu
