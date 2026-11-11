import { act, render } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { librarySidebarStyle, SidebarWarmup, WARM_DELAY_MS } from './Sidebar'

// Each pane part records the space it was laid out for; the warm-up's own rules are what matter here.
const laid: string[] = []
vi.mock('../tabs/FavoritesGrid', () => ({ FavoritesGrid: () => null }))
vi.mock('../archive/ArchivePanel', () => ({ ArchivePanel: ({ children }: { children: unknown }) => children }))
const answering: boolean[] = []
vi.mock('../tabs/SpaceTabs', () => ({
  SpaceTabs: ({ spaceId, answersAsks = false }: { spaceId: string; answersAsks?: boolean }) => {
    answering.push(answersAsks)
    return <div data-tab-id={`row-${spaceId}`} />
  }
}))
vi.mock('../spaces/SpaceHeader', () => ({
  SpaceHeader: ({ spaceId }: { spaceId: string }) => {
    laid.push(spaceId)
    return null
  }
}))

beforeEach(() => {
  vi.useFakeTimers()
  laid.length = 0
  answering.length = 0
})
afterEach(() => {
  vi.useRealTimers()
  vi.restoreAllMocks()
})

describe('SidebarWarmup', () => {
  it('lays out every pane once, off screen, after the delay, and drops them in the same commit', () => {
    const layout = vi.spyOn(HTMLElement.prototype, 'getBoundingClientRect')
    const view = render(<SidebarWarmup spaceIds={['work', 'play']} mode="main" />)
    act(() => vi.advanceTimersByTime(WARM_DELAY_MS - 1))
    expect(laid).toEqual([])
    expect(layout).not.toHaveBeenCalled()
    act(() => vi.advanceTimersByTime(1))
    expect(laid).toEqual(['work', 'play'])
    // The copies never act on a menu ask (Tabs ▸ New Folder…, Reveal Tab in Sidebar).
    expect(answering).toEqual([false, false])
    // The layout ran on the warm copies, and they were gone before anything else could run.
    expect(layout.mock.contexts.some((el) => el instanceof HTMLElement && el.classList.contains('clu-sidebar-warm'))).toBe(true)
    expect(view.container.querySelector('.clu-sidebar-warm')).toBeNull()
    expect(view.container.querySelector('[data-tab-id]')).toBeNull()
    // Once only.
    act(() => vi.advanceTimersByTime(WARM_DELAY_MS * 5))
    expect(laid).toEqual(['work', 'play'])
  })

  it('with no other space there is nothing to lay out', () => {
    const view = render(<SidebarWarmup spaceIds={[]} mode="main" />)
    act(() => vi.advanceTimersByTime(WARM_DELAY_MS))
    expect(laid).toEqual([])
    expect(view.container.innerHTML).toBe('')
  })

  it('an unmount before the delay leaves no timer behind', () => {
    const view = render(<SidebarWarmup spaceIds={['work']} mode="peek" />)
    view.unmount()
    act(() => vi.advanceTimersByTime(WARM_DELAY_MS))
    expect(laid).toEqual([])
  })
})

describe('librarySidebarStyle', () => {
  it('moves the Library, open or sliding out, down by the revealed full-screen bar', () => {
    expect(librarySidebarStyle(417, 52)).toEqual({ width: 417, paddingTop: 'calc(0px + 52px)' })
    expect(librarySidebarStyle(417, 0)).toEqual({ width: 417 })
  })
})
