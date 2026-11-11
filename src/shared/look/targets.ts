// What the M11 screenshot suites check (docs/chromium/m11.md section 4): each target names its look token, the
// value Clu must show (from tokens.ts), a tolerance, the state it is shot in and how it is read. The WebUI harness
// (e2e/chromium-webui/look.webui.ts) and clu_browsertests (CluLook*) run the same probes through
// chromium/clu/test/data/look/measure.js and judge them with compare.ts; the C++ side gets this table generated into
// clu_look_tokens.h. Targets with `native` are read by the browser test from Chromium itself, not from a WebUI.
import { LOOK, type LookColor } from './tokens'

/** The design.md section 5 states the suites shoot (the surfaces M11 styles; M6 surfaces pick the tokens up). */
export const LOOK_STATES = [
  'page-card',
  'page-card-dark',
  'empty-page',
  'rows-rest',
  'row-hover',
  'row-active',
  'row-pressed',
  'row-focus',
  'row-loading',
  'row-audible',
  'row-muted',
  'row-long-title',
  'favorites-1',
  'favorites-6',
  'favorites-12',
  'favorite-hover',
  'folders',
  'toolbar-hover',
  'tooltip',
  'command-empty',
  'command-typing',
  'command-selected',
  'menu-root',
  'native'
] as const
export type LookState = (typeof LOOK_STATES)[number]

/** Where a value is read. Boxes are in window points (the frame and overlay WebUIs fill the window). */
export type Probe =
  | { doc: 'frame' | 'overlay'; sel: string; box: 'x' | 'y' | 'w' | 'h' | 'cx' | 'cy' | 'fromRight' | 'fromBottom' }
  /** `transition-ms`: the background's fade (or the longest transition or animation); `transform-ms`: the press. */
  | { doc: 'frame' | 'overlay'; sel: string; css: 'radius' | 'font-size' | 'font-weight' | 'transition-ms' | 'transform-ms' | 'scale' }
  /** A screen pixel `at` (dx, dy) from the element's top-left; negative counts from the right or bottom edge. */
  | { doc: 'frame' | 'overlay'; sel: string; pixel: [number, number] }
  /** Gap between two elements: the second's top minus the first's bottom (`axis` x: left minus right). */
  | { doc: 'frame' | 'overlay'; sel: string; gapTo: string; axis?: 'x' | 'y' }
  /** Read by the browser test from Chromium (traffic lights, the page view, dialogs, the find bar, colors). */
  | { native: string }

/** How the expected color is formed: `color` composited over the sampled `base` (the space tint, white, or the
 *  command bar panel), or `share` of the tint over white (the empty page, tooltips). */
export type Expected =
  | { px: number }
  | { ms: number }
  | { ratio: number }
  | { color: LookColor; over?: 'tint' | 'panel' }
  | { tintShare: number }

export interface LookTarget { key: string; state: LookState; probe: Probe; expect: Expected; tol: number }

const L = LOOK
const S = LOOK.sidebar
const C = LOOK.color.light
const ROW = '.clu-tab-tree .clu-tab-row'
const ACTIVE_ROW = '.clu-tab-tree .clu-tab-row[data-active="true"]'
const REST_ROW = '.clu-tab-tree .clu-tab-row[data-active="false"]'
const PILL = '[data-shell="url-pill"]'
const CARD = '[data-surface="page-card"]'
const TILE = '.clu-favorites .clu-favorite-tile'
const CMD = '.clu-cmd'

const px = (n: number) => ({ px: n })
const box = (doc: 'frame' | 'overlay', sel: string, b: 'x' | 'y' | 'w' | 'h' | 'cx' | 'cy' | 'fromRight' | 'fromBottom') => ({ doc, sel, box: b })
const css = (doc: 'frame' | 'overlay', sel: string, c: 'radius' | 'font-size' | 'font-weight' | 'transition-ms' | 'transform-ms' | 'scale') => ({ doc, sel, css: c })
const pixel = (doc: 'frame' | 'overlay', sel: string, at: [number, number]) => ({ doc, sel, pixel: at })

// Tolerances: 1 pt for boxes (Clu lays out on whole CSS pixels in places), 0.25 for radii and type, 4 levels per
// channel for colors, 20 ms or one 60 Hz frame for timings.
const T = { box: 1, radius: 0.25, type: 0.25, color: 4, ms: 20 }

export const LOOK_TARGETS: readonly LookTarget[] = [
  // chrome: the window, the card and the native side
  { key: 'chrome.trafficLightCenters', state: 'native', probe: { native: 'traffic-light-0-cx' }, expect: px(L.window.trafficLights.firstCenterX), tol: T.box },
  { key: 'chrome.trafficLightCenters', state: 'native', probe: { native: 'traffic-light-0-cy' }, expect: px(L.window.trafficLights.centerY), tol: T.box },
  { key: 'chrome.trafficLightCenters', state: 'native', probe: { native: 'traffic-light-pitch' }, expect: px(L.window.trafficLights.pitch), tol: T.box },
  // The browser test reads the button's frame, which macOS 26 may make 14 for the 12 pt disc it draws.
  { key: 'chrome.trafficLightDiameter', state: 'native', probe: { native: 'traffic-light-diameter' }, expect: px(L.window.trafficLights.diameter), tol: 2 },
  { key: 'chrome.cardInsetTop', state: 'native', probe: { native: 'page-view-top' }, expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardInsetRight', state: 'native', probe: { native: 'page-view-right' }, expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardInsetBottom', state: 'native', probe: { native: 'page-view-bottom' }, expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardRadius', state: 'native', probe: { native: 'page-view-radius' }, expect: px(L.card.radius), tol: T.radius },
  { key: 'menus.modalDialog', state: 'native', probe: { native: 'dialog-top' }, expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardInsetTop', state: 'native', probe: { native: 'find-bar-top' }, expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardInsetRight', state: 'native', probe: { native: 'find-bar-right' }, expect: px(L.card.inset), tol: T.box },
  { key: 'menus.contextMenu', state: 'native', probe: { native: 'color-popover' }, expect: { color: C.popoverBg }, tol: T.color },

  { key: 'chrome.cardInsetTop', state: 'page-card', probe: box('frame', CARD, 'y'), expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardInsetRight', state: 'page-card', probe: box('frame', CARD, 'fromRight'), expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardInsetBottom', state: 'page-card', probe: box('frame', CARD, 'fromBottom'), expect: px(L.card.inset), tol: T.box },
  { key: 'chrome.cardRadius', state: 'page-card', probe: css('frame', CARD, 'radius'), expect: px(L.card.radius), tol: T.radius },
  { key: 'chrome.cardRadius', state: 'page-card-dark', probe: css('frame', CARD, 'radius'), expect: px(L.card.radius), tol: T.radius },
  { key: 'chrome.emptyPageFill', state: 'empty-page', probe: pixel('frame', CARD, [40, 40]), expect: { tintShare: L.card.emptyTintShare }, tol: T.color },
  { key: 'chrome.sidebarContentInsetX', state: 'rows-rest', probe: box('frame', ROW, 'x'), expect: px(S.inset), tol: T.box },

  // the top row and the URL pill
  { key: 'chrome.toolbarIconPitch', state: 'rows-rest', probe: box('frame', '[data-shell="sidebar-toggle"]', 'cy'), expect: px(S.topRow.centerY), tol: T.box },
  { key: 'chrome.sidebarToggleIcon', state: 'rows-rest', probe: box('frame', '[data-shell="sidebar-toggle"]', 'cx'), expect: px(S.toolbar.toggleCenterX), tol: T.box },
  { key: 'chrome.reloadIcon', state: 'rows-rest', probe: box('frame', '[data-shell="nav-reload"]', 'cy'), expect: px(S.topRow.centerY), tol: T.box },
  { key: 'chrome.reloadIcon', state: 'rows-rest', probe: box('frame', '[data-shell="nav-reload"]', 'cx'), expect: px(S.toolbar.reloadCenterX), tol: T.box },
  { key: 'chrome.toolbarButtonHover', state: 'toolbar-hover', probe: box('frame', '[data-shell="sidebar-toggle"]', 'w'), expect: px(S.toolbar.hoverBox), tol: T.box },
  { key: 'chrome.toolbarButtonHover', state: 'toolbar-hover', probe: css('frame', '[data-shell="sidebar-toggle"]', 'radius'), expect: px(S.toolbar.hoverRadius), tol: T.radius },
  { key: 'chrome.toolbarButtonHover', state: 'toolbar-hover', probe: pixel('frame', '[data-shell="sidebar-toggle"]', [3, 16]), expect: { color: C.buttonHover, over: 'tint' }, tol: T.color },
  { key: 'chrome.urlPill', state: 'rows-rest', probe: box('frame', PILL, 'x'), expect: px(S.urlPill.insetX), tol: T.box },
  { key: 'chrome.urlPill', state: 'rows-rest', probe: box('frame', PILL, 'y'), expect: px(S.urlPill.top), tol: T.box },
  { key: 'chrome.urlPill', state: 'rows-rest', probe: box('frame', PILL, 'h'), expect: px(S.urlPill.height), tol: T.box },
  { key: 'chrome.urlPill', state: 'rows-rest', probe: css('frame', PILL, 'radius'), expect: px(S.urlPill.radius), tol: T.radius },
  { key: 'chrome.urlPill', state: 'rows-rest', probe: pixel('frame', PILL, [6, 18]), expect: { color: C.itemBg, over: 'tint' }, tol: T.color },
  { key: 'chrome.urlPill', state: 'rows-rest', probe: css('frame', '[data-shell="url-host"]', 'font-size'), expect: px(S.urlPill.fontSize), tol: T.type },

  // favorites
  { key: 'rows.favoriteTile', state: 'favorites-6', probe: box('frame', TILE, 'h'), expect: px(S.favorites.tileHeight), tol: T.box },
  { key: 'rows.favoriteTile', state: 'favorites-6', probe: css('frame', TILE, 'radius'), expect: px(S.favorites.tileRadius), tol: T.radius },
  { key: 'rows.favoriteTile', state: 'favorites-6', probe: pixel('frame', TILE, [4, 24]), expect: { color: C.itemBg, over: 'tint' }, tol: T.color },
  { key: 'rows.favoritesTopFromPill', state: 'favorites-6', probe: { doc: 'frame', sel: PILL, gapTo: TILE }, expect: px(S.favorites.gapAbove), tol: T.box },
  { key: 'rows.favoriteGrid', state: 'favorites-12', probe: { doc: 'frame', sel: '.clu-favorites .clu-favorite:nth-child(1) .clu-favorite-tile', gapTo: '.clu-favorites .clu-favorite:nth-child(4) .clu-favorite-tile' }, expect: px(S.favorites.gap), tol: T.box },
  { key: 'rows.favoriteTile', state: 'favorites-1', probe: box('frame', TILE, 'h'), expect: px(S.favorites.tileHeight), tol: T.box },
  { key: 'rows.favoriteHover', state: 'favorite-hover', probe: pixel('frame', TILE, [4, 24]), expect: { color: C.favoriteHover, over: 'tint' }, tol: T.color },
  { key: 'motion.rowHoverFade', state: 'favorite-hover', probe: css('frame', TILE, 'transition-ms'), expect: { ms: S.favorites.hoverInMs }, tol: T.ms },
  { key: 'motion.rowHoverFade', state: 'favorites-6', probe: css('frame', TILE, 'transition-ms'), expect: { ms: S.favorites.hoverOutMs }, tol: T.ms },

  // space header and rows
  { key: 'rows.spaceHeaderRow', state: 'rows-rest', probe: css('frame', '.clu-space-header', 'font-size'), expect: px(S.spaceHeader.fontSize), tol: T.type },
  { key: 'rows.spaceHeaderRow', state: 'rows-rest', probe: css('frame', '.clu-space-header', 'font-weight'), expect: px(S.spaceHeader.fontWeight), tol: 10 },
  { key: 'rows.tabRowHeight', state: 'rows-rest', probe: box('frame', REST_ROW, 'h'), expect: px(S.row.height), tol: T.box },
  { key: 'rows.tabRowGap', state: 'rows-rest', probe: { doc: 'frame', sel: `${ROW}:nth-child(1 of ${ROW})`, gapTo: `${ROW}:nth-child(2 of ${ROW})` }, expect: px(S.row.gap), tol: T.box },
  { key: 'rows.tabRowBox', state: 'rows-rest', probe: box('frame', REST_ROW, 'w'), expect: px(0), tol: -1 },
  { key: 'rows.tabFavicon', state: 'rows-rest', probe: box('frame', `${REST_ROW} :is(.clu-row-favicon, .clu-letter-tile)`, 'x'), expect: px(S.inset + S.row.faviconX), tol: T.box },
  { key: 'rows.tabFavicon', state: 'rows-rest', probe: box('frame', `${REST_ROW} :is(.clu-row-favicon, .clu-letter-tile)`, 'w'), expect: px(S.row.favicon), tol: T.box },
  { key: 'rows.tabTitle', state: 'rows-rest', probe: box('frame', `${REST_ROW} .clu-row-title`, 'x'), expect: px(S.inset + S.row.titleX), tol: T.box },
  { key: 'rows.tabTitle', state: 'rows-rest', probe: css('frame', `${REST_ROW} .clu-row-title`, 'font-size'), expect: px(S.row.fontSize), tol: T.type },
  { key: 'rows.rowHover', state: 'row-hover', probe: css('frame', REST_ROW, 'radius'), expect: px(S.row.hoverRadius), tol: T.radius },
  { key: 'rows.rowHover', state: 'row-hover', probe: pixel('frame', REST_ROW, [-40, 4]), expect: { color: C.rowHover, over: 'tint' }, tol: T.color },
  { key: 'rows.closeButton', state: 'row-hover', probe: box('frame', `${REST_ROW} .clu-row-close`, 'cx'), expect: px(0), tol: -1 },
  { key: 'rows.rowActive', state: 'row-active', probe: css('frame', ACTIVE_ROW, 'radius'), expect: px(S.row.activeRadius), tol: T.radius },
  { key: 'rows.rowActive', state: 'row-active', probe: pixel('frame', ACTIVE_ROW, [-40, 8]), expect: { color: C.rowCurrent, over: 'tint' }, tol: T.color },
  { key: 'rows.rowPressed', state: 'row-pressed', probe: css('frame', REST_ROW, 'scale'), expect: { ratio: S.row.pressScale }, tol: 0.003 },
  { key: 'motion.rowPress', state: 'row-pressed', probe: css('frame', REST_ROW, 'transform-ms'), expect: { ms: S.row.pressMs }, tol: T.ms },
  { key: 'rows.rowAudio', state: 'row-audible', probe: box('frame', `${ROW}[data-audible="true"][data-muted="false"] .clu-row-title`, 'x'), expect: px(S.inset + S.row.titleX + S.row.audioShift), tol: T.box },
  { key: 'rows.rowAudio', state: 'row-muted', probe: box('frame', `${ROW}[data-muted="true"] .clu-row-title`, 'x'), expect: px(S.inset + S.row.titleX + S.row.audioShift), tol: T.box },
  { key: 'rows.rowLoading', state: 'row-loading', probe: box('frame', `${ROW}[data-loading="true"]`, 'h'), expect: px(S.row.height), tol: T.box },
  { key: 'chrome.loadingIndicator', state: 'row-loading', probe: box('frame', '[data-shell="card-loading"]', 'y'), expect: px(L.loading.top), tol: T.box },
  { key: 'chrome.loadingIndicator', state: 'row-loading', probe: box('frame', '[data-shell="card-loading"]', 'h'), expect: px(L.loading.height), tol: T.box },
  { key: 'rows.tabRowHeight', state: 'row-long-title', probe: box('frame', REST_ROW, 'h'), expect: px(S.row.height), tol: T.box },
  { key: 'rows.folderNesting', state: 'folders', probe: box('frame', `${ROW}[aria-level="2"] :is(.clu-row-favicon, .clu-letter-tile)`, 'x'), expect: px(S.inset + S.row.faviconX + S.row.folderIndent), tol: T.box },
  { key: 'rows.tabRowHeight', state: 'row-focus', probe: box('frame', REST_ROW, 'h'), expect: px(S.row.height), tol: T.box },

  // spaces
  { key: 'spaces.switcherPitch', state: 'rows-rest', probe: { doc: 'frame', sel: '.clu-space-icon[data-index="0"]', gapTo: '.clu-space-icon[data-index="1"]', axis: 'x' }, expect: px(0), tol: -1 },
  { key: 'chrome.bottomBarCenterY', state: 'rows-rest', probe: box('frame', '.clu-space-icon[data-index="0"]', 'fromBottom'), expect: px(0), tol: -1 },

  // tooltips
  { key: 'menus.toolbarTooltip', state: 'tooltip', probe: box('frame', '.clu-tooltip', 'h'), expect: px(L.tooltip.height), tol: T.box },
  { key: 'menus.toolbarTooltip', state: 'tooltip', probe: css('frame', '.clu-tooltip', 'font-size'), expect: px(L.tooltip.fontSize), tol: T.type },

  // command bar
  { key: 'command.panel', state: 'command-empty', probe: css('overlay', CMD, 'radius'), expect: px(L.command.radius), tol: T.radius },
  { key: 'command.panel', state: 'command-empty', probe: box('overlay', CMD, 'cx'), expect: px(640), tol: T.box },
  { key: 'command.panel', state: 'command-empty', probe: box('overlay', CMD, 'y'), expect: px(231), tol: 2 },
  { key: 'command.panel', state: 'command-empty', probe: box('overlay', CMD, 'w'), expect: px(L.command.maxWidth), tol: T.box },
  { key: 'command.panel', state: 'command-empty', probe: pixel('overlay', CMD, [20, 20]), expect: { color: C.cmdBg }, tol: T.color },
  { key: 'command.inputRowHeight', state: 'command-empty', probe: box('overlay', '.clu-cmd-field', 'h'), expect: px(L.command.inputHeight), tol: T.box },
  { key: 'command.inputFont', state: 'command-typing', probe: css('overlay', '.clu-cmd-field input', 'font-size'), expect: px(L.command.inputFontSize), tol: T.type },
  { key: 'command.resultRowSelectedBox', state: 'command-selected', probe: box('overlay', '.clu-cmd-row[aria-selected="true"]', 'h'), expect: px(L.command.rowHeight), tol: T.box },
  { key: 'command.resultRowSelectedBox', state: 'command-selected', probe: css('overlay', '.clu-cmd-row[aria-selected="true"]', 'radius'), expect: px(L.command.rowRadius), tol: T.radius },
  { key: 'command.resultRowPitch', state: 'command-selected', probe: { doc: 'overlay', sel: '.clu-cmd-row:has(+ .clu-cmd-row)', gapTo: '.clu-cmd-row + .clu-cmd-row' }, expect: px(L.command.rowPitch - L.command.rowHeight), tol: T.box },
  { key: 'command.resultTitle', state: 'command-selected', probe: css('overlay', '.clu-cmd-row .clu-cmd-title', 'font-size'), expect: px(L.command.titleFontSize), tol: T.type },
  { key: 'motion.commandBarOpen', state: 'command-empty', probe: css('overlay', CMD, 'transition-ms'), expect: { ms: L.command.openMs }, tol: T.ms },

  // overlay menus
  { key: 'menus.contextMenu', state: 'menu-root', probe: css('overlay', '.clu-menu', 'radius'), expect: px(L.menu.radius), tol: T.radius },
  { key: 'menus.contextMenu', state: 'menu-root', probe: box('overlay', '.clu-menu-item', 'h'), expect: px(L.menu.itemHeight), tol: T.box }
]

/** Targets whose expected value is computed at measurement time from the layout (tol -1 above): sidebar width rules. */
export function resolveTarget(t: LookTarget, ctx: { sidebarWidth: number; windowHeight: number }): LookTarget {
  if (t.tol >= 0) return t
  if (t.key === 'rows.tabRowBox') return { ...t, expect: px(ctx.sidebarWidth - 2 * S.inset), tol: T.box }
  if (t.key === 'rows.closeButton') return { ...t, expect: px(ctx.sidebarWidth - S.inset - S.row.close.centerFromRight), tol: T.box }
  if (t.key === 'spaces.switcherPitch') return { ...t, expect: px(S.switcher.pitch - S.switcher.hit), tol: T.box }
  if (t.key === 'chrome.bottomBarCenterY') return { ...t, expect: px(S.switcher.centerFromBottom - S.switcher.hoverBox / 2), tol: T.box }
  return t
}

/**
 * Look tokens M11 leaves out, each with the reason: user settings, content, and
 * states the suites do not reach. Everything else is in LOOK_TARGETS or tokens.ts.
 */
export const NOT_TARGETS: Record<string, string> = {
  'chrome.sidebarWidth': 'a user setting: 214 by default, resizable from 164 to 500',
  'chrome.sidebarContentWidth': 'scales with the sidebar width; the rule (inset 8 on both sides) is a target',
  'chrome.windowTopHighlight': 'system-drawn window rim',
  'chrome.trafficLightInactiveFill': 'system-drawn from the tint',
  'chrome.forwardIcon': 'positions scale with the sidebar width; the pitch and the right anchor are targets',
  'chrome.backIcon': 'positions scale with the sidebar width; the pitch and the right anchor are targets',
  'chrome.sectionDivider': 'its width is the sidebar width; the color rule is a token (--divider)',
  'chrome.archiveButton': 'position scales with the window; the bottom-bar center rule is a target',
  'chrome.newSpaceButton': 'position scales with the sidebar width',
  'chrome.hiddenModeCard': 'covered by the layout rule (inset 10 on every side with the sidebar hidden)',
  'rows.favoritesColumns': 'the owner\'s favorites count and sidebar width; the column rule fills the content width',
  'rows.favoriteGrid.colX': 'scales with the sidebar width; the gap is a target',
  'spaces.count': 'content',
  'spaces.switcherIconCentersX': 'content and width; the pitch is a target',
  'spaces.space1Green': 'the owner\'s space color',
  'spaces.space2Pink': 'the owner\'s space color',
  'spaces.space3Gradient': 'the owner\'s space color',
  'spaces.grain': 'a per-space setting',
  'command.selectionFill': 'the accent of the active space; Rio fills the selection with --accent',
  'multitask.miniWindowPrompt.screenPos': 'display size and a saved position',
  'pages.settingsWindow': 'the settings window is Chromium\'s own page in Rio (M8); width and radius are tokens',
  'menus.downloadsPanel': 'a library surface Rio does not have; the archive panel is M3\'s',
  'menus.libraryHoverPeek': 'a library surface Rio does not have'
}
