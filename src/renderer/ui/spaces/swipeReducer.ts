// Two-finger swipe between spaces (docs/plan/03-spaces.md "Sliding animation and swipe", spaces-t4).
// A pure reducer over wheel events and idle ticks, so the gesture is tested with a fake clock.
//
// Travel is signed: positive goes toward the next space (wheel deltaX > 0, fingers moving left). DOM wheel events
// carry no phase, so the browser reports the trackpad's own (windows[id].wheel): with real phases the
// gesture ends only when the fingers lift (a `lift` input), never on a pause or a timer, since fingers resting on the
// trackpad send nothing. Without phases (a mouse wheel, or before the first phased event reaches the frame) the first
// momentum event from the decaying delta curve or IDLE ms without events ends it. Momentum never moves the panels or feeds the
// velocity, and after a release the rest of the momentum is swallowed. One gesture state serves the whole sidebar
// (`sharedSwipe`), so the momentum of the swipe that opens or closes the Library is swallowed by the view that mounts
// in its place instead of starting a new swipe there.

export const SWIPE = {
  /** Travel before the axis locks. */
  lockPx: 12,
  /** Horizontal when |dx| > axisRatio * |dy| at lock. */
  axisRatio: 1.5,
  /** No wheel event for this long ends the gesture. */
  idleMs: 120,
  /** With real phases a pause is fingers resting; a drag silent this long lost its lift and settles. */
  lostLiftMs: 1500,
  /** Commit at this share of the sidebar width... */
  commitFraction: 0.3,
  /** ...or at this release velocity (px/ms) toward the neighbor; the same speed back springs back. */
  commitVelocity: 0.6,
  /** Finger samples this close to the last one measure the release velocity. */
  velocityWindowMs: 100,
  /** Past the first or last space the panels follow at this rate. */
  rubberBand: 0.3,
  /** Wheel events this soon after a release are swallowed. */
  cooldownMs: 350,
  /** The Library springs back in this long after a swipe out that did not commit. */
  settleMs: 200,
  /** The settle and the spring back: a critically damped spring at this rate (1/ms) from the release velocity.
   *  The settle decays 0.45 to 0.65 per 16.7 ms frame. */
  springOmega: 0.035,
  /** Momentum from the curve: three same-sign deltas, each this share of the one before, with ratios this close. */
  decayMin: 0.55,
  decayMax: 0.97,
  decaySpread: 0.05,
  decayMinPx: 1
} as const

export type SwipePhase = 'idle' | 'pending' | 'dragging' | 'ignored' | 'cooldown'

export type SwipeEffect =
  | { kind: 'begin' | 'update'; travel: number }
  | { kind: 'commit'; step: 1 | -1; travel: number; momentum: boolean }
  | { kind: 'cancel'; travel: number; momentum: boolean }

export interface SwipeSample { t: number; dx: number }

export interface SwipeState {
  phase: SwipePhase
  /** Accumulated deltas before the axis locks. */
  sumX: number
  sumY: number
  /** Finger samples of the current gesture (horizontal px). */
  samples: SwipeSample[]
  /** Last horizontal delta seen, for momentum continuation during the cooldown. */
  lastDx: number
  lastAt: number
  /** Cooldown end. */
  until: number
  width: number
  hasPrev: boolean
  hasNext: boolean
  /** The device reports real trackpad phases: only a lift or momentum ends the drag. */
  phased: boolean
  /** The last release's finger velocity in px/ms (positive toward the next page), for the settle's spring. */
  velocity: number
  /** Displayed travel in px (rubber-banded, clamped to the width). */
  travel: number
  /** The last event belongs to the swipe (the hook calls preventDefault). */
  handled: boolean
  effect: SwipeEffect | null
}

export type SwipeInput =
  | {
      kind: 'wheel'
      dx: number
      dy: number
      ctrlKey: boolean
      shiftKey: boolean
      /** The platform's wheel phase says momentum (DOM wheel events never do; the curve covers them). */
      momentum?: boolean
      width: number
      hasPrev: boolean
      hasNext: boolean
      /** windows[id].wheel.phased: the device reports real trackpad phases. */
      phased?: boolean
    }
  | { kind: 'tick' }
  /** The fingers left the trackpad (phase ended or cancelled, or momentum began). */
  | { kind: 'lift' }
  /** The active space changed under the gesture: the rest of it is ignored. */
  | { kind: 'abandon' }

export const initialSwipe = (): SwipeState => ({
  phase: 'idle',
  sumX: 0,
  sumY: 0,
  samples: [],
  lastDx: 0,
  lastAt: 0,
  until: 0,
  width: 0,
  hasPrev: false,
  hasNext: false,
  phased: false,
  velocity: 0,
  travel: 0,
  handled: false,
  effect: null
})

const sum = (samples: SwipeSample[]) => samples.reduce((a, s) => a + s.dx, 0)

function displayed(raw: number, s: Pick<SwipeState, 'width' | 'hasPrev' | 'hasNext'>): number {
  const open = raw > 0 ? s.hasNext : s.hasPrev
  const t = open ? raw : raw * SWIPE.rubberBand
  return Math.max(-s.width, Math.min(s.width, t))
}

/** Release velocity in px/ms from finger samples near the last one; fewer than two samples measure nothing. */
export function releaseVelocity(samples: SwipeSample[]): number {
  const last = samples.at(-1)
  if (!last) return 0
  const recent = samples.filter((s) => s.t >= last.t - SWIPE.velocityWindowMs)
  const first = recent[0]
  if (!first || recent.length < 2 || last.t <= first.t) return 0
  return sum(recent.slice(1)) / (last.t - first.t)
}

/** The last three deltas decay at a steady rate, the shape of macOS momentum after the fingers lift. */
export function isDecaying(dxs: number[]): boolean {
  if (dxs.length < 3) return false
  const [a, b, c] = dxs.slice(-3) as [number, number, number]
  if (Math.sign(a) === 0 || Math.sign(a) !== Math.sign(b) || Math.sign(b) !== Math.sign(c)) return false
  if (Math.abs(c) < SWIPE.decayMinPx) return false
  const r1 = Math.abs(b / a)
  const r2 = Math.abs(c / b)
  const inRange = (r: number) => r >= SWIPE.decayMin && r <= SWIPE.decayMax
  return inRange(r1) && inRange(r2) && Math.abs(r1 - r2) <= SWIPE.decaySpread
}

/**
 * Where a lifted swipe settles: -1, 0 or 1 page from where it started, never two. `raw` is the finger
 * travel in px (positive toward the next page), `velocity` the release speed in px/ms in the same sign. The nearest
 * page wins (past `commitFraction` of the width); a flick at `commitVelocity` toward the neighbor picks it from
 * anywhere, and the same speed back springs back even past the fraction. Junk (NaN, zero width) stays put.
 */
export function settleStep(raw: number, width: number, velocity: number, hasPrev: boolean, hasNext: boolean): -1 | 0 | 1 {
  if (!Number.isFinite(raw) || !Number.isFinite(width) || width <= 0 || raw === 0) return 0
  const dir = raw > 0 ? 1 : -1
  if (!(dir > 0 ? hasNext : hasPrev)) return 0
  const v = (Number.isFinite(velocity) ? velocity : 0) * dir
  if (v <= -SWIPE.commitVelocity) return 0
  return Math.abs(raw) >= SWIPE.commitFraction * width || v >= SWIPE.commitVelocity ? dir : 0
}

/**
 * The settle as one spring (it held, then snapped in one frame): `distance` px to go, `velocity` px/ms toward
 * the target at the release. A critically damped spring, x(t) = (x0 + (v0 + w x0) t) e^(-w t), sampled every 8 ms
 * into progress points for a compositor linear() curve until it is within half a pixel. Junk gives a short plain slide.
 */
export function springCurve(distance: number, velocity: number): { ms: number; curve: [number, number][] } {
  const x0 = Math.abs(distance)
  if (!Number.isFinite(x0) || x0 < 0.5) return { ms: 1, curve: [[0, 0], [1, 1]] }
  const w = SWIPE.springOmega
  // Displacement from the target shrinks: its rate is minus the speed toward the target, capped at a sane flick. A
  // critically damped spring crosses the target when the speed toward it passes w x0, so the cap there keeps the
  // panels from running past their rest and back (springCurve(24, 3) without it peaked at 1.64).
  const v0 = -Math.max(-8, Math.min(8, w * x0, Number.isFinite(velocity) ? velocity : 0))
  const curve: [number, number][] = [[0, 0]]
  let t = 8
  for (; t < 1000; t += 8) {
    const x = (x0 + (v0 + w * x0) * t) * Math.exp(-w * t)
    curve.push([t, Math.round((1 - x / x0) * 1000) / 1000])
    const speed = (v0 - w * (v0 + w * x0) * t) * Math.exp(-w * t)
    if (Math.abs(x) < 0.5 && Math.abs(speed) < 0.05) break
  }
  curve.push([t + 8, 1])
  return { ms: t + 8, curve }
}

function release(s: SwipeState, now: number, momentum: boolean): SwipeState {
  const raw = sum(s.samples)
  const travel = displayed(raw, s)
  const velocity = releaseVelocity(s.samples)
  const step = settleStep(raw, s.width, velocity, s.hasPrev, s.hasNext)
  const effect: SwipeEffect = step !== 0 ? { kind: 'commit', step, travel, momentum } : { kind: 'cancel', travel, momentum }
  return { ...s, phase: 'cooldown', until: now + SWIPE.cooldownMs, lastAt: now, travel: 0, samples: [], velocity, effect }
}

function wheel(s: SwipeState, e: Extract<SwipeInput, { kind: 'wheel' }>, now: number): SwipeState {
  // Pinch zoom arrives as ctrlKey wheel events; it is never a swipe and never ends or extends one.
  if (e.ctrlKey) return { ...s, handled: false, effect: null }
  let dx = Number.isFinite(e.dx) ? e.dx : 0
  let dy = Number.isFinite(e.dy) ? e.dy : 0
  // Shift + a vertical mouse wheel counts as horizontal.
  if (e.shiftKey && dx === 0) [dx, dy] = [dy, 0]
  const width = Number.isFinite(e.width) && e.width > 0 ? e.width : 1
  const ctx = { width, hasPrev: e.hasPrev, hasNext: e.hasNext, phased: e.phased === true }

  // A late idle tick: end what was running before this event. With real phases a pause is fingers resting, not a lift.
  if (s.phase !== 'idle' && s.phase !== 'cooldown' && now - s.lastAt > (s.phased && s.phase === 'dragging' ? SWIPE.lostLiftMs : SWIPE.idleMs)) {
    const ended = tick(s, now)
    if (ended.effect) return { ...ended, lastDx: dx, handled: true }
    s = ended
  }

  if (s.phase === 'cooldown') {
    // Inside the cooldown everything is swallowed; after it, so is every event that keeps the stream going. macOS
    // momentum runs for a second or more and its deltas jitter (an integer delta can repeat or grow by one), so a
    // size test let a late momentum event start a second swipe and carry one page too far. Only a pause
    // (the fingers lifted and touched again) starts a new gesture.
    const continuing = e.momentum === true || now - s.lastAt <= SWIPE.idleMs
    if (now < s.until || continuing) return { ...s, lastDx: dx, lastAt: now, handled: true, effect: null }
    s = initialSwipe()
  }

  if (s.phase === 'idle') {
    // Momentum never starts a gesture.
    if (e.momentum) return { ...s, handled: false, effect: null }
    s = { ...initialSwipe(), phase: 'pending' }
  }

  if (s.phase === 'ignored') return { ...s, lastAt: now, handled: false, effect: null }

  if (s.phase === 'pending') {
    const sumX = s.sumX + dx
    const sumY = s.sumY + dy
    const samples = [...s.samples, { t: now, dx }]
    const next = { ...s, ...ctx, sumX, sumY, samples, lastDx: dx, lastAt: now, handled: false, effect: null }
    if (Math.max(Math.abs(sumX), Math.abs(sumY)) < SWIPE.lockPx) return next
    if (Math.abs(sumX) > SWIPE.axisRatio * Math.abs(sumY)) {
      const travel = displayed(sumX, ctx)
      return { ...next, phase: 'dragging', travel, handled: true, effect: { kind: 'begin', travel } }
    }
    return { ...next, phase: 'ignored' }
  }

  // Dragging. A momentum event releases at once and moves nothing.
  if (e.momentum) return { ...release({ ...s, ...ctx }, now, true), lastDx: dx, handled: true }
  const samples = [...s.samples, { t: now, dx }]
  // Without phases the decaying curve is the only sign of momentum. With them it is a guess that fingers slowing down
  // also match (6, 5, 4 px), so only the lift ends the drag.
  if (!ctx.phased && isDecaying(samples.map((x) => x.dx))) {
    // The two newest deltas were already momentum: drop them so they neither move the panels nor feed the velocity.
    return { ...release({ ...s, ...ctx, samples: samples.slice(0, -2) }, now, true), lastDx: dx, handled: true }
  }
  const travel = displayed(sum(samples), ctx)
  return { ...s, ...ctx, samples, travel, lastDx: dx, lastAt: now, handled: true, effect: { kind: 'update', travel } }
}

function tick(s: SwipeState, now: number): SwipeState {
  if (s.phase === 'dragging' && now - s.lastAt >= (s.phased ? SWIPE.lostLiftMs : SWIPE.idleMs)) return { ...release(s, now, false), handled: false }
  if ((s.phase === 'pending' || s.phase === 'ignored') && now - s.lastAt >= SWIPE.idleMs) return initialSwipe()
  return { ...s, handled: false, effect: null }
}

export function swipeReducer(s: SwipeState, e: SwipeInput, now: number): SwipeState {
  if (e.kind === 'wheel') return wheel(s, e, now)
  if (e.kind === 'tick') return tick(s, now)
  if (e.kind === 'lift') {
    if (s.phase === 'dragging') return { ...release(s, now, false), handled: false }
    if (s.phase === 'pending' || s.phase === 'ignored') return initialSwipe()
    return { ...s, handled: false, effect: null }
  }
  if (s.phase === 'dragging') return { ...s, phase: 'ignored', travel: 0, samples: [], handled: false, effect: { kind: 'cancel', travel: s.travel, momentum: false } }
  return { ...s, handled: false, effect: null }
}

/**
 * Where a swipe can go from space index `at` of `count`: the Library sits before the first space, so
 * fingers moving right on the first space follow fully and a commit there opens the Library. -1 means no space.
 */
export function swipeEdges(at: number, count: number): { hasPrev: boolean; hasNext: boolean } {
  return { hasPrev: at >= 0, hasNext: at >= 0 && at < count - 1 }
}

/** A commit of `step` from space index `at` opens the Library instead of switching spaces. */
export const opensLibrary = (at: number, step: number): boolean => at === 0 && step < 0

/**
 * A swipe out of the Library (it used to vanish in one frame and the space list appeared at
 * x 79.8 or 158.4). The Library slides from `from` px to its `width` on `curve`, and the sidebar that replaces it rides
 * the same curve from `at`, with the Library drawn beside it, so the two move as one strip from where the fingers left.
 */
export interface LibraryExit { from: number; width: number; at: number; ms: number; curve: [number, number][] }

/**
 * The sidebar's one gesture, shared by the space slider and the Library: a swipe that opens or closes the
 * Library unmounts the view it started in, and the view that mounts next must keep swallowing that swipe's momentum.
 */
export const sharedSwipe: { state: SwipeState; exit: LibraryExit | null } = { state: initialSwipe(), exit: null }

const FRAME_MS = 1000 / 60

/**
 * A committed swipe's settle, the space strip's and the Library's exit: over `distance` px from the release
 * `velocity` (px/ms toward the target), one point per 60 Hz
 * frame for a linear() curve: the first frame moves at the fingers' speed, each next frame at most 1.45x the one
 * before, then the rest decays 0.55 a frame to half a pixel.
 * So no frame steps more than 1.5x the last one. Junk gives a one-frame curve.
 */
export function exitCurve(distance: number, velocity: number): { ms: number; curve: [number, number][] } {
  const total = Math.abs(distance)
  if (!Number.isFinite(total) || total < 0.5) return { ms: 1, curve: [[0, 0], [1, 1]] }
  let move = Math.max(1, Math.min(8, Number.isFinite(velocity) ? Math.abs(velocity) : 0) * FRAME_MS)
  const curve: [number, number][] = [[0, 0]]
  let left = total
  let f = 0
  while (left > 0 && f < 120) {
    move = Math.min(left, f === 0 ? move : Math.min(move * 1.45, left * 0.45))
    left -= move
    // The last half pixel goes with this frame: 1.22x the frame before it at most, never a snap.
    if (left < 0.5) left = 0
    f++
    curve.push([Math.round(f * FRAME_MS * 10) / 10, Math.round((1 - left / total) * 10000) / 10000])
  }
  return { ms: curve.at(-1)?.[0] ?? 1, curve }
}

/**
 * Where a settle's Web Animation starts on its curve, in ms (the strip used to hold for one or two frames
 * after the lift, then stepped 11.7 to 19 px). An animation started in this frame paints its start time first, so a
 * settle that began at `at` starts one frame further on: the first frame after the lift already moves at the release
 * speed, and an animation picked up later (main's switch, the sidebar after the Library) lands where the one it
 * replaces was going to be. Junk `at` starts one frame in.
 */
export function settleStart(at: number | undefined, now: number, ms: number): number {
  const elapsed = at !== undefined && Number.isFinite(at) && Number.isFinite(now) ? Math.max(0, now - at) : 0
  return Math.max(0, Math.min(Number.isFinite(ms) ? ms : 0, elapsed + FRAME_MS))
}

/** The exit of a commit `from` px into a Library `width` px wide, released at `velocity` px/ms, at time `at`. */
export function libraryExit(from: number, width: number, velocity: number, at: number): LibraryExit {
  const w = Number.isFinite(width) && width > 0 ? width : 0
  const f = Number.isFinite(from) ? Math.max(0, Math.min(w, from)) : 0
  return { from: f, width: w, at, ...exitCurve(w - f, velocity) }
}

/** Progress (0 to 1) of a linear() curve's points at `t` ms. */
export function curveAt(curve: readonly (readonly [number, number])[], t: number): number {
  if (!(t > 0)) return 0
  for (let i = 1; i < curve.length; i++) {
    const [t1, p1] = curve[i] as readonly [number, number]
    const [t0, p0] = curve[i - 1] as readonly [number, number]
    if (t <= t1) return t1 > t0 ? p0 + ((p1 - p0) * (t - t0)) / (t1 - t0) : p1
  }
  return 1
}

/** How far the Library has slid out at `now` in px (its width once done). */
export function exitTravel(exit: LibraryExit, now: number): number {
  return exit.from + (exit.width - exit.from) * curveAt(exit.curve, now - exit.at)
}

/** The exit still running at `now`, or null (none, junk, or finished). */
export function liveExit(exit: LibraryExit | null, now: number): LibraryExit | null {
  if (!exit || !Number.isFinite(exit.at) || !(exit.width > 0) || now - exit.at >= exit.ms) return null
  return exit
}

/** A view going away keeps a cooldown (its momentum is still coming) and drops anything else. */
export function leaveSwipe(): void {
  if (sharedSwipe.state.phase !== 'cooldown') sharedSwipe.state = initialSwipe()
}
