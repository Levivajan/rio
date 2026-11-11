// Hover peek timing (docs/plan/01-shell.md F4, PLAN 3.3 and decision 51). Pure: the UI's edge hot zone
// runs the dwell, the overlay's peek runs the leave grace, and both take their timers as an argument so tests drive
// them by hand.
import type { OverlaySurfaceState } from '../state'
import type { InstanceId, Rect } from '../types'
import type { ShellLayout } from './layout'

/** The hidden sidebar: the pointer within 5 of the window's left edge reveals it,
 *  sliding in on a spring that overshoots 3.5 and settles by 250 ms; about 165 ms after the pointer leaves it retracts
 *  in about 90 ms, ease-in. A 40 ms dwell still lets a pointer sweep past the
 *  edge without revealing it and starts the slide within about 60 ms. kPeekDwell in clu_sidebar_peek.h. */
export const PEEK_ZONE_X = 5
export const PEEK_DWELL_MS = 40
/** A pointer moving left reaches the edge when it would be in the zone one 60 Hz frame after its last sample, so a
 *  fast pointer that crosses the zone between two samples still reveals. Samples further apart than the gap are a new
 *  approach; the speed is taken over at least the minimum gap so two samples a millisecond apart cannot fling it. */
export const PEEK_EDGE_LOOKAHEAD_MS = 1000 / 60
export const PEEK_EDGE_SAMPLE_GAP_MS = 50
export const PEEK_EDGE_MIN_GAP_MS = 8
export const PEEK_GRACE_MS = 165
export const PEEK_SLIDE_IN_MS = 250
export const PEEK_SLIDE_OUT_MS = 90
/** The slide-out's progress at each fifth of PEEK_SLIDE_OUT_MS: ease-in, every frame a bigger step than the one
 *  before, so the panel accelerates off the edge instead of slowing down. */
export const PEEK_SLIDE_OUT_STOPS = [0, 0.06, 0.2, 0.4, 0.66, 1] as const
/** The overlay asks main to close the peek two frames after its slide ends, so the slide's last frame is on screen
 *  before the widget goes; main holds the widget up to kPeekSlideOutHold (150 ms) after its grace for it. */
export const PEEK_SLIDE_OUT_DONE_MS = PEEK_SLIDE_OUT_MS + 34
export const PEEK_SLIDE_OUT_EASE = `linear(${PEEK_SLIDE_OUT_STOPS.map((p, i) => (i === 0 || i === PEEK_SLIDE_OUT_STOPS.length - 1 ? `${p}` : `${p} ${i * 20}%`)).join(', ')})`
/** Main closes the peek when no shell.pointerAlive arrives for this long. */
export const PEEK_GUARD_MS = 5000
/** The overlay sends shell.pointerAlive this often while the pointer is over the peek. */
export const PEEK_ALIVE_MS = 1000
/** Transparent strip right of the peek sidebar that carries its shadow. */
export const PEEK_BLEED = 16
export const PEEK_SURFACE = 'sidebar-peek'
/** The passive overlay view the peek shows in (kPeekSlot in clu_window_sidebar_peek.h). */
export const PEEK_SLOT = 3

/** The floating sidebar's motion: `enter` off the window's left edge until it first draws, `shown` in place
 *  (the spring in), `leaving` sliding back out (the ease-in). A pointer back over it while leaving returns it to
 *  `shown`, from wherever the slide had taken it. */
export type PeekMotion = 'enter' | 'shown' | 'leaving'

export function peekMotion(entered: boolean, leaving: boolean): PeekMotion {
  if (leaving) return 'leaving'
  return entered ? 'shown' : 'enter'
}

export interface PeekTimers {
  setTimeout(fn: () => void, ms: number): unknown
  clearTimeout(handle: unknown): void
}

export const realTimers: PeekTimers = {
  setTimeout: (fn, ms) => setTimeout(fn, ms),
  clearTimeout: (h) => clearTimeout(h as ReturnType<typeof setTimeout>)
}

/** Overlay bounds of the peek: the sidebar's rect plus the shadow bleed, from the window's left edge. */
export function peekRect(layout: Pick<ShellLayout, 'sidebarRect'>): Rect {
  const size = (n: number) => (Number.isFinite(n) ? Math.max(0, Math.round(n)) : 0)
  const width = size(layout.sidebarRect.width)
  // No sidebar width means nothing to peek: an empty rect, which main refuses to open.
  return { x: 0, y: 0, width: width > 0 ? width + PEEK_BLEED : 0, height: size(layout.sidebarRect.height) }
}

export interface EdgeSample {
  x: number
  y: number
  /** Milliseconds, any monotonic clock. */
  t: number
}

/**
 * Whether the pointer at `point` reaches the reveal `zone`: inside it (left of the window counts), or moving left fast
 * enough from `previous` to be in it within one frame, at the zone's height. A held button (a tab, a text selection or
 * a window resize dragged to the edge) never reaches it. ReachesPeekZone in clu_sidebar_peek.cc is the twin.
 */
export function reachesPeekZone(zone: Rect, point: EdgeSample, previous: EdgeSample | null, buttons: number): boolean {
  if (buttons !== 0 || zone.width <= 0 || zone.height <= 0) return false
  if (!Number.isFinite(point.x) || !Number.isFinite(point.y)) return false
  if (point.y < zone.y || point.y >= zone.y + zone.height) return false
  const right = zone.x + zone.width
  if (point.x < right) return true
  if (!previous || !Number.isFinite(previous.x) || !Number.isFinite(previous.t)) return false
  const gap = point.t - previous.t
  const dx = point.x - previous.x
  if (!(gap > 0) || gap > PEEK_EDGE_SAMPLE_GAP_MS || dx >= 0) return false
  return point.x + (dx * PEEK_EDGE_LOOKAHEAD_MS) / Math.max(gap, PEEK_EDGE_MIN_GAP_MS) < right
}

/** Instances opened from the peek, directly or through another child (a submenu of its row menu). */
export function childrenOf(list: OverlaySurfaceState[], peekId: InstanceId): OverlaySurfaceState[] {
  const ids = new Set([peekId])
  const out: OverlaySurfaceState[] = []
  for (let grew = true; grew; ) {
    grew = false
    for (const s of list) {
      if (s.openedFrom && ids.has(s.openedFrom) && !ids.has(s.instanceId)) {
        ids.add(s.instanceId)
        out.push(s)
        grew = true
      }
    }
  }
  return out
}

export interface Dwell {
  /** Pointer entered (or moved inside) the hot zone: starts the dwell unless one runs or already fired. */
  enter(): void
  /** Pointer left: cancels a running dwell and allows the next one. */
  leave(): void
  dispose(): void
}

/** Fires once after the pointer stays `ms` in the hot zone; leaving earlier cancels it. */
export function createDwell(fire: () => void, opts: { ms?: number; timers?: PeekTimers } = {}): Dwell {
  const timers = opts.timers ?? realTimers
  let handle: unknown = null
  let fired = false
  const cancel = () => {
    if (handle !== null) timers.clearTimeout(handle)
    handle = null
  }
  return {
    enter() {
      if (handle !== null || fired) return
      handle = timers.setTimeout(() => {
        handle = null
        fired = true
        fire()
      }, opts.ms ?? PEEK_DWELL_MS)
    },
    leave() {
      cancel()
      fired = false
    },
    dispose: cancel
  }
}

export interface PeekHover {
  /** Pointer entered or moved over the peek sidebar: it is inside, and a running grace stops. */
  enter(): void
  /** Pointer left the peek sidebar: the grace starts. */
  leave(): void
  /**
   * Surfaces opened from the peek (hover card, row menu). While any is open the grace cannot close the peek. When the
   * last one closes, `pointerLeft` says the pointer was taken by it (a covering menu): the peek then counts as left.
   */
  setChildren(count: number, pointerLeft: boolean): void
  /**
   * The Library, or the bottom-left pop-up's row menu or Rename, is open in the peek: it holds the peek open
   * whatever the pointer does. Released, it waits for the pointer: main asks where it is and slides the peek out
   * through `leaving` once it is off (a stale leave would retract it under the pointer). A leave after the
   * release starts no grace here until the pointer enters again (the leave the narrowing panel sends would close
   * it at once).
   */
  setHeld(held: boolean): void
  readonly hovered: boolean
  dispose(): void
}

/**
 * Leave grace of the open peek: `expire` runs `graceMs` after the pointer leaves, unless it comes back first. A
 * child surface holds the peek open; once the last child closes and the pointer is out, the grace runs again.
 */
export function createPeekHover(expire: () => void, opts: { graceMs?: number; timers?: PeekTimers } = {}): PeekHover {
  const timers = opts.timers ?? realTimers
  let handle: unknown = null
  // The peek opens under the pointer (it is at the window edge, inside the sidebar rect).
  let hovered = true
  let children = 0
  let held = false
  // A hold ended and the pointer has not come onto the panel since: a leave now may be the panel
  // narrowing or handing the keyboard back under a pointer still on it, so main decides by the system pointer and
  // slides the peek out through `leaving`. The overlay's own grace waits for the pointer's next enter.
  let released = false
  const holding = () => children > 0 || held
  const stop = () => {
    if (handle !== null) timers.clearTimeout(handle)
    handle = null
  }
  const start = () => {
    if (handle !== null || hovered || holding() || released) return
    handle = timers.setTimeout(() => {
      handle = null
      if (!hovered && !holding()) expire()
    }, opts.graceMs ?? PEEK_GRACE_MS)
  }
  return {
    enter() {
      hovered = true
      released = false
      stop()
    },
    leave() {
      hovered = false
      start()
    },
    setChildren(count, pointerLeft) {
      const before = children
      children = Math.max(0, count)
      if (children > 0) {
        stop()
        return
      }
      if (before > 0 && pointerLeft) hovered = false
      start()
    },
    setHeld(on) {
      if (held && !on) released = true
      held = on
      if (held) stop()
    },
    get hovered() {
      return hovered
    },
    dispose: stop
  }
}
