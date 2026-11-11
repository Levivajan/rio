// History page (data-t3): day groups, debounced search, More from this site, Shift range delete with one confirm,
// paging after a history change, empty states.
import { act, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { HistoryRow } from '../../../shared/ipc/data'
import { defaultState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { HistoryPage } from './HistoryPage'

const NOW = new Date(2026, 10, 7, 15).getTime()
const row = (id: number, url: string, title: string, visitedAt: number): HistoryRow => ({ id, urlId: id, url, title, visitedAt, transition: 'link', favicon: null })

type Query = { text?: string; cursor?: { visitedAt: number; id: number }; limit?: number }

function setup(initial: HistoryRow[], opts: { refuseDeletes?: boolean } = {}) {
  let rows = [...initial]
  const queries: Query[] = []
  const handlers: Record<string, (p: unknown) => unknown> = {
    'history.query': (p) => {
      const q = p as Query
      queries.push(q)
      const text = q.text ?? ''
      const site = /^site:(.+)$/.exec(text)?.[1]
      const hit = rows.filter((r) => (site ? new URL(r.url).hostname === site : !text || r.title.includes(text) || r.url.includes(text)))
      const after = q.cursor ? hit.filter((r) => r.visitedAt < (q.cursor?.visitedAt ?? 0)) : hit
      const page = after.slice(0, q.limit ?? 100)
      const last = page[page.length - 1]
      return { rows: page, nextCursor: after.length > page.length && last ? { visitedAt: last.visitedAt, id: last.id } : null }
    },
    'history.deleteVisits': (p) => {
      const ids = new Set((p as { ids: number[] }).ids)
      rows = rows.filter((r) => !ids.has(r.id))
    },
    'history.deleteUrl': (p) => {
      rows = rows.filter((r) => r.url !== (p as { url: string }).url)
    }
  }
  // Without a handler the stub answers not-implemented, as main does for a refused call.
  if (opts.refuseDeletes) {
    delete handlers['history.deleteVisits']
    delete handlers['history.deleteUrl']
  }
  const stub = stubClu(handlers)
  window.clu = stub.clu
  const bump = (rev: number) => act(() => stub.push({ kind: 'snapshot', rev, state: { ...defaultState(), rev, historyRevision: rev } }))
  return { stub, queries, bump }
}

const flush = () => act(async () => {
  await vi.runOnlyPendingTimersAsync()
})
const titles = () => screen.queryAllByRole('listitem').map((li) => li.querySelector('.clu-history-title')?.textContent)

let nearBottom: (() => void)[] = []
beforeEach(() => {
  vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] })
  nearBottom = []
  vi.stubGlobal(
    'IntersectionObserver',
    class {
      constructor(cb: (entries: { isIntersecting: boolean }[]) => void) {
        nearBottom.push(() => cb([{ isIntersecting: true }]))
      }
      observe() {}
      disconnect() {}
    }
  )
})
afterEach(() => {
  vi.useRealTimers()
  vi.unstubAllGlobals()
})

const today = (h: number) => new Date(2026, 10, 7, h).getTime()
const three = () => [
  row(3, 'https://c.test/3', 'Gamma', today(14)),
  row(2, 'https://b.test/2', 'Beta', today(13)),
  row(1, 'https://b.test/1', 'Alpha', new Date(2026, 10, 6, 9).getTime())
]

describe('history page', () => {
  it('lists visits newest first under Today and Yesterday, as links to the page', async () => {
    setup(three())
    render(<HistoryPage now={() => NOW} />)
    await flush()
    const todaySection = screen.getByRole('region', { name: 'Today' })
    expect(within(todaySection).getAllByRole('link').map((a) => a.getAttribute('href'))).toEqual(['https://c.test/3', 'https://b.test/2'])
    expect(within(screen.getByRole('region', { name: 'Yesterday' })).getByRole('link')).toHaveAttribute('target', '_blank')
    // No icon in the store: a letter tile, no <img> at all.
    expect(document.querySelectorAll('img')).toHaveLength(0)
  })

  it('debounces search, and More from this site filters by host', async () => {
    const { queries } = setup(three())
    render(<HistoryPage now={() => NOW} />)
    await flush()
    const box = screen.getByRole('searchbox', { name: 'Search history' })
    fireEvent.change(box, { target: { value: 'G' } })
    fireEvent.change(box, { target: { value: 'Gam' } })
    await flush()
    expect(queries.filter((q) => q.text).map((q) => q.text)).toEqual(['Gam'])
    expect(titles()).toEqual(['Gamma'])

    fireEvent.change(box, { target: { value: 'zzz' } })
    await flush()
    expect(screen.getByRole('status')).toHaveTextContent('No results for ‘zzz’')

    fireEvent.change(box, { target: { value: '' } })
    await flush()
    fireEvent.click(screen.getAllByRole('button', { name: 'More actions' })[1] as HTMLElement)
    fireEvent.click(screen.getByRole('menuitem', { name: 'More from this site' }))
    await flush()
    expect(box).toHaveValue('site:b.test')
    expect(titles()).toEqual(['Beta', 'Alpha'])
  })

  it('click and Shift+click select a range; Delete asks once, then removes them', async () => {
    const { stub, bump } = setup(three())
    render(<HistoryPage now={() => NOW} />)
    await flush()
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select Gamma' }))
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select Beta' }), { shiftKey: true })
    expect(screen.getByText('2 selected')).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: 'Delete' }))
    const dialog = screen.getByRole('dialog', { name: 'Remove 2 items from history?' })
    fireEvent.click(within(dialog).getByRole('button', { name: 'Remove' }))
    await flush()
    expect(stub.calls.filter((c) => c.name === 'history.deleteVisits').map((c) => c.payload)).toEqual([{ ids: [3, 2] }])
    expect(titles()).toEqual(['Alpha'])
    bump(5)
    await flush()
    expect(titles()).toEqual(['Alpha'])
    expect(screen.queryByText(/selected/)).toBeNull()
  })

  it('one selected row deletes without asking; Escape clears a selection; Remove from history drops the URL', async () => {
    const { stub } = setup(three())
    render(<HistoryPage now={() => NOW} />)
    await flush()
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select Alpha' }))
    fireEvent.keyDown(screen.getByRole('checkbox', { name: 'Select Alpha' }), { key: 'Escape' })
    expect(screen.queryByText('1 selected')).toBeNull()
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select Alpha' }))
    fireEvent.keyDown(screen.getByRole('checkbox', { name: 'Select Alpha' }), { key: 'Delete' })
    await flush()
    expect(screen.queryByRole('dialog')).toBeNull()
    expect(titles()).toEqual(['Gamma', 'Beta'])
    fireEvent.click(screen.getAllByRole('button', { name: 'More actions' })[0] as HTMLElement)
    fireEvent.click(screen.getByRole('menuitem', { name: 'Remove from history' }))
    await flush()
    expect(stub.calls.find((c) => c.name === 'history.deleteUrl')?.payload).toEqual({ url: 'https://c.test/3' })
    expect(titles()).toEqual(['Beta'])
  })

  it('a refused delete brings the hidden rows back', async () => {
    setup(three(), { refuseDeletes: true })
    render(<HistoryPage now={() => NOW} />)
    await flush()
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select Alpha' }))
    fireEvent.keyDown(screen.getByRole('checkbox', { name: 'Select Alpha' }), { key: 'Delete' })
    await flush()
    expect(titles()).toEqual(['Gamma', 'Beta', 'Alpha'])
    fireEvent.click(screen.getAllByRole('button', { name: 'More actions' })[0] as HTMLElement)
    fireEvent.click(screen.getByRole('menuitem', { name: 'Remove from history' }))
    await flush()
    expect(titles()).toEqual(['Gamma', 'Beta', 'Alpha'])
  })

  it('a history change reloads as many rows as were listed, not just the first page', async () => {
    const many = Array.from({ length: 150 }, (_, i) => row(1000 - i, `https://m.test/${i}`, `M${i}`, today(14) - i * 1000))
    const { queries, bump } = setup(many)
    render(<HistoryPage now={() => NOW} />)
    await flush()
    expect(titles()).toHaveLength(100)
    act(() => nearBottom[nearBottom.length - 1]?.())
    await flush()
    expect(titles()).toHaveLength(150)
    queries.length = 0
    bump(1)
    await flush()
    expect(titles()).toHaveLength(150)
    expect(queries.map((q) => [q.cursor?.id ?? null, q.limit])).toEqual([[null, 100], [901, 100]])
  })

  it('shows No history yet when there is none', async () => {
    setup([])
    render(<HistoryPage now={() => NOW} />)
    await flush()
    expect(screen.getByRole('status')).toHaveTextContent('No history yet')
  })
})
