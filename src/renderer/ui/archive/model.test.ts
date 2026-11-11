// Archive panel model (archive-t3): day groups, labels and page merging.
import { describe, expect, it } from 'vitest'
import type { ArchiveRow } from '../../../shared/archive'
import { dayLabel, groupByDay, hostOf, mergePage, reasonLabel } from './model'

const NOW = new Date(2026, 10, 7, 9, 30).getTime()
const row = (id: string, archivedAt: number, over: Partial<ArchiveRow> = {}): ArchiveRow => ({
  id, profileId: 'p', spaceId: null, title: id, url: 'https://www.a.test/', favicon: null, reason: 'close', archivedAt, ...over
})

describe('archive panel model', () => {
  it('labels days relative to today: Today, 1 day ago, 1 week ago, 1 month ago, 1 year ago', () => {
    expect(dayLabel(new Date(2026, 10, 7, 0, 1).getTime(), NOW)).toBe('Today')
    expect(dayLabel(NOW + 13 * 3600_000, NOW)).toBe('Today')
    expect(dayLabel(new Date(2026, 10, 6, 23, 59).getTime(), NOW)).toBe('1 day ago')
    expect(dayLabel(new Date(2026, 10, 3, 12).getTime(), NOW)).toBe('4 days ago')
    expect(dayLabel(new Date(2026, 9, 31, 12).getTime(), NOW)).toBe('1 week ago')
    expect(dayLabel(new Date(2026, 9, 20, 12).getTime(), NOW)).toBe('2 weeks ago')
    expect(dayLabel(new Date(2026, 8, 30, 12).getTime(), NOW)).toBe('1 month ago')
    expect(dayLabel(new Date(2025, 10, 1, 12).getTime(), NOW)).toBe('1 year ago')
  })

  it('groups newest-first rows by relative day and keeps their order', () => {
    const rows = [
      row('a', NOW - 1000),
      row('b', NOW - 2000),
      row('c', new Date(2026, 10, 6, 8).getTime()),
      row('d', new Date(2026, 9, 30, 8).getTime()),
      row('e', new Date(2026, 9, 28, 7).getTime())
    ]
    const groups = groupByDay(rows, NOW)
    expect(groups.map((g) => [g.label, g.rows.map((r) => r.id)])).toEqual([
      ['Today', ['a', 'b']],
      ['1 day ago', ['c']],
      ['1 week ago', ['d', 'e']]
    ])
    expect(new Set(groups.map((g) => g.key)).size).toBe(3)
  })

  it('shows the host without www, the scheme for a URL without a host, nothing for junk', () => {
    expect(hostOf('https://www.Example.com:8443/x?y')).toBe('example.com')
    expect(hostOf('javascript:alert(1)')).toBe('javascript:')
    expect(hostOf('not a url')).toBe('')
  })

  it('names every reason and falls back for an unknown one', () => {
    expect(['close', 'clear', 'auto', 'delete', 'mini', 'junk'].map(reasonLabel)).toEqual(['Closed', 'Cleared', 'Auto-archived', 'Removed', 'Mini window', 'Archived'])
  })

  it('merges a page without repeating rows already listed', () => {
    const merged = mergePage([row('a', 3), row('b', 2)], [row('b', 2), row('c', 1), row('c', 1)])
    expect(merged.map((r) => r.id)).toEqual(['a', 'b', 'c'])
  })
})
