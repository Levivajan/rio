import { act, fireEvent, render } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultWindowState, type CluState } from '../../../shared/state'
import { PEEK_DWELL_MS } from '../../../shared/shell/peek'
import { stubClu } from '../../../test-utils/stubClu'
import { EdgeHotZone } from './EdgeHotZone'

const WIN = '00000000-0000-4000-8000-000000000001'

function mount() {
  const stub = stubClu({ 'shell.setSidebarPeek': () => null })
  window.clu = { ...stub.clu, windowId: WIN }
  const view = render(<EdgeHotZone />)
  const state: CluState = { ...defaultState(), rev: 1 }
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.shell.sidebar.hidden = true
  win.shell.layout = { ...win.shell.layout, hotZone: { x: 0, y: 0, width: 6, height: 800 } }
  state.windows = { [WIN]: win }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  const zone = view.container.querySelector<HTMLElement>('[data-shell="hot-zone"]')
  if (!zone) throw new Error('no hot zone')
  return { stub, zone, peeks: () => stub.calls.filter((c) => c.name === 'shell.setSidebarPeek') }
}

// The reveal zone is x 0 to 5 (x 7 does not trigger). The slide starts within about 60 ms,
// a pointer flung left across the zone between samples counts, and a held button never reveals.
describe('EdgeHotZone', () => {
  beforeEach(() => void vi.useFakeTimers())
  afterEach(() => void vi.useRealTimers())

  it('opens the peek once the pointer rests within 5 of the edge for the dwell', () => {
    const { zone, peeks } = mount()
    fireEvent.pointerMove(zone, { clientX: 3, clientY: 400 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS - 1))
    expect(peeks()).toHaveLength(0)
    act(() => void vi.advanceTimersByTime(1))
    expect(peeks().map((c) => c.payload)).toEqual([{ open: true }])
  })

  it('does nothing at 7 or 8 from the edge, and moving out of the 5 pt band cancels the dwell', () => {
    const { zone, peeks } = mount()
    fireEvent.pointerMove(zone, { clientX: 7, clientY: 400 })
    act(() => void vi.advanceTimersByTime(16))
    fireEvent.pointerMove(zone, { clientX: 8, clientY: 410 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS * 3))
    expect(peeks()).toHaveLength(0)
    fireEvent.pointerMove(zone, { clientX: 4, clientY: 410 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS - 10))
    fireEvent.pointerMove(zone, { clientX: 6, clientY: 410 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS))
    expect(peeks()).toHaveLength(0)
  })
  it('opens for a pointer flung left that crosses the zone between two samples', () => {
    const { peeks } = mount()
    fireEvent.pointerMove(window, { clientX: 300, clientY: 400 })
    act(() => void vi.advanceTimersByTime(16))
    fireEvent.pointerMove(window, { clientX: 9, clientY: 400 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS))
    expect(peeks().map((c) => c.payload)).toEqual([{ open: true }])
  })

  it('a fling that stops short of the zone cancels at its next sample', () => {
    const { peeks } = mount()
    fireEvent.pointerMove(window, { clientX: 300, clientY: 400 })
    act(() => void vi.advanceTimersByTime(16))
    fireEvent.pointerMove(window, { clientX: 9, clientY: 400 })
    act(() => void vi.advanceTimersByTime(16))
    fireEvent.pointerMove(window, { clientX: 8, clientY: 400 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS * 3))
    expect(peeks()).toHaveLength(0)
  })

  it('never opens while a button is held, as when dragging a tab, a selection or the window edge', () => {
    const { zone, peeks } = mount()
    fireEvent.pointerMove(zone, { clientX: 2, clientY: 400, buttons: 1 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS * 3))
    expect(peeks()).toHaveLength(0)
    // Released at the edge: the next sample there reveals.
    fireEvent.pointerMove(zone, { clientX: 2, clientY: 401, buttons: 0 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS))
    expect(peeks()).toHaveLength(1)
  })

  it('counts a pointer that comes off a page frame with only a pointerover', () => {
    const { zone, peeks } = mount()
    fireEvent.pointerOver(zone, { clientX: 3, clientY: 400 })
    fireEvent.pointerMove(zone, { clientX: 3, clientY: 400 })
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS))
    expect(peeks()).toHaveLength(1)
  })

  it('leaving the frame cancels the dwell', () => {
    const { zone, peeks } = mount()
    fireEvent.pointerMove(zone, { clientX: 2, clientY: 400 })
    fireEvent.pointerLeave(document.documentElement)
    act(() => void vi.advanceTimersByTime(PEEK_DWELL_MS * 3))
    expect(peeks()).toHaveLength(0)
  })
})
