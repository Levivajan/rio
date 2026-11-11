// Theme editor math: the popover's geometry, the color wheel on
// the dotted canvas (angle is hue, distance from the center its lightness), up to three dots that each move on their
// own, keyboard nudges, the wavy intensity slider whose wave and thumb grow with the value, the grain
// dial (continuous, keys step 0.05), the preset pages, the popover's place beside the sidebar and the preview throttle.
import { hexToRgb, hslToRgb, rgbToHex, rgbToHsl, type Rgb } from './space'
import type { Rect } from '../types'
import type { SpaceTheme, ThemeColor } from './types'

/** The popover, its dotted canvas and their insets (in points). */
export const PICKER_SIZE = { width: 356, height: 508 } as const
export const PICKER_RADIUS = 19
export const CANVAS = { x: 8, y: 8, size: 340, radius: 8, pitch: 4.25 } as const
/** The wheel's radius on the canvas: a color at distance 1 sits this far from the center. */
export const WHEEL_RADIUS = 150
/** A handle's center stays within a square this far from the canvas center (22.5 pt inside the canvas). */
export const HANDLE_LIMIT = 147.5
/** Handle diameters and white rings: a lone handle, the main one beside others, the others. */
export const HANDLE = { single: 49, primary: 45, primaryRing: 5, secondary: 28, secondaryRing: 4 } as const
/** Where the other colors sit from the main one, by count: the complement for two, -150 and +150 degrees for three. */
export const HARMONY: Record<number, readonly number[]> = { 1: [], 2: [180], 3: [210, 150] }
/** Mode buttons (center y), minus and plus (center y), both 40 pt apart around the popover's middle. */
export const MODE_ROW = { centerY: 49, box: 32, pitch: 40 } as const
export const COUNT_ROW = { centerY: 317, pitch: 40 } as const
/** Preset swatches: 9 a page, 24 pt, 29.5 pt apart; the arrows' centers. A page slides 265.5 pt in 330 ms, a soft start
 * and a long tail (5% at 16 ms, 88% at 183 ms, 99% at 275 ms). */
export const SWATCHES = { centerY: 380.5, size: 24, pitch: 29.5, firstX: 60.5, prevX: 24.5, nextX: 331.5, slideMs: 330 } as const
/** The page slide's curve as progress points. */
export const SWATCH_SLIDE_EASING = 'linear(0, 0.05 5%, 0.14 10%, 0.55 33%, 0.88 55%, 0.925 61%, 0.95 66%, 0.966 71%, 0.977 76%, 0.992 83%, 0.996 88%, 1)'
/** The intensity slider: a 224 x 16 track and the wave inside it; the thumb's center runs 5.5 to 218.5. The wave is
 * flat at 0 and 7.75 pt high at 0.628; the thumb grows from 13 x 48 through 19.5 x 51.5 to 21.5 x 59. */
export const SLIDER = {
  x: 18.5, centerY: 452.5, width: 224, height: 16, travelStart: 5.5, travelEnd: 218.5, maxAmplitude: 12.34, period: 39, stroke: 3.5
} as const
const THUMB: readonly [number, number, number][] = [[0, 13, 48], [0.5, 19.5, 51.5], [1, 21.5, 59]]
/** The grain dial: a 40 pt knob, 20 dots on a 32 pt ring (one every 18 degrees from the top), a radial indicator. */
export const DIAL = { centerX: 298.5, centerY: 452.5, knob: 40, ring: 32, dots: 20, dotOn: 5, dotOff: 3, pinFrom: 25, pinTo: 39, pinWidth: 6 } as const
/** Gap between the sidebar and the popover, and the margin it keeps from the window edges. */
export const PICKER_GAP = 13
export const PICKER_MARGIN = 8
export const MAX_COLORS = 3
/** Keys move grain in steps of 0.05 (one ring dot each); the pointer moves it continuously. */
export const STEP = 0.05
/** Arrow keys nudge the chosen handle by 2 degrees of hue or 0.02 of depth. */
export const NUDGE_HUE = 2
export const NUDGE_SAT = 0.02
/** Previews go to the browser at most once a frame (state patches are coalesced to 16 ms). */
export const PREVIEW_MS = 16
export const PRESET_PAGE = 9

/** The 5 preset pages of 9 under 4 set names: Brand runs over the first two pages (Brand 1 to 18). */
export const SWATCH_PAGES: readonly { name: string; colors: readonly string[] }[] = [
  { name: 'Brand', colors: ['#f0eae5', '#e59eba', '#9e749b', '#e1676f', '#ef8c63', '#f2d46c', '#73e49b', '#7eb6d5', '#656786'] },
  { name: 'Brand', colors: ['#d0d1c8', '#c99c8e', '#8a6772', '#c59a60', '#d0c85d', '#a0d164', '#68c7c8', '#7274c1', '#6b5d7a'] },
  { name: 'Pastel', colors: ['#fdfbf9', '#fbeaf3', '#ebd2ea', '#f6ccd2', '#f9e1d4', '#fef9e8', '#e4fceb', '#e0f1fb', '#c4c6e1'] },
  { name: 'Drab', colors: ['#4b3a57', '#623857', '#854343', '#a87048', '#d1b26a', '#cdcba7', '#85a885', '#35604a', '#2b4367'] },
  { name: 'Greyscale', colors: ['#ffffff', '#e5e5e5', '#cccccc', '#b2b2b2', '#808080', '#666666', '#333333', '#1a1a1a', '#000000'] }
]

/** The largest hue the validator accepts at one decimal (h is in [0,360)). */
const MAX_HUE = 359.9
const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, Number.isFinite(v) ? v : lo))
const round = (v: number, places: number) => {
  const k = 10 ** places
  return Math.round(v * k) / k
}
const wrapHue = (h: number) => Math.min(MAX_HUE, round(((h % 360) + 360) % 360, 1))

export interface Point { x: number; y: number }
export interface Size { width: number; height: number }

/** A pointer at (x, y) from the canvas center as a color: the angle is the hue (0 at 3 o'clock, clockwise on screen:
 * hue 357 sits just above the center line), the distance over the wheel radius is the depth (s),
 * clamped to 1. */
export function canvasToColor(p: Point, radius: number = WHEEL_RADIUS): ThemeColor {
  const x = clamp(p.x, -HANDLE_LIMIT, HANDLE_LIMIT)
  const y = clamp(p.y, -HANDLE_LIMIT, HANDLE_LIMIT)
  const dist = Math.hypot(x, y)
  if (dist === 0) return { h: 0, s: 0 }
  return { h: wrapHue((Math.atan2(y, x) * 180) / Math.PI), s: round(clamp(dist / (radius || 1), 0, 1), 3) }
}

/** Where a color's handle sits, from the canvas center, inside the handle square. */
export function colorToCanvas(c: ThemeColor, radius: number = WHEEL_RADIUS): Point {
  const a = (clamp(c.h, 0, 360) * Math.PI) / 180
  const d = clamp(c.s, 0, 1) * radius
  const lim = (v: number) => round(clamp(v, -HANDLE_LIMIT, HANDLE_LIMIT), 3)
  return { x: lim(d * Math.cos(a)), y: lim(d * Math.sin(a)) }
}

/** A theme's colors arranged around its main color: the others at the main's distance, 180 degrees away for two,
 * -150 and +150 for three. */
export function harmony(main: ThemeColor, count: number): ThemeColor[] {
  const n = Math.round(clamp(count, 1, MAX_COLORS))
  return [main, ...(HARMONY[n] ?? []).map((d) => ({ h: wrapHue(main.h + d), s: main.s }))]
}

/** The theme with only the color at `index` moved to `c`; every other dot stays exactly where it is (any
 * mix of colors is allowed, so the others neither rotate with the main one nor re-place when one is grabbed). An
 * index with no color leaves the theme as it is. */
export function moveColor(t: SpaceTheme, index: number, c: ThemeColor): SpaceTheme {
  if (!Number.isInteger(index) || index < 0 || index >= t.colors.length) return t
  return { ...t, colors: t.colors.map((old, i) => (i === index ? c : old)) }
}

/** Whether the colors still sit at the design's starting spots around the main one (nobody moved a dot since + placed it). */
const atStartingSpots = (colors: readonly ThemeColor[]) => {
  const main = colors[0]
  return !!main && harmony(main, colors.length).every((c, i) => c.h === colors[i]?.h && c.s === colors[i]?.s)
}

/** Distance from the center is lightness at full saturation (the center is about 1%, half
 * the radius about 44%, the clamped edge about 86%); the angle is the hue. */
export const WHEEL_LIGHTNESS = { center: 0.01, span: 0.87 } as const

/** The color a handle stands for: the hue at full saturation, lighter toward the edge (east at half radius is the
 * `#df0001`, at the edge `#ffbaba`, the center `#050001`). */
export function wheelRgb(c: ThemeColor): Rgb {
  return hslToRgb(clamp(c.h, 0, 360), 1, WHEEL_LIGHTNESS.center + WHEEL_LIGHTNESS.span * clamp(c.s, 0, 1))
}

/** The handle's fill, as CSS. */
export function handleFill(c: ThemeColor): string {
  return rgbToHex(wheelRgb(c))
}

/** A swatch's place on the wheel: its hue, and the distance whose lightness matches its own. Grays sit at the
 * center. Junk reads as the center too. */
export function swatchColor(hex: string): ThemeColor {
  const rgb = hexToRgb(hex)
  if (!rgb) return { h: 0, s: 0 }
  const { h, s, l } = rgbToHsl(rgb)
  if (s < 0.08) return { h: 0, s: 0 }
  return { h: wrapHue(h), s: round(clamp((l - WHEEL_LIGHTNESS.center) / WHEEL_LIGHTNESS.span, 0, 1), 3) }
}

/** Whether a swatch stands for this color: the same place on the wheel to within what the browser's rounding moves it
 * (a tenth of a degree, a thousandth of depth), so the picked swatch stays marked after the round trip. */
export function swatchMatches(hex: string, c: ThemeColor): boolean {
  const w = swatchColor(hex)
  const dh = Math.abs(w.h - c.h)
  return Math.min(dh, 360 - dh) <= 0.15 && Math.abs(w.s - c.s) <= 0.0015
}

/** A swatch's accessible name: its set and its place in the set (Brand 10 is the first swatch of the second page). */
export function swatchLabel(page: number, index: number): string {
  const name = SWATCH_PAGES[page]?.name ?? ''
  const before = SWATCH_PAGES.slice(0, Math.max(0, page)).filter((p) => p.name === name).length
  return `${name} ${before * PRESET_PAGE + index + 1}`
}

export type NudgeKey = 'ArrowLeft' | 'ArrowRight' | 'ArrowUp' | 'ArrowDown'

/** Arrow keys: left and right turn the hue (wrapping), up and down move toward the edge or the center. */
export function nudge(c: ThemeColor, key: NudgeKey, times = 1): ThemeColor {
  if (key === 'ArrowLeft' || key === 'ArrowRight') return { h: wrapHue(c.h + (key === 'ArrowRight' ? 1 : -1) * NUDGE_HUE * times), s: c.s }
  return { h: c.h, s: round(clamp(c.s + (key === 'ArrowUp' ? 1 : -1) * NUDGE_SAT * times, 0, 1), 3) }
}

/** Snaps a value in [0,1] to the 0.05 grid. */
export function snap(v: number): number {
  return round(clamp(Math.round(clamp(v, 0, 1) / STEP) * STEP, 0, 1), 2)
}

/** The next value one 0.05 step up or down from `v`, clamped to [0,1]. */
export function step(v: number, dir: 1 | -1, times = 1): number {
  return snap(snap(v) + dir * STEP * times)
}

/** Intensity is continuous; it is kept to 3 decimals. */
export const roundIntensity = (v: number) => round(clamp(v, 0, 1), 3)

/** The thumb's width and height for a value, between its sizes at 0, 0.5 and 1. */
export function thumbSize(v: number): { width: number; height: number } {
  const x = clamp(v, 0, 1)
  const [a, b] = x <= 0.5 ? [THUMB[0], THUMB[1]] : [THUMB[1], THUMB[2]]
  const t = ((x - (a?.[0] ?? 0)) / ((b?.[0] ?? 1) - (a?.[0] ?? 0))) || 0
  const lerp = (i: 1 | 2) => round((a?.[i] ?? 0) + ((b?.[i] ?? 0) - (a?.[i] ?? 0)) * t, 2)
  return { width: lerp(1), height: lerp(2) }
}

/** The wave's height for a value: flat at 0, growing straight to its tallest at 1. */
export const waveAmplitude = (v: number) => round(SLIDER.maxAmplitude * clamp(v, 0, 1), 2)

/** The slider value for a pointer at `x` from the track's left edge. */
export function sliderValue(x: number): number {
  return roundIntensity((x - SLIDER.travelStart) / (SLIDER.travelEnd - SLIDER.travelStart))
}

/** The thumb's center for a value, from the track's left edge. */
export function sliderX(v: number): number {
  return round(SLIDER.travelStart + clamp(v, 0, 1) * (SLIDER.travelEnd - SLIDER.travelStart), 3)
}

/** The wave as an SVG path in track coordinates: a sine from travelStart to travelEnd around the track's middle. */
export function wavePath(a: number = waveAmplitude(0.628)): string {
  const { travelStart: x0, travelEnd: x1, period, height } = SLIDER
  const mid = height / 2
  const parts: string[] = []
  for (let x = x0; x <= x1 + 1e-9; x += 1) {
    const y = mid - a * Math.sin(((x - x0) / period) * 2 * Math.PI)
    parts.push(`${parts.length ? 'L' : 'M'}${round(x, 2)} ${round(y, 2)}`)
  }
  return parts.join(' ')
}

/** The dial's scale leaves this many degrees each side of the top. */
export const DIAL_GAP = 15

/** The dial's indicator angle in degrees, clockwise from the top: 0 at DIAL_GAP, 1 at 360 - DIAL_GAP. */
export function dialAngle(v: number): number {
  return round(DIAL_GAP + clamp(v, 0, 1) * (360 - 2 * DIAL_GAP), 3)
}

/** The dial value under a pointer at (dx, dy) from the dial's center, continuous as the pointer turns. It
 * wraps: past the end of the scale the top is the least grain again. Junk keeps `current`. */
export function dialValue(dx: number, dy: number, current = 0): number {
  if (!Number.isFinite(dx) || !Number.isFinite(dy) || (dx === 0 && dy === 0)) return round(clamp(current, 0, 1), 3)
  const a = (((Math.atan2(dx, -dy) * 180) / Math.PI) + 360) % 360
  if (a > 360 - DIAL_GAP) return 0
  return round(clamp((a - DIAL_GAP) / (360 - 2 * DIAL_GAP), 0, 1), 3)
}

/** A theme with one more color (at most 3) at the design's starting spot: the complement for the second, then -150 and +150
 * degrees from the main one. Dots still at their starting spots move to the new ones; once a dot was
 * dragged it stays put and the new one takes the last starting spot. */
export function addColor(t: SpaceTheme): SpaceTheme {
  const main = t.colors[0] ?? { h: 210, s: 0.4 }
  if (t.colors.length >= MAX_COLORS) return t
  const spots = harmony(main, t.colors.length + 1)
  if (t.colors.length === 0 || atStartingSpots(t.colors)) return { ...t, colors: spots }
  const added = spots[spots.length - 1]
  return added ? { ...t, colors: [...t.colors, added] } : t
}

/** A theme with one color fewer (at least 1 stays): the last dot goes and the others stay where they are. */
export function removeColor(t: SpaceTheme): SpaceTheme {
  return t.colors.length <= 1 ? t : { ...t, colors: t.colors.slice(0, -1) }
}

export const sameTheme = (a: SpaceTheme, b: SpaceTheme) =>
  a.intensity === b.intensity && a.grain === b.grain && a.colors.length === b.colors.length && a.colors.every((c, i) => c.h === b.colors[i]?.h && c.s === b.colors[i]?.s)

/**
 * Top-left of the popover: 12 pt right of the sidebar's edge, its top at the anchor's top, kept inside the window.
 * With the sidebar hidden (width 0) it opens at the window's left margin.
 */
export function placePicker(anchor: Rect, viewport: Size, sidebarRight: number, size: Size = PICKER_SIZE): Point {
  const top = clamp(anchor.y, -1e6, 1e6)
  const edge = clamp(sidebarRight, 0, 1e6)
  const maxLeft = Math.max(PICKER_MARGIN, viewport.width - size.width - PICKER_MARGIN)
  const maxTop = Math.max(PICKER_MARGIN, viewport.height - size.height - PICKER_MARGIN)
  const left = edge > 0 ? edge + PICKER_GAP : PICKER_MARGIN
  return { x: Math.round(clamp(left, PICKER_MARGIN, maxLeft)), y: Math.round(clamp(top, PICKER_MARGIN, maxTop)) }
}

/** Calls `fn` at most once per `ms` with the latest value; `flush` sends a pending value now, `cancel` drops it. */
export function throttle<T>(fn: (v: T) => void, ms: number, now: () => number = Date.now) {
  let last = -Infinity
  let pending: { v: T } | null = null
  let timer: ReturnType<typeof setTimeout> | null = null
  const fire = () => {
    timer = null
    if (!pending) return
    const { v } = pending
    pending = null
    last = now()
    fn(v)
  }
  return {
    call(v: T) {
      pending = { v }
      const wait = last + ms - now()
      if (wait <= 0) {
        if (timer) clearTimeout(timer)
        fire()
      } else timer ??= setTimeout(fire, wait)
    },
    flush: () => {
      if (timer) clearTimeout(timer)
      fire()
    },
    cancel: () => {
      if (timer) clearTimeout(timer)
      timer = null
      pending = null
    }
  }
}
