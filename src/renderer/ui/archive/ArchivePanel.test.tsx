// Archive panel hook and component (archive-t3) over the stubbed window.clu: paging, refetch on archiveRevision,
// search, and Esc.
import { act, fireEvent, render, renderHook, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { ArchiveRow } from '../../../shared/archive'
import { defaultState } from '../../../shared/state'
import { createStateMirror, setStateMirrorForTest } from '../../common/store'
import { stubClu } from '../../../test-utils/stubClu'
import { ArchivePanel } from './ArchivePanel'
import { ARCHIVE_PAGE, useArchivePages } from './useArchivePages'

const P = '00000000-0000-4000-8000-0000000000aa'
const S = '00000000-0000-4000-8000-0000000000bb'

/** A fake archive.list over `total` rows, newest first, with cursor = index of the next row. */
function fakeArchive(total: number) {
  let rows: ArchiveRow[] = Array.from({ length: total }, (_, i) => ({
    id: `row-${i}`, profileId: P, spaceId: null, title: i === 0 ? '100% done' : `Row ${i}`, url: `https://a.test/${i}`, favicon: null, reason: 'auto', archivedAt: Date.now() - i * 1000
  }))
  const lists: { query?: string; cursor?: string }[] = []
  const list = (payload: unknown) => {
    const p = payload as { query?: string; cursor?: string; limit?: number }
    lists.push({ ...(p.query ? { query: p.query } : {}), ...(p.cursor ? { cursor: p.cursor } : {}) })
    const matching = p.query ? rows.filter((r) => r.title.includes(p.query ?? '')) : rows
    const start = p.cursor ? Number(p.cursor) : 0
    const page = matching.slice(start, start + (p.limit ?? 100))
    const end = start + page.length
    return { rows: page, nextCursor: end < matching.length ? String(end) : null }
  }
  return { list, lists, drop: (id: string) => (rows = rows.filter((r) => r.id !== id)) }
}

describe('useArchivePages', () => {
  it('loads 100 rows, pages on loadMore without duplicates, and stops at the end', async () => {
    const fake = fakeArchive(250)
    window.clu = stubClu({ 'archive.list': fake.list }).clu
    const { result } = renderHook(() => useArchivePages(P, '', 0))
    await waitFor(() => expect(result.current.rows).toHaveLength(ARCHIVE_PAGE))
    expect(result.current.hasMore).toBe(true)
    act(() => {
      result.current.loadMore()
      result.current.loadMore()
    })
    await waitFor(() => expect(result.current.rows).toHaveLength(200))
    act(() => result.current.loadMore())
    await waitFor(() => expect(result.current.rows).toHaveLength(250))
    expect(result.current.hasMore).toBe(false)
    expect(new Set(result.current.rows.map((r) => r.id)).size).toBe(250)
    // The double loadMore made one request.
    expect(fake.lists.map((l) => l.cursor ?? '')).toEqual(['', '100', '200'])
  })

  it('refetches as many rows as were loaded when archiveRevision changes, and starts over on a new query', async () => {
    const fake = fakeArchive(250)
    window.clu = stubClu({ 'archive.list': fake.list }).clu
    const { result, rerender } = renderHook(({ q, rev }) => useArchivePages(P, q, rev), { initialProps: { q: '', rev: 0 } })
    await waitFor(() => expect(result.current.rows).toHaveLength(100))
    act(() => result.current.loadMore())
    await waitFor(() => expect(result.current.rows).toHaveLength(200))
    fake.drop('row-5')
    rerender({ q: '', rev: 1 })
    await waitFor(() => expect(result.current.rows.some((r) => r.id === 'row-5')).toBe(false))
    expect(result.current.rows).toHaveLength(200)
    rerender({ q: '100%', rev: 1 })
    await waitFor(() => expect(result.current.rows.map((r) => r.id)).toEqual(['row-0']))
    expect(fake.lists.at(-1)).toEqual({ query: '100%' })
  })

  it('reports a failed list instead of throwing', async () => {
    const { result } = renderHook(() => useArchivePages(P, '', 0))
    await waitFor(() => expect(result.current.loading).toBe(false))
    expect(result.current.failed).toBe(true)
    expect(result.current.rows).toEqual([])
  })
})

describe('ArchivePanel', () => {
  function mount(open: boolean) {
    const fake = fakeArchive(3)
    const stub = stubClu({ 'archive.list': fake.list, 'archive.close': () => null, 'archive.restore': () => null, 'archive.delete': () => null })
    window.clu = stub.clu
    const s = defaultState()
    const windowId = stub.clu.windowId ?? ''
    const state = {
      ...s,
      rev: 1,
      archivePanel: open ? { [windowId]: true as const } : {},
      windows: { [windowId]: { activeSpaceId: S } },
      spaces: { [S]: { profileId: P } }
    } as unknown as typeof s
    const mirror = createStateMirror(async () => state)
    mirror.receive({ kind: 'snapshot', rev: 1, state })
    setStateMirrorForTest(mirror)
    render(<ArchivePanel><div>tab list</div></ArchivePanel>)
    return stub
  }

  it('shows the tab list while closed', () => {
    mount(false)
    expect(screen.getByText('tab list')).toBeInTheDocument()
  })

  it('replaces the tab list with day groups of rows showing title, host and reason; Esc closes', async () => {
    const stub = mount(true)
    expect(screen.queryByText('tab list')).toBeNull()
    const today = await screen.findByRole('region', { name: 'Today' })
    expect(today.querySelectorAll('[data-archive-id]')).toHaveLength(3)
    expect(today).toHaveTextContent('100% done')
    expect(today).toHaveTextContent('a.test')
    expect(today).toHaveTextContent('Auto-archived')
    expect(screen.getByRole('searchbox', { name: 'Search Archive' })).toHaveFocus()
    fireEvent.click(screen.getByRole('button', { name: /100% done/ }))
    fireEvent.keyDown(document.body, { key: 'Escape' })
    await waitFor(() => expect(stub.calls.map((c) => c.name)).toContain('archive.close'))
    expect(stub.calls.find((c) => c.name === 'archive.restore')?.payload).toEqual({ archiveId: 'row-0' })
  })

  it('rows carry no buttons; right-click offers Restore Tab and Delete from Archive, Delete on a row deletes it', async () => {
    const stub = mount(true)
    const today = await screen.findByRole('region', { name: 'Today' })
    const row = today.querySelector('[data-archive-id="row-1"]') as HTMLElement
    expect(row.querySelectorAll('button')).toHaveLength(1)
    fireEvent.contextMenu(row, { clientX: 40, clientY: 60 })
    expect(screen.getAllByRole('menuitem').map((m) => m.textContent)).toEqual(['Restore Tab', 'Delete from Archive'])
    fireEvent.click(screen.getByRole('menuitem', { name: 'Delete from Archive' }))
    expect(stub.calls.find((c) => c.name === 'archive.delete')?.payload).toEqual({ archiveId: 'row-1' })
    expect(screen.queryByRole('menu')).toBeNull()
    fireEvent.keyDown(within(today.querySelector('[data-archive-id="row-2"]') as HTMLElement).getByRole('button'), { key: 'Delete' })
    expect(stub.calls.filter((c) => c.name === 'archive.delete').at(-1)?.payload).toEqual({ archiveId: 'row-2' })
  })
})
