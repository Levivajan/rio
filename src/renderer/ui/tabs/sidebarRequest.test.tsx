import { act, cleanup, render, renderHook } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultWindowState, type SidebarRequest } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { useCluState } from '../../common/useCluState'
import { setRenaming, useRenaming } from './renaming'
import { applySidebarRequest, useFloatingRenamePending, useSidebarRequest, useSidebarRequestBaseline } from './sidebarRequest'
import { useAnyRenaming } from './renaming'

// The applied seq outlives a list, so each case takes its own window, as a reloaded frame starts afresh.
let WIN = ''
let winSeq = 0
const SPACE = '00000000-0000-4000-8000-000000000002'
const FOLDER = '00000000-0000-4000-8000-0000000000f1'
const TAB = '00000000-0000-4000-8000-0000000000a1'

function setup() {
  winSeq += 1
  WIN = `00000000-0000-4000-8000-${String(winSeq).padStart(12, '0')}`
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  // The mirror listens from here on, so a push before the first render is not lost.
  stateMirror()
  let rev = 0
  const push = (sidebarRequest: SidebarRequest | null, library = false) =>
    act(() => {
      rev += 1
      stub.push({
        kind: 'snapshot',
        rev,
        state: {
          ...defaultState(),
          rev,
          archivePanel: library ? { [WIN]: true as const } : {},
          windows: { [WIN]: { ...defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE }), sidebarRequest } }
        }
      })
    })
  return { push }
}

// The sidebar's shape: the tab list that answers asks shows only with the Library closed.
function TabList() {
  useSidebarRequest(WIN, true)
  return <div data-folder-id={FOLDER} />
}
function SidebarShape() {
  const library = useCluState((s) => s.archivePanel[WIN] === true)
  useSidebarRequestBaseline(WIN)
  return library ? <nav data-library="" /> : <TabList />
}
// The floating sidebar's: its tab list answers the floating asks, and the peek counts a waiting rename as a hold.
function FloatingTabList() {
  useSidebarRequest(WIN, true, true)
  return <div data-folder-id={FOLDER} />
}
const holds: boolean[] = []
function FloatingShape() {
  const library = useCluState((s) => s.archivePanel[WIN] === true)
  const pending = useFloatingRenamePending(WIN)
  const renaming = useAnyRenaming()
  useSidebarRequestBaseline(WIN)
  holds.push(library || pending || renaming)
  return library ? <nav data-library="" /> : <FloatingTabList />
}

afterEach(() => {
  setRenaming(null)
  cleanup()
})

// Tabs ▸ New Folder… and Reveal Tab in Sidebar reach the sidebar through windows[id].sidebarRequest.
describe('sidebarRequest', () => {
  it('starts the new folder rename once per seq (Tabs ▸ New Folder…)', () => {
    const { push } = setup()
    push(null)
    renderHook(() => useSidebarRequest(WIN, true))
    const renaming = renderHook(() => useRenaming(FOLDER))
    expect(renaming.result.current).toBe(false)
    push({ seq: 1, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(true)
    // The same ask again (a later patch keeping it) does not restart a rename the user ended.
    act(() => setRenaming(null))
    push({ seq: 1, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(false)
    push({ seq: 2, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(true)
  })

  it('skips the ask that was there when the list mounted', () => {
    const { push } = setup()
    push({ seq: 4, kind: 'rename', nodeId: FOLDER })
    renderHook(() => useSidebarRequest(WIN, true))
    const renaming = renderHook(() => useRenaming(FOLDER))
    expect(renaming.result.current).toBe(false)
  })

  it('acts on the ask that closes the Library, though the list mounts from that patch', () => {
    const { push } = setup()
    push({ seq: 3, kind: 'rename', nodeId: FOLDER }, true)
    const view = render(<SidebarShape />)
    const renaming = renderHook(() => useRenaming(FOLDER))
    expect(view.container.querySelector('[data-library]')).not.toBeNull()
    // Tabs ▸ New Folder… closes the Library and asks in one update.
    push({ seq: 4, kind: 'rename', nodeId: FOLDER }, false)
    expect(view.container.querySelector('[data-library]')).toBeNull()
    expect(renaming.result.current).toBe(true)

    // Reveal Tab in Sidebar from the Library: the row mounts and scrolls into view.
    act(() => setRenaming(null))
    push({ seq: 4, kind: 'rename', nodeId: FOLDER }, true)
    const scroll = vi.fn()
    // jsdom has no scrollIntoView.
    Object.defineProperty(HTMLElement.prototype, 'scrollIntoView', { value: scroll, configurable: true })
    push({ seq: 5, kind: 'reveal', nodeId: FOLDER }, false)
    expect(scroll).toHaveBeenCalledWith({ block: 'nearest' })
    // Closing the Library with no new ask does nothing.
    scroll.mockClear()
    push({ seq: 5, kind: 'reveal', nodeId: FOLDER }, true)
    push({ seq: 5, kind: 'reveal', nodeId: FOLDER }, false)
    expect(scroll).not.toHaveBeenCalled()
    expect(renaming.result.current).toBe(false)
    Reflect.deleteProperty(HTMLElement.prototype, 'scrollIntoView')
  })

  it('skips the ask that was there when the sidebar first showed the Library', () => {
    const { push } = setup()
    push({ seq: 7, kind: 'rename', nodeId: FOLDER }, true)
    render(<SidebarShape />)
    const renaming = renderHook(() => useRenaming(FOLDER))
    push({ seq: 7, kind: 'rename', nodeId: FOLDER }, false)
    expect(renaming.result.current).toBe(false)
  })

  it('the floating sidebar answers the asks made while it was open, and the docked list only the others', () => {
    const { push } = setup()
    push(null)
    const floating = renderHook(() => useSidebarRequest(WIN, true, true))
    const renaming = renderHook(() => useRenaming(FOLDER))
    // The docked sidebar's ask is only marked seen here.
    push({ seq: 1, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(false)
    // Tabs ▸ New Folder… with the floating sidebar open: its row enters rename there.
    push({ seq: 2, kind: 'rename', nodeId: FOLDER, floating: true })
    expect(renaming.result.current).toBe(true)
    act(() => setRenaming(null))
    // Opened again later with that ask still in the state, it does not replay it, nor the docked list's next one.
    floating.unmount()
    renderHook(() => useSidebarRequest(WIN, true, true))
    push({ seq: 2, kind: 'rename', nodeId: FOLDER, floating: true })
    push({ seq: 3, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(false)
  })

  it('a floating rename keeps the floating sidebar held from the Library closing until the rename ends', () => {
    const { push } = setup()
    // The floating sidebar shows its Library: the tab list is not mounted.
    push(null, true)
    holds.length = 0
    render(<FloatingShape />)
    const renaming = renderHook(() => useRenaming(FOLDER))
    // Tabs ▸ New Folder…: the Library closes with the ask in the same patch, and the list mounts and starts the
    // rename. The hold never drops in between.
    push({ seq: 1, kind: 'rename', nodeId: FOLDER, floating: true }, false)
    expect(renaming.result.current).toBe(true)
    expect(holds.length).toBeGreaterThan(1)
    expect(holds.every(Boolean)).toBe(true)
    // The rename ends (committed or cancelled): nothing holds it any more.
    act(() => setRenaming(null))
    expect(holds.at(-1)).toBe(false)
    // A docked ask or a reveal never holds the floating sidebar this way.
    Object.defineProperty(HTMLElement.prototype, 'scrollIntoView', { value: vi.fn(), configurable: true })
    push({ seq: 2, kind: 'rename', nodeId: FOLDER })
    push({ seq: 3, kind: 'reveal', nodeId: FOLDER, floating: true })
    Reflect.deleteProperty(HTMLElement.prototype, 'scrollIntoView')
    expect(holds.at(-1)).toBe(false)
    const pending = renderHook(() => useFloatingRenamePending(WIN))
    expect(pending.result.current).toBe(false)
  })

  it('the docked list leaves an ask for the floating sidebar alone, then and once it shows again', () => {
    const { push } = setup()
    push(null)
    const docked = renderHook(() => useSidebarRequest(WIN, true))
    const renaming = renderHook(() => useRenaming(FOLDER))
    push({ seq: 1, kind: 'rename', nodeId: FOLDER, floating: true })
    expect(renaming.result.current).toBe(false)
    docked.unmount()
    renderHook(() => useSidebarRequest(WIN, true))
    push({ seq: 1, kind: 'rename', nodeId: FOLDER, floating: true })
    expect(renaming.result.current).toBe(false)
    push({ seq: 2, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(true)
  })

  it('a list that does not answer (the warm-up copies) never acts on an ask, at mount or later', () => {
    const { push } = setup()
    push(null)
    const floating = renderHook(() => useSidebarRequest(WIN, false))
    const renaming = renderHook(() => useRenaming(FOLDER))
    // The docked sidebar, in the frame's document, answers this one.
    push({ seq: 1, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(false)
    // The copy mounts again later, with the ask the frame already answered still in the state.
    floating.unmount()
    renderHook(() => useSidebarRequest(WIN, false))
    push({ seq: 1, kind: 'rename', nodeId: FOLDER })
    expect(renaming.result.current).toBe(false)
    const scroll = vi.fn()
    Object.defineProperty(HTMLElement.prototype, 'scrollIntoView', { value: scroll, configurable: true })
    render(<div data-folder-id={FOLDER} />)
    push({ seq: 2, kind: 'reveal', nodeId: FOLDER })
    expect(scroll).not.toHaveBeenCalled()
    Reflect.deleteProperty(HTMLElement.prototype, 'scrollIntoView')
  })

  it('scrolls the active row into view (Tabs ▸ Reveal Tab in Sidebar)', () => {
    const view = render(
      <nav>
        <div data-tab-id="other" />
        <div data-tab-id={TAB} />
      </nav>
    )
    const row = view.container.querySelector<HTMLElement>(`[data-tab-id="${TAB}"]`)
    if (!row) throw new Error('no row')
    const scroll = vi.fn()
    row.scrollIntoView = scroll
    applySidebarRequest({ seq: 1, kind: 'reveal', nodeId: TAB }, view.container)
    expect(scroll).toHaveBeenCalledWith({ block: 'nearest' })
    // A row the sidebar doesn't show (a junk id) does nothing.
    expect(() => applySidebarRequest({ seq: 2, kind: 'reveal', nodeId: 'x"]' }, view.container)).not.toThrow()
  })

  it('scrolls the split row that holds the tab into view', () => {
    const view = render(
      <nav>
        <div role="treeitem" data-split-id="split-1">
          <span data-split-member="other" />
          <span data-split-member={TAB} />
        </div>
      </nav>
    )
    const row = view.container.querySelector<HTMLElement>('[data-split-id="split-1"]')
    if (!row) throw new Error('no row')
    const scroll = vi.fn()
    row.scrollIntoView = scroll
    applySidebarRequest({ seq: 1, kind: 'reveal', nodeId: TAB }, view.container)
    expect(scroll).toHaveBeenCalledWith({ block: 'nearest' })
  })
})
