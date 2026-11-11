import { act, cleanup, fireEvent, render } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { FOLDER_PEEK } from '../../../shared/tabs'
import { defaultState, defaultWindowState, type FolderState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { folderPeekCancel } from './folderPeek'
import { FolderRow } from './FolderRow'

const WIN = '00000000-0000-4000-8000-000000000001'
const SPACE = '00000000-0000-4000-8000-000000000002'
const YC = '00000000-0000-4000-8000-0000000000f1'
const PROVIDERS = '00000000-0000-4000-8000-0000000000f2'
const folder = (id: string, collapsed: boolean): FolderState => ({ id, spaceId: SPACE, name: id === YC ? 'YC' : 'Providers', icon: null, collapsed, children: [] })

function mount(folders: FolderState[]) {
  const stub = stubClu({ 'folders.peek': () => null, 'folders.setCollapsed': () => null, 'tabs.openMenu': () => ({ rename: null }) })
  window.clu = { ...stub.clu, windowId: WIN }
  const view = render(
    <nav className="clu-sidebar">
      {folders.map((f) => (
        <FolderRow key={f.id} folder={f} level={1} dragEnabled={false} />
      ))}
    </nav>
  )
  act(() =>
    stub.push({
      kind: 'snapshot',
      rev: 1,
      state: { ...defaultState(), rev: 1, folders: Object.fromEntries(folders.map((f) => [f.id, f])), windows: { [WIN]: defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE }) } }
    })
  )
  const row = (id: string) => {
    const el = view.container.querySelector<HTMLElement>(`[data-folder-id="${id}"]`)
    if (!el) throw new Error(`no row ${id}`)
    return el
  }
  const calls = (name: string) => stub.calls.filter((c) => c.name === name).map((c) => c.payload)
  return { row, calls }
}

beforeEach(() => vi.useFakeTimers())
afterEach(() => {
  folderPeekCancel()
  vi.useRealTimers()
  cleanup()
})

// A left click on a folder, its glyph included, only opens or closes it; the menu is the right-click's.
describe('FolderRow clicks', () => {
  it('toggles on a left click on the open folder glyph and never opens the folder menu', () => {
    const { row, calls } = mount([folder(YC, false)])
    const glyph = row(YC).querySelector('.clu-folder-glyph')
    expect(glyph?.tagName).toBe('SPAN')
    expect(row(YC).querySelector('[aria-label="Folder options"]')).toBeNull()
    fireEvent.click(glyph as Element, { detail: 1 })
    expect(calls('tabs.openMenu')).toEqual([])
    expect(calls('folders.setCollapsed')).toEqual([{ folderId: YC, collapsed: true }])
  })

  it('toggles on a left click on a collapsed row and opens the menu only on a right-click', () => {
    const { row, calls } = mount([folder(YC, true)])
    fireEvent.click(row(YC), { detail: 1 })
    expect(calls('tabs.openMenu')).toEqual([])
    expect(calls('folders.setCollapsed')).toEqual([{ folderId: YC, collapsed: false }])
    fireEvent.contextMenu(row(YC), { clientX: 40, clientY: 50 })
    expect(calls('tabs.openMenu')).toHaveLength(1)
  })
})

// The popover is for the hovered folder only and closes on leave, even after the browser kept it held.
describe('FolderRow popover', () => {
  it('opens for the hovered collapsed folder and asks to close when the pointer leaves', () => {
    const { row, calls } = mount([folder(YC, true)])
    fireEvent.pointerEnter(row(YC))
    vi.advanceTimersByTime(FOLDER_PEEK.delayMs)
    expect(calls('folders.peek')).toEqual([expect.objectContaining({ folderId: YC, rect: expect.any(Object) })])
    fireEvent.pointerLeave(row(YC))
    vi.advanceTimersByTime(FOLDER_PEEK.closeMs)
    expect(calls('folders.peek').at(-1)).toEqual({ folderId: null })
  })

  it('closes the last folder\'s popover by name when another folder row is hovered, open or collapsed', () => {
    const { row, calls } = mount([folder(YC, true), folder(PROVIDERS, false)])
    fireEvent.pointerEnter(row(YC))
    vi.advanceTimersByTime(FOLDER_PEEK.delayMs)
    // The pointer went over the popover (held) and back: the frame's close request came and went.
    fireEvent.pointerLeave(row(YC))
    vi.advanceTimersByTime(FOLDER_PEEK.closeMs)
    fireEvent.pointerEnter(row(PROVIDERS))
    expect(calls('folders.peek').at(-1)).toEqual({ folderId: YC, held: false })
  })

  it('closes a held popover on a press after the close request on leaving', () => {
    const { row, calls } = mount([folder(YC, true), folder(PROVIDERS, false)])
    fireEvent.pointerEnter(row(YC))
    vi.advanceTimersByTime(FOLDER_PEEK.delayMs)
    fireEvent.pointerLeave(row(YC))
    vi.advanceTimersByTime(FOLDER_PEEK.closeMs)
    fireEvent.pointerDown(row(PROVIDERS), { button: 0 })
    expect(calls('folders.peek').at(-1)).toEqual({ folderId: YC, held: false })
  })

  it('switches to the other collapsed folder after its delay', () => {
    const { row, calls } = mount([folder(YC, true), folder(PROVIDERS, true)])
    fireEvent.pointerEnter(row(YC))
    vi.advanceTimersByTime(FOLDER_PEEK.delayMs)
    fireEvent.pointerLeave(row(YC))
    fireEvent.pointerEnter(row(PROVIDERS))
    vi.advanceTimersByTime(FOLDER_PEEK.delayMs)
    const peeks = calls('folders.peek')
    expect(peeks).toContainEqual({ folderId: YC, held: false })
    expect(peeks.at(-1)).toEqual(expect.objectContaining({ folderId: PROVIDERS, rect: expect.any(Object) }))
  })
})
