import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { OverlaySurfaceState } from '../state'
import {
  childrenOf,
  createDwell,
  createPeekHover,
  PEEK_DWELL_MS,
  PEEK_GRACE_MS,
  PEEK_SLIDE_IN_MS,
  PEEK_SLIDE_OUT_EASE,
  PEEK_SLIDE_OUT_MS,
  PEEK_SLIDE_OUT_STOPS,
  PEEK_ZONE_X,
  peekMotion,
  peekRect,
  reachesPeekZone
} from './peek'

beforeEach(() => vi.useFakeTimers())
afterEach(() => vi.useRealTimers())

const surface = (instanceId: string, openedFrom: string | null, layer: OverlaySurfaceState['layer'] = 'passive'): OverlaySurfaceState => ({
  instanceId,
  surface: 'x',
  layer,
  modality: layer === 'passive' ? 'passive' : 'light-dismiss',
  rect: null,
  openedFrom,
  focus: false,
  slot: null,
  props: {}
})

describe('peekRect', () => {
  it('is the sidebar rect plus the 16 px shadow bleed from the window edge', () => {
    expect(peekRect({ sidebarRect: { x: 0, y: 0, width: 260, height: 800 } })).toEqual({ x: 0, y: 0, width: 276, height: 800 })
    expect(peekRect({ sidebarRect: { x: 0, y: 0, width: Number.NaN, height: -5 } })).toEqual({ x: 0, y: 0, width: 0, height: 0 })
  })
})

describe('dwell', () => {
  it('fires once after the pointer stays the dwell', () => {
    const fire = vi.fn()
    const d = createDwell(fire)
    d.enter()
    vi.advanceTimersByTime(PEEK_DWELL_MS - 1)
    expect(fire).not.toHaveBeenCalled()
    d.enter() // pointer moves inside: the running dwell keeps its start
    vi.advanceTimersByTime(1)
    expect(fire).toHaveBeenCalledTimes(1)
    d.enter()
    vi.advanceTimersByTime(PEEK_DWELL_MS * 2)
    expect(fire).toHaveBeenCalledTimes(1)
  })

  it('cancels if the pointer leaves before 260 ms, and starts over on the next entry', () => {
    const fire = vi.fn()
    const d = createDwell(fire)
    d.enter()
    vi.advanceTimersByTime(PEEK_DWELL_MS - 1)
    d.leave()
    vi.advanceTimersByTime(500)
    expect(fire).not.toHaveBeenCalled()
    d.enter()
    vi.advanceTimersByTime(PEEK_DWELL_MS)
    expect(fire).toHaveBeenCalledTimes(1)
  })

  it('dispose stops a running dwell', () => {
    const fire = vi.fn()
    const d = createDwell(fire)
    d.enter()
    d.dispose()
    vi.advanceTimersByTime(PEEK_DWELL_MS)
    expect(fire).not.toHaveBeenCalled()
  })
})

describe('peek leave grace', () => {
  it('expires 165 ms after the pointer leaves', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS - 1)
    expect(expire).not.toHaveBeenCalled()
    vi.advanceTimersByTime(1)
    expect(expire).toHaveBeenCalledTimes(1)
  })

  it('cancels when the pointer comes back, and restarts in full on the next leave', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS - 15)
    h.enter()
    vi.advanceTimersByTime(1000)
    expect(expire).not.toHaveBeenCalled()
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS - 1)
    expect(expire).not.toHaveBeenCalled()
    vi.advanceTimersByTime(1)
    expect(expire).toHaveBeenCalledTimes(1)
  })

  it('starts hovered, since it opens under the pointer at the window edge', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    expect(h.hovered).toBe(true)
    vi.advanceTimersByTime(10_000)
    expect(expire).not.toHaveBeenCalled()
  })

  it('a child surface holds it open past the grace; it closes once the child closes with the pointer out', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.setChildren(1, false) // hover card opened from a row
    h.leave()
    vi.advanceTimersByTime(5000)
    expect(expire).not.toHaveBeenCalled()
    h.setChildren(0, false)
    vi.advanceTimersByTime(PEEK_GRACE_MS)
    expect(expire).toHaveBeenCalledTimes(1)
  })

  it('a child closing while the pointer is still over the peek keeps it open until the pointer leaves', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.setChildren(1, false)
    h.setChildren(0, false)
    vi.advanceTimersByTime(5000)
    expect(expire).not.toHaveBeenCalled()
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS)
    expect(expire).toHaveBeenCalledTimes(1)
  })

  it('a grace already running when a child opens stops; a covering child (menu) closing counts as the pointer leaving', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS - 15)
    h.setChildren(1, false)
    vi.advanceTimersByTime(1000)
    expect(expire).not.toHaveBeenCalled()
    h.enter() // pointer back on the peek while the menu is open
    h.setChildren(0, true)
    expect(h.hovered).toBe(false)
    vi.advanceTimersByTime(100)
    h.enter() // moving over the peek again keeps it
    vi.advanceTimersByTime(1000)
    expect(expire).not.toHaveBeenCalled()
  })
})

describe('peek held by the Library or the pop-up', () => {
  it('stays open while held, whatever the pointer does, and a release leaves the pointer to main', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.setHeld(true) // the Library opened from the peek's Library button
    h.leave()
    vi.advanceTimersByTime(10_000)
    expect(expire).not.toHaveBeenCalled()
    // Back: a leave seen while held may be stale, and the pointer may be on the panel. Main asks where it is and slides
    // the peek out through `leaving` when it is off; the overlay starts no grace of its own.
    h.setHeld(false)
    vi.advanceTimersByTime(10_000)
    expect(expire).not.toHaveBeenCalled()
    h.enter()
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS - 1)
    expect(expire).not.toHaveBeenCalled()
    vi.advanceTimersByTime(1)
    expect(expire).toHaveBeenCalledTimes(1)
  })

  it('released with the pointer still over the peek, it stays until the pointer leaves', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.setHeld(true) // a pop-up row's Rename
    h.setHeld(false)
    vi.advanceTimersByTime(5000)
    expect(expire).not.toHaveBeenCalled()
    h.enter()
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS)
    expect(expire).toHaveBeenCalledTimes(1)
  })

  it('Back or Escape with the pointer inside: the leave the narrowing panel sends starts no grace; main decides', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.enter() // the pointer on the Library's Back arrow, or resting on it for Escape
    h.setHeld(true)
    h.setHeld(false) // Back or Escape closed the Library
    h.leave() // the panel narrowed or gave the keyboard back under the pointer
    h.setChildren(0, false)
    vi.advanceTimersByTime(10_000)
    expect(expire).not.toHaveBeenCalled()
    expect(h.hovered).toBe(false)
    // The pointer moves on the narrowed panel, then really leaves: the usual grace.
    h.enter()
    vi.advanceTimersByTime(10_000)
    expect(expire).not.toHaveBeenCalled()
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS - 1)
    expect(expire).not.toHaveBeenCalled()
    vi.advanceTimersByTime(1)
    expect(expire).toHaveBeenCalledTimes(1)
  })

  it('a hold stops a running grace, and a child closing under a hold does not release it', () => {
    const expire = vi.fn()
    const h = createPeekHover(expire)
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS - 15)
    h.setHeld(true) // the pop-up's row menu opened as the pointer slid off
    h.setChildren(1, false)
    h.setChildren(0, true)
    vi.advanceTimersByTime(5000)
    expect(expire).not.toHaveBeenCalled()
    h.setHeld(false)
    vi.advanceTimersByTime(5000)
    expect(expire).not.toHaveBeenCalled()
    h.enter()
    h.leave()
    vi.advanceTimersByTime(PEEK_GRACE_MS)
    expect(expire).toHaveBeenCalledTimes(1)
  })
})

describe('childrenOf', () => {
  it('lists surfaces opened from the peek, through nested children, and nothing else', () => {
    const list = [surface('peek', null), surface('menu', 'peek', 'top'), surface('sub', 'menu', 'top'), surface('bar', null, 'top'), surface('card', 'peek')]
    expect(childrenOf(list, 'peek').map((s) => s.instanceId).sort()).toEqual(['card', 'menu', 'sub'])
    expect(childrenOf(list, 'gone')).toEqual([])
  })
})

// The zone is x 0 to 5 (6 pt, the layout's hot zone), a pointer flung left across it between two samples counts,
// and a held button never reveals.
describe('reachesPeekZone', () => {
  const zone = { x: 0, y: 0, width: PEEK_ZONE_X + 1, height: 800 }
  const at = (x: number, t: number, y = 400) => ({ x, y, t })

  it('starts the slide within about 60 ms of reaching the edge, on the design retract timing', () => {
    expect(PEEK_DWELL_MS).toBeLessThanOrEqual(45)
    expect(PEEK_DWELL_MS + 1000 / 60).toBeLessThanOrEqual(60)
    expect([PEEK_SLIDE_IN_MS, PEEK_GRACE_MS, PEEK_SLIDE_OUT_MS]).toEqual([250, 165, 90])
  })

  it('counts x 0 to 5 and left of the window, never 6 and up at rest, and only at the zone height', () => {
    for (const x of [0, 2.5, 5, 5.9, -3]) expect(reachesPeekZone(zone, at(x, 0), null, 0)).toBe(true)
    for (const x of [6, 7, 9]) expect(reachesPeekZone(zone, at(x, 0), null, 0)).toBe(false)
    expect(reachesPeekZone(zone, at(2, 0, 800), null, 0)).toBe(false)
    expect(reachesPeekZone(zone, at(2, 0, -1), null, 0)).toBe(false)
  })

  it('catches a fast pointer moving left that would cross the zone before its next sample', () => {
    // 60 px in one 16 ms frame: at x 9 now, in the zone well before the next sample.
    expect(reachesPeekZone(zone, at(9, 16), at(69, 0), 0)).toBe(true)
    // 120 Hz samples 8 ms apart, 2 px per sample, from x 9: projected 9 - 4.2 = 4.8.
    expect(reachesPeekZone(zone, at(9, 8), at(11, 0), 0)).toBe(true)
  })

  it('does not fling a slow drift, a move to the right, stale samples or junk', () => {
    expect(reachesPeekZone(zone, at(8, 16), at(9, 0), 0)).toBe(false)
    expect(reachesPeekZone(zone, at(9, 16), at(3, 0), 0)).toBe(false)
    expect(reachesPeekZone(zone, at(9, 200), at(200, 0), 0)).toBe(false)
    // Two samples a millisecond apart: the speed is taken over at least 8 ms.
    expect(reachesPeekZone(zone, at(9, 1), at(10, 0), 0)).toBe(false)
    expect(reachesPeekZone(zone, at(9, 0), at(60, 0), 0)).toBe(false)
    expect(reachesPeekZone(zone, at(Number.NaN, 16), at(60, 0), 0)).toBe(false)
    expect(reachesPeekZone(zone, at(9, 16), at(Number.NaN, 0), 0)).toBe(false)
    expect(reachesPeekZone({ ...zone, width: 0 }, at(0, 0), null, 0)).toBe(false)
  })

  it('never reveals with a button held: a tab, a text selection or a window resize dragged to the edge', () => {
    expect(reachesPeekZone(zone, at(2, 0), null, 1)).toBe(false)
    expect(reachesPeekZone(zone, at(9, 16), at(69, 0), 1)).toBe(false)
    expect(reachesPeekZone(zone, at(-2, 0), null, 2)).toBe(false)
  })
})

describe('peekMotion', () => {
  it('starts off the edge, slides in once drawn, slides out on leaving and back in from there', () => {
    expect(peekMotion(false, false)).toBe('enter')
    expect(peekMotion(true, false)).toBe('shown')
    expect(peekMotion(true, true)).toBe('leaving')
    // Left before its first frame: it leaves from the edge.
    expect(peekMotion(false, true)).toBe('leaving')
  })
})

describe('peek slide-out curve', () => {
  it('eases in over 90 ms: every frame steps further than the one before, ending at the edge', () => {
    expect(PEEK_SLIDE_OUT_MS).toBe(90)
    expect(PEEK_SLIDE_OUT_STOPS[0]).toBe(0)
    expect(PEEK_SLIDE_OUT_STOPS.at(-1)).toBe(1)
    const stops: readonly number[] = PEEK_SLIDE_OUT_STOPS
    const steps = stops.slice(1).map((p, i) => p - (stops[i] ?? 0))
    for (let i = 1; i < steps.length; i++) expect(steps[i] ?? 0).toBeGreaterThan(steps[i - 1] ?? 0)
    expect(PEEK_SLIDE_OUT_EASE).toBe('linear(0, 0.06 20%, 0.2 40%, 0.4 60%, 0.66 80%, 1)')
  })
})
