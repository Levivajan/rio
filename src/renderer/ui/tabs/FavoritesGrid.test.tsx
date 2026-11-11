import { act, fireEvent, render } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { defaultSpaceState, defaultState, defaultTabState, defaultWindowState, type TabState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { FavoritesGrid } from './FavoritesGrid'

const WIN = '00000000-0000-4000-8000-000000000001'
const SPACE = '00000000-0000-4000-8000-000000000002'
const PROFILE = '00000000-0000-4000-8000-000000000003'
const A = '00000000-0000-4000-8000-00000000000a'
const B = '00000000-0000-4000-8000-00000000000b'

function mount(extra: Partial<TabState> = {}) {
  const stub = stubClu({ 'tabs.activate': () => null })
  window.clu = { ...stub.clu, windowId: WIN }
  const view = render(
    <div className="clu-sidebar">
      <FavoritesGrid />
    </div>
  )
  const tab = (id: string, title: string): TabState => ({ ...defaultTabState({ id, profileId: PROFILE, spaceId: null, section: 'favorite', url: 'https://example.com/', title }), ...extra })
  act(() =>
    stub.push({
      kind: 'snapshot',
      rev: 1,
      state: {
        ...defaultState(),
        rev: 1,
        windows: { [WIN]: { ...defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE }), activeTabId: A } },
        spaces: { [SPACE]: defaultSpaceState({ id: SPACE, name: 'Home', icon: { kind: 'letter' }, color: 'blue', profileId: PROFILE, ephemeral: false }) },
        tabs: { [A]: tab(A, 'Mail'), [B]: tab(B, 'Calendar') },
        favorites: { [PROFILE]: [A, B] }
      }
    })
  )
  const tile = (id: string) => view.container.querySelector<HTMLElement>(`.clu-favorite[data-tab-id="${id}"]`) as HTMLElement
  return { stub, view, tile, activations: () => stub.calls.filter((c) => c.name === 'tabs.activate').map((c) => c.payload) }
}

// A favorite shows no title tooltip and no speaker badge, and a
// favorite selects on the press, as rows do.
describe('FavoritesGrid tiles', () => {
  it('select on the press, not again on the click, and not when already active', () => {
    const { tile, activations } = mount()
    fireEvent.pointerDown(tile(B), { button: 0 })
    expect(activations()).toEqual([{ tabId: B }])
    fireEvent.click(tile(B).querySelector('button') as HTMLElement, { detail: 1 })
    expect(activations()).toEqual([{ tabId: B }])
    fireEvent.pointerDown(tile(A), { button: 0 })
    expect(activations()).toEqual([{ tabId: B }])
  })

  it('a right or modified press selects nothing; Enter on the tile activates it', () => {
    const { tile, activations } = mount()
    fireEvent.pointerDown(tile(B), { button: 2 })
    fireEvent.pointerDown(tile(B), { button: 0, metaKey: true })
    expect(activations()).toEqual([])
    fireEvent.click(tile(B).querySelector('button') as HTMLElement, { detail: 0 })
    expect(activations()).toEqual([{ tabId: B }])
  })

  it('has no tooltip and no speaker badge, even when audible', () => {
    const { view, tile } = mount({ audible: true })
    fireEvent.pointerEnter(tile(B))
    expect(view.container.querySelector('.clu-tooltip-anchor, .clu-tooltip')).toBeNull()
    expect(view.container.querySelector('.clu-favorite-badge, [data-audio]')).toBeNull()
    expect(tile(B).querySelector('button')?.getAttribute('aria-label')).toBe('Calendar')
  })
})

// The selected tile's ring is the space's accent (tabs.css), never the site icon's color.
describe('FavoritesGrid ring color', () => {
  it('sets no per-tile ring color, even with an icon color from the browser', () => {
    const { tile } = mount({ iconColor: '#494949' })
    expect(tile(A).getAttribute('data-active')).toBe('true')
    expect(tile(A).style.getPropertyValue('--favorite-ring')).toBe('')
    expect(tile(A).getAttribute('style')).toBeNull()
  })
})

// Show Unread Badge off (the favorite's menu) hides the dot; on, an unread count shows it.
describe('FavoritesGrid unread badge', () => {
  it('shows the dot for an unread count while the badge is on', () => {
    const on = mount({ title: '(3) Inbox' })
    expect(on.tile(A).querySelector('.clu-favorite-unread')).not.toBeNull()
  })

  // The dot is anchored to the tile's lower right corner (it keeps 10 pt from it on a 45 and a 59 pt tile), so the
  // dot is the tile button's own child, never the favicon's.
  it('anchors the dot to the tile corner, not the favicon', () => {
    const on = mount({ title: '(3) Inbox' })
    const dot = on.tile(A).querySelector('.clu-favorite-unread')
    expect(dot?.parentElement?.className).toBe('clu-favorite-tile')
    expect(on.tile(A).querySelector('.clu-favorite-icon .clu-favorite-unread')).toBeNull()
  })

  it('hides it once the badge is off', () => {
    const off = mount({ title: '(3) Inbox', unreadBadge: false })
    expect(off.tile(A).querySelector('.clu-favorite-unread')).toBeNull()
    expect(off.tile(A).dataset.unread).toBe('false')
  })
})

// A mail favorite asks for its preview card 230 ms into a hover, beside the sidebar level with the tile; other
// favorites ask for nothing, and leaving the tile hides it.
describe('FavoritesGrid mail preview', () => {
  it('asks for the card after 230 ms on a Gmail tile only', () => {
    vi.useFakeTimers()
    try {
      const { stub, tile } = mount({ url: 'https://mail.google.com/mail/u/0/#inbox' })
      tile(A).getBoundingClientRect = () => ({ x: 8, y: 90, width: 60.5, height: 47, top: 90, left: 8, right: 68.5, bottom: 137, toJSON: () => null })
      ;(tile(A).parentElement as HTMLElement).getBoundingClientRect = () => ({ x: 8, y: 90, width: 197, height: 47, top: 90, left: 8, right: 205, bottom: 137, toJSON: () => null })
      const hovers = () => stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => c.payload)
      fireEvent.pointerEnter(tile(A))
      vi.advanceTimersByTime(229)
      expect(hovers()).toEqual([])
      vi.advanceTimersByTime(1)
      // Gmail's card hangs from the tile's lower right corner, 3.5 over it.
      expect(hovers()).toEqual([{ tabId: A, anchor: { x: Math.round(68.5 - 3.5 - 11), y: Math.round(137 - 3.5 - 12), width: 0, height: 0 } }])
      // The card closes 75 ms after the pointer leaves, time to cross onto it.
      fireEvent.pointerLeave(tile(A))
      vi.advanceTimersByTime(74)
      expect(hovers()).toHaveLength(1)
      vi.advanceTimersByTime(1)
      expect(hovers().at(-1)).toEqual({ tabId: null })
    } finally {
      vi.useRealTimers()
    }
  })

  // The hold before Gmail's card closes. Coming back to the tile keeps the card as it is; a press closes it
  // at once; once the hold ran out the frame lets go, and the card itself (tabs.hover held) decides.
  it('holds Gmail\'s card 75 ms after the pointer leaves the tile, and a return or a press ends the hold', () => {
    vi.useFakeTimers()
    try {
      const { stub, tile } = mount({ url: 'https://mail.google.com/mail/u/0/#inbox' })
      const hovers = () => stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => (c.payload as { tabId: string | null }).tabId)
      fireEvent.pointerEnter(tile(A))
      vi.advanceTimersByTime(230)
      fireEvent.pointerLeave(tile(A))
      vi.advanceTimersByTime(50)
      fireEvent.pointerEnter(tile(A))
      vi.advanceTimersByTime(1000)
      // The return asks for the card again (it may have faded after the pointer crossed it) and nothing closes it.
      expect(hovers()).toEqual([A, A])
      fireEvent.pointerLeave(tile(A))
      vi.advanceTimersByTime(30)
      fireEvent.pointerDown(tile(A))
      expect(hovers()).toEqual([A, A, null])
      vi.advanceTimersByTime(1000)
      expect(hovers()).toEqual([A, A, null])
    } finally {
      vi.useRealTimers()
    }
  })

  // A Calendar tile's live card takes no pointer, so it closes as the pointer leaves.
  it('closes a Calendar tile\'s card at once on leave', () => {
    vi.useFakeTimers()
    try {
      const { stub, tile } = mount({ url: 'https://calendar.google.com/calendar/u/0/r' })
      const hovers = () => stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => (c.payload as { tabId: string | null }).tabId)
      fireEvent.pointerEnter(tile(A))
      vi.advanceTimersByTime(230)
      fireEvent.pointerLeave(tile(A))
      expect(hovers()).toEqual([A, null])
    } finally {
      vi.useRealTimers()
    }
  })

  // The second column's card hangs at the first column's place, on the sidebar under its own tile.
  it("hangs a later column's Gmail card inside the sidebar, under its tile", () => {
    vi.useFakeTimers()
    try {
      const { stub, tile } = mount({ url: 'https://mail.google.com/mail/u/1/#inbox' })
      tile(A).getBoundingClientRect = () => ({ x: 8, y: 90, width: 60.5, height: 47, top: 90, left: 8, right: 68.5, bottom: 137, toJSON: () => null })
      tile(B).getBoundingClientRect = () => ({ x: 76.5, y: 90, width: 60.5, height: 47, top: 90, left: 76.5, right: 137, bottom: 137, toJSON: () => null })
      fireEvent.pointerEnter(tile(B))
      vi.advanceTimersByTime(230)
      expect(stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => c.payload)).toEqual([{ tabId: B, anchor: { x: Math.round(68.5 - 3.5 - 11), y: Math.round(137 - 3.5 - 12), width: 0, height: 0 } }])
      fireEvent.pointerLeave(tile(B))
      vi.advanceTimersByTime(75)
      expect(stub.calls.filter((c) => c.name === 'tabs.hover').at(-1)?.payload).toEqual({ tabId: null })
    } finally {
      vi.useRealTimers()
    }
  })

  // A Calendar tile's live card still opens beside the sidebar, 12.5 right of the grid's edge, level with it.
  it('hangs a Calendar tile\'s card beside the sidebar', () => {
    vi.useFakeTimers()
    try {
      const { stub, tile } = mount({ url: 'https://calendar.google.com/calendar/u/0/r' })
      tile(A).getBoundingClientRect = () => ({ x: 8, y: 90, width: 60.5, height: 47, top: 90, left: 8, right: 68.5, bottom: 137, toJSON: () => null })
      ;(tile(A).parentElement as HTMLElement).getBoundingClientRect = () => ({ x: 8, y: 90, width: 197, height: 47, top: 90, left: 8, right: 205, bottom: 137, toJSON: () => null })
      fireEvent.pointerEnter(tile(A))
      vi.advanceTimersByTime(230)
      expect(stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => c.payload)).toEqual([{ tabId: A, anchor: { x: Math.round(205 + 12.5 - 11), y: Math.round(90 - 12), width: 0, height: 0 } }])
    } finally {
      vi.useRealTimers()
    }
  })

  // Entering the next tile closes the first one's card at once; the next card waits its own 230 ms.
  it('closes the previous tile\'s card as the pointer reaches the next one', () => {
    vi.useFakeTimers()
    try {
      const { stub, tile } = mount({ url: 'https://mail.google.com/mail/u/0/#inbox' })
      const hovers = () => stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => (c.payload as { tabId: string | null }).tabId)
      fireEvent.pointerEnter(tile(A))
      vi.advanceTimersByTime(230)
      expect(hovers()).toEqual([A])
      fireEvent.pointerEnter(tile(B))
      expect(hovers()).toEqual([A, null])
      vi.advanceTimersByTime(230)
      expect(hovers()).toEqual([A, null, B])
    } finally {
      vi.useRealTimers()
    }
  })

  it('asks for nothing on another site', () => {
    vi.useFakeTimers()
    try {
      const { stub, tile } = mount()
      fireEvent.pointerEnter(tile(A))
      vi.advanceTimersByTime(2000)
      expect(stub.calls.some((c) => c.name === 'tabs.hover')).toBe(false)
    } finally {
      vi.useRealTimers()
    }
  })
})
