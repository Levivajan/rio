import { describe, expect, it } from 'vitest'
import type { IndexRow } from '../../../shared/ipc/command'
import { applyIndexDelta, emptyHistoryCache } from './historyCache'

const row = (url: string, title = url): IndexRow => ({ key: url, kind: 'history', url, title, visitCount: 1, typedCount: 0, lastVisitAt: 1 })

describe('overlay history cache', () => {
  it('replaces on full, applies deltas and reuses the built index when nothing changed', () => {
    const cache = emptyHistoryCache()
    const a = applyIndexDelta(cache, { rev: 1, full: true, upserts: [row('https://a.test/'), row('https://b.test/')], deletes: [] })
    expect(a.index.size).toBe(2)
    const b = applyIndexDelta(cache, { rev: 3, full: false, upserts: [row('https://c.test/')], deletes: ['https://a.test/'] })
    expect([...cache.rows.keys()].sort()).toEqual(['https://b.test/', 'https://c.test/'])
    expect(b.index.size).toBe(2)
    expect(cache.rev).toBe(3)
    expect(applyIndexDelta(cache, { rev: 3, full: false, upserts: [], deletes: [] })).toBe(b)
    applyIndexDelta(cache, { rev: 4, full: true, upserts: [row('https://z.test/')], deletes: [] })
    expect([...cache.rows.keys()]).toEqual(['https://z.test/'])
  })
})
