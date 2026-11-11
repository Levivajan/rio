import { describe, expect, it } from 'vitest'
import { initialNavSwipe, MAX_TITLE, NAV_SWIPE, navSwipeCommit, navSwipeReducer, sanitizeTitle, type NavSwipeInput, type OverscrollMessage } from './engine'

const URL_A = 'https://a.test/page'

describe('sanitizeTitle', () => {
  it('falls back to the host when empty or not a string', () => {
    for (const raw of ['', '   ', '\u0000\u0007', null, undefined, 42]) expect(sanitizeTitle(raw, URL_A)).toBe('a.test')
    expect(sanitizeTitle('', 'about:blank')).toBe('about:blank')
    expect(sanitizeTitle('', 'not a url')).toBe('not a url')
  })
  it('caps 1 MB at 1024 characters without throwing', () => {
    expect(sanitizeTitle('T'.repeat(1024 * 1024), URL_A)).toHaveLength(MAX_TITLE)
    const emoji = sanitizeTitle('\u{1F600}'.repeat(512 * 1024), URL_A)
    expect(emoji.length).toBeLessThanOrEqual(MAX_TITLE)
    expect(emoji.isWellFormed()).toBe(true)
  })
  it('strips control and format characters and collapses whitespace', () => {
    expect(sanitizeTitle('  a\u0000b\t\n c‮​d  ', URL_A)).toBe('a b c d')
  })
  it('keeps ordinary text', () => {
    expect(sanitizeTitle('Inbox (3) – 日本語', URL_A)).toBe('Inbox (3) – 日本語')
  })
})

describe('navSwipeReducer', () => {
  const WIDTH = 1000
  const run = () => {
    let s = initialNavSwipe()
    let now = 1000
    const messages: OverscrollMessage[] = []
    const apply = (input: NavSwipeInput) => {
      s = navSwipeReducer(s, input, now)
      if (s.message) messages.push(s.message)
    }
    return {
      messages,
      state: () => s,
      /** One wheel event per `every` ms. */
      wheel(dxs: number[], o: { dy?: number; blocked?: boolean; every?: number } = {}) {
        for (const dx of dxs) {
          now += o.every ?? 16
          apply({ kind: 'wheel', dx, dy: o.dy ?? 0, width: WIDTH, blocked: o.blocked ?? false })
        }
      },
      /** Time passes with no events, and the idle timer fires. */
      idle(ms: number = NAV_SWIPE.idleMs) {
        now += ms
        apply({ kind: 'tick' })
      },
      advance(ms: number) {
        now += ms
      }
    }
  }
  const steady = (dx: number, count: number) => Array.from({ length: count }, () => dx)
  const ends = (messages: OverscrollMessage[]) => messages.filter((m) => m.phase === 'end')

  it('commits back past 30% of the page width, once', () => {
    const r = run()
    r.wheel(steady(-4, 80))
    expect(r.messages[0]?.phase).toBe('begin')
    expect(r.messages[0]?.dx).toBeLessThan(0)
    expect(ends(r.messages)).toEqual([])
    r.idle()
    expect(ends(r.messages)).toEqual([{ phase: 'end', dx: -0.32 }])
    expect(navSwipeCommit(ends(r.messages)[0]?.dx ?? 0)).toBe('back')
  })

  it('commits forward past 30% the other way', () => {
    const r = run()
    r.wheel(steady(4, 80))
    r.idle()
    expect(navSwipeCommit(ends(r.messages)[0]?.dx ?? 0)).toBe('forward')
  })

  it('springs back short of 30%', () => {
    const r = run()
    r.wheel(steady(-4, 70))
    r.idle()
    expect(ends(r.messages)).toEqual([{ phase: 'end', dx: 0 }])
    expect(navSwipeCommit(0)).toBeNull()
  })

  it('reports the travel so far as a share of the page width', () => {
    const r = run()
    r.wheel(steady(-10, 10))
    expect(r.messages.at(-1)).toEqual({ phase: 'update', dx: -0.1 })
  })

  it('a fast flick commits short of 30%, a tiny or slow one does not', () => {
    const flick = run()
    flick.wheel(steady(-16, 10))
    flick.idle()
    expect(navSwipeCommit(ends(flick.messages)[0]?.dx ?? 0)).toBe('back')
    const tiny = run()
    tiny.wheel(steady(-16, 4))
    tiny.idle()
    expect(navSwipeCommit(ends(tiny.messages)[0]?.dx ?? 0)).toBeNull()
  })

  it('springs back when the fingers come back fast, however far they went', () => {
    const r = run()
    r.wheel(steady(-4, 120))
    r.wheel(steady(12, 8))
    r.idle()
    expect(ends(r.messages)).toEqual([{ phase: 'end', dx: 0 }])
  })

  it('swallows momentum for 350 ms after a commit', () => {
    const r = run()
    r.wheel(steady(-4, 80))
    r.idle()
    r.messages.length = 0
    r.wheel(steady(-30, 5), { every: 60 })
    expect(r.messages).toEqual([])
    expect(r.state().phase).toBe('cooldown')
  })

  it('swallows momentum for 350 ms after a spring', () => {
    const r = run()
    r.wheel(steady(-4, 20))
    r.idle()
    r.messages.length = 0
    r.wheel([-40], { every: 349 })
    expect(r.messages).toEqual([])
  })

  it('keeps swallowing a tail that outlasts the cooldown, and takes a fresh gesture after a gap', () => {
    const r = run()
    r.wheel(steady(-4, 80))
    r.idle()
    r.messages.length = 0
    r.wheel(steady(-40, 8), { every: 60 })
    expect(r.messages).toEqual([])
    r.advance(NAV_SWIPE.cooldownMs + NAV_SWIPE.idleMs)
    r.wheel(steady(-4, 5))
    expect(r.messages[0]?.phase).toBe('begin')
  })

  it('ends the gesture at the first momentum, so the tail neither adds travel nor delays the commit', () => {
    const r = run()
    r.wheel(steady(-8, 40))
    r.messages.length = 0
    r.wheel([-7.2, -6.5, -5.8, -5.2, -4.7, -4.2, -3.8])
    expect(ends(r.messages)).toHaveLength(1)
    const end = ends(r.messages)[0]?.dx ?? 0
    expect(navSwipeCommit(end)).toBe('back')
    expect(Math.abs(end)).toBeLessThan(0.33)
  })

  it('a vertical-dominant gesture never commits, even with a long sideways drift', () => {
    const r = run()
    r.wheel(steady(-4, 200), { dy: 30 })
    r.idle()
    expect(r.messages).toEqual([])
    expect(r.state().phase).toBe('idle')
  })

  it('does not start while the page can still scroll that way, and the page keeps that gesture to its end', () => {
    const r = run()
    r.wheel(steady(-4, 100), { blocked: true })
    expect(r.state().phase).toBe('ignored')
    // The page reached its edge; the same stream of events stays with it.
    r.wheel(steady(-4, 100), { blocked: false })
    r.idle()
    expect(r.messages).toEqual([])
    r.wheel(steady(-4, 80))
    r.idle()
    expect(navSwipeCommit(ends(r.messages)[0]?.dx ?? 0)).toBe('back')
  })

  it('a page scroller reaching its edge during the axis lock still keeps the gesture', () => {
    const r = run()
    r.wheel([-4], { blocked: true })
    r.wheel(steady(-4, 80))
    r.idle()
    expect(r.messages).toEqual([])
  })

  it('a gesture that comes back past its start stays in its direction and reports 0', () => {
    const r = run()
    r.wheel(steady(-4, 10))
    r.wheel(steady(4, 30))
    expect(r.messages.at(-1)?.dx).toBeCloseTo(0)
    r.idle()
    expect(ends(r.messages)).toEqual([{ phase: 'end', dx: 0 }])
  })

  it('ends the gesture before the next one when the idle timer was late', () => {
    const r = run()
    r.wheel(steady(-4, 80))
    r.advance(1000)
    r.wheel([-4])
    expect(navSwipeCommit(ends(r.messages)[0]?.dx ?? 0)).toBe('back')
  })

  it('sends at most one update per 16 ms however fast the trackpad reports', () => {
    const r = run()
    r.wheel(steady(-1, 240), { every: 4 })
    expect(r.messages.length).toBeLessThanOrEqual(61)
    expect(r.messages.length).toBeGreaterThan(50)
  })

  it('stays calm on junk: NaN and infinite deltas count as nothing, a bad width never starts', () => {
    const r = run()
    r.wheel([NaN, Infinity, -Infinity])
    expect(r.state().sumX).toBe(0)
    for (const width of [0, -5, NaN]) {
      const s = navSwipeReducer(initialNavSwipe(), { kind: 'wheel', dx: -50, dy: 0, width, blocked: false }, 1000)
      expect(s.message).toBeNull()
      expect(s.phase).toBe('ignored')
    }
  })

  it('caps the share at the whole page width', () => {
    const r = run()
    r.wheel(steady(-200, 10))
    expect(r.messages.at(-1)?.dx).toBe(-1)
  })
})
