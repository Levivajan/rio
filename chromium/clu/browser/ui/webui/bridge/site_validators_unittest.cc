// Copyright 2026 The Rio Authors. All rights reserved.

#include <limits>
#include <optional>
#include <string>

#include "clu/browser/ui/webui/bridge/bridge_validators.h"
#include "clu/common/mojom/extensions.mojom.h"
#include "clu/common/mojom/tweaks.mojom.h"
#include "clu/common/mojom/types.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

// Junk for every M7 command (blocking.*, siteControls.*, extensions.*, tweaks.*) fails the generated caps, so the guard
// answers kBadPayload before a handler runs (docs/PLAN.md 12.1, docs/chromium/m7.md). The valid case passes too.
namespace clu {
namespace {

constexpr char kId[] = "00000000-0000-4000-8000-000700000001";
constexpr char kNotAnId[] = "tab-1";
constexpr char kBadUtf8[] = "\xC3";
constexpr char kExt[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

mojom::RectPtr Rect(double x = 10) {
  return mojom::Rect::New(x, 20, 24, 24);
}

mojom::TweakControlsPtr Controls(double hue = 0,
                                 std::optional<std::string> font = std::nullopt,
                                 double scale = 1) {
  return mojom::TweakControls::New(hue, false, 1, 1, 1, std::move(font), scale);
}

TEST(CluSiteValidatorsTest, BlockingAndSiteControls) {
  EXPECT_TRUE(IsValid(*mojom::BlockingSetSiteEnabledInput::New(kId, false)));
  EXPECT_FALSE(
      IsValid(*mojom::BlockingSetSiteEnabledInput::New(kNotAnId, false)));
  EXPECT_TRUE(IsValid(*mojom::SiteControlsOpenInput::New(Rect())));
  EXPECT_FALSE(IsValid(*mojom::SiteControlsOpenInput::New(Rect(kNaN))));
  EXPECT_FALSE(IsValid(*mojom::SiteControlsOpenInput::New(Rect(kInf))));
}

TEST(CluSiteValidatorsTest, ExtensionIdsAreThirtyTwoLettersAToP) {
  EXPECT_TRUE(IsValid(*mojom::ExtensionsRemoveInput::New(kId, kExt)));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsRemoveInput::New(kId, std::string(32, 'z'))));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsRemoveInput::New(kId, std::string(31, 'a'))));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsRemoveInput::New(kNotAnId, kExt)));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsOpenOptionsInput::New(kId, "../x")));
  EXPECT_FALSE(
      IsValid(*mojom::ExtensionsSetEnabledInput::New(kId, kBadUtf8, true)));
  EXPECT_TRUE(IsValid(*mojom::ExtensionsListInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsListInput::New(kNotAnId)));
}

TEST(CluSiteValidatorsTest, TheActionRowAndTheMenu) {
  EXPECT_TRUE(IsValid(*mojom::ExtensionsRunActionInput::New(kExt, Rect())));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsRunActionInput::New(kExt, Rect(kInf))));
  EXPECT_FALSE(
      IsValid(*mojom::ExtensionsRunActionInput::New("chrome-extension", Rect())));
  EXPECT_TRUE(IsValid(*mojom::ExtensionsSetPinnedInput::New(kExt, true)));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsSetPinnedInput::New("", true)));
  EXPECT_TRUE(IsValid(*mojom::ExtensionsOpenMenuInput::New(Rect())));
  EXPECT_FALSE(IsValid(*mojom::ExtensionsOpenMenuInput::New(Rect(kNaN))));
  EXPECT_TRUE(IsValid(*mojom::ExtensionsRespondInstallInput::New(kId, true)));
  EXPECT_FALSE(
      IsValid(*mojom::ExtensionsRespondInstallInput::New(kNotAnId, true)));
}

TEST(CluSiteValidatorsTest, TweakCodeIsCappedAndTabsAreIds) {
  const std::string max(262144, 'a');
  const std::string over(262145, 'a');
  EXPECT_TRUE(IsValid(*mojom::TweaksSaveInput::New(kId, max, "", nullptr)));
  EXPECT_FALSE(IsValid(*mojom::TweaksSaveInput::New(kId, over, "", nullptr)));
  EXPECT_FALSE(IsValid(*mojom::TweaksSaveInput::New(kId, "", over, nullptr)));
  EXPECT_FALSE(IsValid(*mojom::TweaksSaveInput::New(kNotAnId, "", "", nullptr)));
  EXPECT_FALSE(IsValid(*mojom::TweaksSaveInput::New(kId, kBadUtf8, "", nullptr)));
  EXPECT_TRUE(IsValid(*mojom::TweaksPreviewCssInput::New(kId, "h1{}")));
  EXPECT_FALSE(IsValid(*mojom::TweaksPreviewCssInput::New(kId, over)));
  EXPECT_TRUE(IsValid(*mojom::TweaksGetInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::TweaksGetInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::TweaksSetEnabledInput::New(kNotAnId, true)));
  EXPECT_TRUE(IsValid(*mojom::TweaksOpenEditorInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::TweaksOpenEditorInput::New(kNotAnId)));
}

TEST(CluSiteValidatorsTest, ControlsStayInTheirRanges) {
  EXPECT_TRUE(IsValid(*mojom::TweaksPreviewControlsInput::New(kId, Controls())));
  EXPECT_TRUE(IsValid(
      *mojom::TweaksSaveInput::New(kId, "", "", Controls(-180, "Georgia", 1.5))));
  EXPECT_FALSE(
      IsValid(*mojom::TweaksPreviewControlsInput::New(kId, Controls(181))));
  EXPECT_FALSE(
      IsValid(*mojom::TweaksPreviewControlsInput::New(kId, Controls(kNaN))));
  EXPECT_FALSE(IsValid(*mojom::TweaksPreviewControlsInput::New(
      kId, Controls(0, std::nullopt, 2))));
  EXPECT_FALSE(IsValid(*mojom::TweaksPreviewControlsInput::New(
      kId, Controls(0, std::string(65, 'f')))));
}

TEST(CluSiteValidatorsTest, ZapCommands) {
  EXPECT_TRUE(IsValid(*mojom::TweaksStartZapInput::New(kId)));
  EXPECT_TRUE(IsValid(*mojom::TweaksStopZapInput::New(kId)));
  EXPECT_TRUE(IsValid(*mojom::TweaksUndoZapInput::New(kId)));
  EXPECT_TRUE(IsValid(*mojom::TweaksClearZapsInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::TweaksStartZapInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::TweaksStopZapInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::TweaksUndoZapInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::TweaksClearZapsInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::TweaksRemoveZapInput::New(kId, 0)));
  EXPECT_TRUE(IsValid(*mojom::TweaksRemoveZapInput::New(kId, 10000)));
  EXPECT_FALSE(IsValid(*mojom::TweaksRemoveZapInput::New(kId, -1)));
  EXPECT_FALSE(IsValid(*mojom::TweaksRemoveZapInput::New(kId, 10001)));
}

TEST(CluSiteValidatorsTest, TheSettingsList) {
  EXPECT_TRUE(IsValid(*mojom::TweaksListInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::TweaksListInput::New(kNotAnId)));
  EXPECT_TRUE(
      IsValid(*mojom::TweaksSetEnabledForSiteInput::New(kId, "a.test", true)));
  EXPECT_FALSE(IsValid(*mojom::TweaksSetEnabledForSiteInput::New(
      kId, std::string(32769, 'a'), true)));
  EXPECT_TRUE(IsValid(*mojom::TweaksDeleteForSiteInput::New(kId, "a.test")));
  EXPECT_FALSE(IsValid(*mojom::TweaksDeleteForSiteInput::New(kNotAnId, "a")));
  EXPECT_FALSE(IsValid(*mojom::TweaksDeleteForSiteInput::New(kId, kBadUtf8)));
}

}  // namespace
}  // namespace clu
