// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/theme/clu_overlay_scrollbars.h"

#include <memory>

#include "base/no_destructor.h"
#include "ui/native_theme/os_settings_provider_mac.h"

namespace clu {
namespace {

// macOS's own answers for everything else (dark mode, contrast, caret blink); only the scroller style is fixed.
class CluOsSettingsProvider : public ui::OsSettingsProviderMac {
 public:
  explicit CluOsSettingsProvider(PriorityLevel priority = PriorityLevel::kProduction)
      : ui::OsSettingsProviderMac(priority) {
    // The base constructors announced this provider before this class existed, so the themes read the base answers;
    // announce it again now that they read this one.
    NotifyOnSettingsChanged(/*force_notify=*/true);
  }

  // ui::OsSettingsProvider:
  bool PrefersOverlayScrollbars() const override { return true; }
};

}  // namespace

void InstallOverlayScrollbars() {
  static base::NoDestructor<CluOsSettingsProvider> provider;
}

std::unique_ptr<ui::OsSettingsProvider> CreateOverlayScrollbarsProviderForTesting(
    ui::OsSettingsProvider::PriorityLevel priority) {
  return std::make_unique<CluOsSettingsProvider>(priority);
}

}  // namespace clu
