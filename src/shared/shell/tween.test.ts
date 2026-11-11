import { describe, expect, it } from 'vitest'
import { easeAt, easeOutExp, edgeAt, hideEase, LIBRARY_TWEEN, lerpRect, progressAt, sidebarTweenMs, type SidebarAnim } from './tween'

const from = { x: 260, y: 8, width: 1012, height: 784 }
const to = { x: 8, y: 8, width: 1264, height: 784 }
const FRAME_120 = 1000 / 120

/** Every display frame of one tween at `hz`, from the first frame after the start to the first at or past the end. */
function frames(anim: SidebarAnim, hz: number): number[] {
  const out: number[] = []
  for (let now = anim.startAt + 1000 / hz; ; now += 1000 / hz) {
    out.push(progressAt(anim, now))
    if (now >= anim.startAt + anim.ms) return out
  }
}

describe('sidebar tween', () => {
  it('eases out over 180 ms, a new position on every 120 Hz frame, ending exactly at the target', () => {
    const anim: SidebarAnim = { from: from.x, to: to.x, startAt: 1000, ms: 180, fromRect: from, toRect: to, curve: 'cubic' }
    const ps = frames({ ...anim, from: 8, to: 260 }, 120)
    expect(ps).toHaveLength(22)
    for (let i = 1; i < ps.length; i++) expect(ps[i]).toBeGreaterThan(ps[i - 1] as number)
    expect(ps.at(-1)).toBe(1)
    // Two 120 Hz frames inside what used to be one 16 ms tick draw two positions, not the same one twice.
    expect(progressAt(anim, 1016 + FRAME_120)).toBeGreaterThan(progressAt(anim, 1016))
    expect(lerpRect(from, to, progressAt(anim, 1180))).toEqual(to)
    // Ease-out: moving most at the start.
    expect((ps[0] as number) - 0).toBeGreaterThan(1 - (ps.at(-2) as number))
  })

  it('docks the sidebar in 85 ms and hides it in 80', () => {
    expect(sidebarTweenMs(true)).toBe(85)
    expect(sidebarTweenMs(false)).toBe(80)
  })

  it('hides on the design curve: 41% at 25 ms, 61 at 50, 88 at 63, done at 80, the dock eases out', () => {
    expect(hideEase(25 / 80)).toBeCloseTo(0.41, 5)
    expect(hideEase(50 / 80)).toBeCloseTo(0.61, 5)
    expect(hideEase(63 / 80)).toBeCloseTo(0.88, 5)
    expect(hideEase(1)).toBe(1)
    expect(hideEase(0)).toBe(0)
    expect(hideEase(Number.NaN)).toBe(0)
    const ms = sidebarTweenMs(false)
    // A hide (edge moving left) takes the curve, a dock the ease-out.
    const hide: SidebarAnim = { from: 214, to: 10, startAt: 0, ms, fromRect: from, toRect: to }
    // The C++ twin (clu_layout_unittest.cc HidingFollowsTheSampledCurve) pins the same samples.
    expect([16, 32, 48, 64, 80].map((now) => progressAt(hide, now))).toEqual(
      [0.2624, 0.466, 0.594, 0.88706, 1].map((p) => expect.closeTo(p, 3))
    )
    expect(edgeAt(hide, 48)).toBeCloseTo(214 - 204 * 0.594, 3)
    const dock: SidebarAnim = { ...hide, from: 10, to: 214, ms: sidebarTweenMs(true) }
    expect(edgeAt(dock, 48)).toBeCloseTo(10 + 204 * easeAt(48 / dock.ms, dock.ms), 5)
    const ph = frames(hide, 120)
    for (let i = 1; i < ph.length; i++) expect(ph[i]).toBeGreaterThan(ph[i - 1] as number)
  })

  it('reads time, never frames: 60 and 120 Hz draw the same edge at the same moment, and a late frame skips ahead', () => {
    const anim: SidebarAnim = { from: from.x, to: to.x, startAt: 50_000, ms: 180, fromRect: from, toRect: to }
    for (const ms of [25, 50, 100, 150]) expect(edgeAt(anim, anim.startAt + ms)).toBe(from.x + (to.x - from.x) * easeAt(ms / 180, 180, 'hide'))
    const at60 = frames(anim, 60)
    const at120 = frames(anim, 120)
    expect(at120).toHaveLength(22)
    expect(at60).toHaveLength(11)
    at60.forEach((p, i) => expect(at120[2 * i + 1]).toBeCloseTo(p, 9))
    // A frame that comes 40 ms late draws where the tween is at that moment, it never lags behind.
    expect(progressAt(anim, anim.startAt + 100)).toBe(easeAt(100 / 180, 180, 'hide'))
  })

  it('lands at once with reduced motion or a junk clock, and holds the start before it', () => {
    const anim: SidebarAnim = { from: from.x, to: to.x, startAt: 1000, ms: 180, fromRect: from, toRect: to }
    expect(progressAt(anim, 999)).toBe(0)
    expect(progressAt(anim, 1000)).toBe(0)
    expect(progressAt(anim, 99_999)).toBe(1)
    expect(progressAt({ ...anim, ms: 0 }, 0)).toBe(1)
    expect(progressAt({ ...anim, ms: Number.NaN }, 1100)).toBe(1)
    expect(progressAt({ ...anim, startAt: Number.NaN }, 1100)).toBe(1)
    expect(edgeAt(anim, anim.startAt + 0)).toBe(from.x)
  })

  it('the Library tween is exponential over 290 ms: half way by about 30 ms, 90% by about 100, exactly there at the end', () => {
    const at = (ms: number) => easeOutExp(ms / LIBRARY_TWEEN.ms)
    expect(at(29)).toBeGreaterThan(0.48)
    expect(at(29)).toBeLessThan(0.52)
    expect(at(97)).toBeGreaterThan(0.89)
    expect(at(97)).toBeLessThan(0.92)
    expect(at(LIBRARY_TWEEN.ms)).toBe(1)
    const anim: SidebarAnim = { from: 214, to: 417, startAt: 0, ms: LIBRARY_TWEEN.ms, fromRect: from, toRect: to, curve: 'exp' }
    expect(progressAt(anim, LIBRARY_TWEEN.ms)).toBe(1)
    expect(progressAt(anim, 32)).toBeCloseTo(easeOutExp(32 / 290), 9)
    expect(edgeAt(anim, 32)).toBeCloseTo(214 + 203 * easeOutExp(32 / 290), 6)
    // Closing keeps the curve.
    expect(progressAt({ ...anim, from: 417, to: 214 }, 32)).toBeCloseTo(easeOutExp(32 / 290), 9)
  })
})
