import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState } from '../../../shared/state'
import type { SplitView } from '../../../shared/split'
import { stubClu } from '../../../test-utils/stubClu'
import { titlebarPad, titlebarShift, TopRow } from './TopRow'

const WIN = '00000000-0000-4000-8000-000000000001'
const A = '00000000-0000-4000-8000-00000000000a'
const B = '00000000-0000-4000-8000-00000000000b'
const RECT = { x: 214, y: 10, width: 500, height: 780 }

function mount(split: boolean, width = 214) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  render(<TopRow />)
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.shell.layout.sidebarRect = { ...win.shell.layout.sidebarRect, width }
  if (split) {
    const view: SplitView = { splitId: 'S', panes: [A, B].map((tabId) => ({ tabId, rect: RECT, bar: { ...RECT, height: 0 } })), dividers: [] }
    win.splitView = view
  }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
}

describe('TopRow', () => {
  // In full screen the row moves down with the bar macOS reveals at the screen's top, and back up.
  it('publishes what the revealed full-screen bar covers and draws no shift of its own over the column', () => {
    const stub = stubClu({ 'shell.toggleSidebar': () => null })
    window.clu = { ...stub.clu, windowId: WIN }
    const view = render(<TopRow />)
    const row = () => view.container.querySelector<HTMLElement>('[data-surface="top-row"]') as HTMLElement
    const push = (rev: number, titlebarReveal: number | undefined) => {
      const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
      win.fullscreen = true
      win.shell.titlebarReveal = titlebarReveal
      act(() => stub.push({ kind: 'snapshot', rev, state: { ...defaultState(), rev, windows: { [WIN]: win } } }))
    }
    push(1, 0)
    expect(row().dataset.titlebarReveal).toBe('0')
    push(2, 26)
    expect(row().dataset.titlebarReveal).toBe('26')
    push(3, 52)
    expect(row().dataset.titlebarReveal).toBe('52')
    // The row alone never moves over the address pill and the favorites; the sidebar pads the whole column.
    expect(row().style.transform).toBe('')
    // Still usable where it moved to.
    fireEvent.click(screen.getByRole('button', { name: 'Hide sidebar' }))
    expect(stub.calls.some((c) => c.name === 'shell.toggleSidebar')).toBe(true)
    push(4, undefined)
    expect(row().dataset.titlebarReveal).toBe('0')
  })

  it('pads the whole sidebar column by the bar, from its own top padding, and nothing once it hides', () => {
    expect(titlebarPad(0)).toBeUndefined()
    expect(titlebarPad(52)).toEqual({ paddingTop: 'calc(var(--top-row-y) + 52px)' })
    expect(titlebarPad(titlebarShift(24.4), '0px')).toEqual({ paddingTop: 'calc(0px + 24px)' })
    expect(titlebarPad(titlebarShift(Number.NaN))).toBeUndefined()
  })

  it('reads junk as no shift', () => {
    for (const junk of [Number.NaN, -5, '30', null, undefined, Number.POSITIVE_INFINITY]) expect(titlebarShift(junk)).toBe(0)
    expect(titlebarShift(1e6)).toBe(200)
    expect(titlebarShift(27.6)).toBe(28)
  })

  it('shows the sidebar toggle, back, forward and reload', () => {
    mount(false)
    expect(screen.getByRole('button', { name: 'Hide sidebar' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Go back' })).toBeInTheDocument()
  })

  // Glyphs are drawn at absolute sizes, so nothing is scaled by the traffic lights (14 / 12).
  it('draws the toggle, back, forward and reload in their unscaled 20, 16, 16 and 18 boxes', () => {
    mount(false)
    const box = (name: string) => screen.getByRole('button', { name }).querySelector('svg')
    expect(box('Hide sidebar')?.getAttribute('width')).toBe('20')
    expect(box('Go back')?.getAttribute('width')).toBe('16')
    expect(box('Go forward')?.getAttribute('height')).toBe('16')
    expect(box('Reload')?.getAttribute('width')).toBe('18')
  })

  it('keeps only the toggle in split view, where each pane has its own back, forward and reload', () => {
    mount(true)
    expect(screen.getByRole('button', { name: 'Hide sidebar' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Go back' })).toBeNull()
    expect(screen.queryByRole('button', { name: 'Reload' })).toBeNull()
  })

  // The breakpoint: up to 211 the toggle and reload fold into "...".
  it('folds the toggle and reload into "..." at 211 and shows them again at 212', () => {
    mount(false, 211)
    expect(screen.queryByRole('button', { name: 'Hide sidebar' })).toBeNull()
    expect(screen.queryByRole('button', { name: 'Reload' })).toBeNull()
    expect(screen.getByRole('button', { name: 'Go back' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Go forward' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'More options' })).toHaveAttribute('aria-haspopup', 'menu')
  })

  it('opens the "..." menu under its button', async () => {
    const stub = stubClu()
    window.clu = { ...stub.clu, windowId: WIN }
    render(<TopRow />)
    const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
    win.shell.layout.sidebarRect = { ...win.shell.layout.sidebarRect, width: 180 }
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
    await act(async () => screen.getByRole('button', { name: 'More options' }).click())
    expect(stub.calls.filter((c) => c.name === 'tabs.openMenu').map((c) => (c.payload as { menu: string }).menu)).toEqual(['more'])
  })

  it('keeps the toggle, back, forward and reload at 212 and at 500', () => {
    for (const w of [212, 500]) {
      mount(false, w)
      expect(screen.getByRole('button', { name: 'Hide sidebar' })).toBeInTheDocument()
      expect(screen.getByRole('button', { name: 'Reload' })).toBeInTheDocument()
      expect(screen.queryByRole('button', { name: 'More options' })).toBeNull()
      cleanup()
    }
  })
  // The reload swaps reload for stop while the page loads; the reload button that comes back must not turn again.
  it('reloads once and turns once per click, and the button back after the load stays at rest', async () => {
    const stub = stubClu({ 'nav.reload': () => null })
    window.clu = { ...stub.clu, windowId: WIN }
    render(<TopRow />)
    const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
    win.activeTabId = A
    const state = (rev: number, loading: boolean) => {
      const tab = { ...defaultTabState({ id: A, profileId: WIN, spaceId: WIN, section: 'today', url: 'https://example.com/' }), loading }
      return { ...defaultState(), rev, windows: { [WIN]: win }, tabs: { [A]: tab } }
    }
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: state(1, false) }))
    const glyph = () => screen.getByRole('button', { name: 'Reload' }).querySelector('svg')
    expect(glyph()?.classList.contains('clu-reload-spin')).toBe(false)
    await act(async () => screen.getByRole('button', { name: 'Reload' }).click())
    expect(stub.calls.filter((c) => c.name === 'nav.reload').map((c) => c.payload)).toEqual([{ tabId: A }])
    expect(glyph()?.classList.contains('clu-reload-spin')).toBe(true)
    act(() => stub.push({ kind: 'snapshot', rev: 2, state: state(2, true) }))
    expect(screen.getByRole('button', { name: 'Stop' })).toBeInTheDocument()
    act(() => stub.push({ kind: 'snapshot', rev: 3, state: state(3, false) }))
    expect(glyph()?.classList.contains('clu-reload-spin')).toBe(false)
    expect(stub.calls.filter((c) => c.name === 'nav.reload')).toHaveLength(1)
    // A turn that finishes before the page reports loading ends at rest as well.
    await act(async () => screen.getByRole('button', { name: 'Reload' }).click())
    expect(glyph()?.classList.contains('clu-reload-spin')).toBe(true)
    // jsdom has no AnimationEvent, so React listens for the prefixed name.
    fireEvent(glyph() as SVGSVGElement, new Event('webkitAnimationEnd', { bubbles: true }))
    expect(glyph()?.classList.contains('clu-reload-spin')).toBe(false)
    expect(stub.calls.filter((c) => c.name === 'nav.reload')).toHaveLength(2)
  })
})
