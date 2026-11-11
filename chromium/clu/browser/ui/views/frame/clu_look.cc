// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_look.h"

#include "base/no_destructor.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "clu/browser/ui/views/frame/clu_look_tokens.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"

namespace clu {

// static
CluColorSupplier* CluColorSupplier::Get() {
  static base::NoDestructor<CluColorSupplier> supplier;
  return supplier.get();
}

CluColorSupplier::CluColorSupplier() = default;
CluColorSupplier::~CluColorSupplier() = default;

CluNativeColors NativeColorsFor(ui::ColorProviderKey::ColorMode mode,
                                std::optional<SkColor> accent) {
  const bool dark = mode == ui::ColorProviderKey::ColorMode::kDark;
  const look::Palette& p = dark ? look::kDark : look::kLight;
  return {.frame = dark ? look::kDefaultTintDark : look::kDefaultTintFrom,
          .surface = p.popover_bg,
          .page = p.page_bg,
          .focus_ring = p.focus_ring,
          .separator = p.separator,
          .split_focus = accent.value_or(p.accent_strong)};
}

void CluColorSupplier::AddColorMixers(ui::ColorProvider* provider,
                                      const ui::ColorProviderKey& key) const {
  const CluNativeColors c = NativeColorsFor(key.color_mode, key.user_color);
  ui::ColorMixer& mixer = provider->AddMixer();
  // The frame shows only while the frame WebUI has not painted (a new window, a resize): the default tint.
  mixer[ui::kColorFrameActive] = {c.frame};
  mixer[ui::kColorFrameInactive] = {c.frame};
  mixer[kColorToolbar] = {c.frame};
  // Bubbles, dialogs, menus, the find bar and infobars are popovers: the popover background.
  mixer[ui::kColorBubbleBackground] = {c.surface};
  mixer[ui::kColorDialogBackground] = {c.surface};
  mixer[ui::kColorMenuBackground] = {c.surface};
  mixer[kColorFindBarBackground] = {c.surface};
  mixer[kColorInfoBarBackground] = {c.surface};
  mixer[kColorOmniboxResultsBackground] = {c.surface};
  // The page before its first paint and the side panel next to the card: the page color, never white in dark.
  mixer[kColorWebContentsBackground] = {c.page};
  mixer[kColorSidePanelBackground] = {c.page};
  mixer[kColorSidePanelContentBackground] = {c.page};
  mixer[kColorSidePanelContentAreaSeparator] = {c.separator};
  mixer[kColorToolbarContentAreaSeparator] = {c.separator};
  // Keyboard focus: the focus blue, not the space accent.
  mixer[ui::kColorFocusableBorderFocused] = {c.focus_ring};
  // Split view: only the focused pane is outlined, in the space's strong accent (the key's user color, M8); the
  // other panes have none.
  mixer[kColorMultiContentsViewActiveContentOutline] = {c.split_focus};
  mixer[kColorMultiContentsViewInactiveContentOutline] = {SK_ColorTRANSPARENT};
  // Chromium's own dialogs (extension install and permission prompts): gray secondary buttons and macOS blue default
  // buttons, never the theme's tonal navy.
  const bool dark = key.color_mode == ui::ColorProviderKey::ColorMode::kDark;
  const SkColor tonal = dark ? SkColorSetRGB(0x3a, 0x3b, 0x3e) : SkColorSetRGB(0xe5, 0xe5, 0xea);
  const SkColor tonal_ink = dark ? SkColorSetRGB(0xe8, 0xe8, 0xea) : SkColorSetRGB(0x1d, 0x1d, 0x1f);
  const SkColor prominent = dark ? SkColorSetRGB(0x0a, 0x84, 0xff) : SkColorSetRGB(0x00, 0x7a, 0xff);
  mixer[ui::kColorButtonBackgroundTonal] = {tonal};
  mixer[ui::kColorButtonBackgroundTonalFocused] = {tonal};
  mixer[ui::kColorButtonForegroundTonal] = {tonal_ink};
  mixer[ui::kColorButtonBackgroundProminent] = {prominent};
  mixer[ui::kColorButtonBackgroundProminentFocused] = {prominent};
  mixer[ui::kColorButtonForegroundProminent] = {SK_ColorWHITE};
  // Chromium's WebUI pages (settings, history, extensions) in dark: a neutral #1f2123 page, not a
  // tone of the theme's seed color.
  if (dark) {
    mixer[ui::kColorSysBase] = {SkColorSetRGB(0x1f, 0x21, 0x23)};
    mixer[ui::kColorSysSurface] = {SkColorSetRGB(0x1f, 0x21, 0x23)};
    mixer[ui::kColorSysSurface1] = {SkColorSetRGB(0x25, 0x27, 0x29)};
    mixer[ui::kColorSysSurface2] = {SkColorSetRGB(0x28, 0x2a, 0x2d)};
    mixer[ui::kColorSysSurface3] = {SkColorSetRGB(0x2c, 0x2e, 0x31)};
    mixer[ui::kColorSysSurface4] = {SkColorSetRGB(0x30, 0x32, 0x35)};
    mixer[ui::kColorSysSurface5] = {SkColorSetRGB(0x33, 0x35, 0x38)};
    mixer[ui::kColorSysBaseContainer] = {SkColorSetRGB(0x28, 0x2a, 0x2d)};
    mixer[ui::kColorSysBaseContainerElevated] = {SkColorSetRGB(0x2c, 0x2e, 0x31)};
    mixer[ui::kColorSysTonalContainer] = {tonal};
    mixer[ui::kColorSysOnTonalContainer] = {tonal_ink};
    mixer[kColorWebuiPageBackground] = {SkColorSetRGB(0x1f, 0x21, 0x23)};
    mixer[kColorSettingsWebuiPageBackground] = {SkColorSetRGB(0x1f, 0x21, 0x23)};
    mixer[kColorWebuiCardBackground] = {SkColorSetRGB(0x28, 0x2a, 0x2d)};
  }
}

}  // namespace clu
