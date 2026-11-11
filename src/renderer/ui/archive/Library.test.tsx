// The Library panel over the stubbed window.clu: Downloads merges Rio's downloads with the Downloads
// folder, Media shows the screenshot folder, rows open and reveal by bare name, and search filters.
import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import type { LibraryFile } from '../../../shared/ipc/capabilities'
import { defaultState } from '../../../shared/state'
import type { DownloadState } from '../../../shared/state/capabilities'
import { createStateMirror, setStateMirrorForTest } from '../../common/store'
import { stubClu } from '../../../test-utils/stubClu'
import { Library, libraryOpenedFromButton } from './Library'
import { spacesLibraryWidth } from './LibrarySpaces'

const P = '00000000-0000-4000-8000-0000000000aa'
const S = '00000000-0000-4000-8000-0000000000bb'
const D = '00000000-0000-4000-8000-0000000000cc'
const now = Date.now()
const file = (name: string, minutesAgo: number, kind: LibraryFile['kind']): LibraryFile => ({ name, size: 2048, modifiedAt: now - minutesAgo * 60_000, kind })
const ours: DownloadState = {
  id: D, profileId: P, filename: 'invoice.pdf', url: 'https://a.test/invoice.pdf', state: 'completed', receivedBytes: 10, totalBytes: 10,
  bytesPerSecond: 0, etaMs: null, startedAt: now - 120_000, endedAt: now - 60_000, exists: true
}

const S2 = '00000000-0000-4000-8000-0000000000dd'
const archived = (id: string, spaceId: string, favicon: string | null) => ({
  id, profileId: P, spaceId, title: `Tab ${id}`, url: `https://a.test/${id}`, favicon, reason: 'close', archivedAt: now - 1000
})

function mount() {
  const folders: Record<string, LibraryFile[]> = {
    downloads: [file('invoice.pdf', 1, 'file'), file('Team offsite.key', 5, 'file')],
    media: [file('Screenshot 1.png', 2, 'image'), file('notes.txt', 1, 'file')]
  }
  const stub = stubClu({
    'download.files': (p) => folders[(p as { source: string }).source],
    'download.fileAct': () => null,
    'download.act': () => null,
    'download.thumb': (p) => ((p as { name: string }).name === 'Screenshot 1.png' ? 'data:image/png;base64,iVBORw0KGgo=' : 'javascript:alert(1)'),
    'archive.list': () => ({ rows: [archived('a1', S, 'https://a.test/1'), archived('a2', S2, null)], nextCursor: null }),
    'archive.close': () => null,
    'archive.setWidth': () => null,
    'spaces.switch': () => null,
    'spaces.reorder': () => null,
    'spaces.openIconMenu': () => null,
    'tabs.activate': () => null,
    'tabs.openMenu': () => ({ rename: null }),
    'theme.openPicker': () => null,
    'commands.run': () => null,
    'tabs.create': () => ({ tabId: 't' }),
    'tweaks.list': () => [
      { site: 'news.example.com', enabled: true, hasCss: true, hasJs: false, hasControls: false, zapCount: 0, updatedAt: 1 },
      { site: 'docs.example.org', enabled: false, hasCss: false, hasJs: true, hasControls: false, zapCount: 0, updatedAt: 1 },
      { site: '', enabled: true, hasCss: false, hasJs: false, hasControls: false, zapCount: 0, updatedAt: 1 }
    ]
  })
  window.clu = stub.clu
  const s = defaultState()
  const windowId = stub.clu.windowId ?? ''
  const state = {
    ...s,
    rev: 1,
    downloads: [ours],
    archivePanel: { [windowId]: true as const },
    windows: { [windowId]: { activeSpaceId: S } },
    spaceOrder: [S, S2],
    spaces: {
      [S]: { id: S, profileId: P, name: 'Home', icon: { kind: 'emoji', value: '🪐' }, color: 'blue', theme: null, pinned: [{ kind: 'tab', id: 'pt1' }, { kind: 'folder', id: 'f1' }, { kind: 'tab', id: 'gone' }] },
      [S2]: { id: S2, profileId: P, name: 'Work', icon: { kind: 'letter' }, color: 'red', theme: { colors: [{ h: 140, s: 0.5 }, { h: 30, s: 0.8 }], intensity: 0.5, grain: 0.2 }, pinned: [] }
    },
    tabs: { pt1: { id: 'pt1', spaceId: S, section: 'pinned', title: 'Calendar', url: 'https://cal.test/', favicon: null, loading: false } },
    folders: { f1: { id: 'f1', spaceId: S, name: 'Providers', icon: null, collapsed: true, children: [] } },
    profiles: { [P]: { id: P, name: 'Personal' } }
  } as unknown as typeof s
  const mirror = createStateMirror(async () => state)
  mirror.receive({ kind: 'snapshot', rev: 1, state })
  setStateMirrorForTest(mirror)
  render(<Library />)
  return Object.assign(stub, { mirror, windowId })
}

describe('Library', () => {
  it('has Media, Downloads, Spaces, Site tweaks and Archived Tabs in the rail, in the design order', () => {
    mount()
    expect(screen.getAllByRole('tab').map((t) => t.textContent)).toEqual(['Media', 'Downloads', 'Spaces', 'Site tweaks', 'Archived Tabs'])
  })

  it('from the button opens on Downloads, then the last section; the archive command opens Archived Tabs; Esc does nothing', async () => {
    libraryOpenedFromButton()
    mount()
    expect(screen.getByRole('tab', { name: 'Downloads' })).toHaveAttribute('aria-selected', 'true')
    fireEvent.click(screen.getByRole('tab', { name: 'Media' }))
    cleanup()
    libraryOpenedFromButton()
    mount()
    expect(screen.getByRole('tab', { name: 'Media' })).toHaveAttribute('aria-selected', 'true')
    cleanup()
    const stub = mount()
    expect(screen.getByRole('tab', { name: 'Archived Tabs' })).toHaveAttribute('aria-selected', 'true')
    fireEvent.keyDown(document.body, { key: 'Escape' })
    expect(stub.calls.some((c) => c.name === 'archive.close')).toBe(false)
    fireEvent.click(screen.getByRole('button', { name: 'Back to tabs' }))
    expect(stub.calls.some((c) => c.name === 'archive.close')).toBe(true)
  })

  it('Downloads lists the folder files and Rio downloads once, and opens or reveals them', async () => {
    const stub = mount()
    fireEvent.click(screen.getByRole('tab', { name: 'Downloads' }))
    await waitFor(() => expect(screen.getByText('Team offsite.key')).toBeInTheDocument())
    expect(screen.getAllByText('invoice.pdf')).toHaveLength(1)
    expect(stub.calls.find((c) => c.name === 'download.files')?.payload).toEqual({ source: 'downloads' })

    fireEvent.click(screen.getByText('Team offsite.key'))
    expect(stub.calls.find((c) => c.name === 'download.fileAct')?.payload).toEqual({ source: 'downloads', name: 'Team offsite.key', action: 'open' })
    fireEvent.click(screen.getByText('invoice.pdf'))
    expect(stub.calls.find((c) => c.name === 'download.act')?.payload).toEqual({ id: D, action: 'open' })

    const row = screen.getByText('Team offsite.key').closest('li') as HTMLElement
    // Kind and source under the name; a thumbnail that is not an image URL leaves the glyph.
    expect(row).toHaveTextContent('Download')
    expect(screen.getByText('invoice.pdf').closest('li')).toHaveTextContent('PDF from a.test')
    await waitFor(() => expect(stub.calls.some((c) => c.name === 'download.thumb' && (c.payload as { name: string }).name === 'Team offsite.key')).toBe(true))
    expect(row.querySelector('img')).toBeNull()
    fireEvent.click(within(row).getByRole('button', { name: 'Show in Finder' }))
    expect(stub.calls.filter((c) => c.name === 'download.fileAct').at(-1)?.payload).toEqual({ source: 'downloads', name: 'Team offsite.key', action: 'reveal' })

    fireEvent.change(screen.getByRole('searchbox', { name: 'Search Downloads' }), { target: { value: 'team' } })
    expect(screen.queryByText('invoice.pdf')).not.toBeInTheDocument()
    expect(screen.getByText('Team offsite.key')).toBeInTheDocument()
  })

  it('Media shows the screenshot folder images as tiles', async () => {
    const stub = mount()
    fireEvent.click(screen.getByRole('tab', { name: 'Media' }))
    const tile = await screen.findByRole('button', { name: 'Screenshot 1.png' })
    expect(screen.queryByText('notes.txt')).not.toBeInTheDocument()
    await waitFor(() => expect(tile.querySelector('img')).toHaveAttribute('src', 'data:image/png;base64,iVBORw0KGgo='))
    // The framed thumbnail sits on its tile with no name under it; the tooltip and the label carry the name.
    expect(tile.querySelector('.clu-library-thumb > img')).not.toBeNull()
    expect(tile).not.toHaveTextContent('Screenshot 1.png')
    expect(stub.calls.find((c) => c.name === 'download.thumb')?.payload).toEqual({ source: 'media', name: 'Screenshot 1.png' })
    fireEvent.click(tile)
    expect(stub.calls.find((c) => c.name === 'download.fileAct')?.payload).toEqual({ source: 'media', name: 'Screenshot 1.png', action: 'open' })
  })

  it('a Media tile shows its full name in a tooltip on hover', async () => {
    mount()
    fireEvent.click(screen.getByRole('tab', { name: 'Media' }))
    const tile = await screen.findByRole('button', { name: 'Screenshot 1.png' })
    fireEvent.pointerEnter(tile.parentElement as HTMLElement, { clientX: 10, clientY: 10 })
    expect(await screen.findByRole('tooltip', {}, { timeout: 2000 })).toHaveTextContent('Screenshot 1.png')
  })

  it('right-click on a Media tile offers the item menu, and each item acts on the file by bare name', async () => {
    const stub = mount()
    fireEvent.click(screen.getByRole('tab', { name: 'Media' }))
    const tile = (await screen.findByRole('button', { name: 'Screenshot 1.png' })).closest('li') as HTMLElement
    const menuFor = () => {
      fireEvent.contextMenu(tile, { clientX: 40, clientY: 50 })
      return screen.getByRole('menu')
    }
    expect(within(menuFor()).getAllByRole('menuitem').map((m) => m.textContent)).toEqual([
      'Open', 'Show in Finder', 'Copy', 'Copy Path', 'Share…', 'Rename…', 'Move to Trash'
    ])
    const last = () => stub.calls.filter((c) => c.name === 'download.fileAct').at(-1)?.payload
    fireEvent.click(within(screen.getByRole('menu')).getByRole('menuitem', { name: 'Show in Finder' }))
    expect(screen.queryByRole('menu')).not.toBeInTheDocument()
    expect(last()).toEqual({ source: 'media', name: 'Screenshot 1.png', action: 'reveal' })
    fireEvent.click(within(menuFor()).getByRole('menuitem', { name: 'Copy' }))
    expect(last()).toEqual({ source: 'media', name: 'Screenshot 1.png', action: 'copy' })
    fireEvent.click(within(menuFor()).getByRole('menuitem', { name: 'Copy Path' }))
    expect(last()).toEqual({ source: 'media', name: 'Screenshot 1.png', action: 'copyPath' })
    fireEvent.click(within(menuFor()).getByRole('menuitem', { name: 'Share…' }))
    expect(last()).toMatchObject({ source: 'media', name: 'Screenshot 1.png', action: 'share', anchor: { x: 0, y: 0 } })

    const reads = () => stub.calls.filter((c) => c.name === 'download.files').length
    const before = reads()
    fireEvent.click(within(menuFor()).getByRole('menuitem', { name: 'Move to Trash' }))
    expect(last()).toEqual({ source: 'media', name: 'Screenshot 1.png', action: 'trash' })
    await waitFor(() => expect(reads()).toBeGreaterThan(before))
  })

  it('Cmd+C on a focused Downloads row or Media tile copies the file; other chords, the rename field and an open menu do not', async () => {
    const stub = mount()
    const copies = () => stub.calls.filter((c) => c.name === 'download.fileAct' && (c.payload as { action: string }).action === 'copy').map((c) => c.payload)
    fireEvent.click(screen.getByRole('tab', { name: 'Downloads' }))
    const open = (await screen.findByText('Team offsite.key')).closest('button') as HTMLElement
    fireEvent.keyDown(open, { key: 'c', metaKey: true, shiftKey: true })
    fireEvent.keyDown(open, { key: 'c', ctrlKey: true })
    fireEvent.keyDown(open, { key: 'v', metaKey: true })
    expect(copies()).toEqual([])
    const press = fireEvent.keyDown(open, { key: 'c', metaKey: true })
    expect(press).toBe(false)
    expect(copies()).toEqual([{ source: 'downloads', name: 'Team offsite.key', action: 'copy' }])

    fireEvent.click(screen.getByRole('tab', { name: 'Media' }))
    const tile = await screen.findByRole('button', { name: 'Screenshot 1.png' })
    fireEvent.keyDown(tile, { key: 'C', metaKey: true })
    expect(copies().at(-1)).toEqual({ source: 'media', name: 'Screenshot 1.png', action: 'copy' })

    const li = tile.closest('li') as HTMLElement
    fireEvent.contextMenu(li, { clientX: 40, clientY: 50 })
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'c', metaKey: true })
    expect(copies()).toHaveLength(2)
    fireEvent.click(within(screen.getByRole('menu')).getByRole('menuitem', { name: 'Rename…' }))
    fireEvent.keyDown(screen.getByRole('textbox', { name: 'Rename Screenshot 1.png' }), { key: 'c', metaKey: true })
    expect(copies()).toHaveLength(2)
  })

  it('Rename edits the name in place: Enter renames, Esc and an unchanged or bad name do not', async () => {
    const stub = mount()
    fireEvent.click(screen.getByRole('tab', { name: 'Downloads' }))
    const row = (await screen.findByText('Team offsite.key')).closest('li') as HTMLElement
    const renames = () => stub.calls.filter((c) => c.name === 'download.fileAct' && (c.payload as { action: string }).action === 'rename')
    const startRename = () => {
      fireEvent.contextMenu(row, { clientX: 40, clientY: 50 })
      fireEvent.click(within(screen.getByRole('menu')).getByRole('menuitem', { name: 'Rename…' }))
      return screen.getByRole('textbox', { name: 'Rename Team offsite.key' }) as HTMLInputElement
    }
    let field = startRename()
    expect(field.value).toBe('Team offsite.key')
    expect([field.selectionStart, field.selectionEnd]).toEqual([0, 'Team offsite'.length])
    fireEvent.change(field, { target: { value: 'Offsite.key' } })
    fireEvent.keyDown(field, { key: 'Escape' })
    expect(screen.queryByRole('textbox', { name: /^Rename/ })).not.toBeInTheDocument()
    for (const bad of ['Team offsite.key', '', '.hidden', 'a/b.key']) {
      field = startRename()
      fireEvent.change(field, { target: { value: bad } })
      fireEvent.keyDown(field, { key: 'Enter' })
    }
    expect(renames()).toHaveLength(0)
    field = startRename()
    fireEvent.change(field, { target: { value: ' Offsite 2026.key ' } })
    fireEvent.keyDown(field, { key: 'Enter' })
    fireEvent.blur(field)
    expect(renames().map((c) => c.payload)).toEqual([{ source: 'downloads', name: 'Team offsite.key', action: 'rename', newName: 'Offsite 2026.key' }])
  })

  it('Filter narrows Downloads by kind and the archive by space; a row without a favicon shows a blank square', async () => {
    mount()
    fireEvent.click(screen.getByRole('tab', { name: 'Downloads' }))
    await waitFor(() => expect(screen.getByText('Team offsite.key')).toBeInTheDocument())
    fireEvent.click(screen.getByRole('button', { name: 'Filter' }))
    fireEvent.click(screen.getByRole('menuitem', { name: 'Images' }))
    expect(screen.queryByText('Team offsite.key')).not.toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Images' })).toHaveAttribute('aria-haspopup', 'menu')

    fireEvent.click(screen.getByRole('tab', { name: 'Archived Tabs' }))
    await waitFor(() => expect(screen.getByText('Tab a2')).toBeInTheDocument())
    const blank = screen.getByText('Tab a2').closest('li')?.querySelector('.clu-archive-blank')
    expect(blank).not.toBeNull()
    fireEvent.click(screen.getByRole('button', { name: 'Filter' }))
    expect(screen.getAllByRole('menuitem').map((m) => m.textContent)).toEqual(['All Spaces', 'Home', 'Work'])
    fireEvent.click(screen.getByRole('menuitem', { name: 'Work' }))
    expect(screen.queryByText('Tab a1')).not.toBeInTheDocument()
    expect(screen.getByText('Tab a2')).toBeInTheDocument()
  })

  it('says so when a folder lists nothing or cannot be read', async () => {
    const stub = stubClu({ 'download.files': () => { throw new Error('denied') } })
    window.clu = stub.clu
    render(<Library />)
    fireEvent.click(screen.getByRole('tab', { name: 'Media' }))
    expect(await screen.findByText('No screenshots or recordings')).toBeInTheDocument()
  })

  it('Spaces shows a tinted card per space with its header, pinned rows, foot and +; Site tweaks lists and opens sites', async () => {
    const stub = mount()
    fireEvent.click(screen.getByRole('tab', { name: 'Spaces' }))
    const cards = [...document.querySelectorAll<HTMLElement>('.clu-space-card')]
    expect(cards.map((c) => c.dataset.spaceId)).toEqual([S, S2])
    // Each card wears its own theme: Home the default gradient of blue, Work its two-color theme.
    expect(cards[0]?.style.background).toMatch(/gradient/)
    expect(cards[1]?.style.background).toMatch(/gradient/)
    expect(cards[0]?.style.background).not.toBe(cards[1]?.style.background)
    const home = within(cards[0] as HTMLElement)
    expect(home.getByRole('button', { name: 'Change Space Icon' })).toHaveTextContent('🪐')
    expect(cards[0]?.querySelector('.clu-space-card-profile')).toHaveTextContent('Personal')
    // Pinned rows in order; a row whose tab is gone is skipped.
    expect([...(cards[0] as HTMLElement).querySelectorAll('.clu-space-card-row-title')].map((r) => r.textContent)).toEqual(['Calendar', 'Providers'])

    fireEvent.click(home.getByRole('button', { name: 'Edit Theme Color' }))
    expect(stub.calls.find((c) => c.name === 'theme.openPicker')?.payload).toMatchObject({ spaceId: S })
    fireEvent.click(home.getByRole('button', { name: 'Change Space Icon' }))
    expect(stub.calls.find((c) => c.name === 'spaces.openIconMenu')?.payload).toMatchObject({ spaceId: S })
    fireEvent.click(home.getByRole('button', { name: 'Space options' }))
    expect(stub.calls.find((c) => c.name === 'tabs.openMenu')?.payload).toMatchObject({ menu: 'space', nodeId: S })
    fireEvent.click(home.getByRole('button', { name: 'Calendar' }))
    expect(stub.calls.find((c) => c.name === 'tabs.activate')?.payload).toEqual({ tabId: 'pt1' })

    // The move handle drags the card onto another to reorder.
    const data = new Map<string, string>()
    const dataTransfer = {
      get types() { return [...data.keys()] },
      setData: (k: string, v: string) => data.set(k, v),
      getData: (k: string) => data.get(k) ?? '',
      setDragImage: () => undefined,
      effectAllowed: '',
      dropEffect: ''
    }
    fireEvent.dragStart(within(cards[1] as HTMLElement).getByRole('button', { name: 'Move Space' }), { dataTransfer })
    fireEvent.dragOver(cards[0] as HTMLElement, { dataTransfer })
    fireEvent.drop(cards[0] as HTMLElement, { dataTransfer })
    expect(stub.calls.find((c) => c.name === 'spaces.reorder')?.payload).toEqual({ spaceId: S2, toIndex: 0 })

    fireEvent.click(screen.getByRole('button', { name: 'New Space' }))
    expect(stub.calls.find((c) => c.name === 'commands.run')?.payload).toEqual({ id: 'spaces.newSpace' })

    fireEvent.click(within(cards[1] as HTMLElement).getByRole('button', { name: 'Work' }))
    expect(stub.calls.findLast((c) => c.name === 'spaces.switch')?.payload).toEqual({ spaceId: S2 })
    expect(stub.calls.some((c) => c.name === 'archive.close')).toBe(true)

    fireEvent.click(screen.getByRole('tab', { name: 'Site tweaks' }))
    const row = await screen.findByRole('button', { name: /news\.example\.com/ })
    expect(row).toHaveTextContent('CSS')
    expect(screen.getByRole('button', { name: /docs\.example\.org/ })).toHaveTextContent('Off')
    expect(stub.calls.find((c) => c.name === 'tweaks.list')?.payload).toEqual({ profileId: P })
    fireEvent.click(row)
    expect(stub.calls.find((c) => c.name === 'tabs.create')?.payload).toEqual({ url: 'https://news.example.com/', activate: true })
  })

  it('Spaces widens the Library until every card and the + fit, asks again on reopen, and gives the width back when another section shows', () => {
    // Laid-out widths: a slide's transform moves the rects but never these.
    const widths: Record<string, number> = { 'clu-sidebar-frame': 417, 'clu-space-card': 163, 'clu-space-cards-add': 44 }
    const offset = vi.spyOn(HTMLElement.prototype, 'offsetWidth', 'get').mockImplementation(function (this: HTMLElement) {
      return widths[Object.keys(widths).find((k) => this.classList.contains(k)) ?? ''] ?? 0
    })
    const client = vi.spyOn(Element.prototype, 'clientWidth', 'get').mockImplementation(function (this: Element) {
      return this.classList.contains('clu-space-cards') ? 329 : 0
    })
    try {
      const stub = mount()
      document.querySelector('.clu-library')?.parentElement?.classList.add('clu-sidebar-frame')
      fireEvent.click(screen.getByRole('tab', { name: 'Spaces' }))
      // 88 of frame outside the row, two 163 cards and the 44 "+" column, plus the row's own padding and two gaps.
      const css = getComputedStyle(document.querySelector('.clu-space-cards') as Element)
      const own = (parseFloat(css.paddingLeft) || 0) + (parseFloat(css.paddingRight) || 0) + 2 * (parseFloat(css.columnGap) || 0)
      const asks = () => stub.calls.filter((c) => c.name === 'archive.setWidth').map((c) => c.payload)
      expect(asks()).toEqual([{ width: Math.ceil(458 + own) }])
      // The browser forgets the width on close; the same view still mounted from the close slide asks again on reopen.
      act(() => stub.mirror.receive({ kind: 'patch', rev: 2, ops: [{ op: 'set', path: ['archivePanel', stub.windowId], value: false }] }))
      expect(asks()).toHaveLength(1)
      act(() => stub.mirror.receive({ kind: 'patch', rev: 3, ops: [{ op: 'set', path: ['archivePanel', stub.windowId], value: true }] }))
      expect(asks()).toEqual([{ width: Math.ceil(458 + own) }, { width: Math.ceil(458 + own) }])
      // A close and reopen in one patch: only the browser's width changes back to 417, and the view asks again.
      act(() => stub.mirror.receive({ kind: 'patch', rev: 4, ops: [{ op: 'set', path: ['windows', stub.windowId, 'shell'], value: { layout: { sidebarRect: { x: 0, y: 0, width: 417, height: 800 } } } }] }))
      expect(asks()).toHaveLength(3)
      expect(asks().at(-1)).toEqual({ width: Math.ceil(458 + own) })
      fireEvent.click(screen.getByRole('tab', { name: 'Downloads' }))
      expect(asks().at(-1)).toEqual({ width: 0 })
    } finally {
      offset.mockRestore()
      client.mockRestore()
      cleanup()
    }
  })

  it('spacesLibraryWidth adds the room outside the row, its padding, items and gaps; junk measures count as 0', () => {
    expect(spacesLibraryWidth({ frame: 417, row: 335, padding: 16, gap: 16, items: [163, 163, 163, 44] })).toBe(82 + 16 + 163 * 3 + 44 + 48)
    // Five spaces fit a 1440 window's 1080 of Library: 112 of rail, the row's 16 of padding.
    expect(spacesLibraryWidth({ frame: 417, row: 305, padding: 16, gap: 16, items: [163, 163, 163, 163, 163, 44] })).toBeLessThanOrEqual(1440 - 360)
    expect(spacesLibraryWidth({ frame: 300, row: 226, padding: 0, gap: 16, items: [] })).toBe(74)
    // A row wider than its frame never subtracts; junk measures count as 0.
    expect(spacesLibraryWidth({ frame: 300, row: 900, padding: 0, gap: 0, items: [10] })).toBe(10)
    expect(spacesLibraryWidth({ frame: Number.NaN, row: 300, padding: -4, gap: Number.POSITIVE_INFINITY, items: [Number.NaN, 10] })).toBe(10)
  })
})
