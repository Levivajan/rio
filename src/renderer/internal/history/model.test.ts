// History page logic (data-t3): day groups, Shift+click ranges, selection pruning, delete chunks.
import { describe, expect, it } from 'vitest'
import type { HistoryRow } from '../../../shared/ipc/data'
import { chunks, clickSelect, dayLabel, emptySelection, groupByDay, hostOf, pruneSelection } from './model'

const at = (y: number, m: number, d: number, h = 12) => new Date(y, m - 1, d, h).getTime()
const NOW = at(2026, 11, 7, 9)
const row = (id: number, visitedAt: number, url = `https://a.test/${id}`): HistoryRow => ({ id, urlId: id, url, title: `T${id}`, visitedAt, transition: 'link', favicon: null })

describe('day groups', () => {
  it('labels Today and Yesterday in local time, then the full date', () => {
    expect(dayLabel(at(2026, 11, 7, 0), NOW, 'en-US')).toBe('Today')
    expect(dayLabel(at(2026, 11, 6, 23), NOW, 'en-US')).toBe('Yesterday')
    expect(dayLabel(at(2026, 11, 5), NOW, 'en-US')).toBe('Thursday, November 5, 2026')
    // Across a month boundary.
    expect(dayLabel(at(2026, 10, 31), at(2026, 11, 1), 'en-US')).toBe('Yesterday')
  })

  it('groups consecutive rows of one day, newest first', () => {
    const groups = groupByDay([row(5, at(2026, 11, 7, 8)), row(4, at(2026, 11, 7, 1)), row(3, at(2026, 11, 6)), row(1, at(2026, 11, 1))], NOW, 'en-US')
    expect(groups.map((g) => [g.label, g.rows.map((r) => r.id)])).toEqual([
      ['Today', [5, 4]],
      ['Yesterday', [3]],
      ['Sunday, November 1, 2026', [1]]
    ])
    expect(groupByDay([], NOW)).toEqual([])
  })

  it('hostOf is empty for junk', () => {
    expect(hostOf('https://www.b.test:8080/x')).toBe('www.b.test')
    expect(hostOf('not a url')).toBe('')
  })
})

describe('selection', () => {
  const order = [10, 9, 8, 7, 6]

  it('click toggles one row; Shift+click selects the range from the anchor either way', () => {
    let s = clickSelect(emptySelection, order, 9, false)
    expect([...s.ids]).toEqual([9])
    s = clickSelect(s, order, 7, true)
    expect([...s.ids].sort()).toEqual([7, 8, 9])
    expect(s.anchor).toBe(9)
    s = clickSelect(s, order, 10, true)
    expect([...s.ids].sort()).toEqual([10, 7, 8, 9].sort())
    s = clickSelect(s, order, 8, false)
    expect(s.ids.has(8)).toBe(false)
    expect(s.anchor).toBe(8)
  })

  it('Shift+click without a listed anchor acts as a plain click', () => {
    expect(clickSelect(emptySelection, order, 8, true)).toEqual({ ids: new Set([8]), anchor: 8 })
    expect(clickSelect({ ids: new Set(), anchor: 99 }, order, 8, true)).toEqual({ ids: new Set([8]), anchor: 8 })
  })

  it('pruning drops rows that are gone and keeps the same object when nothing changed', () => {
    const s = { ids: new Set([9, 8]), anchor: 9 }
    expect(pruneSelection(s, order)).toBe(s)
    expect(pruneSelection(s, [10, 8])).toEqual({ ids: new Set([8]), anchor: null })
  })

  it('splits deletes into calls of at most 1,000 ids', () => {
    const ids = Array.from({ length: 2001 }, (_, i) => i)
    expect(chunks(ids).map((c) => c.length)).toEqual([1000, 1000, 1])
    expect(chunks([])).toEqual([])
  })
})
