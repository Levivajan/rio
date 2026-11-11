import { act, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import type { DownloadState, ScreenshotArrival } from '../../../shared/state/capabilities'
import { defaultState, defaultWindowState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { FILE_ARRIVALS, arrivalAgo, kindOf, stackArrivals, takeArrivals } from './arrivalRules'
import { FileArrivals } from './FileArrivals'
import { LIBRARY_PEEK, libraryPeekEnter, libraryPeekLeave } from '../archive/LibraryPeek'

// A finished download or a screenshot landing shows a row right above the bottom bar at once.
const WIN = '00000000-0000-4000-8000-000000000001'
const PROFILE = '00000000-0000-4000-8000-000200000001'
const T0 = 1_700_000_000_000

function row(n: number, patch: Partial<DownloadState> = {}): DownloadState {
  return {
    id: `00000000-0000-4000-8000-0006${n.toString(16).padStart(8, '0')}`,
    profileId: PROFILE,
    filename: `file-${n}.pdf`,
    url: 'https://example.com/file.pdf',
    state: 'completed',
    receivedBytes: 1000,
    totalBytes: 1000,
    bytesPerSecond: 0,
    etaMs: null,
    startedAt: T0 + n,
    endedAt: T0 + 1000 + n,
    exists: true,
    ...patch
  }
}
const shot = (name: string, at = T0 + 2000, kind: ScreenshotArrival['kind'] = 'image'): ScreenshotArrival => ({ name, kind, at })

const takeKey = (list: readonly { key: string; name: string }[], name: string) => list.find((a) => a.name === name)?.key ?? ''

describe('takeArrivals', () => {
  it('takes downloads once they are completed and on disk, and screenshots, after the sidebar started, once each', () => {
    const seen = new Set<string>()
    const rows = [row(1, { state: 'progressing', endedAt: null }), row(2, { exists: false }), row(3), row(4, { endedAt: T0 - 5 })]
    const got = takeArrivals(rows, [shot('Screenshot 1.png'), shot('Old.png', T0 - 1)], seen, T0)
    expect(got.map((a) => [a.source, a.name])).toEqual([
      ['downloads', 'file-3.pdf'],
      ['media', 'Screenshot 1.png']
    ])
    expect(got[0]?.downloadId).toBe(rows[2]?.id)
    // Shown once: the same rows bring nothing new.
    expect(takeArrivals(rows, [shot('Screenshot 1.png')], seen, T0)).toEqual([])
    // The running download finishing later arrives then.
    expect(takeArrivals([row(1, { endedAt: T0 + 3000 })], [], seen, T0).map((a) => a.name)).toEqual(['file-1.pdf'])
  })

  it('skips junk rows: no name, a path, a hidden file, a bad time, a missing list', () => {
    const seen = new Set<string>()
    const junk = [row(5, { filename: '' }), row(6, { filename: '../x.pdf' }), row(7, { endedAt: Number.NaN })]
    const shots = [shot(''), shot('a/b.png'), shot('.tmp.png'), { name: 'ok.mov', kind: 'video', at: Number.POSITIVE_INFINITY } as ScreenshotArrival]
    expect(takeArrivals(junk, shots, seen, T0)).toEqual([])
    expect(takeArrivals([], undefined, seen, T0)).toEqual([])
  })

  it('names kinds by extension and lists at most four, newest on top', () => {
    expect([kindOf('a.PNG'), kindOf('b.mov'), kindOf('c.zip')]).toEqual(['image', 'video', 'file'])
    const seen = new Set<string>()
    const all = takeArrivals([], [1, 2, 3, 4, 5].map((n) => shot(`s${n}.png`, T0 + n)), seen, T0)
    expect(stackArrivals([], all).map((a) => a.name)).toEqual(['s5.png', 's4.png', 's3.png', 's2.png'])
    const later = takeArrivals([], [shot('s6.png', T0 + 6)], seen, T0)
    expect(stackArrivals(stackArrivals([], all), later).map((a) => a.name)).toEqual(['s6.png', 's5.png', 's4.png', 's3.png'])
    expect(FILE_ARRIVALS.maxShown).toBe(4)
    // A busy row (its menu or rename open) stays; the oldest row that is not busy drops instead.
    const full = stackArrivals([], all)
    expect(stackArrivals(full, later, new Set([takeKey(full, 's2.png')])).map((a) => a.name)).toEqual(['s6.png', 's5.png', 's4.png', 's2.png'])
    expect(stackArrivals(full, later, new Set([takeKey(full, 's4.png')])).map((a) => a.name)).toEqual(['s6.png', 's5.png', 's4.png', 's3.png'])
    // A row outlives "5 seconds ago".
    expect(FILE_ARRIVALS.showMs).toBeGreaterThan(5000)
  })

  it('says how long ago in seconds under a minute', () => {
    expect(arrivalAgo(T0, T0 + 5_400)).toBe('5 seconds ago')
    expect(arrivalAgo(T0, T0 + 200)).toBe('1 second ago')
    expect(arrivalAgo(T0 + 3000, T0)).toBe('1 second ago')
    expect(arrivalAgo(T0, T0 + 3 * 60_000)).toBe('3 minutes ago')
    expect(arrivalAgo(Number.NaN, T0)).toBe('')
  })
})

describe('FileArrivals', () => {
  afterEach(() => {
    vi.useRealTimers()
  })

  function mount() {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout', 'setInterval', 'clearInterval', 'Date'] })
    vi.setSystemTime(T0)
    const stub = stubClu({ 'download.act': () => null, 'download.fileAct': () => null, 'download.thumb': () => 'data:image/png;base64,iVBORw0KGgo=' })
    window.clu = { ...stub.clu, windowId: WIN }
    render(<FileArrivals />)
    const base = defaultState()
    const space = Object.values(base.spaces)[0]
    const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: PROFILE })
    let rev = 0
    const push = (patch: Partial<CluState>) =>
      act(() =>
        stub.push({
          kind: 'snapshot',
          rev: ++rev,
          state: {
            ...base,
            rev,
            windows: { [WIN]: win },
            spaces: { [PROFILE]: { ...space, id: PROFILE, profileId: PROFILE } } as typeof base.spaces,
            profiles: { [PROFILE]: { ephemeral: false } } as unknown as typeof base.profiles,
            ...patch
          }
        })
      )
    return { stub, push }
  }

  it('shows nothing for what was there before, then a row the moment a screenshot lands, with its thumbnail, and opens it on click', async () => {
    const { stub, push } = mount()
    push({ downloads: [row(1, { endedAt: T0 - 60_000 })], screenshotArrivals: [shot('Before.png', T0 - 10)] })
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()

    push({ screenshotArrivals: [shot('Screenshot 2026-11-09 at 15.40.png', T0 + 5)] })
    const card = screen.getByRole('button', { name: /^Screenshot 2026-11-09 at 15\.40\.png/ })
    expect(card.closest('li')).toHaveAttribute('data-arrival', 'media')
    // The tile is the file's QuickLook thumbnail, asked for by folder and name.
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.thumb', payload: { source: 'media', name: 'Screenshot 2026-11-09 at 15.40.png' } })
    expect(card.querySelector('.clu-library-peek-tile')).toHaveAttribute('data-thumb', 'image')
    expect(card.querySelector('img')).toHaveAttribute('src', 'data:image/png;base64,iVBORw0KGgo=')
    // The time under the name counts up in seconds.
    expect(card).toHaveTextContent('1 second ago')
    act(() => void vi.advanceTimersByTime(6000))
    expect(card).toHaveTextContent('5 seconds ago')
    fireEvent.click(card)
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.fileAct', payload: { source: 'media', name: 'Screenshot 2026-11-09 at 15.40.png', action: 'open' } })
    // Opening dismisses it: it drops away.
    expect(card.closest('li')).toHaveAttribute('data-leaving', 'true')
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()
  })

  it('drags a screenshot out by folder and name and a download by id, as the file itself', async () => {
    const { stub, push } = mount()
    push({ screenshotArrivals: [shot('Shot.png', T0 + 5)] })
    const card = screen.getByRole('button', { name: /^Shot\.png/ })
    expect(card).toHaveAttribute('draggable', 'true')
    expect(fireEvent.dragStart(card)).toBe(false)
    push({ downloads: [row(3, { endedAt: T0 + 50 })], screenshotArrivals: [shot('Shot.png', T0 + 5)] })
    fireEvent.dragStart(screen.getByRole('button', { name: /^file-3\.pdf/ }))
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.fileAct', payload: { source: 'media', name: 'Shot.png', action: 'drag' } })
    expect(stub.calls).toContainEqual({ name: 'download.act', payload: { id: row(3).id, action: 'drag' } })
  })

  it('right-click opens the Library menu for the row, and Copy Path and Show in Finder act on that file', async () => {
    const { stub, push } = mount()
    push({ screenshotArrivals: [shot('Shot.png', T0 + 5)] })
    fireEvent.contextMenu(screen.getByRole('button', { name: /^Shot\.png/ }), { clientX: 20, clientY: 400 })
    expect(screen.getAllByRole('menuitem').map((m) => m.textContent)).toEqual(['Open', 'Show in Finder', 'Copy', 'Copy Path', 'Share…', 'Rename…', 'Move to Trash'])
    fireEvent.click(screen.getByRole('menuitem', { name: 'Copy Path' }))
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.fileAct', payload: { source: 'media', name: 'Shot.png', action: 'copyPath' } })
    fireEvent.contextMenu(screen.getByRole('button', { name: /^Shot\.png/ }), { clientX: 20, clientY: 400 })
    fireEvent.click(screen.getByRole('menuitem', { name: 'Show in Finder' }))
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.fileAct', payload: { source: 'media', name: 'Shot.png', action: 'reveal' } })
    expect(screen.queryByRole('menu')).toBeNull()
  })

  it('an open menu holds the rows past the show time, and closing it starts their time again', () => {
    const { push } = mount()
    push({ screenshotArrivals: [shot('Shot.png', T0 + 5)] })
    fireEvent.contextMenu(screen.getByRole('button', { name: /^Shot\.png/ }), { clientX: 20, clientY: 400 })
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs * 2))
    expect(screen.getByRole('menu')).toBeInTheDocument()
    expect(screen.getByRole('list', { name: 'Just added' }).querySelector('[data-leaving]')).toBeNull()
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'Escape' })
    expect(screen.queryByRole('menu')).toBeNull()
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs - 1000))
    expect(screen.getByRole('list', { name: 'Just added' }).querySelector('[data-leaving]')).toBeNull()
    act(() => void vi.advanceTimersByTime(1000))
    expect(screen.getByRole('list', { name: 'Just added' }).querySelector('[data-leaving]')).not.toBeNull()
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()
  })

  it('keeps a row whose Rename is open when a new arrival fills the list, with its typed text, and the rename applies', async () => {
    const { stub, push } = mount()
    const shots = [1, 2, 3, 4].map((n) => shot(`Screenshot ${n}.png`, T0 + n))
    push({ screenshotArrivals: shots })
    expect(screen.getAllByRole('listitem')).toHaveLength(FILE_ARRIVALS.maxShown)
    // Rename the oldest row, at the bottom.
    fireEvent.contextMenu(screen.getByRole('button', { name: /^Screenshot 1\.png/ }), { clientX: 20, clientY: 400 })
    fireEvent.click(screen.getByRole('menuitem', { name: 'Rename…' }))
    const field = screen.getByRole('textbox', { name: 'Rename Screenshot 1.png' })
    fireEvent.change(field, { target: { value: 'Receipt.png' } })
    // One more file lands: the oldest row that is not being renamed drops, the renamed row stays.
    push({ screenshotArrivals: [...shots, shot('Screenshot 5.png', T0 + 5)] })
    expect(screen.getAllByRole('listitem').map((li) => li.querySelector('.clu-library-peek-title')?.textContent)).toEqual([
      'Screenshot 5.png',
      'Screenshot 4.png',
      'Screenshot 3.png',
      'Screenshot 1.png'
    ])
    const still = screen.getByRole('textbox', { name: 'Rename Screenshot 1.png' })
    expect(still).toBe(field)
    expect(still).toHaveValue('Receipt.png')
    fireEvent.keyDown(still, { key: 'Enter' })
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.fileAct', payload: { source: 'media', name: 'Screenshot 1.png', action: 'rename', newName: 'Receipt.png' } })
    expect(screen.queryByRole('textbox')).toBeNull()
  })

  it('Rename on each of 4 rows in turn, with files landing in between, names and renames that row only', async () => {
    const { stub, push } = mount()
    const shots = [1, 2, 3, 4].map((n) => shot(`Screenshot ${n}.png`, T0 + n))
    push({ screenshotArrivals: shots })
    const titles = () => screen.getAllByRole('listitem').map((li) => li.querySelector('.clu-library-peek-title')?.textContent ?? '')
    const renamed: string[] = []
    for (let i = 0; i < 4; i++) {
      const list = titles()
      const name = list[i % list.length] ?? ''
      fireEvent.contextMenu(screen.getByRole('button', { name: new RegExp(`^${name.replace('.', '\\.')}`) }), { clientX: 20, clientY: 400 })
      expect(screen.getByRole('menu', { name })).toBeInTheDocument()
      // A file lands while the menu is open: the rows shift, the menu still names its row.
      shots.push(shot(`Screenshot ${5 + 2 * i}.png`, T0 + 5 + 2 * i))
      push({ screenshotArrivals: [...shots] })
      fireEvent.click(screen.getByRole('menuitem', { name: 'Rename…' }))
      const field = screen.getByRole('textbox', { name: `Rename ${name}` })
      expect(field).toHaveValue(name)
      // Another lands while the field is open.
      shots.push(shot(`Screenshot ${6 + 2 * i}.png`, T0 + 6 + 2 * i))
      push({ screenshotArrivals: [...shots] })
      expect(screen.getByRole('textbox', { name: `Rename ${name}` })).toBe(field)
      fireEvent.change(field, { target: { value: `Renamed ${i}.png` } })
      fireEvent.keyDown(field, { key: 'Enter' })
      await act(async () => {})
      renamed.push(name)
      act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    }
    expect(new Set(renamed).size).toBe(4)
    expect(stub.calls.filter((c) => c.name === 'download.fileAct' && (c.payload as { action: string }).action === 'rename').map((c) => c.payload)).toEqual(
      renamed.map((name, i) => ({ source: 'media', name, action: 'rename', newName: `Renamed ${i}.png` }))
    )
  })

  it('a row that times out under the pointer keeps its slot, so the rows above never slide under it', async () => {
    const { stub, push } = mount()
    const shots: ScreenshotArrival[] = []
    for (let n = 1; n <= 4; n++) {
      shots.push(shot(`Screenshot ${n}.png`, T0 + n * 2000))
      push({ screenshotArrivals: [...shots] })
      act(() => void vi.advanceTimersByTime(2000))
    }
    const list = screen.getByRole('list', { name: 'Just added' })
    // The pointer rests on the bottom row: no move, so no hold, and the oldest row times out.
    fireEvent.pointerEnter(list)
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs - 8000))
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    const items = screen.getAllByRole('listitem', { hidden: true })
    expect(items).toHaveLength(4)
    expect(items[3]).toHaveAttribute('data-gap')
    expect(items[2]?.querySelector('.clu-library-peek-title')).toHaveTextContent('Screenshot 2.png')
    fireEvent.contextMenu(screen.getByRole('button', { name: /^Screenshot 2\.png/ }), { clientX: 20, clientY: 400 })
    fireEvent.click(screen.getByRole('menuitem', { name: 'Rename…' }))
    const field = screen.getByRole('textbox', { name: 'Rename Screenshot 2.png' })
    expect(field).toHaveValue('Screenshot 2.png')
    // Leaving the list closes the gap; the row being renamed stays.
    fireEvent.pointerLeave(list)
    expect(list.querySelector('[data-gap]')).toBeNull()
    expect(screen.getByRole('textbox', { name: 'Rename Screenshot 2.png' })).toBe(field)
    fireEvent.change(field, { target: { value: 'Two.png' } })
    fireEvent.keyDown(field, { key: 'Enter' })
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.fileAct', payload: { source: 'media', name: 'Screenshot 2.png', action: 'rename', newName: 'Two.png' } })
  })

  it('the Library hover peek stays shut while a row is being renamed, so the field and its text survive', () => {
    const { push } = mount()
    push({ screenshotArrivals: [shot('Shot.png', T0 + 5)] })
    fireEvent.contextMenu(screen.getByRole('button', { name: /^Shot\.png/ }), { clientX: 20, clientY: 400 })
    fireEvent.click(screen.getByRole('menuitem', { name: 'Rename…' }))
    const field = screen.getByRole('textbox', { name: 'Rename Shot.png' })
    fireEvent.change(field, { target: { value: 'Receipt.png' } })
    // The pointer moves onto the Library button just below.
    act(() => libraryPeekEnter())
    act(() => void vi.advanceTimersByTime(LIBRARY_PEEK.delayMs * 2))
    expect(screen.getByRole('textbox', { name: 'Rename Shot.png' })).toBe(field)
    expect(field).toHaveValue('Receipt.png')
    // With the edit over, the peek opens again and takes the pop-up's place.
    fireEvent.keyDown(field, { key: 'Escape' })
    act(() => libraryPeekEnter())
    act(() => void vi.advanceTimersByTime(LIBRARY_PEEK.delayMs))
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()
    act(() => libraryPeekLeave(true))
    act(() => void vi.advanceTimersByTime(LIBRARY_PEEK.closeMs))
  })

  it('shows a finished download, opens it by id, and lets it go on its own after the show time unless held', () => {
    const { stub, push } = mount()
    push({ downloads: [row(2, { state: 'progressing', endedAt: null })] })
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()
    act(() => void vi.advanceTimersByTime(1000))
    push({ downloads: [row(2, { endedAt: T0 + 1000 })] })
    const list = screen.getByRole('list', { name: 'Just added' })
    expect(list).toHaveTextContent('file-2.pdf')
    // Still up past "5 seconds ago".
    act(() => void vi.advanceTimersByTime(6000))
    expect(list.querySelector('[data-leaving]')).toBeNull()
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs - 6000))
    expect(list.querySelector('[data-leaving]')).not.toBeNull()
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    push({ downloads: [row(2, { endedAt: T0 + 1000 }), row(4, { endedAt: T0 + 20_000 })] })
    const again = screen.getByRole('list', { name: 'Just added' })

    // Held while the pointer moves over the list.
    fireEvent.pointerEnter(again)
    for (let i = 0; i < 4; i++) {
      fireEvent.pointerMove(again)
      act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs / 2))
    }
    expect(again.querySelector('[data-leaving]')).toBeNull()
    fireEvent.pointerLeave(again)
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs - 1))
    expect(again.querySelector('[data-leaving]')).toBeNull()
    act(() => void vi.advanceTimersByTime(1))
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()
    expect(stub.calls.some((c) => c.name === 'download.act')).toBe(false)
  })

  it('lets the rows go when the pointer rests on them or leaves without a pointerleave', () => {
    const { push } = mount()
    push({ screenshotArrivals: [shot('Screenshot A.png', T0 + 5)] })
    const list = screen.getByRole('list', { name: 'Just added' })
    // The row rises under a resting pointer: an enter with no move holds nothing.
    fireEvent.pointerEnter(list)
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs))
    expect(list.querySelector('[data-leaving]')).not.toBeNull()
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()

    // A move holds them, then the pointer goes to another app and never sends pointerleave: the hold lapses.
    push({ screenshotArrivals: [shot('Screenshot A.png', T0 + 5), shot('Screenshot B.png', T0 + 20_000)] })
    const again = screen.getByRole('list', { name: 'Just added' })
    fireEvent.pointerMove(again)
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.showMs - 1))
    expect(again.querySelector('[data-leaving]')).toBeNull()
    // The lapse lets the row go at once: its time ran out while held.
    act(() => void vi.advanceTimersByTime(1))
    act(() => void vi.advanceTimersByTime(0))
    expect(again.querySelectorAll('[data-leaving]')).toHaveLength(1)
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()
  })

  it('opens a download by its row id and dismisses with the x', async () => {
    const { stub, push } = mount()
    const r = row(3, { endedAt: T0 + 10 })
    push({ downloads: [r], screenshotArrivals: [shot('Screen Recording.mov', T0 + 20, 'video')] })
    // Newest on top.
    expect(screen.getAllByRole('listitem').map((li) => li.getAttribute('data-arrival'))).toEqual(['media', 'downloads'])
    fireEvent.click(screen.getByRole('button', { name: /^file-3\.pdf/ }))
    await act(async () => {})
    expect(stub.calls).toContainEqual({ name: 'download.act', payload: { id: r.id, action: 'open' } })
    fireEvent.click(screen.getByRole('button', { name: 'Dismiss Screen Recording.mov' }))
    act(() => void vi.advanceTimersByTime(FILE_ARRIVALS.leaveMs))
    expect(screen.queryByRole('list', { name: 'Just added' })).toBeNull()
  })
})
