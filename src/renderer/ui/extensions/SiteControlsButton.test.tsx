import { act, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState, type TabState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { blockedBadge, SiteControlsButton } from './SiteControlsButton'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-000000000002'

function mount(tab: { url: string; blockedCount?: number; blockingEnabled?: boolean } | null, panelOpen = false) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  render(<SiteControlsButton />)
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.activeTabId = tab ? TAB : null
  if (panelOpen) win.overlay.surfaces = [{ instanceId: '77777777-7777-4777-8777-777777777777', surface: 'site-controls', layer: 'top', modality: 'light-dismiss', rect: null, openedFrom: null, focus: true, slot: null, props: {} }]
  const tabs: Record<string, TabState> = tab ? { [TAB]: { ...defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url: 'https://a.com/' }), ...tab } } : {}
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win }, tabs } }))
}

function mountMini(page: { url: string; blockedCount?: number; blockingEnabled?: boolean }) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  render(<SiteControlsButton />)
  const win = defaultWindowState({ id: WIN, kind: 'mini', private: false, activeSpaceId: WIN })
  win.mini = { tabId: TAB, targetSpaceId: WIN, targetSpaceName: 'Space', profileMismatch: false, confirming: false, url: page.url, title: '', favicon: null, loading: false, progress: 1, canGoBack: false }
  win.miniPage = { site: 'a.com', blockedCount: page.blockedCount ?? 0, blockingEnabled: page.blockingEnabled ?? true, devMode: false }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
}

describe('blockedBadge', () => {
  it.each([[0, ''], [-1, ''], [Number.NaN, ''], [1, '1'], [99, '99'], [100, '99+'], [5000, '99+']])('%d shows "%s"', (count, text) => {
    expect(blockedBadge(count)).toBe(text)
  })
})

describe('SiteControlsButton', () => {
  it('shows the count', () => {
    mount({ url: 'https://a.com/', blockedCount: 2 })
    expect(screen.getByRole('button', { name: 'Site controls, 2 blocked' })).toHaveTextContent('2')
  })

  it('shows no count while blocking is off for the site', () => {
    mount({ url: 'https://a.com/', blockedCount: 2, blockingEnabled: false })
    expect(screen.getByRole('button', { name: 'Site controls' })).toHaveTextContent('')
  })

  it('draws ring knobs and no slash while blocking is off', () => {
    mount({ url: 'https://a.com/', blockingEnabled: false })
    const svg = screen.getByRole('button', { name: 'Site controls' }).querySelector('svg')
    expect(svg?.querySelectorAll('path')).toHaveLength(1)
    expect(svg?.getAttribute('width')).toBe('17') // 16.2 pt drawn, rounded up
    for (const c of svg?.querySelectorAll('circle') ?? []) expect(c.getAttribute('fill')).toBeNull()
  })

  it('is not pressed while its panel is closed', () => {
    mount({ url: 'https://a.com/' })
    expect(screen.getByRole('button', { name: 'Site controls' })).toHaveAttribute('aria-expanded', 'false')
  })

  it('is pressed with the panel open', () => {
    mount({ url: 'https://a.com/' }, true)
    expect(screen.getByRole('button', { name: 'Site controls' })).toHaveAttribute('aria-expanded', 'true')
  })

  it('shows the Mini window page count, which is not a tab', () => {
    mountMini({ url: 'https://a.com/', blockedCount: 3 })
    expect(screen.getByRole('button', { name: 'Site controls, 3 blocked' })).toHaveTextContent('3')
  })

  it('renders nothing for a blank Mini window page', () => {
    mountMini({ url: 'about:blank' })
    expect(screen.queryByRole('button')).toBeNull()
  })

  it.each([null, { url: 'about:blank' }, { url: '' }])('renders nothing without a page: %j', (tab) => {
    mount(tab)
    expect(screen.queryByRole('button')).toBeNull()
  })
})
