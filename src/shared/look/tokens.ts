// Clu's look (milestone M11, docs/chromium/m11.md): its design values, in one place. Where a number scales with a
// user setting (sidebar width, window size, space colors, grain, favorites column count), the rule behind it is
// kept instead (insets, anchors to an edge, ratios).
//
// Consumers:
// - src/renderer/common/tokens.css carries every CSS value (tokens.test.ts checks each variable against this file);
// - scripts/chromium/gen-mojom.ts writes chromium/clu/browser/ui/views/frame/clu_look_tokens.h from it, so the
//   native layout, the traffic lights, dialogs, the find bar and the color mixer read the same numbers;
// - targets.ts lists what the screenshot suites measure against these values.
//
// Colors that sit on the space tint are black (light) or white (dark) at an alpha: every sidebar fill and glyph is
// derived that way, so one alpha gives the right color on any tint (derive.ts).

/** An sRGB color with alpha, channels 0..255, alpha 0..1. */
export interface LookColor { r: number; g: number; b: number; a: number }
const rgba = (r: number, g: number, b: number, a = 1): LookColor => ({ r, g, b, a })
const black = (a: number) => rgba(0, 0, 0, a)
const white = (a: number) => rgba(255, 255, 255, a)
/** The light glyph base (#0e0f10) and dark highlight base (#fafbff). */
const ink0 = (a: number) => rgba(14, 15, 16, a)
const snow = (a: number) => rgba(250, 251, 255, a)

export interface LookShadow { x: number; y: number; blur: number; spread: number; color: LookColor }
const shadow = (y: number, blur: number, color: LookColor): LookShadow => ({ x: 0, y, blur, spread: 0, color })

export const LOOK = {
  window: {
    /** System-drawn on macOS 26 (estimated 16.5); Clu asks for nothing and checks the result. */
    cornerRadius: 16.5,
    /** Window buttons: first center in window points, diameter and center pitch as macOS draws them. */
    trafficLights: { firstCenterX: 19.75, centerY: 23.75, diameter: 12, pitch: 23 }
  },
  card: {
    /** Page card inset from the window's top, right and bottom edges, and from the left one with the sidebar hidden. */
    inset: 10,
    radius: 6.5,
    shadow: shadow(1.5, 7, black(0.23)),
    /** Empty page: the space tint at this share over white. */
    emptyTintShare: 0.33
  },
  sidebar: {
    /** Content inset on both sides of the sidebar; the card starts at the sidebar's right edge. */
    inset: 8,
    /** The top row: its buttons at y 7.5, centered 0.25 above the traffic lights. */
    topRow: { centerY: 23.5, height: 32 },
    toolbar: {
      pitch: 34,
      /** The sidebar toggle's center, after the lights (window x). */
      toggleCenterX: 90.25,
      /** Back, forward and reload follow the toggle at the pitch (centers 124, 158, 192). */
      reloadCenterX: 192,
      hoverBox: 32,
      /** Up to 211 wide, back, forward and "..." are 28 boxes on a 28 pitch. */
      compactBox: 28,
      hoverRadius: 7.5,
      iconStroke: 1.5
    },
    urlPill: {
      /** Inset from each sidebar edge (the pill is 0.5 wider than the content span). */
      insetX: 7.5,
      top: 45.5,
      height: 36.5,
      radius: 13,
      fontSize: 12.5,
      fontWeight: 500,
      iconSize: 12,
      /** Magnifier and host text from the pill's left edge. */
      iconX: 12.5,
      textX: 35,
      hostTextX: 14,
      /** Copy link and site controls: centers from the pill's right edge. */
      buttonCentersFromRight: [47.25, 19],
      hoverMs: 95
    },
    favorites: {
      /** First row's top under the URL pill. */
      gapAbove: 8,
      tileHeight: 47,
      tileMinWidth: 60.5,
      tileRadius: 12.5,
      gap: 8,
      favicon: 16,
      hoverInMs: 100,
      hoverOutMs: 110
    },
    spaceHeader: { height: 36, fontSize: 13, fontWeight: 590, iconX: 9.5, textX: 36.5 },
    divider: { thickness: 1 },
    row: {
      height: 36,
      gap: 5,
      /** Favicon left inside the row (window x 18 with the 8 inset) and the title's left (window x 45). */
      faviconX: 10,
      titleX: 37,
      favicon: 16,
      fontSize: 14,
      fontWeight: 400,
      activeRadius: 14,
      hoverRadius: 12.5,
      activeShadow: shadow(1, 1.5, black(0.15)),
      hoverInMs: 0,
      hoverOutMs: 33,
      pressScale: 0.985,
      pressMs: 75,
      /** Close: glyph 11, centered 18 from the row's right edge; its own hover box 24 with radius 7. */
      close: { glyph: 11, centerFromRight: 18, box: 24, radius: 7, inMs: 85 },
      /** Speaker glyph slot: the title moves right by this much while a tab plays or is muted. */
      audioShift: 25.5,
      audioShiftMs: 150,
      speakerBox: 23,
      /** The speaker's hover box starts this far left of the slot after the favicon (glyph at x 45, box x 42 to 65). */
      speakerInset: 3,
      /** The speaker shows once a tab has played for this long. */
      speakerInMs: 800,
      speakerRadius: 6,
      newTabFontSize: 13.5,
      folderIndent: 16,
      folderFontWeight: 590
    },
    /** Space items: 36 hit boxes on a 34 pitch, centered between the Library and +. */
    switcher: { pitch: 34, hit: 36, icon: 15, dot: 8, centerFromBottom: 25, hoverBox: 32, hoverRadius: 7.5 },
    floating: { inset: 5, radius: 12 },
    mediaBar: { height: 33, radius: 8.75 },
    /** Sidebar show (dock) and hide with a page loaded: the card edge settles in about 85 and 80 ms, ease-out. */
    showMs: 85,
    hideMs: 80,
    /** Sampled progress of the hide (ms, share of the card edge's travel): 41% at 25 ms, 61 at 50, 88 at 63, all of it
     *  at 80. Nearly even, where the dock is an ease-out. */
    hideCurve: [[0, 0], [25, 0.41], [50, 0.61], [63, 0.88], [80, 1]] as [number, number][]
  },
  loading: {
    /** In the margin above the page card, centered on the card. */
    top: 3,
    height: 4,
    waitingWidth: 48,
    trackWidth: 128,
    pulseMs: 1600,
    /** Receiving (frames 729 to 796): one 67-frame cycle in thirds, all linear: the left end drains
     *  across the track, the track stays empty, the right end fills it again. */
    sweepMs: 1117,
    /** Waiting to receiving (frames 664 to 684): the pill widens from its center to the track and darkens, full dark
     *  until the first drain starts. */
    openMs: 320,
    /** Load done (frames 1673 to 1695): the track fills, then shrinks to its center while it fades. */
    doneMs: 370,
    doneScale: 0.25
  },
  command: {
    /** Centered on the window; the top at this share of the window height. */
    topShare: 231 / 800,
    maxWidth: 766,
    radius: 14,
    border: { width: 0.5, color: black(0.34) },
    shadow: shadow(25, 96, black(0.45)),
    inputHeight: 64,
    inputFontSize: 17,
    inputFontWeight: 500,
    iconCenterX: 32,
    textX: 53,
    dividerWidth: 0.5,
    rowPitch: 50,
    rowHeight: 46,
    rowInsetX: 10,
    rowRadius: 7.5,
    firstRowTop: 70.5,
    titleFontSize: 13.5,
    labelFontSize: 12.5,
    arrowPill: 24,
    arrowPillRadius: 6,
    /** "Switch to Tab" to its arrow pill, and the white tile under a selected tab's favicon (read by eye). */
    badgeGap: 9,
    tileRadius: 5,
    maxRows: 5,
    openMs: 0,
    closeMs: 0
  },
  tooltip: {
    height: 28,
    radius: 14,
    /** The corner toward the pointer. */
    tipRadius: 4,
    paddingX: 11,
    fontSize: 12,
    fontWeight: 500,
    offset: 9.5,
    shadow: shadow(1, 8, black(0.25)),
    delayMs: 450,
    outMs: 280,
    hoverCardDelayMs: 1500,
    hoverCardOutMs: 90,
    keycap: { size: 19, height: 19.5, radius: 4, gap: 3.5, labelGap: 6 },
    /** The tooltip fill: the space tint at this share over white. */
    tintShare: 0.35
  },
  menu: {
    radius: 13,
    paddingY: 5,
    itemHeight: 24,
    highlightInset: 5,
    highlightRadius: 6,
    fontSize: 13,
    separator: 11,
    /** The material: the window tint at this share over near-white (#fdeff0 over the pink tint). */
    tintShare: 0.2,
    outline: black(0.25),
    rim: white(0.5)
  },
  popover: { radius: 15 },
  /** The Library (Downloads and Archived Tabs): the sidebar widens to this; a section rail, then the list column. */
  library: { width: 417, railWidth: 112, railItemHeight: 80, railItemRadius: 12, railLabelSize: 11, searchHeight: 36, searchRadius: 10, rowPitch: 56, iconCenterX: 30, titleX: 52, titleSize: 13.5, subtitleSize: 11.5, openMs: 290 },
  dialog: { radius: 16.5, buttonHeight: 37.5, buttonRadius: 6 },
  split: {
    radius: 6,
    gap: 10,
    focusWidth: 2,
    handle: { w: 2, h: 24 },
    toolbarHeight: 32,
    /** The sidebar split item: sub-pills on the active plate, hairlines between panes at rest. */
    pill: { height: 28, inset: 5, gap: 8.5, radius: 9 },
    hairlineHeight: 16
  },
  peek: {
    radius: 8,
    insetY: 12,
    buttonSize: 34,
    buttonGap: 13,
    buttonPitch: 43,
    /** The first rail button's center below the card's top. */
    firstButtonCenterY: 24.25,
    /** The page card behind is washed with the window tint at this share (white reads #f1f0fe to #f9f4fb). */
    washShare: 0.57,
    /** Soft shadows: about 8 pt around the card, 3 pt under each button. */
    shadow: shadow(2, 16, black(0.18)),
    buttonShadow: shadow(1, 3, black(0.2)),
    backdropScale: 0.983,
    openMs: 185
  },
  mini: { radius: 17, inputHeight: 64, openMs: 95, closeMs: 150, openScale: 0.98, closeButton: { size: 24, radius: 6 } },
  settings: { width: 684, radius: 17, cardRadius: 7.5 },
  motion: {
    /** Space switch: the tint cross-fades, the content slides on a critically damped spring. */
    spaceTintMs: 120,
    spaceSlideMs: 240,
    /** Sampled progress of the slide (ms to share), a spring with no overshoot. */
    spaceSlideCurve: [[0, 0], [16, 0.28], [50, 0.62], [91, 0.85], [125, 0.95], [191, 0.99], [240, 1]] as [number, number][],
    /** Sampled ease-out of the tint cross-fade. */
    tintCurve: [[0, 0], [17, 0.3], [33, 0.55], [50, 0.75], [75, 0.9], [120, 1]] as [number, number][],
    folderCollapseMs: 225,
    folderExpandMs: 190,
    menuCloseMs: 190
  },
  /** Fixed colors (light, dark). The dark popover, command bar and Peek buttons are a neutral gray of the dark
   *  window (docs/chromium/ui-polish.md). */
  color: {
    light: {
      accent: rgba(0x90, 0x95, 0xfb),
      accentStrong: rgba(0x20, 0x28, 0xf2),
      focusRing: rgba(0x45, 0x8a, 0xff),
      itemBg: ink0(0.05),
      itemHover: black(0.088),
      favoriteHover: black(0.0975),
      pillHover: black(0.085),
      buttonHover: black(0.07),
      rowHover: black(0.05),
      rowCurrent: rgba(246, 245, 245, 0.9),
      rowPressed: ink0(0.065),
      rowMulti: black(0.1),
      closeHover: black(0.08),
      divider: black(0.05),
      todayDivider: black(0.075),
      separator: black(0.15),
      glyph: black(0.57),
      label: black(0.46),
      ink3: black(0.31),
      glyphInactive: black(0.265),
      glyphDisabled: black(0.225),
      pillText: black(0.63),
      pillPlaceholder: black(0.35),
      dragHint: black(0.2),
      loadingInk: black(0.8),
      loadingTrack: black(0.2),
      popoverBg: rgba(0xfa, 0xfb, 0xff),
      popoverShadow: rgba(0x15, 0x1c, 0x32, 0.3),
      cmdBg: rgba(0xf8, 0xf9, 0xf9),
      cmdText: black(0.8),
      cmdText2: black(0.33),
      cmdPlaceholder: black(0.5),
      cmdHairline: black(0.1),
      cmdAccessory: black(0.05),
      cmdAccessoryInk: black(0.5),
      cmdCloseBg: rgba(0xe6, 0xe6, 0xe8),
      peekButtonBg: rgba(0xfd, 0xfd, 0xff),
      menuHighlight: rgba(0x52, 0x8f, 0xf2),
      splitPill: black(0.07),
      splitHairline: black(0.12),
      mediaBarBg: rgba(0xfd, 0xfd, 0xff),
      pageBg: white(1)
    },
    dark: {
      accent: rgba(0x90, 0x95, 0xfb),
      accentStrong: rgba(0x6a, 0x70, 0xff),
      focusRing: rgba(0x45, 0x8a, 0xff),
      itemBg: snow(0.1),
      itemHover: snow(0.15),
      favoriteHover: snow(0.15),
      pillHover: snow(0.15),
      buttonHover: snow(0.12),
      rowHover: snow(0.05),
      rowCurrent: snow(0.2),
      rowPressed: snow(0.065),
      rowMulti: snow(0.1),
      closeHover: snow(0.12),
      divider: white(0.05),
      todayDivider: white(0.075),
      separator: white(0.15),
      glyph: white(0.55),
      label: white(0.45),
      ink3: white(0.33),
      glyphInactive: white(0.3),
      glyphDisabled: white(0.25),
      pillText: white(0.66),
      pillPlaceholder: white(0.3),
      dragHint: white(0.2),
      loadingInk: white(0.8),
      loadingTrack: white(0.2),
      popoverBg: rgba(0x2a, 0x2a, 0x2d),
      popoverShadow: rgba(0, 0, 0, 0.5),
      cmdBg: rgba(0x2a, 0x2a, 0x2d),
      cmdText: white(0.8),
      cmdText2: white(0.33),
      cmdPlaceholder: white(0.6),
      cmdHairline: white(0.1),
      cmdAccessory: white(0.05),
      cmdAccessoryInk: white(0.66),
      cmdCloseBg: white(0.1),
      peekButtonBg: rgba(0x2a, 0x2a, 0x2d),
      menuHighlight: rgba(0x52, 0x8f, 0xf2),
      splitPill: white(0.1),
      splitHairline: white(0.15),
      mediaBarBg: snow(0.1),
      pageBg: rgba(0x21, 0x22, 0x22)
    }
  },
  /** A space with no theme: the default new-space gradient (top-left to bottom-right); `dark` is Clu's own.
   *  tokens.css starts --tint at the gradient's middle (`--tint`). */
  defaultTint: { from: rgba(0xda, 0xdb, 0xf1), to: rgba(0xef, 0xe4, 0xe4), dark: rgba(0x2c, 0x2b, 0x36) }
} as const

export type LookColorName = keyof typeof LOOK.color.light

const hex2 = (n: number) => Math.round(Math.min(255, Math.max(0, n))).toString(16).padStart(2, '0')
const trim = (n: number) => String(Math.round(n * 1000) / 1000)

/** `#rrggbb` for an opaque color, `rgb(r g b / a%)` otherwise (the form tokens.css uses). */
export function cssColor(c: LookColor): string {
  if (c.a >= 1) return `#${hex2(c.r)}${hex2(c.g)}${hex2(c.b)}`
  return `rgb(${Math.round(c.r)} ${Math.round(c.g)} ${Math.round(c.b)} / ${trim(c.a * 100)}%)`
}

export function cssShadow(s: LookShadow): string {
  const px = (n: number) => (n === 0 ? '0' : `${trim(n)}px`)
  return `${px(s.x)} ${px(s.y)} ${px(s.blur)}${s.spread ? ` ${px(s.spread)}` : ''} ${cssColor(s.color)}`
}

/** A sampled curve as a CSS `linear()` easing over its own duration. */
export function cssLinear(curve: readonly (readonly [number, number])[]): string {
  const end = curve.at(-1)?.[0] ?? 1
  return `linear(${curve.map(([t, p], i) => (i === 0 || i === curve.length - 1 ? trim(p) : `${trim(p)} ${trim((t / end) * 100)}%`)).join(', ')})`
}

/** The tokens.css variables M11 owns, as the CSS text each must hold (light set; `dark` for the dark overrides). */
export function cssTokens(mode: 'light' | 'dark'): Record<string, string> {
  const c = LOOK.color[mode]
  const colorVars = Object.fromEntries(
    (Object.keys(c) as LookColorName[]).map((k) => [`--${k.replace(/[A-Z]/g, (m) => `-${m.toLowerCase()}`)}`, cssColor(c[k])])
  )
  if (mode === 'dark') return colorVars
  const px = (n: number) => `${trim(n)}px`
  const ms = (n: number) => `${trim(n)}ms`
  const s = LOOK.sidebar
  return {
    ...colorVars,
    '--row-h': px(s.row.height),
    '--row-r': px(s.row.activeRadius),
    '--row-hover-r': px(s.row.hoverRadius),
    '--row-px': px(s.row.faviconX),
    '--fav': px(s.row.favicon),
    '--row-gap': px(s.row.titleX - s.row.faviconX - s.row.favicon),
    '--row-pitch-gap': px(s.row.gap),
    '--tile-r': px(s.favorites.tileRadius),
    '--tile-h': px(s.favorites.tileHeight),
    '--tile-min-w': px(s.favorites.tileMinWidth),
    '--tile-gap': px(s.favorites.gap),
    '--card-r': px(LOOK.card.radius),
    '--card-inset': px(LOOK.card.inset),
    '--shadow-card': cssShadow(LOOK.card.shadow),
    '--pop-r': px(LOOK.menu.radius),
    '--menu-item-h': px(LOOK.menu.itemHeight),
    '--menu-item-r': px(LOOK.menu.highlightRadius),
    '--fs-menu': px(LOOK.menu.fontSize),
    '--menu-pad-y': px(LOOK.menu.paddingY),
    '--menu-tint': `${trim(LOOK.menu.tintShare * 100)}%`,
    '--menu-outline': cssColor(LOOK.menu.outline),
    '--menu-rim': cssColor(LOOK.menu.rim),
    '--t-menu-out': ms(LOOK.motion.menuCloseMs),
    '--popover-r': px(LOOK.popover.radius),
    '--lib-rail-w': px(LOOK.library.railWidth),
    '--lib-rail-item-h': px(LOOK.library.railItemHeight),
    '--lib-rail-item-r': px(LOOK.library.railItemRadius),
    '--fs-lib-rail': px(LOOK.library.railLabelSize),
    '--lib-search-h': px(LOOK.library.searchHeight),
    '--lib-search-r': px(LOOK.library.searchRadius),
    '--lib-row-h': px(LOOK.library.rowPitch),
    '--lib-icon-x': px(LOOK.library.iconCenterX - s.row.favicon / 2),
    '--lib-title-gap': px(LOOK.library.titleX - LOOK.library.iconCenterX - s.row.favicon / 2),
    '--fs-lib-title': px(LOOK.library.titleSize),
    '--fs-lib-sub': px(LOOK.library.subtitleSize),
    '--sidebar-inset': px(s.inset),
    '--top-row-y': px(s.topRow.centerY - s.topRow.height / 2),
    '--top-row-h': px(s.topRow.height),
    '--tool-pitch': px(s.toolbar.pitch),
    '--tool-box': px(s.toolbar.hoverBox),
    '--tool-box-compact': px(s.toolbar.compactBox),
    '--tool-r': px(s.toolbar.hoverRadius),
    '--pill-top': px(s.urlPill.top),
    '--pill-h': px(s.urlPill.height),
    '--pill-r': px(s.urlPill.radius),
    '--pill-inset': px(s.urlPill.insetX),
    '--fs-pill': px(s.urlPill.fontSize),
    '--fs-row': px(s.row.fontSize),
    '--fs-header': px(s.spaceHeader.fontSize),
    '--fw-header': String(s.spaceHeader.fontWeight),
    '--fs-new-tab': px(s.row.newTabFontSize),
    '--fs-cmd': px(LOOK.command.inputFontSize),
    '--fs-cmd-row': px(LOOK.command.titleFontSize),
    '--fs-2': px(LOOK.tooltip.fontSize),
    '--space-header-h': px(s.spaceHeader.height),
    '--switch-pitch': px(s.switcher.pitch),
    '--switch-hit': px(s.switcher.hit),
    '--switch-icon': px(s.switcher.icon),
    '--switch-dot': px(s.switcher.dot),
    '--close-box': px(s.row.close.box),
    '--close-r': px(s.row.close.radius),
    '--folder-indent': px(s.row.folderIndent),
    '--audio-shift': px(s.row.audioShift),
    '--speaker-box': px(s.row.speakerBox),
    '--speaker-inset': px(s.row.speakerInset),
    '--shadow-row': cssShadow(s.row.activeShadow),
    '--cmd-w': px(LOOK.command.maxWidth),
    '--cmd-r': px(LOOK.command.radius),
    '--cmd-top': `${trim(LOOK.command.topShare * 100)}%`,
    '--cmd-input-h': px(LOOK.command.inputHeight),
    '--cmd-row-h': px(LOOK.command.rowHeight),
    '--cmd-row-gap': px(LOOK.command.rowPitch - LOOK.command.rowHeight),
    '--cmd-row-r': px(LOOK.command.rowRadius),
    '--cmd-row-x': px(LOOK.command.rowInsetX),
    '--cmd-icon-x': px(LOOK.command.iconCenterX - s.row.favicon / 2),
    '--cmd-icon-gap': px(LOOK.command.textX - LOOK.command.iconCenterX - s.row.favicon / 2),
    '--cmd-arrow-r': px(LOOK.command.arrowPillRadius),
    '--cmd-badge-gap': px(LOOK.command.badgeGap),
    '--cmd-tile-r': px(LOOK.command.tileRadius),
    '--fs-cmd-badge': px(LOOK.command.labelFontSize),
    '--cmd-close-r': px(LOOK.mini.closeButton.radius),
    '--cmd-border': `${px(LOOK.command.border.width)} solid ${cssColor(LOOK.command.border.color)}`,
    '--shadow-panel': cssShadow(LOOK.command.shadow),
    '--tip-h': px(LOOK.tooltip.height),
    '--tip-r': px(LOOK.tooltip.radius),
    '--tip-px': px(LOOK.tooltip.paddingX),
    '--tip-corner': px(LOOK.tooltip.tipRadius),
    '--keycap': px(LOOK.tooltip.keycap.size),
    '--keycap-h': px(LOOK.tooltip.keycap.height),
    '--keycap-r': px(LOOK.tooltip.keycap.radius),
    '--keycap-gap': px(LOOK.tooltip.keycap.gap),
    '--keycap-label-gap': px(LOOK.tooltip.keycap.labelGap),
    '--shadow-tip': cssShadow(LOOK.tooltip.shadow),
    '--tip-delay': ms(LOOK.tooltip.delayMs),
    '--tip-out': ms(LOOK.tooltip.outMs),
    '--hover-card-delay': ms(LOOK.tooltip.hoverCardDelayMs),
    '--split-r': px(LOOK.split.radius),
    '--split-gap': px(LOOK.split.gap),
    '--split-pill-h': px(LOOK.split.pill.height),
    '--split-pill-inset': px(LOOK.split.pill.inset),
    '--split-pill-gap': px(LOOK.split.pill.gap),
    '--split-pill-r': px(LOOK.split.pill.radius),
    '--split-hairline-h': px(LOOK.split.hairlineHeight),
    '--peek-r': px(LOOK.peek.radius),
    '--peek-inset-y': px(LOOK.peek.insetY),
    '--peek-wash': `${trim(LOOK.peek.washShare * 100)}%`,
    '--shadow-peek': cssShadow(LOOK.peek.shadow),
    '--shadow-peek-button': cssShadow(LOOK.peek.buttonShadow),
    '--mini-r': px(LOOK.mini.radius),
    '--dialog-r': px(LOOK.dialog.radius),
    '--settings-card-r': px(LOOK.settings.cardRadius),
    '--float-inset': px(s.floating.inset),
    '--float-r': px(s.floating.radius),
    '--t-hover-out': ms(s.row.hoverOutMs),
    '--t-press': ms(s.row.pressMs),
    '--t-close-hover': ms(s.row.close.inMs),
    '--t-hover-card-out': ms(LOOK.tooltip.hoverCardOutMs),
    '--press-scale': trim(s.row.pressScale),
    '--t-fade': ms(LOOK.motion.spaceTintMs),
    '--t-slow': ms(LOOK.motion.spaceSlideMs),
    '--spring': cssLinear(LOOK.motion.spaceSlideCurve),
    '--ease-tint': cssLinear(LOOK.motion.tintCurve)
  }
}
