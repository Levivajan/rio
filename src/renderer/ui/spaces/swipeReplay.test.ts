// Replays of swipe traces: continuous pixel wheel events on an
// 8 ms clock with phases began, changed and ended, then momentum. A 60 Hz frame loop processes the events queued
// since the last frame (with their own timestamps, as the hooks pass e.timeStamp) and records the strip position;
// a stall holds the frames and the queue, as a busy main thread does.
import { describe, expect, it } from 'vitest'
import { exitTravel, initialSwipe, libraryExit, springCurve, SWIPE, swipeReducer, type SwipeEffect, type SwipeState } from './swipeReducer'

const WIDTH = 300
const FRAME = 1000 / 60

interface WheelIn { t: number; dx: number; phase: 'began' | 'changed' | 'ended' | 'momentum' }

/** `px` of finger travel over `ms` in 8 ms events, then `momentumPx` decaying over 8 ms events. */
function trace(px: number, ms: number, momentumPx = 0, t0 = 1000): WheelIn[] {
  const n = Math.max(1, Math.round(ms / 8))
  const out: WheelIn[] = []
  let sent = 0
  for (let i = 1; i <= n; i++) {
    // Integer deltas, as macOS sends them, that add up to exactly `px`.
    const dx = Math.round((px * i) / n) - sent
    sent += dx
    out.push({ t: t0 + i * 8, dx, phase: i === 1 ? 'began' : 'changed' })
  }
  const end = t0 + n * 8 + 8
  out.push({ t: end, dx: 0, phase: 'ended' })
  let v = (px / ms) * 8
  let left = momentumPx
  for (let t = end + 8; left > 0.5; t += 8) {
    const dx = Math.min(left, Math.max(1, Math.round(v)))
    left -= dx
    v *= 0.92
    out.push({ t, dx, phase: 'momentum' })
  }
  return out
}

interface Run { frames: { t: number; travel: number | null; input: number }[]; effects: SwipeEffect[]; state: SwipeState }

/**
 * Feeds the trace through the reducer as the hook does: phase ended reaches the frame as a `lift` (the browser's
 * windows[id].wheel), momentum events reach the DOM with no phase. `stall` = [from, to] holds every frame and
 * event in that window until `to`.
 */
function replay(input: WheelIn[], opts: { phased?: boolean; hasPrev?: boolean; hasNext?: boolean; stall?: [number, number] } = {}): Run {
  const { phased = true, hasPrev = true, hasNext = true, stall } = opts
  let state = initialSwipe()
  const effects: SwipeEffect[] = []
  const frames: Run['frames'] = []
  let travel: number | null = null
  let fed = 0
  let sum = 0
  const last = input.at(-1)?.t ?? 0
  for (let f = (input[0]?.t ?? 0) - FRAME; f <= last + 600; f += FRAME) {
    if (stall && f > stall[0] && f < stall[1]) continue
    // Chromium hands queued wheel events to the page before the frame's callbacks.
    for (let e = input[fed]; e && e.t <= f; e = input[fed]) {
      fed++
      if (e.phase === 'ended') {
        if (phased) state = swipeReducer(state, { kind: 'lift' }, f)
      } else {
        if (e.phase !== 'momentum') sum += e.dx
        state = swipeReducer(state, { kind: 'wheel', dx: e.dx, dy: 0, ctrlKey: false, shiftKey: false, width: WIDTH, hasPrev, hasNext, phased }, e.t)
      }
      if (state.effect) {
        effects.push(state.effect)
        travel = state.effect.kind === 'begin' || state.effect.kind === 'update' ? state.effect.travel : null
      }
    }
    // The hook's once-per-frame idle check.
    state = swipeReducer(state, { kind: 'tick' }, f)
    if (state.effect) {
      effects.push(state.effect)
      travel = null
    }
    frames.push({ t: f, travel, input: sum })
  }
  return { frames, effects, state }
}

const released = (r: Run) => r.effects.filter((e) => e.kind === 'commit' || e.kind === 'cancel')

describe('swipe trace replays', () => {
  it('a slow 140 px swipe over 700 ms with a 183 ms stall keeps tracking 1:1 and ends only at the lift', () => {
    const input = trace(140, 700)
    const lift = input.find((e) => e.phase === 'ended')?.t ?? 0
    const r = replay(input, { stall: [1300, 1483] })
    // No release before the fingers lift.
    const first = r.frames.findIndex((f) => f.travel === null && f.input >= SWIPE.lockPx)
    expect(r.frames[first]?.t).toBeGreaterThanOrEqual(lift)
    // Every drawn frame shows exactly the finger travel so far: each wheel delta moves the strip by that delta.
    const dragging = r.frames.filter((f) => f.travel !== null)
    expect(dragging.length).toBeGreaterThan(25)
    for (const f of dragging) expect(f.travel).toBe(f.input)
    // 140 px is past the commit share of 300 px: one page on.
    expect(released(r)).toEqual([{ kind: 'commit', step: 1, travel: 140, momentum: false }])
  })

  it('a slow swipe that slows down before the lift is never read as momentum', () => {
    // 6, 5, 4, 3 px steps: the decay shape of momentum, with the fingers still down.
    const input: WheelIn[] = [
      ...[10, 10, 8, 6, 5, 4, 3, 3, 2].map((dx, i) => ({ t: 1008 + i * 8, dx, phase: (i ? 'changed' : 'began') as WheelIn['phase'] })),
      { t: 1200, dx: 2, phase: 'changed' },
      { t: 1208, dx: 0, phase: 'ended' }
    ]
    const r = replay(input)
    expect(released(r)).toEqual([{ kind: 'cancel', travel: 53, momentum: false }])
    expect(r.frames.filter((f) => f.t < 1208 && f.input >= SWIPE.lockPx).every((f) => f.travel === f.input)).toBe(true)
  })

  it('fingers resting mid-swipe (no events for 400 ms) do not end the gesture', () => {
    const a = trace(60, 240)
    const rest = a.filter((e) => e.phase !== 'ended')
    const b = trace(60, 240, 0, (rest.at(-1)?.t ?? 0) + 400).map((e) => (e.phase === 'began' ? { ...e, phase: 'changed' as const } : e))
    const r = replay([...rest, ...b])
    expect(released(r)).toEqual([{ kind: 'commit', step: 1, travel: 120, momentum: false }])
  })

  it('260 px over 1000 ms reaches the next space with no repeated or jumped frames', () => {
    const r = replay(trace(260, 1000), { stall: [1400, 1533] })
    const dragging = r.frames.filter((f) => f.travel !== null)
    for (const f of dragging) expect(f.travel).toBe(f.input)
    // While the fingers move, every frame moves the strip.
    for (let i = 1; i < dragging.length; i++) expect(dragging[i]?.travel).toBeGreaterThan(dragging[i - 1]?.travel ?? Infinity)
    expect(released(r)).toEqual([{ kind: 'commit', step: 1, travel: 260, momentum: false }])
  })

  it('a flick with a long momentum tail moves exactly one page', () => {
    const r = replay(trace(90, 96, 420))
    expect(released(r)).toHaveLength(1)
    expect(released(r)[0]).toMatchObject({ kind: 'commit', step: 1 })
    // Momentum never moves the strip or starts a second swipe.
    expect(r.effects.filter((e) => e.kind === 'begin')).toHaveLength(1)
    expect(r.state.phase).toBe('cooldown')
  })

  it('a slow swipe out of the Library follows the fingers and closes it', () => {
    // The Library has only a next side: the space it was opened from.
    const r = replay(trace(260, 800), { hasPrev: false, hasNext: true, stall: [1300, 1430] })
    const dragging = r.frames.filter((f) => f.travel !== null)
    expect(Math.max(...dragging.map((f) => f.travel ?? 0))).toBeGreaterThan(250)
    for (const f of dragging) expect(f.travel).toBe(f.input)
    expect(released(r)).toEqual([{ kind: 'commit', step: 1, travel: 260, momentum: false }])
  })

  it('a flick out of the Library closes it once, and the space list slides in from where the Library got to', () => {
    const r = replay(trace(90, 96, 420), { hasPrev: false, hasNext: true })
    expect(released(r)).toHaveLength(1)
    expect(released(r)[0]).toMatchObject({ kind: 'commit', step: 1 })
    // The Library committed at 90 of 300 px: its exit slides the other 210 px, from where the fingers left it.
    const exit = libraryExit(90, 300, 1, 0)
    expect(exitTravel(exit, 0)).toBe(90)
    expect(exitTravel(exit, exit.ms / 2)).toBeGreaterThan(90)
    expect(exitTravel(exit, exit.ms * 2)).toBe(300)
    expect(libraryExit(Number.NaN, 300, 1, 0).from).toBe(0)
    expect(libraryExit(90, Number.NaN, 1, 0).ms).toBe(1)
  })

  it('without phases (a mouse wheel) a stall alone still never ends a moving gesture', () => {
    const r = replay(trace(140, 700), { phased: false, stall: [1300, 1483] })
    for (const f of r.frames.filter((x) => x.travel !== null)) expect(f.travel).toBe(f.input)
    // The idle end comes after the last event, never inside the stall.
    expect(released(r)).toEqual([{ kind: 'commit', step: 1, travel: 140, momentum: false }])
  })

  it('a lift outside a drag is harmless', () => {
    expect(swipeReducer(initialSwipe(), { kind: 'lift' }, 0)).toMatchObject({ phase: 'idle', effect: null })
    const pending = swipeReducer(initialSwipe(), { kind: 'wheel', dx: 3, dy: 0, ctrlKey: false, shiftKey: false, width: WIDTH, hasPrev: true, hasNext: true, phased: true }, 0)
    expect(swipeReducer(pending, { kind: 'lift' }, 8).phase).toBe('idle')
  })

  it('the settle is one spring from the release velocity: it moves from the first frame and never snaps', () => {
    // 110 px left to go after a 1000 ms drag at 0.26 px/ms.
    const slow = springCurve(110, 0.26)
    const flick = springCurve(110, 1.5)
    for (const { curve } of [slow, flick]) {
      expect(curve[0]).toEqual([0, 0])
      expect(curve.at(-1)?.[1]).toBe(1)
      // Moving on the first 8 ms sample (no hold), and no step bigger than a fifth of the way (no one-frame snap).
      expect(curve[1]?.[1]).toBeGreaterThan(0)
      for (let i = 1; i < curve.length; i++) expect(Math.abs((curve[i]?.[1] ?? 0) - (curve[i - 1]?.[1] ?? 0))).toBeLessThan(0.2)
    }
    // A faster release gets there sooner, and both settle within 70 to 300 ms.
    expect(flick.curve[2]?.[1]).toBeGreaterThan(slow.curve[2]?.[1] ?? 1)
    expect(slow.ms).toBeGreaterThan(70)
    expect(slow.ms).toBeLessThan(300)
    // The release velocity is kept for the spring.
    const r = replay(trace(260, 1000))
    expect(r.state.velocity).toBeGreaterThan(0.2)
    // Junk distance or velocity never throws.
    expect(springCurve(Number.NaN, 0).ms).toBe(1)
    expect(springCurve(100, Number.NaN).curve.at(-1)?.[1]).toBe(1)
  })

  it('a phased drag whose lift never comes settles after SWIPE.lostLiftMs, never before', () => {
    const w = (state: SwipeState, dx: number, t: number, phased: boolean) =>
      swipeReducer(state, { kind: 'wheel', dx, dy: 0, ctrlKey: false, shiftKey: false, width: WIDTH, hasPrev: true, hasNext: true, phased }, t)
    let s = initialSwipe()
    for (let i = 0; i < 12; i++) s = w(s, 12, i * 8, true)
    expect(s.phase).toBe('dragging')
    const last = 11 * 8
    // Fingers resting: still dragging just before the fallback.
    expect(swipeReducer(s, { kind: 'tick' }, last + SWIPE.lostLiftMs - 1).effect).toBeNull()
    const lost = swipeReducer(s, { kind: 'tick' }, last + SWIPE.lostLiftMs)
    expect(lost.effect).toMatchObject({ kind: 'commit', step: 1 })
    // The window went unphased (a mouse after the trackpad): the drag ends on the plain idle.
    let m = initialSwipe()
    for (let i = 0; i < 12; i++) m = w(m, 12, i * 8, false)
    expect(swipeReducer(m, { kind: 'tick' }, last + SWIPE.idleMs).effect).toMatchObject({ kind: 'commit', step: 1 })
    expect(SWIPE.lostLiftMs).toBeGreaterThan(400)
  })

  it('a fast release near the end never runs past the target', () => {
    for (const [distance, velocity] of [[24, 3], [20, 4], [5, 8], [60, 2.5], [1, 8], [110, 1.5]] as const) {
      const { curve } = springCurve(distance, velocity)
      for (const [, p] of curve) {
        expect(p).toBeGreaterThanOrEqual(0)
        expect(p).toBeLessThanOrEqual(1)
      }
      for (let i = 1; i < curve.length; i++) expect(curve[i]?.[1]).toBeGreaterThanOrEqual(curve[i - 1]?.[1] ?? 0)
    }
  })
})
