import { act, fireEvent, render } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState, type TabState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { setRenaming, useRenaming } from './renaming'
import { TabRow } from './TabRow'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-00000000000a'
const tab = (section: TabState['section'], url = 'https://example.com/', patch: Partial<TabState> = {}): TabState => ({
  ...defaultTabState({ id: TAB, profileId: crypto.randomUUID(), spaceId: crypto.randomUUID(), section, url, title: 'Example' }),
  ...patch
})

function Probe() {
  return <span data-testid="renaming">{String(useRenaming(TAB))}</span>
}

function mount(section: TabState['section'], active: boolean, url?: string, patch?: Partial<TabState>, onPointerDown = () => {}) {
  const stub = stubClu({ 'tabs.activate': () => null })
  window.clu = { ...stub.clu, windowId: WIN }
  const view = render(
    <>
      <TabRow tab={tab(section, url, patch)} active={active} drag={{ onPointerDown }} />
      <Probe />
    </>
  )
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN }) } } }))
  const row = view.container.querySelector<HTMLElement>('[data-tab-id]')
  if (!row) throw new Error('no row')
  return { stub, row, view, activations: () => stub.calls.filter((c) => c.name === 'tabs.activate') }
}

afterEach(() => setRenaming(null))

// A press selects at once; a second click on the selected pinned
// row's title renames it inline.
describe('TabRow press', () => {
  it('activates the row on the press, before the click, and not again on the click', () => {
    const { row, activations } = mount('today', false)
    fireEvent.pointerDown(row, { button: 0 })
    expect(activations().map((c) => c.payload)).toEqual([{ tabId: TAB }])
  })

  it('does not activate on a right press or a modified press', () => {
    const { row, activations } = mount('today', false)
    fireEvent.pointerDown(row, { button: 2 })
    fireEvent.pointerDown(row, { button: 0, metaKey: true })
    expect(activations()).toEqual([])
  })

  it('renames the selected pinned row on a click of its title, but not a Today row', () => {
    const pinned = mount('pinned', true)
    const title = pinned.row.querySelector('.clu-row-title') as HTMLElement
    fireEvent.pointerDown(title, { button: 0 })
    fireEvent.click(title, { detail: 1 })
    expect(pinned.view.getByTestId('renaming').textContent).toBe('true')
    pinned.view.unmount()
    setRenaming(null)

    const today = mount('today', true)
    const t = today.row.querySelector('.clu-row-title') as HTMLElement
    fireEvent.pointerDown(t, { button: 0 })
    fireEvent.click(t, { detail: 1 })
    expect(today.view.getByTestId('renaming').textContent).toBe('false')
  })

  // The favicon of a pinned row that navigated away is a button, and a press on it still selects the row and
  // starts its drag.
  it('selects and lifts an unselected row pressed on its back-to-pinned icon', () => {
    const lifted = vi.fn()
    const { row, activations } = mount('pinned', false, 'https://example.com/next', { pinnedUrl: 'https://example.com/', offPinned: true }, lifted)
    const icon = row.querySelector('.clu-row-reset') as HTMLElement
    expect(icon).not.toBeNull()
    fireEvent.pointerDown(icon, { button: 0 })
    expect(activations().map((c) => c.payload)).toEqual([{ tabId: TAB }])
    expect(lifted).toHaveBeenCalledTimes(1)
  })

  it('a first click on a pinned row that was not selected only selects it', () => {
    const { row, view } = mount('pinned', false)
    const title = row.querySelector('.clu-row-title') as HTMLElement
    fireEvent.pointerDown(title, { button: 0 })
    fireEvent.click(title, { detail: 1 })
    expect(view.getByTestId('renaming').textContent).toBe('false')
  })
})

// A pinned Calendar row asks for its live preview 230 ms into a hover, hung 12.5 right of the row and centered
// on it (hoverCardRect adds the 11 and 12 offsets back); pointer moves do not drag it, and a Today row keeps the pill.
describe('TabRow live preview', () => {
  const rect = { x: 8, y: 300, width: 240, height: 36, top: 300, left: 8, right: 248, bottom: 336, toJSON: () => null }
  it('anchors a pinned preview row beside the row', () => {
    vi.useFakeTimers()
    try {
      const { stub, row } = mount('pinned', false, 'https://calendar.google.com/calendar/u/0/r', { lifecycle: 'live' })
      row.getBoundingClientRect = () => rect
      const hovers = () => stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => c.payload)
      fireEvent.pointerEnter(row, { clientX: 40, clientY: 310 })
      fireEvent.pointerMove(row, { clientX: 90, clientY: 320 })
      vi.advanceTimersByTime(229)
      expect(hovers()).toEqual([])
      vi.advanceTimersByTime(1)
      expect(hovers()).toEqual([{ tabId: TAB, anchor: { x: Math.round(248 + 12.5 - 11), y: Math.round(318 - 128 - 12), width: 0, height: 0 } }])
    } finally {
      vi.useRealTimers()
    }
  })
  // An asleep row gets the same 256 tall live card as a loaded one (BuildHoverCard), so the anchor centers
  // that card on the row whether the row is loaded or asleep.
  it.each([
    'https://calendar.google.com/calendar/u/0/r/day',
    'https://meet.google.com/'
  ])('gives an asleep %s row the anchor the same row gets loaded', (url) => {
    vi.useFakeTimers()
    try {
      const anchorFor = (lifecycle: 'unloaded' | 'live') => {
        const { stub, row, view } = mount('pinned', false, url, { lifecycle })
        row.getBoundingClientRect = () => rect
        fireEvent.pointerEnter(row, { clientX: 40, clientY: 310 })
        vi.advanceTimersByTime(230)
        const hover = stub.calls.find((c) => c.name === 'tabs.hover')?.payload as { anchor: { x: number; y: number } } | undefined
        view.unmount()
        return hover?.anchor
      }
      const asleep = anchorFor('unloaded')
      expect(asleep).toMatchObject({ x: Math.round(248 + 12.5 - 11), y: Math.round(318 - 128 - 12) })
      expect(anchorFor('live')).toEqual(asleep)
    } finally {
      vi.useRealTimers()
    }
  })
  // Gmail's card grows with the unread messages the browser reads, so a pinned Gmail row hangs it level with
  // the row's top, 12.5 right of it, loaded or asleep.
  it('hangs a pinned Gmail row\'s mail card level with the row\'s top', () => {
    vi.useFakeTimers()
    try {
      for (const lifecycle of ['unloaded', 'live'] as const) {
        const { stub, row, view } = mount('pinned', false, 'https://mail.google.com/mail/u/0/', { lifecycle })
        row.getBoundingClientRect = () => rect
        fireEvent.pointerEnter(row, { clientX: 40, clientY: 310 })
        vi.advanceTimersByTime(230)
        const hover = stub.calls.find((c) => c.name === 'tabs.hover')?.payload as { anchor: { x: number; y: number } } | undefined
        expect(hover?.anchor, lifecycle).toMatchObject({ x: Math.round(rect.right + 12.5 - 11), y: Math.round(rect.top - 12) })
        view.unmount()
      }
    } finally {
      vi.useRealTimers()
    }
  })
  // The browser draws the feed's message list for any Gmail row, so the card holds when the title has no count.
  it('holds a pinned Gmail row\'s card whose title carries no unread count', () => {
    vi.useFakeTimers()
    try {
      for (const title of ['Re: invoice - me@x.com - Gmail', 'Gmail']) {
        const { stub, row, view } = mount('pinned', false, 'https://mail.google.com/mail/u/0/#inbox/abc', { title })
        const hovers = () => stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => (c.payload as { tabId: string | null }).tabId)
        row.getBoundingClientRect = () => rect
        fireEvent.pointerEnter(row, { clientX: 40, clientY: 310 })
        vi.advanceTimersByTime(230)
        expect(hovers(), title).toEqual([TAB])
        fireEvent.pointerLeave(row)
        expect(hovers(), title).toEqual([TAB])
        vi.advanceTimersByTime(75)
        expect(hovers(), title).toEqual([TAB, null])
        view.unmount()
      }
    } finally {
      vi.useRealTimers()
    }
  })
  it('keeps the pointer pill on a Today row of the same site', () => {
    vi.useFakeTimers()
    try {
      const { stub, row } = mount('today', false, 'https://calendar.google.com/')
      row.getBoundingClientRect = () => rect
      fireEvent.pointerEnter(row, { clientX: 40, clientY: 310 })
      vi.advanceTimersByTime(2000)
      const hover = stub.calls.find((c) => c.name === 'tabs.hover')?.payload as { anchor: { x: number; y: number } } | undefined
      expect(hover?.anchor).toMatchObject({ x: 40, y: 310 })
    } finally {
      vi.useRealTimers()
    }
  })
})

// The same control unloads a loaded pinned row, then removes it once the published row says unloaded.
describe('TabRow close control', () => {
  const press = (row: HTMLElement) => {
    const button = row.querySelector<HTMLElement>('.clu-row-close')
    if (!button) throw new Error('no close control')
    fireEvent.click(button)
    return button
  }
  it('unloads a loaded pinned row with "−"', () => {
    const { row, stub } = mount('pinned', true, undefined, { lifecycle: 'live' })
    const button = press(row)
    expect(button.dataset.close).toBe('unload')
    expect(button.getAttribute('aria-label')).toBe('Unload tab')
    expect(stub.calls.map((c) => c.name)).toContain('tabs.close')
    expect(stub.calls.map((c) => c.name)).not.toContain('tabs.delete')
  })
  it('turns into "×" the moment the row is unloaded, and removes the row', () => {
    const { row, stub } = mount('pinned', false, undefined, { lifecycle: 'unloaded' })
    const button = press(row)
    expect(button.dataset.close).toBe('remove')
    expect(button.getAttribute('aria-label')).toBe('Remove from sidebar')
    expect(stub.calls.find((c) => c.name === 'tabs.delete')?.payload).toEqual({ nodeId: TAB })
    expect(stub.calls.map((c) => c.name)).not.toContain('tabs.close')
  })
  it('closes a Today row with "×"', () => {
    const { row, stub } = mount('today', true, undefined, { lifecycle: 'live' })
    expect(press(row).dataset.close).toBe('close')
    expect(stub.calls.map((c) => c.name)).toContain('tabs.close')
  })
})

// A loaded Google Calendar pinned row shows its next event under the title; nothing without one.
describe('TabRow Calendar subtitle', () => {
  const calendar = 'https://calendar.google.com/calendar/u/0/r/day'
  it('shows the next event under a pinned row title', () => {
    const { row } = mount('pinned', false, calendar, { subtitle: 'Event at 4:30 PM' })
    const stack = row.querySelector('.clu-row-title-stack[data-event="true"]')
    expect(stack?.querySelector('.clu-row-title')?.textContent).toBe('Example')
    expect(stack?.querySelector('.clu-row-subtitle')?.textContent).toBe('Event at 4:30 PM')
  })

  it('shows nothing when the row has no subtitle (not loaded, or no event)', () => {
    const { row } = mount('pinned', false, calendar)
    expect(row.querySelector('.clu-row-subtitle')).toBeNull()
  })

  it('shows nothing on a Today row even with a subtitle', () => {
    const { row } = mount('today', false, calendar, { subtitle: 'Event at 4:30 PM' })
    expect(row.querySelector('.clu-row-subtitle')).toBeNull()
  })
})
