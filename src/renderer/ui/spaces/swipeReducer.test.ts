// swipeReducer with a fake clock (spaces-t4): commit and spring back, flick velocity, momentum, axis lock, rubber
// band and pinch.
import { describe, expect, it } from 'vitest'
import { initialSwipe, isDecaying, leaveSwipe, opensLibrary, settleStep, sharedSwipe, SWIPE, swipeEdges, swipeReducer, type SwipeEffect, type SwipeInput, type SwipeState } from './swipeReducer'

const W = 260
interface Ev { dx: number; dy?: number; ctrlKey?: boolean; shiftKey?: boolean; momentum?: boolean }

/** Feeds events 16 ms apart (or `gap` ms) starting at `t0`, then optional idle ticks; records every effect. */
function run(events: Ev[], opts: { t0?: number; gap?: number; hasPrev?: boolean; hasNext?: boolean; state?: SwipeState } = {}) {
  let s = opts.state ?? initialSwipe()
  let t = opts.t0 ?? 1000
  const gap = opts.gap ?? 16
  const effects: { at: number; effect: SwipeEffect }[] = []
  const handled: boolean[] = []
  const travels: number[] = []
  const feed = (e: SwipeInput, at: number) => {
    s = swipeReducer(s, e, at)
    if (s.effect) effects.push({ at, effect: s.effect })
    travels.push(s.travel)
  }
  for (const ev of events) {
    feed({ kind: 'wheel', dx: ev.dx, dy: ev.dy ?? 0, ctrlKey: ev.ctrlKey ?? false, shiftKey: ev.shiftKey ?? false, momentum: ev.momentum, width: W, hasPrev: opts.hasPrev ?? true, hasNext: opts.hasNext ?? true }, t)
    handled.push(s.handled)
    t += gap
  }
  const lastAt = t - gap
  return {
    get state() {
      return s
    },
    effects,
    handled,
    travels,
    lastAt,
    /** Ticks at the idle deadline after the last event. */
    idle() {
      feed({ kind: 'tick' }, lastAt + SWIPE.idleMs)
      return this
    },
    released: () => effects.find((x) => x.effect.kind === 'commit' || x.effect.kind === 'cancel')
  }
}

const steady = (total: number, n: number): Ev[] => Array.from({ length: n }, () => ({ dx: total / n }))
/** macOS-style momentum: deltas decaying by `ratio` per frame. */
const momentum = (start: number, n: number, ratio = 0.9, flag = false): Ev[] => Array.from({ length: n }, (_, i) => ({ dx: start * ratio ** i, ...(flag ? { momentum: true } : {}) }))

describe('swipeReducer', () => {
  it('a slow 40%-width horizontal drag commits step +1 on idle, and 20% springs back', () => {
    // 104 px over 13 events, 0.5 px/ms: below the flick velocity, so only the distance decides.
    const forty = run(steady(104, 13)).idle()
    expect(forty.effects[0]?.effect.kind).toBe('begin')
    expect(forty.state.phase).toBe('cooldown')
    expect(forty.released()?.effect).toEqual({ kind: 'commit', step: 1, travel: 104, momentum: false })
    const twenty = run(steady(52, 13), { gap: 30 }).idle()
    expect(twenty.released()?.effect).toMatchObject({ kind: 'cancel', travel: 52 })
    // The other way commits step -1.
    const back = run(steady(-104, 13)).idle()
    expect(back.released()?.effect).toMatchObject({ kind: 'commit', step: -1 })
  })

  it('panels follow the drag 1:1 and nothing commits before the release', () => {
    const r = run(steady(104, 13))
    expect(r.effects.filter((x) => x.effect.kind === 'commit')).toEqual([])
    expect(r.travels.at(-1)).toBeCloseTo(104)
    expect(r.state.phase).toBe('dragging')
  })

  it('a short 0.8 px/ms flick commits on its first momentum event', () => {
    // 12.8 px every 16 ms is 0.8 px/ms; 51 px is under 30% of the width.
    const r = run([...steady(4 * 12.8, 4), ...momentum(11, 6, 0.9, true)])
    expect(r.released()?.effect).toMatchObject({ kind: 'commit', step: 1, momentum: true })
    expect(r.released()?.at).toBe(1000 + 4 * 16)
    // Without momentum (inertia off) the same flick commits at the idle release.
    expect(run(steady(4 * 12.8, 4)).idle().released()?.effect).toMatchObject({ kind: 'commit', step: 1 })
    // A slower drag of the same distance springs back.
    expect(run(steady(4 * 12.8, 4), { gap: 40 }).idle().released()?.effect.kind).toBe('cancel')
  })

  it('a fast flick back toward rest springs back even past 30%', () => {
    const r = run([...steady(W, 20), { dx: -40 }, { dx: -40 }, { dx: -40 }, { dx: -40 }]).idle()
    expect(r.released()?.effect).toMatchObject({ kind: 'cancel', travel: 100 })
  })

  it('velocity comes from finger-down samples only: large momentum deltas never turn a slow drag into a flick', () => {
    // A slow 20% drag (0.2 px/ms), then momentum flagged by the wheel phase with big deltas.
    const flagged = run([...steady(52, 13), ...momentum(40, 12, 0.9, true)], { gap: 20 })
    expect(flagged.released()?.effect).toEqual({ kind: 'cancel', travel: 52, momentum: true })
    // The same from the decaying curve alone (no phase): the momentum deltas seen before detection are dropped.
    const curve = run([...steady(52, 13), ...momentum(4, 12, 0.9)], { gap: 20 })
    expect(curve.released()?.effect).toMatchObject({ kind: 'cancel', momentum: true })
    expect((curve.released()?.effect as { travel: number }).travel).toBeCloseTo(52 + 4)
  })

  it('momentum never moves or delays the commit, and momentum within 350 ms after it is swallowed', () => {
    const fingers = steady(104, 13)
    const fingerEnd = 1000 + 12 * 16
    for (const flag of [true, false]) {
      const r = run([...fingers, ...momentum(10, 40, 0.92, flag)])
      const commit = r.released()
      expect(commit?.effect).toMatchObject({ kind: 'commit', step: 1, momentum: true })
      // Decided on the first momentum event with a phase, or the third from the curve: never after the tail.
      expect(commit?.at).toBe(fingerEnd + (flag ? 16 : 48))
      // Momentum never moved the panels: the commit's travel is the fingers' travel...
      const travel = (commit?.effect as { travel: number }).travel
      expect(travel).toBeCloseTo(flag ? 104 : 114)
      // ...and the highest travel seen is no more than that.
      expect(Math.max(...r.travels)).toBeLessThanOrEqual(104 + 10 + 9.2 + 0.001)
      // One commit, every event after it swallowed (handled, no effect), across the whole tail.
      expect(r.effects.filter((x) => x.effect.kind === 'commit')).toHaveLength(1)
      const after = r.handled.slice(13 + (flag ? 1 : 3))
      expect(after.every(Boolean)).toBe(true)
      expect(r.effects.filter((x) => x.at > (commit?.at ?? 0))).toEqual([])
    }
  })

  it('swallows any wheel event within 350 ms after a commit, then starts fresh gestures', () => {
    const r = run(steady(104, 13)).idle()
    const at = r.lastAt + SWIPE.idleMs
    // A big rising delta inside the cooldown is still swallowed.
    let s = swipeReducer(r.state, { kind: 'wheel', dx: 50, dy: 0, ctrlKey: false, shiftKey: false, width: W, hasPrev: true, hasNext: true }, at + 300)
    expect(s).toMatchObject({ handled: true, effect: null, phase: 'cooldown' })
    // After the cooldown and an idle gap, a new drag begins.
    s = swipeReducer(s, { kind: 'wheel', dx: 20, dy: 0, ctrlKey: false, shiftKey: false, width: W, hasPrev: true, hasNext: true }, at + 600)
    expect(s.phase).toBe('dragging')
    expect(s.effect).toEqual({ kind: 'begin', travel: 20 })
  })

  it('a vertical-dominant gesture never commits and is never handled, so the list scrolls', () => {
    const r = run(Array.from({ length: 30 }, () => ({ dx: 6, dy: 10 }))).idle()
    expect(r.effects).toEqual([])
    expect(r.handled.every((h) => !h)).toBe(true)
    // Diagonal but not 1.5x horizontal: still ignored.
    expect(run(Array.from({ length: 30 }, () => ({ dx: 14, dy: 10 }))).idle().effects).toEqual([])
    // A horizontal tail after the lock does not revive it.
    expect(run([...Array.from({ length: 3 }, () => ({ dx: 0, dy: 10 })), ...steady(200, 10)]).idle().effects).toEqual([])
  })

  it('past the last space it rubber-bands at 0.3x and never commits, even as a flick', () => {
    const r = run(steady(200, 10), { hasNext: false }).idle()
    expect(r.travels.at(-2)).toBeCloseTo(60)
    expect(r.released()?.effect).toMatchObject({ kind: 'cancel' })
    expect(run(steady(-200, 4), { hasPrev: false, gap: 4 }).idle().released()?.effect.kind).toBe('cancel')
    // Toward an existing neighbor it still follows 1:1.
    expect(run(steady(-100, 10), { hasNext: false }).travels.at(-1)).toBeCloseTo(-100)
  })

  it('ctrlKey wheel (pinch) is ignored and leaves a running drag alone', () => {
    const pinch = run(Array.from({ length: 20 }, () => ({ dx: 20, ctrlKey: true }))).idle()
    expect(pinch.effects).toEqual([])
    expect(pinch.handled.every((h) => !h)).toBe(true)
    const mixed = run([...steady(60, 5), { dx: 500, ctrlKey: true }, ...steady(60, 5)])
    expect(mixed.state.travel).toBeCloseTo(120)
  })

  it('shift + vertical wheel counts as horizontal', () => {
    const r = run([{ dx: 0, dy: 100, shiftKey: true }, { dx: 0, dy: 100, shiftKey: true }], { gap: 50 }).idle()
    expect(r.released()?.effect).toMatchObject({ kind: 'commit', step: 1 })
  })

  it('momentum never starts a gesture, and abandon cancels a running drag', () => {
    expect(run(momentum(30, 10, 0.9, true)).effects).toEqual([])
    const r = run(steady(80, 8))
    const s = swipeReducer(r.state, { kind: 'abandon' }, r.lastAt + 1)
    expect(s.effect).toMatchObject({ kind: 'cancel' })
    expect(s.phase).toBe('ignored')
    expect(swipeReducer(s, { kind: 'tick' }, r.lastAt + 1 + SWIPE.idleMs).phase).toBe('idle')
  })

  it('a late idle tick ends the gesture on the next event, and junk deltas are harmless', () => {
    const r = run(steady(104, 13))
    const s = swipeReducer(r.state, { kind: 'wheel', dx: 30, dy: 0, ctrlKey: false, shiftKey: false, width: W, hasPrev: true, hasNext: true }, r.lastAt + 500)
    expect(s.effect).toMatchObject({ kind: 'commit', step: 1, travel: 104 })
    const junk = run([{ dx: Number.NaN }, { dx: Number.POSITIVE_INFINITY, dy: Number.NaN }, ...steady(30, 3)])
    expect(junk.state.travel).toBeCloseTo(30)
  })

  it('isDecaying needs a steady decay, not a jittery or linear one', () => {
    expect(isDecaying([10, 9, 8.1])).toBe(true)
    expect(isDecaying([10, 10, 10])).toBe(false)
    expect(isDecaying([10, 6, 9])).toBe(false)
    expect(isDecaying([10, -9, 8.1])).toBe(false)
    expect(isDecaying([10, 9.5, 6])).toBe(false)
  })
})

describe('the Library before the first space', () => {
  it('the first space has a previous side, the Library; the last space has no next side; an unknown space has none', () => {
    expect(swipeEdges(0, 3)).toEqual({ hasPrev: true, hasNext: true })
    expect(swipeEdges(2, 3)).toEqual({ hasPrev: true, hasNext: false })
    expect(swipeEdges(0, 1)).toEqual({ hasPrev: true, hasNext: false })
    expect(swipeEdges(-1, 3)).toEqual({ hasPrev: false, hasNext: false })
  })

  it('fingers moving right on the first space follow fully and commit step -1, which opens the Library', () => {
    const r = run(steady(-104, 13), swipeEdges(0, 3)).idle()
    // No rubber band toward the Library: the panel moves the whole way the fingers did.
    expect(r.travels.at(-2)).toBe(-104)
    expect(r.released()?.effect).toMatchObject({ kind: 'commit', step: -1 })
    expect(opensLibrary(0, -1)).toBe(true)
    // Any other commit switches spaces.
    expect(opensLibrary(0, 1)).toBe(false)
    expect(opensLibrary(1, -1)).toBe(false)
    expect(opensLibrary(-1, -1)).toBe(false)
  })

  it('inside the Library only fingers moving left close it; right rubber-bands and springs back', () => {
    const close = run(steady(104, 13), { hasPrev: false, hasNext: true }).idle()
    expect(close.released()?.effect).toMatchObject({ kind: 'commit', step: 1 })
    const right = run(steady(-200, 13), { hasPrev: false, hasNext: true }).idle()
    expect(right.travels.at(-2)).toBeCloseTo(-200 * SWIPE.rubberBand)
    expect(right.released()?.effect.kind).toBe('cancel')
  })
})

describe('settleStep: position + velocity -> at most one page', () => {
  it('lands on the nearest page, a flick picks the neighbor, a flick back springs back', () => {
    expect(settleStep(0.4 * W, W, 0, true, true)).toBe(1)
    expect(settleStep(-0.4 * W, W, 0, true, true)).toBe(-1)
    expect(settleStep(0.2 * W, W, 0, true, true)).toBe(0)
    expect(settleStep(0.05 * W, W, 0.9, true, true)).toBe(1)
    // Reversed direction: past the fraction but the fingers were already heading back fast.
    expect(settleStep(0.6 * W, W, -0.9, true, true)).toBe(0)
    expect(settleStep(-0.6 * W, W, 0.9, true, true)).toBe(0)
  })

  it('never moves two pages, whatever the travel or speed, and never past an edge', () => {
    for (const raw of [W, 2 * W, 10 * W, -10 * W]) for (const v of [0, 5, -5, 100]) expect(Math.abs(settleStep(raw, W, v, true, true))).toBeLessThanOrEqual(1)
    expect(settleStep(W, W, 3, true, false)).toBe(0)
    expect(settleStep(-W, W, -3, false, true)).toBe(0)
  })

  it('junk input stays put', () => {
    expect(settleStep(0, W, 5, true, true)).toBe(0)
    expect(settleStep(Number.NaN, W, 0, true, true)).toBe(0)
    expect(settleStep(100, 0, 0, true, true)).toBe(0)
    expect(settleStep(100, Number.NaN, 0, true, true)).toBe(0)
    expect(settleStep(50, W, Number.NaN, true, true)).toBe(0)
    expect(settleStep(100, W, Number.NaN, true, true)).toBe(1)
    expect(settleStep(Number.POSITIVE_INFINITY, W, 0, true, true)).toBe(0)
  })
})

describe('one swipe, one page', () => {
  const ev = (dx: number, extra: Partial<Extract<SwipeInput, { kind: 'wheel' }>> = {}): SwipeInput => ({ kind: 'wheel', dx, dy: 0, ctrlKey: false, shiftKey: false, width: W, hasPrev: true, hasNext: true, ...extra })

  it('a jittery momentum burst after the lift, long past the cooldown, never starts a second swipe', () => {
    const r = run(steady(104, 13)).idle()
    let s = r.state
    let t = r.lastAt + SWIPE.idleMs
    // 1.2 s of momentum with integer jitter: deltas repeat and grow by one, as macOS sends them.
    const burst = [9, 9, 10, 8, 8, 9, 7, 7, 8, 6, 6, 7, 5, 5, 6, 4, 4, 5, 3, 3, 4, 2, 2, 3, 2, 1, 1, 2, 1, 1]
    const effects: SwipeEffect[] = []
    for (let i = 0; i < 75; i++) {
      t += 16
      s = swipeReducer(s, ev(burst[i % burst.length] ?? 1), t)
      expect(s.handled).toBe(true)
      if (s.effect) effects.push(s.effect)
    }
    expect(effects).toEqual([])
    expect(s.phase).toBe('cooldown')
    // A pause (fingers lifted, then down again) starts the next swipe.
    s = swipeReducer(s, ev(20), t + SWIPE.idleMs + 1)
    expect(s.effect).toEqual({ kind: 'begin', travel: 20 })
  })

  it('zero deltas neither start, move nor release a swipe', () => {
    const r = run(Array.from({ length: 20 }, () => ({ dx: 0 }))).idle()
    expect(r.effects).toEqual([])
    expect(r.handled.every((h) => !h)).toBe(true)
  })

  it('reversed direction mid-drag settles on the side the fingers end on, one page at most', () => {
    const r = run([...steady(150, 10), ...steady(-260, 20)]).idle()
    expect(r.released()?.effect).toMatchObject({ kind: 'commit', step: -1 })
    expect(r.effects.filter((x) => x.effect.kind === 'commit')).toHaveLength(1)
  })

  it('the shared state keeps a cooldown across views, so leaving the Library never swipes the space list on', () => {
    sharedSwipe.state = run(steady(104, 13)).idle().state
    leaveSwipe()
    expect(sharedSwipe.state.phase).toBe('cooldown')
    // The space list mounts and the closing swipe's momentum reaches it: swallowed.
    sharedSwipe.state = swipeReducer(sharedSwipe.state, ev(30), sharedSwipe.state.lastAt + 16)
    expect(sharedSwipe.state).toMatchObject({ handled: true, effect: null })
    // A view leaving mid-drag drops the drag.
    sharedSwipe.state = run(steady(40, 4)).state
    leaveSwipe()
    expect(sharedSwipe.state.phase).toBe('idle')
  })
})
