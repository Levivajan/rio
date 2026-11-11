import { act, fireEvent, render } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { computeLayout } from '../../../shared/shell/layout'
import { defaultState, defaultTabState, defaultWindowState, type CluState, type TabState } from '../../../shared/state'
import { PANE_BAR, type SplitView } from '../../../shared/split'
import { stubClu, type Calls } from '../../../test-utils/stubClu'
import { PaneDecorations, paneUrlParts } from './PaneDecorations'

const WIN = '00000000-0000-4000-8000-000000000001'
const A = '11111111-1111-4111-8111-111111111111'
const B = '22222222-2222-4222-8222-222222222222'

const P = '44444444-4444-4444-8444-444444444444'
const page = (id: string, url: string, fields: Partial<TabState> = {}): TabState => ({ ...defaultTabState({ id, profileId: P, spaceId: null, section: 'today', url, title: 'T' }), ...fields })

let calls: Calls = []
function mount(view: SplitView, tabs: CluState['tabs'] = {}) {
  const stub = stubClu({ 'split.dragState': () => null, 'split.focusPane': () => null, 'nav.back': () => null, 'nav.reload': () => null, 'siteControls.open': () => null, 'split.removePane': () => null })
  calls = stub.calls
  window.clu = stub.clu
  const { container } = render(<PaneDecorations tabId={A} />)
  const state: CluState = { ...defaultState(), rev: 1 }
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.shell.layout = computeLayout({ width: 1280, height: 800 }, { sidebarWidth: 240, sidebarHidden: false, focusMode: false, fullscreen: false })
  win.splitView = view
  win.focusedTabId = A
  state.windows = { [WIN]: win }
  state.tabs = tabs
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  return container
}

describe('PaneDecorations', () => {
  it('draws nothing over Chromium native split panes (empty bars, no dividers)', () => {
    const container = mount({
      splitId: 'split-1',
      panes: [
        { tabId: A, rect: { x: 260, y: 8, width: 500, height: 784 }, bar: { x: 260, y: 8, width: 500, height: 0 } },
        { tabId: B, rect: { x: 770, y: 8, width: 500, height: 784 }, bar: { x: 770, y: 8, width: 500, height: 0 } }
      ],
      dividers: []
    })
    const view = container.querySelector('[data-split-view="split-1"]')
    expect(view).toHaveAttribute('data-native', 'true')
    expect(container.querySelectorAll('[data-split-pane-bar]')).toHaveLength(0)
    expect(container.querySelectorAll('[data-split-focus-ring]')).toHaveLength(0)
  })

  it('keeps its own bars, ring and dividers for panes with a bar', () => {
    const container = mount({
      splitId: 'split-2',
      panes: [
        { tabId: A, rect: { x: 260, y: 32, width: 496, height: 760 }, bar: { x: 260, y: 8, width: 496, height: 24 } },
        { tabId: B, rect: { x: 764, y: 32, width: 496, height: 760 }, bar: { x: 764, y: 8, width: 496, height: 24 } }
      ],
      dividers: [{ path: [], index: 0, dir: 'row', rect: { x: 756, y: 8, width: 8, height: 784 }, sizes: [0.5, 0.5], avail: 992 }]
    })
    expect(container.querySelectorAll('[data-split-pane-bar]')).toHaveLength(2)
    expect(container.querySelectorAll('[data-split-focus-ring]')).toHaveLength(1)
    expect(container.querySelectorAll('[data-split-divider]')).toHaveLength(1)
  })

  it("draws the design's pane toolbar: nav and URL on every pane, reload, site controls and split on the focused one", () => {
    const C = '33333333-3333-4333-8333-333333333333'
    const pane = (tabId: string, x: number) => ({ tabId, rect: { x, y: 40, width: 330, height: 752 }, bar: { x, y: 8, width: 330, height: PANE_BAR } })
    const container = mount(
      { splitId: 'split-3', panes: [pane(A, 260), pane(B, 600), pane(C, 940)], dividers: [] },
      { [A]: page(A, 'https://rust-lang.org/learn/', { canGoBack: true }), [B]: page(B, 'https://en.wikipedia.org/wiki/Browser'), [C]: page(C, 'about:blank') }
    )
    const bar = (id: string) => container.querySelector(`[data-split-pane-bar="${id}"]`) as HTMLElement
    const labels = (id: string) => [...bar(id).querySelectorAll('button')].map((b) => b.getAttribute('aria-label'))
    expect(bar(A).style.height).toBe('32px')
    expect(labels(A)).toEqual(['Back', 'Forward', 'Reload', 'Site controls', 'Separate pane', 'Close pane'])
    expect(labels(B)).toEqual(['Back', 'Forward', 'Close pane'])
    expect(bar(A).querySelector('[data-split-pane-url]')).toHaveTextContent('rust-lang.org/learn/')
    expect(bar(B).querySelector('[data-split-pane-url]')).toHaveTextContent('en.wikipedia.org/wiki/Browser')
    expect(bar(C).querySelector('[data-split-pane-url]')).toHaveTextContent('Search or Enter URL...')
    expect(bar(A).querySelector('button[aria-label="Back"]')).toBeEnabled()
    expect(bar(B).querySelector('button[aria-label="Back"]')).toBeDisabled()
    expect(bar(A).querySelector('button[aria-label="Forward"]')).toBeDisabled()
  })

  it("sets the host darker than its port and path, and draws the design glyph sizes", () => {
    const pane = (tabId: string, x: number) => ({ tabId, rect: { x, y: 40, width: 496, height: 752 }, bar: { x, y: 8, width: 496, height: PANE_BAR } })
    const container = mount(
      { splitId: 'split-h', panes: [pane(A, 260), pane(B, 764)], dividers: [] },
      { [A]: page(A, 'http://127.0.0.1:8091/long.html', { canGoBack: true }), [B]: page(B, 'https://en.wikipedia.org/wiki/Browser') }
    )
    const bar = (id: string) => container.querySelector(`[data-split-pane-bar="${id}"]`) as HTMLElement
    const text = bar(A).querySelector('.clu-split-bar-url-text') as HTMLElement
    expect(text).toHaveTextContent('127.0.0.1:8091/long.html')
    expect(text.querySelector('.clu-split-bar-url-rest')).toHaveTextContent(':8091/long.html')
    expect(bar(B).querySelector('.clu-split-bar-url-rest')).toHaveTextContent('/wiki/Browser')
    const glyph = (id: string, label: string) => bar(id).querySelector(`button[aria-label="${label}"] svg`) as SVGElement
    for (const label of ['Reload', 'Site controls', 'Separate pane', 'Close pane']) {
      expect(glyph(A, label).getAttribute('width'), label).toBe('16')
    }
    expect(glyph(A, 'Back').getAttribute('width')).toBe('14')
    // The split glyph (two panes in a rounded square), not a separate-tab sign.
    expect(glyph(A, 'Separate pane').querySelector('rect')?.getAttribute('rx')).toBe('3')
  })

  it('splits the URL at the end of the host, junk and other schemes included', () => {
    expect(paneUrlParts('https://rust-lang.org/learn/')).toEqual({ host: 'rust-lang.org', rest: '/learn/' })
    expect(paneUrlParts('http://127.0.0.1:8091/a.html?q=1#x')).toEqual({ host: '127.0.0.1', rest: ':8091/a.html?q=1#x' })
    expect(paneUrlParts('https://example.com')).toEqual({ host: 'example.com', rest: '/' })
    expect(paneUrlParts('http://[::1]:8080/x')).toEqual({ host: '[::1]', rest: ':8080/x' })
    expect(paneUrlParts('http://[::1')).toEqual({ host: '[::1', rest: '' })
    expect(paneUrlParts('about:blank')).toEqual({ host: '', rest: '' })
    expect(paneUrlParts('')).toEqual({ host: '', rest: '' })
    expect(paneUrlParts('chrome://settings/').rest).toBe('')
  })

  it('pane toolbar buttons act on their own pane', () => {
    const pane = (tabId: string, x: number) => ({ tabId, rect: { x, y: 40, width: 496, height: 752 }, bar: { x, y: 8, width: 496, height: PANE_BAR } })
    const container = mount({ splitId: 's', panes: [pane(A, 260), pane(B, 764)], dividers: [] }, { [A]: page(A, 'https://a.test/', { canGoBack: true }) })
    const press = (label: string) => fireEvent.click(container.querySelector(`[data-split-pane-bar="${A}"] button[aria-label="${label}"]`) as Element)
    press('Back')
    press('Reload')
    press('Site controls')
    press('Close pane')
    const acted = calls.filter((c) => c.name !== 'split.dragState')
    expect(acted.map((c) => c.name)).toEqual(['nav.back', 'nav.reload', 'siteControls.open', 'split.removePane'])
    expect(acted[0]?.payload).toEqual({ tabId: A })
    expect(acted[3]?.payload).toEqual({ tabId: A, mode: 'close' })
  })
})
