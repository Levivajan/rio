import { act, fireEvent, render } from '@testing-library/react'
import { beforeAll, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultWindowState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { LOCK_ESCAPE, LOCK_WIDTH, ResizeHandle, lockedWidth } from './ResizeHandle'

const WIN = '00000000-0000-4000-8000-000000000001'

beforeAll(() => {
  // jsdom has no pointer capture.
  Element.prototype.setPointerCapture ??= () => {}
  Element.prototype.releasePointerCapture ??= () => {}
  Element.prototype.hasPointerCapture ??= () => false
  globalThis.requestAnimationFrame ??= (cb: FrameRequestCallback) => setTimeout(() => cb(0), 16) as unknown as number
  globalThis.cancelAnimationFrame ??= (id: number) => clearTimeout(id)
})

function mount(shell: { hidden?: boolean; focusMode?: boolean } = {}) {
  const stub = stubClu({ 'shell.setSidebarWidth': () => null, 'shell.resetSidebarWidth': () => null })
  window.clu = stub.clu
  const view = render(<ResizeHandle />)
  const state: CluState = { ...defaultState(), rev: 1 }
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.shell.sidebar.hidden = shell.hidden ?? false
  win.shell.focusMode = shell.focusMode ?? false
  state.windows = { [WIN]: win }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  const handle = view.container.querySelector<HTMLElement>('[data-shell="resize-handle"]')
  return { stub, handle }
}

describe('ResizeHandle', () => {
  it('a drag commits the width under the pointer on release', () => {
    const { stub, handle } = mount()
    if (!handle) throw new Error('no handle')
    // An 8 px band centered on the sidebar's edge.
    expect(handle.style.left).toBe('210px')
    expect(handle.style.width).toBe('8px')
    fireEvent.pointerDown(handle, { button: 0, clientX: 211, pointerId: 1 })
    fireEvent.pointerMove(handle, { clientX: 300, pointerId: 1 })
    fireEvent.pointerUp(handle, { button: 0, clientX: 357, pointerId: 1 })
    const widths = stub.calls.filter((c) => c.name === 'shell.setSidebarWidth')
    expect(widths.at(-1)).toEqual({ name: 'shell.setSidebarWidth', payload: { width: 360, commit: true } })
    // Only the release commits.
    expect(widths.filter((c) => (c.payload as { commit?: boolean }).commit)).toHaveLength(1)
  })

  it('a right-button press does not start a drag', () => {
    const { stub, handle } = mount()
    if (!handle) throw new Error('no handle')
    fireEvent.pointerDown(handle, { button: 2, clientX: 211, pointerId: 1 })
    fireEvent.pointerUp(handle, { button: 2, clientX: 400, pointerId: 1 })
    expect(stub.calls.some((c) => c.name === 'shell.setSidebarWidth')).toBe(false)
  })

  it('a double-click resets the width', () => {
    const { stub, handle } = mount()
    if (!handle) throw new Error('no handle')
    fireEvent.doubleClick(handle)
    expect(stub.calls.filter((c) => c.name === 'shell.resetSidebarWidth')).toHaveLength(1)
  })

  it('there is no edge to drag while the sidebar is hidden or in focus mode', () => {
    expect(mount({ hidden: true }).handle).toBeNull()
    expect(mount({ focusMode: true }).handle).toBeNull()
  })

  // The first 1 pt after the press is ignored, then the width follows the pointer 1:1.
  it('ignores the first point of movement, then follows the pointer from the press', async () => {
    const { stub, handle } = mount()
    if (!handle) throw new Error('no handle')
    fireEvent.pointerDown(handle, { button: 0, clientX: 212, pointerId: 1 })
    fireEvent.pointerMove(handle, { clientX: 213, pointerId: 1 })
    await new Promise((r) => setTimeout(r, 40))
    expect(stub.calls.some((c) => c.name === 'shell.setSidebarWidth')).toBe(false)
    fireEvent.pointerMove(handle, { clientX: 214, pointerId: 1 })
    await new Promise((r) => setTimeout(r, 40))
    expect(stub.calls.filter((c) => c.name === 'shell.setSidebarWidth').at(-1)?.payload).toEqual({ width: 216 })
    fireEvent.pointerUp(handle, { button: 0, clientX: 214, pointerId: 1 })
    expect(stub.calls.filter((c) => c.name === 'shell.setSidebarWidth').at(-1)?.payload).toEqual({ width: 216, commit: true })
  })

  it('a click without movement commits nothing', () => {
    const { stub, handle } = mount()
    if (!handle) throw new Error('no handle')
    fireEvent.pointerDown(handle, { button: 0, clientX: 212, pointerId: 1 })
    fireEvent.pointerUp(handle, { button: 0, clientX: 213, pointerId: 1 })
    expect(stub.calls.some((c) => c.name === 'shell.setSidebarWidth')).toBe(false)
  })

  it('shows the edge bar 250 ms after the pointer rests on the band, and hides it when it leaves', () => {
    vi.useFakeTimers()
    try {
      const { handle } = mount()
      if (!handle) throw new Error('no handle')
      const bar = document.querySelector<HTMLElement>('[data-shell="resize-bar"]')
      expect(bar?.style.left).toBe('208px')
      fireEvent.pointerEnter(handle)
      act(() => void vi.advanceTimersByTime(249))
      expect(bar?.dataset.shown).toBe('false')
      act(() => void vi.advanceTimersByTime(1))
      expect(bar?.dataset.shown).toBe('true')
      fireEvent.pointerLeave(handle)
      expect(bar?.dataset.shown).toBe('false')
    } finally {
      vi.useRealTimers()
    }
  })
})

describe('lockedWidth', () => {
  it('a drag that reaches or crosses 228 stops there, from either side', () => {
    expect(lockedWidth(214, 226)).toBe(226)
    expect(lockedWidth(226, 231)).toBe(LOCK_WIDTH)
    expect(lockedWidth(260, 225)).toBe(LOCK_WIDTH)
    expect(lockedWidth(200, 300)).toBe(LOCK_WIDTH)
  })

  it('leaving the lock takes more than 6 points of drag past it', () => {
    expect(lockedWidth(LOCK_WIDTH, LOCK_WIDTH + LOCK_ESCAPE)).toBe(LOCK_WIDTH)
    expect(lockedWidth(LOCK_WIDTH, LOCK_WIDTH - LOCK_ESCAPE)).toBe(LOCK_WIDTH)
    expect(lockedWidth(LOCK_WIDTH, LOCK_WIDTH + LOCK_ESCAPE + 1)).toBe(LOCK_WIDTH + LOCK_ESCAPE + 1)
    expect(lockedWidth(LOCK_WIDTH, 215)).toBe(215)
  })

  it('a drag through the edge holds at 228 and commits it when released inside the escape', () => {
    const { stub, handle } = mount()
    if (!handle) throw new Error('no handle')
    fireEvent.pointerDown(handle, { button: 0, clientX: 211, pointerId: 1 })
    fireEvent.pointerMove(handle, { clientX: 220, pointerId: 1 })
    fireEvent.pointerMove(handle, { clientX: 229, pointerId: 1 })
    fireEvent.pointerUp(handle, { button: 0, clientX: 230, pointerId: 1 })
    const widths = stub.calls.filter((c) => c.name === 'shell.setSidebarWidth')
    expect(widths.at(-1)).toEqual({ name: 'shell.setSidebarWidth', payload: { width: LOCK_WIDTH, commit: true } })
  })
})
