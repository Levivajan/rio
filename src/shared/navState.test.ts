import { describe, expect, it } from 'vitest'
import { capNavState, NAV_MAX_BYTES, NAV_MAX_ENTRIES, parseNavState } from './navState'

const entry = (i: number) => ({ url: `https://a.test/${i}`, title: `t${i}` })

describe('capNavState', () => {
  it('caps at 50 entries around the current one', () => {
    const entries = Array.from({ length: 120 }, (_, i) => entry(i))
    const s = capNavState({ index: 100, entries })
    expect(s?.entries).toHaveLength(NAV_MAX_ENTRIES)
    expect(s?.entries[s.index]?.url).toBe('https://a.test/100')
  })
  it('caps at 256 KB, dropping page state first', () => {
    const big = 'x'.repeat(20 * 1024)
    const entries = Array.from({ length: 30 }, (_, i) => ({ ...entry(i), pageState: big }))
    const s = capNavState({ index: 29, entries })
    expect(s).not.toBeNull()
    expect(new TextEncoder().encode(JSON.stringify(s)).length).toBeLessThanOrEqual(NAV_MAX_BYTES)
    expect(s?.entries[s.index]?.url).toBe('https://a.test/29')
  })
  it('drops disallowed schemes and keeps the index on the right entry', () => {
    const s = capNavState({ index: 2, entries: [entry(0), { url: 'file:///etc/hosts', title: '' }, entry(2), { url: 'javascript:1', title: '' }] })
    expect(s?.entries.map((e) => e.url)).toEqual(['https://a.test/0', 'https://a.test/2'])
    expect(s?.index).toBe(1)
  })
  it('returns null for junk', () => {
    expect(capNavState(null)).toBeNull()
    expect(capNavState({ entries: 'x' })).toBeNull()
    expect(capNavState({ index: 0, entries: [{ url: 'data:x' }] })).toBeNull()
    expect(parseNavState('{not json')).toBeNull()
  })
})
