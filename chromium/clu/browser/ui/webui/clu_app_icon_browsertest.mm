// Copyright 2026 The Rio Authors. All rights reserved.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/app_icon/clu_app_icon.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"

// The CluAppIcon* checks of docs/chromium/icons.md: picking an icon through settings.set shows it in the Dock at once
// (NSApp.applicationIconImage) and saves it for the Dock tile plug-in, junk is refused, Flat warm puts the bundle's icon
// back, and the pick is shown again by the next launch (the PRE_ pairs; one keeps a stored Sunset). Each test keeps
// the plug-in key in a defaults domain of its own (a PRE_ pair shares one), never an installed Rio's, and removes it
// when it ends.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;
using Rgb = std::array<double, 3>;

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

// The average color of the middle of `image` (the orb) drawn at 32 x 32: NSApp.applicationIconImage hands back a
// 32 px copy, so the icons are compared by color, not by bytes.
Rgb MiddleColor(NSImage* image) {
  NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:32
                    pixelsHigh:32
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace
                   bytesPerRow:32 * 4
                  bitsPerPixel:32];
  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext.currentContext =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
  [image drawInRect:NSMakeRect(0, 0, 32, 32)
           fromRect:NSZeroRect
          operation:NSCompositingOperationCopy
           fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];
  Rgb sum = {0, 0, 0};
  int count = 0;
  for (int y = 11; y < 21; ++y) {
    for (int x = 11; x < 21; ++x) {
      NSColor* color = [[rep colorAtX:x y:y]
          colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
      sum[0] += color.redComponent * 255;
      sum[1] += color.greenComponent * 255;
      sum[2] += color.blueComponent * 255;
      ++count;
    }
  }
  return {sum[0] / count, sum[1] / count, sum[2] / count};
}

double Distance(const Rgb& a, const Rgb& b) {
  double d = 0;
  for (int c = 0; c < 3; ++c) {
    d = std::max(d, std::abs(a[c] - b[c]));
  }
  return d;
}

Rgb AlternateColor(const std::string& id) {
  const std::string_view png = ui::ResourceBundle::GetSharedInstance()
                                   .GetRawDataResource(AppIconResourceId(id));
  NSImage* image = [[NSImage alloc]
      initWithData:[NSData dataWithBytes:png.data() length:png.size()]];
  return MiddleColor(image);
}

Rgb DockColor() {
  return MiddleColor(NSApp.applicationIconImage);
}

// clu.appIcon in the test's defaults domain, "" when absent.
std::string PlugInChoice() {
  base::apple::ScopedCFTypeRef<CFStringRef> domain =
      base::SysUTF8ToCFStringRef(AppIconDefaultsDomain());
  base::apple::ScopedCFTypeRef<CFStringRef> key =
      base::SysUTF8ToCFStringRef(kAppIconDefaultsKey);
  CFPreferencesAppSynchronize(domain.get());
  base::apple::ScopedCFTypeRef<CFPropertyListRef> value(
      CFPreferencesCopyAppValue(key.get(), domain.get()));
  CFStringRef id = base::apple::CFCast<CFStringRef>(value.get());
  return id ? base::SysCFStringRefToUTF8(id) : std::string();
}

// The test's name without PRE_, so a PRE_ test and its main test share a domain.
std::string TestName() {
  const std::string name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  return base::StartsWith(name, "PRE_") ? name.substr(4) : name;
}

bool IsPreTest() {
  return base::StartsWith(
      ::testing::UnitTest::GetInstance()->current_test_info()->name(), "PRE_");
}

class CluAppIconBrowserTest : public InProcessBrowserTest {
 protected:
  // Before the browser starts, so the first window's ApplyAppIcon() already writes to the test's domain.
  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    SetAppIconDefaultsDomainForTesting("org.chromium.clu-browsertests." +
                                       TestName());
  }

  void TearDownInProcessBrowserTestFixture() override {
    // The main test of a PRE_ pair reads what its PRE_ test saved.
    if (!IsPreTest()) {
      base::apple::ScopedCFTypeRef<CFStringRef> domain =
          base::SysUTF8ToCFStringRef(AppIconDefaultsDomain());
      base::apple::ScopedCFTypeRef<CFStringRef> key =
          base::SysUTF8ToCFStringRef(kAppIconDefaultsKey);
      CFPreferencesSetAppValue(key.get(), nullptr, domain.get());
      CFPreferencesAppSynchronize(domain.get());
    }
    SetAppIconDefaultsDomainForTesting(std::nullopt);
    InProcessBrowserTest::TearDownInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
  }

  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }

  mojom::CallResultPtr Pick(const std::string& value_json) {
    CluSettingsHandler settings(window().GetWeakPtr());
    CallFuture result;
    settings.Set(
        mojom::SettingsSetInput::New(kAppIconKey, "global", value_json),
        result.GetCallback());
    return result.Take();
  }

  std::string Stored() {
    const std::string* id = g_browser_process->local_state()
                                ->GetDict(kCluSettingsPref)
                                .FindString(kAppIconKey);
    return id ? *id : std::string();
  }
};

IN_PROC_BROWSER_TEST_F(CluAppIconBrowserTest,
                       CluAppIconPickingAnIconSetsTheDockIcon) {
  // The window that opened showed the default: Flat warm, the bundle's own icon, and no plug-in key.
  EXPECT_EQ(Stored(), "");
  EXPECT_EQ(CluAppSettings::Get()->AppIcon(), "flatWarm");
  EXPECT_EQ(AppliedAppIconForTesting(), "flatWarm");
  EXPECT_EQ(PlugInChoice(), "");
  const Rgb bundle_icon = DockColor();

  // Ocean, through settings.set as the App icon row sends it.
  ASSERT_TRUE(IsOk(Pick("\"ocean\"")));
  EXPECT_EQ(Stored(), "ocean");
  EXPECT_EQ(AppliedAppIconForTesting(), "ocean");
  EXPECT_LT(Distance(DockColor(), AlternateColor("ocean")), 12);
  EXPECT_GT(Distance(DockColor(), bundle_icon), 30);
  EXPECT_EQ(PlugInChoice(), "ocean");
  window().Flush();
  const base::DictValue state = window().CurrentState();
  EXPECT_EQ(*state.FindDict("settings")->FindDict("global")->FindString(
                kAppIconKey),
            "ocean");

  // Junk is refused and changes nothing.
  for (const char* junk : {"\"Ocean\"", "\"../ocean\"", "7", "null"}) {
    EXPECT_FALSE(IsOk(Pick(junk))) << junk;
  }
  EXPECT_EQ(AppliedAppIconForTesting(), "ocean");
  EXPECT_EQ(PlugInChoice(), "ocean");

  // Each alternate shows its own artwork.
  for (const char* id : {"airySun", "flatBlue", "sunset", "warm", "dusk"}) {
    ASSERT_TRUE(IsOk(Pick(std::string("\"") + id + "\""))) << id;
    EXPECT_EQ(AppliedAppIconForTesting(), id);
    EXPECT_LT(Distance(DockColor(), AlternateColor(id)), 12) << id;
    EXPECT_EQ(PlugInChoice(), id);
  }

  // Flat warm puts the bundle's icon back and removes the plug-in key.
  ASSERT_TRUE(IsOk(Pick("\"flatWarm\"")));
  EXPECT_EQ(AppliedAppIconForTesting(), "flatWarm");
  EXPECT_LT(Distance(DockColor(), bundle_icon), 12);
  EXPECT_EQ(PlugInChoice(), "");
}

IN_PROC_BROWSER_TEST_F(CluAppIconBrowserTest,
                       PRE_CluAppIconSurvivesARelaunch) {
  ASSERT_TRUE(IsOk(Pick("\"dusk\"")));
  EXPECT_EQ(AppliedAppIconForTesting(), "dusk");
}

IN_PROC_BROWSER_TEST_F(CluAppIconBrowserTest, CluAppIconSurvivesARelaunch) {
  // The relaunch read the pick from Local State and the first window showed it.
  EXPECT_EQ(Stored(), "dusk");
  EXPECT_EQ(AppliedAppIconForTesting(), "dusk");
  EXPECT_LT(Distance(DockColor(), AlternateColor("dusk")), 12);
  EXPECT_EQ(PlugInChoice(), "dusk");

  ASSERT_TRUE(IsOk(Pick("\"flatWarm\"")));
  EXPECT_EQ(PlugInChoice(), "");
}

// Sunset was the default before Flat warm. Someone who picked it keeps it after the update: their stored choice
// is read again and Sunset becomes an alternate drawn from its own artwork, not the bundle's icon.
IN_PROC_BROWSER_TEST_F(CluAppIconBrowserTest,
                       PRE_CluAppIconKeepsAStoredSunset) {
  ASSERT_TRUE(IsOk(Pick("\"airySun\"")));
  EXPECT_EQ(Stored(), "airySun");
}

IN_PROC_BROWSER_TEST_F(CluAppIconBrowserTest, CluAppIconKeepsAStoredSunset) {
  EXPECT_EQ(Stored(), "airySun");
  EXPECT_EQ(CluAppSettings::Get()->AppIcon(), "airySun");
  EXPECT_EQ(AppliedAppIconForTesting(), "airySun");
  EXPECT_LT(Distance(DockColor(), AlternateColor("airySun")), 12);
  EXPECT_EQ(PlugInChoice(), "airySun");

  ASSERT_TRUE(IsOk(Pick("\"flatWarm\"")));
  EXPECT_EQ(PlugInChoice(), "");
}

}  // namespace
}  // namespace clu
