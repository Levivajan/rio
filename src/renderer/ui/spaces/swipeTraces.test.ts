// Replays of recorded swipe input: each line of a trace is
// `t phase momentumPhase dx sum` in ms and px, with the CGEvent phases (1 began, 2 changed, 4 ended; momentum 1 began,
// 2 continued, 3 ended) and dx in finger terms (negative toward the next space). The browser reports the lift as
// the native phase ended arrives, so the lift reaches the reducer by the next 60 Hz frame. The test
// draws what the hooks draw after the release: the space settle's spring, or the Library's exit and the sidebar that
// takes over from it, and checks the frame-by-frame motion.
import { describe, expect, it } from 'vitest'
import { curveAt, exitCurve, exitTravel, libraryExit, settleStart, swipeReducer, initialSwipe, type SwipeEffect, type SwipeState } from './swipeReducer'
import slowTrace from './fixtures/verify-swipe2/c1-slow.in?raw'
import slowLongTrace from './fixtures/verify-swipe2/c1-slow-long.in?raw'
import flickTrace from './fixtures/verify-swipe2/c2-flick.in?raw'
import outTrace from './fixtures/verify-swipe2/c3-out.in?raw'
import flickOutTrace from './fixtures/verify-swipe2/c4-flick-out.in?raw'
// A second trace set: the same gestures again, plus a second slow swipe and a second slow exit.
import slow3Trace from './fixtures/verify-swipe3/c1-slow.in?raw'
import slow3bTrace from './fixtures/verify-swipe3/c1-slow-b.in?raw'
import slowLong3Trace from './fixtures/verify-swipe3/c1-slow-long.in?raw'
import flick3Trace from './fixtures/verify-swipe3/c2-flick.in?raw'
import out3Trace from './fixtures/verify-swipe3/c3-out.in?raw'
import out3bTrace from './fixtures/verify-swipe3/c3-out-b.in?raw'
import flickOut3Trace from './fixtures/verify-swipe3/c4-flick-out.in?raw'

const FRAME = 1000 / 60
/** The space panels' width at the default sidebar (214 less two 8 px insets). */
const PANEL = 198
/** The widened Library sidebar. */
const LIBRARY = 400

interface Event { t: number; phase: number; momentum: number; dx: number }

function parse(text: string): Event[] {
  const out: Event[] = []
  for (const line of text.split('\n')) {
    const [t, phase, momentum, dx] = line.trim().split(/\s+/).map(Number)
    if (line.startsWith('T0') || ![t, phase, momentum, dx].every(Number.isFinite)) continue
    out.push({ t: t as number, phase: phase as number, momentum: momentum as number, dx: dx as number })
  }
  return out
}

interface Replay { lift: number; release: number; effect: SwipeEffect; state: SwipeState; frames: { t: number; travel: number }[] }

/**
 * The hooks' loop: wheel events reach the frame with their own timestamps before each frame's callbacks; the lift (the
 * native phase ended, or momentum began when that comes first) is handled by the first frame at or after it.
 */
function replay(text: string, width: number, edges: { hasPrev: boolean; hasNext: boolean }): Replay {
  const events = parse(text)
  const lift = events.find((e) => e.phase === 4 || e.phase === 8 || e.momentum === 1)?.t ?? Number.NaN
  let state = initialSwipe()
  let travel = 0
  let release: { t: number; effect: SwipeEffect } | null = null
  let lifted = false
  const frames: Replay['frames'] = []
  let fed = 0
  for (let f = -FRAME; f <= (events.at(-1)?.t ?? 0) + FRAME && !release; f += FRAME) {
    for (let e = events[fed]; e && e.t <= f; e = events[fed]) {
      fed++
      if (e.phase === 4 || e.phase === 8 || e.dx === 0) continue
      const input = { kind: 'wheel' as const, dx: -e.dx, dy: 0, ctrlKey: false, shiftKey: false, width, momentum: e.momentum !== 0, phased: true, ...edges }
      state = swipeReducer(state, input, e.t)
      if (state.effect?.kind === 'begin' || state.effect?.kind === 'update') travel = state.effect.travel
      else if (state.effect) release = { t: f, effect: state.effect }
    }
    if (!lifted && f >= lift && !release) {
      lifted = true
      state = swipeReducer(state, { kind: 'lift' }, f)
      if (state.effect) release = { t: f, effect: state.effect }
    }
    if (!release) frames.push({ t: f, travel })
  }
  if (!release) throw new Error('the swipe never released')
  return { lift, release: release.t, effect: release.effect, state, frames }
}

/** Movement per frame (positive toward the target) of a run of positions. */
const moves = (xs: number[]) => xs.slice(1).map((x, i) => x - (xs[i] as number))

describe('recorded swipe traces', () => {
  const spaceTraces = [
    ['slow 140 px', slowTrace],
    ['slow 260 px', slowLongTrace],
    ['flick', flickTrace],
    ['second recording, slow 140 px', slow3Trace],
    ['second recording, slow 140 px, repeat', slow3bTrace],
    ['second recording, slow 260 px', slowLong3Trace],
    ['second recording, flick', flick3Trace]
  ] as const
  for (const [name, text] of spaceTraces) {
    it(`${name}: the settle moves in the frame after the lift, at the release speed, with no hold or step`, () => {
      const r = replay(text, PANEL, { hasPrev: true, hasNext: true })
      expect(r.release - r.lift).toBeGreaterThanOrEqual(0)
      expect(r.release - r.lift).toBeLessThanOrEqual(FRAME)
      expect(r.effect).toMatchObject({ kind: 'commit', step: 1 })
      // What SpaceSlider draws from the commit at the release frame: the settle curve, started one frame in (settleStart)
      // because an animation paints its start time first. The release frame is the first frame after the lift.
      const from = Math.min(PANEL, Math.abs(r.effect.travel))
      const { ms, curve } = exitCurve(PANEL - from, r.state.velocity)
      const at = r.release
      const settle = Array.from({ length: Math.ceil(ms / FRAME) + 2 }, (_, k) => from + (PANEL - from) * curveAt(curve, settleStart(at, at + k * FRAME, ms)))
      const lastDrag = Math.min(PANEL, Math.abs(r.frames.at(-1)?.travel ?? 0))
      const m = moves([lastDrag, ...settle])
      if (from < PANEL) {
        // The first settle frame moves from where the release left the strip, at about the fingers' speed.
        expect((settle[0] as number) - from).toBeGreaterThan(0.5)
        expect((settle[0] as number) - from).toBeLessThanOrEqual(Math.max(1, Math.abs(r.state.velocity) * FRAME) * 1.5)
        // No held frame while there is more than a pixel to go.
        for (let k = 0; k < settle.length && PANEL - (settle[k] as number) > 1; k++) expect(m[k + 1]).toBeGreaterThan(0)
      }
      // Never back, and never a frame over 1.5 times the one before it (an earlier build stepped 11.7 to 19 px).
      for (const step of m) expect(step).toBeGreaterThanOrEqual(0)
      for (let i = 2; i < m.length; i++) if ((m[i] as number) > 0.5) expect(m[i]).toBeLessThanOrEqual(1.5 * (m[i - 1] as number))
      expect(settle.at(-1)).toBeCloseTo(PANEL)
      // Main's switch picks the settle up two frames in: the new animation lands where the old one was going to be.
      expect(curveAt(curve, settleStart(at, at + 2 * FRAME, ms))).toBeCloseTo(curveAt(curve, 3 * FRAME), 6)
    })
  }

  const exitTraces = [
    ['slow swipe', outTrace, 1],
    ['flick', flickOutTrace, 1],
    ['slow swipe, late close', outTrace, 4],
    ['second recording, slow swipe', out3Trace, 1],
    ['second recording, slow swipe, repeat', out3bTrace, 1],
    ['second recording, flick', flickOut3Trace, 1],
    ['second recording, flick, late close', flickOut3Trace, 3]
  ] as const
  for (const [name, text, swapAfter] of exitTraces) {
    it(`out of the Library by a ${name}: the Library and the space list move as one slide from the fingers`, () => {
      const r = replay(text, LIBRARY, { hasPrev: false, hasNext: true })
      expect(r.release - r.lift).toBeLessThanOrEqual(FRAME)
      expect(r.effect).toMatchObject({ kind: 'commit', step: 1 })
      // The Library's left edge, every frame: where the fingers put it during the drag...
      const drag = r.frames.map((f) => -f.travel)
      const exit = libraryExit(r.effect.travel, LIBRARY, r.state.velocity, r.release)
      const after: number[] = []
      // Frame 0 is the release frame, the first after the lift. Both animations start one frame in (settleStart).
      for (let k = 0; r.release + k * FRAME <= r.release + exit.ms; k++) {
        const t = r.release + k * FRAME
        const p = curveAt(exit.curve, settleStart(exit.at, t, exit.ms))
        // The Library's own exit slide (useLibrarySwipe)...
        const library = -(exit.from + (exit.width - exit.from) * p)
        // ...and, once it closes, the sidebar's slide in (useLibraryExit) with the same Library drawn left of it: the
        // sidebar's left edge less the Library's width.
        const strip = (exit.width - exit.from) * (1 - p) - exit.width
        // One surface: the frame the Library closes, the space list sits exactly at the Library's right edge.
        expect(Math.abs(strip - library)).toBeLessThan(0.01)
        after.push(k < swapAfter ? library : strip)
      }
      // It starts from exactly where the fingers left it (the release frame draws the last deltas too) and ends with
      // the space list at rest.
      expect(-r.effect.travel).toBeLessThanOrEqual(drag.at(-1) as number)
      expect(exitTravel(exit, r.release)).toBe(r.effect.travel)
      expect(after.at(-1)).toBeCloseTo(-LIBRARY)
      const m = moves([-r.effect.travel, ...after]).map((x) => -x)
      // The first frame after the lift moves (an earlier build held at -260), at most at the fingers' speed, and never a step
      // over 1.5 times the frame before it.
      expect(m[0]).toBeGreaterThan(0.5)
      expect(m[0]).toBeLessThanOrEqual(Math.max(1, r.state.velocity * FRAME) * 1.5)
      for (let i = 1; i < m.length; i++) {
        expect(m[i]).toBeGreaterThanOrEqual(0)
        if ((m[i] as number) > 0.5) expect(m[i]).toBeLessThanOrEqual(1.5 * (m[i - 1] as number))
      }
    })
  }
})
