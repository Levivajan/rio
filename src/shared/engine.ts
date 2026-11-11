// Engine types shared by main and renderers (seeded by the scaffold).
export type { PageFields, PageIssue } from './state/engine'
export type NavTransition = 'typed' | 'link' | 'reload' | 'other'
export interface NavigationEvent {
  tabId: string
  url: string
  title: string
  transition: NavTransition
  isSameDocument: boolean
  profileId: string
}
export const MAX_TITLE = 1024

/** Chromium's default canvas, which a page without its own background expects. */
export const PAGE_CANVAS = '#ffffff'

/** Strips control characters, collapses whitespace and caps at 1024 characters; falls back to the host. */
export function sanitizeTitle(raw: unknown, url: string): string {
  const text = typeof raw === 'string' ? raw.slice(0, MAX_TITLE * 4) : ''
  const clean = text.replace(/[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/gu, ' ').replace(/\s+/g, ' ').trim()
  if (clean) return clean.slice(0, MAX_TITLE).toWellFormed()
  try {
    const u = new URL(url)
    return u.hostname || url.slice(0, MAX_TITLE)
  } catch {
    return url.slice(0, MAX_TITLE)
  }
}

/** Two-finger swipe back and forward (PLAN decision 45). Shaped like spaces' swipe, over overscroll on a page. */
export const NAV_SWIPE = {
  /** Travel before the axis locks. */
  lockPx: 12,
  /** Horizontal when |dx| > axisRatio * |dy| at lock. */
  axisRatio: 1.5,
  /** No wheel event for this long ends the gesture: DOM wheel events carry no phase. */
  idleMs: 120,
  /** Commit at this share of the page width... */
  commitFraction: 0.3,
  /** ...or at this release velocity (px/ms) with at least flickFraction travelled; the same speed back springs back. */
  commitVelocity: 0.6,
  flickFraction: 0.1,
  /** Finger samples this close to the last one measure the release velocity. */
  velocityWindowMs: 100,
  /** Wheel events this soon after a release are swallowed. */
  cooldownMs: 350,
  /** Momentum from the curve: three same-sign deltas, each this share of the one before, with ratios this close. */
  decayMin: 0.55,
  decayMax: 0.97,
  decaySpread: 0.05,
  decayMinPx: 1,
  /** Updates to main at most this often, which keeps a 120 Hz trackpad well under page.overscroll's limit. */
  sendEveryMs: 16,
  samples: 24
} as const

/** dx is signed overscroll as a share of the page width: negative goes back, positive goes forward. */
export interface OverscrollMessage { dx: number; phase: 'begin' | 'update' | 'end' }

export type NavSwipeInput =
  /**
   * dx and dy are the DOM wheel event's deltas. `blocked`: the page can still scroll this way or claimed the event,
   * so the gesture is the page's.
   */
  | { kind: 'wheel'; dx: number; dy: number; width: number; blocked: boolean }
  | { kind: 'tick' }

export interface NavSwipeState {
  phase: 'idle' | 'pending' | 'dragging' | 'ignored' | 'cooldown'
  /** Signed horizontal travel and vertical travel so far, in px. */
  sumX: number
  sumY: number
  blocked: boolean
  dir: -1 | 1
  width: number
  samples: { t: number; dx: number }[]
  lastDx: number
  lastAt: number
  sentAt: number
  /** Cooldown end. */
  until: number
  message: OverscrollMessage | null
}

export const initialNavSwipe = (): NavSwipeState => ({
  phase: 'idle', sumX: 0, sumY: 0, blocked: false, dir: 1, width: 0, samples: [], lastDx: 0, lastAt: 0, sentAt: 0, until: 0, message: null
})

/** What an `end` message asks for: a release that did not commit reports 0. */
export function navSwipeCommit(dx: number): 'back' | 'forward' | null {
  if (Math.abs(dx) < NAV_SWIPE.commitFraction) return null
  return dx < 0 ? 'back' : 'forward'
}

function isDecaying(samples: NavSwipeState['samples']): boolean {
  const [a, b, c] = samples.slice(-3).map((s) => s.dx) as [number, number, number]
  if (samples.length < 3 || a === 0 || Math.sign(a) !== Math.sign(b) || Math.sign(b) !== Math.sign(c) || Math.abs(c) < NAV_SWIPE.decayMinPx) return false
  const r1 = b / a
  const r2 = c / b
  const inRange = (r: number) => r >= NAV_SWIPE.decayMin && r <= NAV_SWIPE.decayMax
  return inRange(r1) && inRange(r2) && Math.abs(r1 - r2) <= NAV_SWIPE.decaySpread
}

/** Share of the page width travelled toward the gesture's direction; going back past the start stops at 0. */
const travelled = (s: NavSwipeState) => Math.min(1, Math.max(0, s.sumX * s.dir) / s.width)

function releaseVelocity(s: NavSwipeState): number {
  const last = s.samples.at(-1)
  if (!last) return 0
  const recent = s.samples.filter((x) => x.t >= last.t - NAV_SWIPE.velocityWindowMs)
  const first = recent[0]
  if (!first || recent.length < 2 || last.t <= first.t) return 0
  return recent.slice(1).reduce((sum, x) => sum + x.dx, 0) / (last.t - first.t)
}

function release(s: NavSwipeState, now: number): NavSwipeState {
  const share = travelled(s)
  const v = releaseVelocity(s) * s.dir
  const commit = v > -NAV_SWIPE.commitVelocity && (share >= NAV_SWIPE.commitFraction || (v >= NAV_SWIPE.commitVelocity && share >= NAV_SWIPE.flickFraction))
  const dx = commit ? s.dir * Math.max(share, NAV_SWIPE.commitFraction) : 0
  return { ...s, phase: 'cooldown', until: now + NAV_SWIPE.cooldownMs, lastAt: now, samples: [], message: { phase: 'end', dx } }
}

function wheel(prev: NavSwipeState, e: Extract<NavSwipeInput, { kind: 'wheel' }>, now: number): NavSwipeState {
  const dx = Number.isFinite(e.dx) ? e.dx : 0
  const dy = Number.isFinite(e.dy) ? e.dy : 0
  let s: NavSwipeState = { ...prev, message: null }
  // A late idle tick: this event belongs to whatever follows the gesture before it.
  if (s.phase !== 'idle' && s.phase !== 'cooldown' && now - s.lastAt > NAV_SWIPE.idleMs) {
    s = tick(s, now)
    if (s.message) return { ...s, lastDx: dx }
  }
  if (s.phase === 'cooldown') {
    // Inside the cooldown everything is swallowed; after it, momentum still decaying from the release is too.
    const continuing = now - s.lastAt <= NAV_SWIPE.idleMs && Math.abs(dx) <= Math.abs(s.lastDx)
    if (now < s.until || continuing) return { ...s, lastDx: dx, lastAt: now }
    s = initialNavSwipe()
  }
  if (s.phase === 'ignored') return { ...s, lastAt: now }
  if (s.phase === 'idle') s = { ...initialNavSwipe(), phase: 'pending' }
  const samples = [...s.samples, { t: now, dx }].slice(-NAV_SWIPE.samples)
  if (s.phase === 'pending') {
    const next: NavSwipeState = { ...s, sumX: s.sumX + dx, sumY: s.sumY + dy, blocked: s.blocked || e.blocked, samples, lastDx: dx, lastAt: now }
    if (Math.max(Math.abs(next.sumX), Math.abs(next.sumY)) < NAV_SWIPE.lockPx) return next
    const horizontal = Math.abs(next.sumX) > NAV_SWIPE.axisRatio * Math.abs(next.sumY)
    if (!horizontal || next.blocked || !Number.isFinite(e.width) || e.width <= 0) return { ...next, phase: 'ignored' }
    const dragging: NavSwipeState = { ...next, phase: 'dragging', dir: next.sumX < 0 ? -1 : 1, width: e.width, sentAt: now }
    return { ...dragging, message: { phase: 'begin', dx: dragging.dir * travelled(dragging) } }
  }
  // Dragging. The two newest deltas of a decay were already momentum, so they neither count as travel nor feed the velocity.
  if (isDecaying(samples)) return { ...release({ ...s, sumX: s.sumX - s.lastDx, samples: samples.slice(0, -2) }, now), lastDx: dx }
  const next: NavSwipeState = { ...s, sumX: s.sumX + dx, samples, lastDx: dx, lastAt: now }
  if (now - s.sentAt < NAV_SWIPE.sendEveryMs) return next
  return { ...next, sentAt: now, message: { phase: 'update', dx: next.dir * travelled(next) } }
}

function tick(s: NavSwipeState, now: number): NavSwipeState {
  if (now - s.lastAt < NAV_SWIPE.idleMs) return { ...s, message: null }
  if (s.phase === 'dragging') return release(s, now)
  if (s.phase === 'pending' || s.phase === 'ignored') return initialNavSwipe()
  return { ...s, message: null }
}

/** One step of the gesture. `message`, when set, goes to main as `page.overscroll`. */
export function navSwipeReducer(s: NavSwipeState, e: NavSwipeInput, now: number): NavSwipeState {
  return e.kind === 'wheel' ? wheel(s, e, now) : tick(s, now)
}
