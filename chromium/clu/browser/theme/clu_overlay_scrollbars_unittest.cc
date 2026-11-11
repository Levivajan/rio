// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/theme/clu_overlay_scrollbars.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/native_theme/mock_os_settings_provider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/os_settings_provider.h"

namespace clu {
namespace {

// A Mac with a mouse (or "Show scroll bars: Always") asks for legacy scrollbars with a track column; Rio answers
// overlay anyway, and the NativeTheme that feeds every page's renderer preferences follows.
TEST(CluOverlayScrollbarsTest, OverlayWinsOverALegacyMac) {
  ui::MockOsSettingsProvider legacy_mac;
  legacy_mac.SetPrefersOverlayScrollbars(false);
  ASSERT_FALSE(ui::OsSettingsProvider::Get().PrefersOverlayScrollbars());
  ASSERT_FALSE(ui::NativeTheme::GetInstanceForWeb()->use_overlay_scrollbar());

  {
    std::unique_ptr<ui::OsSettingsProvider> rio = CreateOverlayScrollbarsProviderForTesting(
        ui::OsSettingsProvider::PriorityLevel::kTesting);
    EXPECT_TRUE(ui::OsSettingsProvider::Get().PrefersOverlayScrollbars());
    EXPECT_TRUE(ui::NativeTheme::GetInstanceForWeb()->use_overlay_scrollbar());
    EXPECT_TRUE(ui::NativeTheme::GetInstanceForNativeUi()->use_overlay_scrollbar());
  }
  // Gone again, the Mac's own answer comes back with its next change (a provider that is no longer the active one
  // announces nothing as it goes).
  legacy_mac.SetPrefersOverlayScrollbars(false);
  EXPECT_FALSE(ui::NativeTheme::GetInstanceForWeb()->use_overlay_scrollbar());
}

// The production install keeps the Mac's other answers and is safe to call twice.
TEST(CluOverlayScrollbarsTest, InstallIsIdempotentAndAnswersOverlay) {
  InstallOverlayScrollbars();
  InstallOverlayScrollbars();
  EXPECT_TRUE(ui::OsSettingsProvider::Get().PrefersOverlayScrollbars());
}

}  // namespace
}  // namespace clu
