import { act, fireEvent, render, renderHook } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { PEEK_GRACE_MS, PEEK_SLIDE_IN_MS, PEEK_SLIDE_OUT_DONE_MS, PEEK_SLIDE_OUT_MS } from '../../../shared/shell/peek'
import { defaultState, defaultWindowState, type CluState, type OverlaySurfaceState, type SidebarRequest } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { holdLibraryPeek } from '../../ui/archive/LibraryPeek'
import { setRenaming } from '../../ui/tabs/renaming'
import { useSidebarRequest, useSidebarRequestBaseline } from '../../ui/tabs/sidebarRequest'
import { SidebarPeek } from './SidebarPeek'

// The rows themselves are the sidebar's (its own tests); here only the peek's hover rules matter.
vi.mock('../../ui/sidebar/Sidebar', () => ({ Sidebar: () => null }))
vi.mock('../../ui/theme/ThemeBackground', () => ({ ThemeBackground: () => null }))

const WIN = '00000000-0000-4000-8000-000000000001'
const PEEK = '99999999-9999-4999-8999-999999999999'
const CARD = '99999999-9999-4999-8999-999999999998'

const surface = (instanceId: string, name: string, openedFrom: string | null, slot: number): OverlaySurfaceState => ({
  instanceId,
  surface: name,
  layer: 'passive',
  modality: 'passive',
  rect: { x: 0, y: 0, width: 276, height: 800 },
  openedFrom,
  focus: false,
  slot,
  props: {}
})

function mount(children: OverlaySurfaceState[] = [], props: OverlaySurfaceState['props'] = {}, library = false) {
  const stub = stubClu({
    'shell.setSidebarPeek': () => null,
    'shell.holdSidebarPeek': () => null,
    'shell.pointerAlive': () => null,
    'archive.close': () => null,
    'window.close': () => null,
    'window.minimize': () => null,
    'window.toggleFullscreen': () => null
  })
  window.clu = { ...stub.clu, layer: 'passive', slot: 3 }
  const peek = { ...surface(PEEK, 'sidebar-peek', null, 3), props }
  const view = render(<SidebarPeek instance={peek} windowId={WIN} />)
  const state: CluState = { ...defaultState(), rev: 1 }
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.shell.sidebar.hidden = true
  win.shell.sidebar.peeking = true
  win.overlay.surfaces = [peek, ...children]
  state.windows = { [WIN]: win }
  state.archivePanel = library ? { [WIN]: true } : {}
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  const el = view.container.querySelector<HTMLElement>('[data-shell="sidebar-peek"]')
  if (!el) throw new Error('no peek')
  const closes = () => stub.calls.filter((c) => c.name === 'shell.setSidebarPeek')
  const holds = () => stub.calls.filter((c) => c.name === 'shell.holdSidebarPeek').map((c) => c.payload)
  // The Library closes (its back arrow, the Library button, Cmd+Shift+L), with a menu ask of the sidebar when given.
  const closeLibrary = (sidebarRequest: SidebarRequest | null = null) =>
    act(() =>
      stub.push({
        kind: 'snapshot',
        rev: 2,
        state: { ...state, rev: 2, archivePanel: {}, windows: { [WIN]: { ...win, sidebarRequest } } }
      })
    )
  // Main's next surface props for the peek (props.leaving).
  const setProps = (next: OverlaySurfaceState['props']) => view.rerender(<SidebarPeek instance={{ ...peek, props: next }} windowId={WIN} />)
  return { stub, el, closes, holds, closeLibrary, setProps }
}

beforeEach(() => vi.useFakeTimers())
afterEach(() => vi.useRealTimers())

describe('SidebarPeek', () => {
  it('draws the window buttons in its top row at their docked place, each running its window command', () => {
    const { stub, el } = mount()
    const lights = el.querySelector<HTMLElement>('[data-shell="peek-lights"]')
    expect(lights?.style.left).toBe('13.75px')
    expect(lights?.style.top).toBe('17.75px')
    expect(lights?.style.gap).toBe('11px')
    // The seeded window is not focused: the buttons draw gray, as macOS's do.
    expect(lights?.dataset.focused).toBe('false')
    const buttons = [...(lights?.querySelectorAll<HTMLButtonElement>('button') ?? [])]
    expect(buttons.map((b) => b.getAttribute('aria-label'))).toEqual(['Close Window', 'Minimize', 'Enter Full Screen'])
    for (const b of buttons) fireEvent.click(b)
    expect(stub.calls.filter((c) => c.name.startsWith('window.')).map((c) => c.name)).toEqual([
      'window.close',
      'window.minimize',
      'window.toggleFullscreen'
    ])
  })

  it('pinned (no page open): floats in place with no slide and never slides out or closes', () => {
    const { el, closes } = mount([], { pinned: true })
    expect(el.dataset.pinned).toBe('true')
    expect(el.style.getPropertyValue('--peek-in')).toBe('0ms')
    fireEvent.pointerEnter(el)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS + PEEK_SLIDE_OUT_MS + 1000))
    expect(el.dataset.leaving).toBe('false')
    fireEvent.keyDown(window, { key: 'Escape' })
    expect(closes()).toHaveLength(0)
  })

  it('unpinned it slides in on the spring', () => {
    const { el } = mount()
    expect(el.dataset.pinned).toBe('false')
    expect(el.style.getPropertyValue('--peek-in')).toBe(`${PEEK_SLIDE_IN_MS}ms`)
    expect(el.style.getPropertyValue('--peek-out')).toBe(`${PEEK_SLIDE_OUT_MS}ms`)
  })

  it('mounts off the edge and starts the slide on its second frame, so a hidden widget cannot skip it', () => {
    const { el } = mount()
    expect(el.dataset.motion).toBe('enter')
    act(() => vi.advanceTimersToNextFrame())
    expect(el.dataset.motion).toBe('enter')
    act(() => vi.advanceTimersToNextFrame())
    expect(el.dataset.motion).toBe('shown')
  })

  it('pinned it shows in place at once', () => {
    const { el } = mount([], { pinned: true })
    expect(el.dataset.motion).toBe('shown')
  })

  it('slides out after the grace and turns back in from there when the pointer returns', () => {
    const { el, closes } = mount()
    act(() => vi.advanceTimersByTime(50))
    expect(el.dataset.motion).toBe('shown')
    fireEvent.pointerEnter(el)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS))
    expect(el.dataset.motion).toBe('leaving')
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_MS / 2))
    fireEvent.pointerEnter(el)
    expect(el.dataset.motion).toBe('shown')
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_MS))
    expect(closes()).toHaveLength(0)
  })

  it("main's grace starts the slide-out on its clock, even before the overlay saw the pointer leave", () => {
    const { el, closes, setProps } = mount()
    act(() => vi.advanceTimersByTime(50))
    fireEvent.pointerEnter(el)
    // The overlay's main thread was busy: no pointerleave yet, but main's 165 ms ran out.
    act(() => setProps({ pinned: false, leaving: true }))
    expect(el.dataset.motion).toBe('leaving')
    expect(el.style.getPropertyValue('--peek-out-ease')).toBe('linear(0, 0.06 20%, 0.2 40%, 0.4 60%, 0.66 80%, 1)')
    // The late leave changes nothing: one slide, one close once it is done.
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_DONE_MS - 1))
    expect(closes()).toHaveLength(0)
    act(() => vi.advanceTimersByTime(1))
    expect(closes()).toEqual([{ name: 'shell.setSidebarPeek', payload: { open: false } }])
  })

  it('main seeing the pointer back during the slide brings it back in', () => {
    const { el, closes, setProps } = mount()
    act(() => vi.advanceTimersByTime(50))
    act(() => setProps({ pinned: false, leaving: true }))
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_MS / 2))
    act(() => setProps({ pinned: false, leaving: false }))
    expect(el.dataset.motion).toBe('shown')
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_MS * 2))
    expect(closes()).toHaveLength(0)
  })

  it('pinned it ignores a leaving prop', () => {
    const { el, closes, setProps } = mount([], { pinned: true })
    act(() => setProps({ pinned: true, leaving: true }))
    expect(el.dataset.motion).toBe('shown')
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_MS * 2))
    expect(closes()).toHaveLength(0)
  })

  it('reports the pointer, then slides out and asks to close after the grace', () => {
    const { stub, el, closes } = mount()
    fireEvent.pointerEnter(el)
    expect(stub.calls.filter((c) => c.name === 'shell.pointerAlive')).toHaveLength(1)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS - 1))
    expect(el.dataset.leaving).toBe('false')
    act(() => vi.advanceTimersByTime(1))
    expect(el.dataset.leaving).toBe('true')
    expect(closes()).toHaveLength(0)
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_MS))
    // The slide's last frame shows before it asks.
    expect(closes()).toHaveLength(0)
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_DONE_MS - PEEK_SLIDE_OUT_MS))
    expect(closes()).toEqual([{ name: 'shell.setSidebarPeek', payload: { open: false } }])
  })

  it('coming back during the slide keeps it', () => {
    const { el, closes } = mount()
    fireEvent.pointerEnter(el)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS + PEEK_SLIDE_OUT_MS / 2))
    fireEvent.pointerMove(el)
    expect(el.dataset.leaving).toBe('false')
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS * 4))
    expect(closes()).toHaveLength(0)
  })

  it('its own hover card holds it open', () => {
    const { el, closes } = mount([surface(CARD, 'tab-hover', PEEK, 2)])
    expect(el.dataset.children).toBe('1')
    fireEvent.pointerEnter(el)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime((PEEK_GRACE_MS + PEEK_SLIDE_OUT_MS) * 4))
    expect(closes()).toHaveLength(0)
  })

  it('the Library open in it holds it and takes the keyboard; closed, main decides by the pointer', () => {
    const { el, closes, holds, closeLibrary, setProps } = mount([], {}, true)
    expect(el.dataset.held).toBe('true')
    expect(holds()).toEqual([{ held: true }])
    fireEvent.pointerEnter(el)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime((PEEK_GRACE_MS + PEEK_SLIDE_OUT_MS) * 10))
    expect(el.dataset.leaving).toBe('false')
    expect(closes()).toHaveLength(0)
    // A leave seen while held is no proof the pointer is off the panel when Back closes the Library: it stays.
    closeLibrary()
    expect(el.dataset.held).toBe('false')
    expect(holds()).toEqual([{ held: true }, { held: false }])
    act(() => vi.advanceTimersByTime((PEEK_GRACE_MS + PEEK_SLIDE_OUT_DONE_MS) * 4))
    expect(el.dataset.leaving).toBe('false')
    expect(closes()).toHaveLength(0)
    // Main finds the pointer off it (Escape or a page click with the pointer over the page): it slides out and closes.
    setProps({ leaving: true })
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_DONE_MS))
    expect(closes()).toEqual([{ name: 'shell.setSidebarPeek', payload: { open: false } }])
  })

  it("a row's rename field (Tabs ▸ New Folder… asked here) holds it and takes the keyboard until the rename ends", () => {
    const { el, holds, stub } = mount()
    expect(holds()).toEqual([])
    act(() => setRenaming('00000000-0000-4000-8000-0000000000f1'))
    expect(el.dataset.held).toBe('true')
    expect(holds()).toEqual([{ held: true }])
    // Escape belongs to the rename field: the peek stays and no Library closes.
    fireEvent.keyDown(window, { key: 'Escape' })
    expect(stub.calls.filter((c) => c.name === 'shell.setSidebarPeek')).toHaveLength(0)
    act(() => setRenaming(null))
    expect(el.dataset.held).toBe('false')
    expect(holds()).toEqual([{ held: true }, { held: false }])
  })

  it('Tabs ▸ New Folder… from its Library keeps it held from the Library closing through the rename, with no release between', () => {
    const { el, holds, closeLibrary } = mount([], {}, true)
    // The sidebar marks the asks there when it first shows as old.
    renderHook(() => useSidebarRequestBaseline(WIN))
    expect(holds()).toEqual([{ held: true }])
    // The Library closes with the ask; the floating tab list has not mounted yet.
    closeLibrary({ seq: 1, kind: 'rename', nodeId: '00000000-0000-4000-8000-0000000000f1', floating: true })
    expect(el.dataset.held).toBe('true')
    expect(holds()).toEqual([{ held: true }])
    // The list mounts and starts the rename.
    renderHook(() => useSidebarRequest(WIN, true, true))
    expect(el.dataset.held).toBe('true')
    expect(holds()).toEqual([{ held: true }])
    // The rename ends: the release goes once.
    act(() => setRenaming(null))
    expect(el.dataset.held).toBe('false')
    expect(holds()).toEqual([{ held: true }, { held: false }])
  })

  it('Escape closes the Library that holds it, so a held peek always has a way out; a field with text or a Rename keeps Escape', () => {
    const { stub, el, closes, closeLibrary } = mount([], {}, true)
    const libraryCloses = () => stub.calls.filter((c) => c.name === 'archive.close')
    const field = document.createElement('input')
    el.appendChild(field)
    const search = document.createElement('input')
    search.type = 'search'
    search.value = 'report'
    el.appendChild(search)
    fireEvent.pointerEnter(el)
    fireEvent.pointerLeave(el)
    fireEvent.keyDown(field, { key: 'Escape' })
    fireEvent.keyDown(search, { key: 'Escape' })
    expect(libraryCloses()).toHaveLength(0)
    // The Library's search field, focused on open, lets Escape through once it is empty.
    search.value = ''
    fireEvent.keyDown(search, { key: 'Escape' })
    expect(libraryCloses()).toHaveLength(1)
    expect(closes()).toHaveLength(0)
    closeLibrary()
    expect(el.dataset.held).toBe('false')
    fireEvent.pointerMove(el)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS + PEEK_SLIDE_OUT_DONE_MS))
    expect(closes()).toHaveLength(1)
    // A pop-up row's Rename holds it and keeps Escape.
    act(() => holdLibraryPeek(true))
    fireEvent.keyDown(window, { key: 'Escape' })
    expect(libraryCloses()).toHaveLength(1)
    act(() => holdLibraryPeek(false))
  })

  it('the Library closing under the pointer keeps it until the pointer leaves', () => {
    const { el, closes, closeLibrary } = mount([], {}, true)
    fireEvent.pointerEnter(el)
    closeLibrary()
    act(() => vi.advanceTimersByTime((PEEK_GRACE_MS + PEEK_SLIDE_OUT_MS) * 4))
    expect(closes()).toHaveLength(0)
    fireEvent.pointerMove(el)
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS + PEEK_SLIDE_OUT_DONE_MS))
    expect(closes()).toHaveLength(1)
  })

  for (const how of ['Back', 'Escape'] as const) {
    it(`${how} with the pointer inside: the floating sidebar keeps its tabs in place through the leave the narrowing panel sends, and retracts only once the pointer leaves`, () => {
      const { stub, el, closes, holds, closeLibrary, setProps } = mount([], {}, true)
      fireEvent.pointerEnter(el)
      fireEvent.pointerMove(el)
      if (how === 'Escape') {
        fireEvent.keyDown(window, { key: 'Escape' })
        expect(stub.calls.filter((c) => c.name === 'archive.close')).toHaveLength(1)
      }
      // Back's click, or Escape, closes the Library in the browser; the state comes back with it closed.
      closeLibrary()
      expect(el.dataset.held).toBe('false')
      expect(holds()).toEqual([{ held: true }, { held: false }])
      // The panel narrows from the Library's width and hands the keyboard back under the pointer: its widget sends a
      // leave. The pointer has not moved, so nothing goes.
      fireEvent.pointerLeave(el)
      act(() => vi.advanceTimersByTime((PEEK_GRACE_MS + PEEK_SLIDE_OUT_DONE_MS) * 4))
      expect(el.dataset.leaving).toBe('false')
      expect(closes()).toHaveLength(0)
      // Escape now that nothing holds it would close it at once; none came. The pointer moves on the tabs: still there.
      fireEvent.pointerMove(el)
      act(() => vi.advanceTimersByTime((PEEK_GRACE_MS + PEEK_SLIDE_OUT_DONE_MS) * 4))
      expect(closes()).toHaveLength(0)
      // It leaves for the page: the grace, the slide, then the close.
      fireEvent.pointerLeave(el)
      act(() => vi.advanceTimersByTime(PEEK_GRACE_MS - 1))
      expect(el.dataset.leaving).toBe('false')
      act(() => vi.advanceTimersByTime(1))
      expect(el.dataset.leaving).toBe('true')
      setProps({ leaving: true })
      act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_DONE_MS))
      expect(closes()).toEqual([{ name: 'shell.setSidebarPeek', payload: { open: false } }])
    })
  }

  it('Back from the Library releases it once, and main seeing the pointer gone with no leave event slides it out and closes it', () => {
    const { el, closes, holds, closeLibrary, setProps } = mount([], {}, true)
    fireEvent.pointerEnter(el)
    // Back: the Library closes under the pointer, which then moves off the narrowed panel with no event reaching it.
    closeLibrary()
    expect(holds()).toEqual([{ held: true }, { held: false }])
    expect(el.dataset.held).toBe('false')
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS * 4))
    expect(closes()).toHaveLength(0)
    setProps({ leaving: true })
    expect(el.dataset.leaving).toBe('true')
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_DONE_MS))
    expect(closes()).toEqual([{ name: 'shell.setSidebarPeek', payload: { open: false } }])
    expect(holds()).toHaveLength(2)
  })

  it("the pop-up's row menu or Rename holds it while the pointer is away", () => {
    const { el, closes, holds, setProps } = mount()
    expect(holds()).toEqual([])
    fireEvent.pointerEnter(el)
    act(() => holdLibraryPeek(true))
    fireEvent.pointerLeave(el)
    act(() => vi.advanceTimersByTime((PEEK_GRACE_MS + PEEK_SLIDE_OUT_MS) * 4))
    expect(closes()).toHaveLength(0)
    expect(holds()).toEqual([{ held: true }])
    act(() => holdLibraryPeek(false))
    act(() => vi.advanceTimersByTime(PEEK_GRACE_MS + PEEK_SLIDE_OUT_DONE_MS))
    expect(closes()).toHaveLength(0)
    setProps({ leaving: true })
    act(() => vi.advanceTimersByTime(PEEK_SLIDE_OUT_DONE_MS))
    expect(closes()).toHaveLength(1)
    expect(holds()).toEqual([{ held: true }, { held: false }])
  })

  it('a surface opened from elsewhere is no child of it', () => {
    const { el } = mount([surface(CARD, 'toast', null, 0)])
    expect(el.dataset.children).toBe('0')
  })
})
