// Layout math (PLAN 3.7, docs/plan/01-shell.md F2). Pure: main and the UI compute the same rects.
import { LOOK } from '../look/tokens'
import type { Layout } from '../types'
import { PEEK_ZONE_X } from './peek'

export const SIDEBAR_WIDTH = 214
export const SIDEBAR_MIN = 164
export const SIDEBAR_MAX = 500
/** The page card's inset and radius (M11, src/shared/look/tokens.ts). */
export const CARD_INSET = LOOK.card.inset
export const CARD_RADIUS = LOOK.card.radius
export const FOCUS_GUTTER = 4
/** Toolbar mode (View > Show Toolbar, ⇧⌘D): the URL bar runs across the top of the page in a band this tall above
 *  the card, its 30 pt field centered on the top row's line (y 23.5). */
export const TOOLBAR_H = 38
/** Room the traffic lights take before the sidebar toggle: its 32 px box centers on x 90 with the 8 px inset. */
export const TRAFFIC_SPACER = 66
/** Room the page card keeps next to the sidebar before the sidebar gives way on a narrow window. */
const MIN_PAGE_ROOM = 360

/** The Library's width: while it is open the docked sidebar widens to it (417, or the wider
 *  `libraryWidth` its Spaces view asks for), never past the page's 360 px of room and never narrower than the
 *  sidebar already is. */
export const LIBRARY_WIDTH = LOOK.library.width

/** `devStrip`: the window's front page is in Developer Mode, whose strip takes the toolbar's band. */
export interface ChromeState { sidebarWidth: number; sidebarHidden: boolean; focusMode: boolean; fullscreen: boolean; libraryOpen?: boolean; libraryWidth?: number; toolbar?: boolean; devStrip?: boolean }
export const DEFAULT_CHROME: ChromeState = { sidebarWidth: SIDEBAR_WIDTH, sidebarHidden: false, focusMode: false, fullscreen: false, toolbar: false }

/** sidebarRect keeps the sidebar's size while it is hidden: the slide and the hover peek draw it at that width. */
export interface ShellLayout extends Layout {
  /** Width the sidebar actually takes: the stored width clamped for this window size. */
  effectiveWidth: number
  /** Width of the traffic-light spacer at the start of the top row. Full screen keeps it: macOS shows the lights there
   *  whenever the pointer reaches the top, so the controls stay after them and never jump when they hide. */
  trafficSpacer: number
  /** Whether the window buttons belong on screen (sidebar shown). */
  trafficLights: boolean
}

const size = (n: number) => (Number.isFinite(n) ? Math.max(0, Math.round(n)) : 0)

/** Stored width as a usable number: junk falls back to the default, then the 164..500 range applies. */
export function storedSidebarWidth(width: number): number {
  if (!Number.isFinite(width)) return SIDEBAR_WIDTH
  return Math.min(SIDEBAR_MAX, Math.max(SIDEBAR_MIN, Math.round(width)))
}

/** The stored width clamped to what this window can give: at most 45% of it and never under 360 px of page. */
export function effectiveSidebarWidth(stored: number, windowWidth: number): number {
  const w = size(windowWidth)
  const max = Math.max(SIDEBAR_MIN, Math.min(SIDEBAR_MAX, Math.floor(w * 0.45), w - MIN_PAGE_ROOM))
  return Math.min(max, storedSidebarWidth(stored))
}

export function computeLayout(contentSize: { width: number; height: number }, chrome: ChromeState = DEFAULT_CHROME): ShellLayout {
  const width = size(contentSize.width)
  const height = size(contentSize.height)
  const docked = effectiveSidebarWidth(chrome.sidebarWidth, width)
  const library = chrome.libraryOpen ? Math.min(Math.max(LIBRARY_WIDTH, size(chrome.libraryWidth ?? 0)), Math.max(0, width - MIN_PAGE_ROOM)) : 0
  const effectiveWidth = Math.max(docked, library)
  const fullHeight = (w: number) => ({ x: 0, y: 0, width: w, height })
  const base = { contentSize: { width, height }, effectiveWidth, trafficSpacer: TRAFFIC_SPACER }

  if (chrome.focusMode) {
    return {
      ...base,
      pageRect: { x: FOCUS_GUTTER, y: 0, width: Math.max(0, width - FOCUS_GUTTER), height },
      sidebarRect: fullHeight(effectiveWidth),
      hotZone: fullHeight(Math.min(FOCUS_GUTTER, width)),
      radius: 0,
      trafficLights: false
    }
  }
  // Toolbar mode and the Developer Mode strip move the card down under the band (focus mode has neither).
  const top = CARD_INSET + (chrome.toolbar || chrome.devStrip ? TOOLBAR_H : 0)
  const inner = { y: top, height: Math.max(0, height - top - CARD_INSET) }
  if (chrome.sidebarHidden) {
    return {
      ...base,
      pageRect: { x: CARD_INSET, ...inner, width: Math.max(0, width - CARD_INSET * 2) },
      sidebarRect: fullHeight(effectiveWidth),
      // x 0 to PEEK_ZONE_X, the edge included (the C++ twin's rect is right-exclusive).
      hotZone: fullHeight(Math.min(PEEK_ZONE_X + 1, width)),
      radius: CARD_RADIUS,
      trafficLights: false
    }
  }
  // The sidebar's own right padding supplies the gap on the card's left.
  return {
    ...base,
    pageRect: { x: effectiveWidth, ...inner, width: Math.max(0, width - effectiveWidth - CARD_INSET) },
    sidebarRect: fullHeight(effectiveWidth),
    hotZone: null,
    radius: CARD_RADIUS,
    trafficLights: true
  }
}
