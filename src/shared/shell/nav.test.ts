import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createLongPress, letterOf, NAV_HISTORY_MAX, NAV_LONG_PRESS_MS, NAV_TITLE_MAX, sanitizeHistory } from './nav'

describe('sanitizeHistory', () => {
  it('caps titles at 120 characters', () => {
    const rows = sanitizeHistory([{ index: 0, title: 'T'.repeat(10_000), url: 'https://example.com/', favicon: null }])
    expect(rows).toHaveLength(1)
    expect(rows[0]?.title).toHaveLength(NAV_TITLE_MAX)
  })

  it('drops disallowed schemes and junk rows', () => {
    const rows = sanitizeHistory([
      { index: 0, title: 'Script', url: 'javascript:alert(1)', favicon: null },
      { index: 1, title: 'File', url: 'file:///etc/hosts', favicon: null },
      { index: 2, title: 'Data', url: 'data:text/html,hi', favicon: null },
      null,
      'row',
      { index: -1, title: 'Neg', url: 'https://a.example/' },
      { index: 1.5, title: 'Frac', url: 'https://a.example/' },
      { index: 3, title: 42, url: 'https://b.example/path' },
      { index: 3, title: 'Duplicate', url: 'https://c.example/' },
      { index: 4, title: '  A\n\tpage ', url: 'http://d.example/', favicon: 'javascript:x' },
      { index: 5, title: 'Settings', url: 'rio://settings/', favicon: 'data:image/png;base64,AAAA' }
    ])
    expect(rows).toEqual([
      { index: 3, title: 'b.example', url: 'https://b.example/path', favicon: null },
      { index: 4, title: 'A page', url: 'http://d.example/', favicon: null },
      { index: 5, title: 'Settings', url: 'rio://settings/', favicon: 'data:image/png;base64,AAAA' }
    ])
  })

  it('keeps at most 15 rows and reads a non-array as empty', () => {
    const many = Array.from({ length: 40 }, (_, i) => ({ index: i, title: `P${i}`, url: `https://e.example/${i}` }))
    expect(sanitizeHistory(many)).toHaveLength(NAV_HISTORY_MAX)
    expect(sanitizeHistory(undefined)).toEqual([])
    expect(sanitizeHistory({ index: 0 })).toEqual([])
  })
})

describe('letterOf', () => {
  it('uses the first character, uppercased', () => {
    expect(letterOf(' alpha')).toBe('A')
    expect(letterOf('')).toBe('?')
    expect(letterOf('😀 smile')).toBe('😀')
  })
})

describe('createLongPress', () => {
  beforeEach(() => vi.useFakeTimers())
  afterEach(() => vi.useRealTimers())

  it('fires after 450 ms and swallows the click that follows', () => {
    const onLong = vi.fn()
    const press = createLongPress(onLong)
    press.down()
    vi.advanceTimersByTime(NAV_LONG_PRESS_MS - 1)
    expect(onLong).not.toHaveBeenCalled()
    vi.advanceTimersByTime(1)
    expect(onLong).toHaveBeenCalledTimes(1)
    press.up()
    expect(press.consume()).toBe(true)
    expect(press.consume()).toBe(false)
  })

  it('does not fire when released early, and a short press keeps its click', () => {
    const onLong = vi.fn()
    const press = createLongPress(onLong)
    press.down()
    vi.advanceTimersByTime(300)
    press.up()
    vi.advanceTimersByTime(1000)
    expect(onLong).not.toHaveBeenCalled()
    expect(press.consume()).toBe(false)
  })

  it('a new press resets the fired flag; dispose cancels', () => {
    const onLong = vi.fn()
    const press = createLongPress(onLong)
    press.down()
    vi.advanceTimersByTime(NAV_LONG_PRESS_MS)
    press.down()
    expect(press.consume()).toBe(false)
    press.dispose()
    vi.advanceTimersByTime(NAV_LONG_PRESS_MS)
    expect(onLong).toHaveBeenCalledTimes(1)
  })
})
