// Sidebar tween math (docs/plan/01-shell.md F3). Pure, shared by main's bounds tween and the UI's slide,
// so both read progress from (now - startAt) / ms on the same 16 ms ticks: tick k means the same time in both.
import { LOOK } from '../look/tokens'
import type { Rect } from '../types'

/** Showing the sidebar docks it in 85 ms and hiding it takes 80, with a page loaded. */
export const SIDEBAR_SHOW_MS = LOOK.sidebar.showMs
export const SIDEBAR_HIDE_MS = LOOK.sidebar.hideMs
export const sidebarTweenMs = (showing: boolean): number => (showing ? SIDEBAR_SHOW_MS : SIDEBAR_HIDE_MS)

/** `from` and `to` are the page card's left edge, which is also the sliding sidebar's right edge. */
/** `curve` 'exp': the Library's widen and narrow, an exponential ease-out; else the dock's ease-out cubic. */
export interface SidebarAnim { from: number; to: number; startAt: number; ms: number; fromRect: Rect; toRect: Rect; curve?: 'cubic' | 'exp' }

/** The Library's tween: 214 to 417 and back over about 290 ms, exponential with a 42 ms time
 *  constant. */
export const LIBRARY_TWEEN = { ms: 290, tauMs: 42 } as const

/** Exponential ease-out normalized to land on 1 at t = 1, for a tween of `ms` with time constant `tauMs`. */
export function easeOutExp(t: number, ms: number = LIBRARY_TWEEN.ms, tauMs: number = LIBRARY_TWEEN.tauMs): number {
  if (t >= 1) return 1
  if (t <= 0) return 0
  return (1 - Math.exp((-t * ms) / tauMs)) / (1 - Math.exp(-ms / tauMs))
}

export const easeOutCubic = (t: number): number => 1 - (1 - t) ** 3

/** The hide's sampled curve (LOOK.sidebar.hideCurve) at `t` (0 to 1 of the tween), linear between samples. */
export function hideEase(t: number): number {
  const curve = LOOK.sidebar.hideCurve
  const end = curve.at(-1)?.[0] ?? 1
  const ms = Number.isFinite(t) ? Math.min(1, Math.max(0, t)) * end : 0
  for (let i = 1; i < curve.length; i++) {
    const [t0, p0] = curve[i - 1] ?? [0, 0]
    const [t1, p1] = curve[i] ?? [end, 1]
    if (ms <= t1) return t1 > t0 ? p0 + ((p1 - p0) * (ms - t0)) / (t1 - t0) : p1
  }
  return 1
}

/** Eased progress at `t` (0 to 1 of a tween of `ms`) on `curve`: 0 at or before the start, exactly 1 at the end. */
export function easeAt(t: number, ms: number, curve: TweenCurve = 'cubic'): number {
  if (!(t > 0)) return 0
  if (t >= 1 || !(ms > 0)) return 1
  return curve === 'exp' ? easeOutExp(t, ms) : curve === 'hide' ? hideEase(t) : easeOutCubic(t)
}

/** Eased progress of `anim` at `now` (ms, the Date.now() clock of startAt): 0 before the start, 1 at the end, with
 *  reduced motion (ms 0) or a junk clock. */
export function progressAt(anim: Pick<SidebarAnim, 'from' | 'to' | 'curve' | 'startAt' | 'ms'>, now: number): number {
  if (!(anim.ms > 0)) return 1
  const t = (now - anim.startAt) / anim.ms
  return Number.isNaN(t) ? 1 : easeAt(t, anim.ms, animCurve(anim))
}

/** A tween hides the sidebar when the card's left edge moves left. */
export const isHiding = (anim: Pick<SidebarAnim, 'from' | 'to'>): boolean => anim.to < anim.from

export type TweenCurve = 'cubic' | 'exp' | 'hide'

/** The Library's tween keeps its exponential curve both ways; any other hide takes the hide's curve. */
export const animCurve = (anim: Pick<SidebarAnim, 'from' | 'to' | 'curve'>): TweenCurve =>
  anim.curve === 'exp' ? 'exp' : isHiding(anim) ? 'hide' : 'cubic'

export const lerp = (a: number, b: number, p: number): number => (p >= 1 ? b : a + (b - a) * p)

export function lerpRect(a: Rect, b: Rect, p: number): Rect {
  return { x: lerp(a.x, b.x, p), y: lerp(a.y, b.y, p), width: lerp(a.width, b.width, p), height: lerp(a.height, b.height, p) }
}

/** The sidebar edge (page card left) the UI draws at `now`. */
export function edgeAt(anim: SidebarAnim, now: number): number {
  return lerp(anim.from, anim.to, progressAt(anim, now))
}
