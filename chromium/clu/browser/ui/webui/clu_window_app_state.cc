// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_window_app_state.h"

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "clu/browser/theme/clu_theme_math.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace clu {

CluWindowAppState::CluWindowAppState(CluWindowState& window, Profile* profile)
    : window_(window) {
  if (CluAppSettings* settings = CluAppSettings::Get()) {
    settings_observation_.Observe(settings);
    // A profile loaded after the mode was set (a cross-profile window, a new profile) follows it too.
    settings->ApplyAppearance(profile);
    // The first window of a launch shows the chosen app icon in the Dock.
    settings->ApplyAppIcon();
    settings->ApplyWarnBeforeQuit();
    settings->ApplyAutoUpdate();
    settings->ApplyAutoPictureInPicture(profile);
  }
  if (CluImportService* import = CluImportService::Get()) {
    import_observation_.Observe(import);
  }
  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
}

CluWindowAppState::~CluWindowAppState() = default;

void CluWindowAppState::Fill(WindowSnapshot& s) {
  ui::NativeTheme* native = ui::NativeTheme::GetInstanceForNativeUi();
  s.reduce_transparency = native->prefers_reduced_transparency();
  CluAppSettings* settings = CluAppSettings::Get();
  if (!settings) {
    return;
  }
  s.appearance_mode = settings->AppearanceMode();
  if (s.appearance_mode != "auto") {
    s.dark = s.appearance_mode == "dark";
  }
  s.onboarding_show = settings->onboarding_show();
  s.global_settings = GlobalSettingsSnapshot(settings->Global());
  for (SpaceSnapshot& space : s.spaces) {
    space.theme = settings->ShownTheme(space.id);
  }
  if (CluImportService* import = CluImportService::Get();
      import && import->progress()) {
    const ImportProgress& p = *import->progress();
    s.import_progress = ImportProgressSnapshot{.source = p.source,
                                               .phase = p.phase,
                                               .done = p.done,
                                               .total = p.total,
                                               .skipped = p.skipped};
  }
}

void CluWindowAppState::Apply(const WindowSnapshot& s) {
  // The accent of the space the window shows; a private window keeps Chromium's incognito colors.
  std::optional<SkColor> accent;
  if (s.has_model && !s.off_the_record) {
    for (const SpaceSnapshot& space : s.spaces) {
      if (space.id == s.space_id) {
        accent = SpaceAccentColor(space.theme, space.color);
      }
    }
  }
  if (accent != accent_) {
    accent_ = accent;
    views::WebView* frame = window_->shell().frame_view();
    if (views::Widget* widget = frame ? frame->GetWidget() : nullptr) {
      // BrowserWidget::GetColorProviderKey() applies the override as the user color (kAccent), so Chromium's color
      // pipeline derives every native surface's accent from it; the widget schedules a theme change itself.
      widget->SetUserColorOverride(accent_);
    }
  }
  CluAppSettings* settings = CluAppSettings::Get();
  if (!settings || s.off_the_record) {
    return;
  }
  if (last_hidden_ != s.chrome.sidebar_hidden) {
    // The first publish only notes the state the window started with.
    if (last_hidden_) {
      settings->SetLastSidebarHidden(s.chrome.sidebar_hidden);
    }
    last_hidden_ = s.chrome.sidebar_hidden;
  }
  if (last_toolbar_ != s.chrome.toolbar) {
    if (last_toolbar_) {
      settings->SetLastToolbar(s.chrome.toolbar);
    }
    last_toolbar_ = s.chrome.toolbar;
  }
  // A row opened while onboarding shows (Cmd+T, Settings from the import step) ends it as skipped; the row the window
  // already showed when it came up does not.
  if (!s.onboarding_show) {
    onboarding_tab_.reset();
  } else if (!onboarding_tab_) {
    onboarding_tab_ = s.active_tab_id;
  } else if (s.active_tab_id && *onboarding_tab_ != s.active_tab_id) {
    onboarding_tab_.reset();
    settings->CompleteOnboarding(/*skipped=*/true);
  }
}

void CluWindowAppState::OnAppSettingsChanged() {
  // Onboarding may have ended (or, in tests, begun).
  window_->UpdatePageShown();
  window_->MarkDirty();
}

void CluWindowAppState::OnImportProgress() {
  window_->MarkDirty();
}

void CluWindowAppState::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  window_->MarkDirty();
}

}  // namespace clu
