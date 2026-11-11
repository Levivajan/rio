import { act, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState, type OverlaySurfaceState, type TabState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { MORE_MENU, popoverPosition, SITE_CONTROLS_CLOSE_MS, SiteControlsPopover } from './SiteControlsPopover'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-000000000002'
const INST = '77777777-7777-4777-8777-777777777777'
const instance: OverlaySurfaceState = { instanceId: INST, surface: 'site-controls', layer: 'top', modality: 'light-dismiss', rect: { x: 200, y: 40, width: 40, height: 28 }, openedFrom: null, focus: true, slot: null, props: {} }

function mount(tab: { site: string | null; blockedCount?: number; blockingEnabled?: boolean; devMode?: boolean; security?: TabState['security'] } | null, overrides: Parameters<typeof stubClu>[0] = {}, surfaces: OverlaySurfaceState[] = []) {
  const stub = stubClu(overrides)
  window.clu = { ...stub.clu, windowId: WIN }
  stateMirror()
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.activeTabId = tab ? TAB : null
  win.overlay.surfaces = surfaces
  const tabs: Record<string, TabState> = tab ? { [TAB]: { ...defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url: 'https://a.com/' }), ...tab } } : {}
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win }, tabs } }))
  render(<SiteControlsPopover instance={instance} windowId={WIN} />)
  return stub
}

/** "…" ▸ All Site Settings…: Rio's own controls in place of the panel's content. */
function openAll() {
  fireEvent.click(screen.getByRole('button', { name: 'More site controls' }))
  fireEvent.click(screen.getByRole('menuitem', { name: 'All Site Settings…' }))
}

function mountMini(page: { site: string | null; blockedCount?: number; blockingEnabled?: boolean }) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  stateMirror()
  const win = defaultWindowState({ id: WIN, kind: 'mini', private: false, activeSpaceId: WIN })
  win.mini = { tabId: TAB, targetSpaceId: WIN, targetSpaceName: 'Space', profileMismatch: false, confirming: false, url: 'https://a.com/', title: '', favicon: null, loading: false, progress: 1, canGoBack: false }
  win.miniPage = { blockedCount: 0, blockingEnabled: true, devMode: false, ...page }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
  render(<SiteControlsPopover instance={instance} windowId={WIN} />)
  openAll()
  return stub
}

describe('popoverPosition', () => {
  const viewport = { width: 1280, height: 800 }
  it('right-aligns under the anchor', () => {
    expect(popoverPosition({ x: 600, y: 40, width: 40, height: 28 }, viewport)).toEqual({ left: 502, top: 78, maxHeight: 714, arrow: 118 })
  })
  it('stays inside the window on both sides', () => {
    expect(popoverPosition({ x: 50, y: 40, width: 40, height: 28 }, viewport)).toMatchObject({ left: 8, arrow: 62 })
    expect(popoverPosition({ x: 1270, y: 40, width: 40, height: 28 }, viewport).left).toBe(1036)
    expect(popoverPosition({ x: 0, y: 900, width: 0, height: 0 }, viewport).maxHeight).toBe(0)
    // The arrow stays clear of the panel's 21 pt corners.
    expect(popoverPosition({ x: 0, y: 40, width: 0, height: 28 }, viewport).arrow).toBe(35)
    expect(popoverPosition({ x: 1280, y: 40, width: 0, height: 28 }, viewport).arrow).toBe(201)
  })
})

describe('SiteControlsPopover', () => {
  it('has the design layout: page actions, extension tiles with "+", the connection pill and "…"', () => {
    const stub = mount({ site: 'a.com' })
    const tiles = document.querySelector('[data-slot="extensions"]')
    expect(tiles?.querySelector('h2')?.textContent).toBe('Extensions')
    expect(document.querySelector('.clu-site-controls-security')?.textContent).toBe('Not secure')
    fireEvent.click(screen.getByRole('button', { name: 'Add extensions' }))
    expect(stub.calls).toContainEqual({ name: 'tabs.create', payload: { url: 'https://chromewebstore.google.com/' } })
  })

  it('opens the share picker at the Share tile and closes the panel', () => {
    const stub = mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'Share' }))
    const call = stub.calls.find((c) => c.name === 'siteControls.share')
    expect(call?.payload).toMatchObject({ anchor: { x: expect.any(Number), y: expect.any(Number) } })
    expect(stub.calls.some((c) => c.name === 'commands.run' && (c.payload as { id: string }).id === 'edit.copyUrl')).toBe(false)
    expect(stub.calls).toContainEqual({ name: 'overlay.close', payload: { instanceId: INST } })
  })

  it('draws a solid lock in a semibold Secure pill', () => {
    mount({ site: 'a.com', security: 'secure' })
    const pill = document.querySelector('.clu-site-controls-security')
    expect(pill?.textContent).toBe('Secure')
    expect(pill?.querySelector('[data-lock="solid"] rect')?.getAttribute('fill')).toBe('currentColor')
  })

  it('draws every setting row as the white gear in a theme circle, lighter when blocked', async () => {
    const rows = [
      { permission: 'camera', decision: 'allow' },
      { permission: 'autoPictureInPicture', decision: 'allow' },
      { permission: 'midi', decision: 'deny' }
    ]
    mount({ site: 'a.com' }, { 'permission.list': () => rows })
    await screen.findByRole('region', { name: 'Settings' })
    const settings = document.querySelector('[data-slot="settings"]')
    const glyph = (p: string) => settings?.querySelector(`[data-permission="${p}"] .clu-site-controls-setting-icon path`)?.getAttribute('d')
    expect(settings?.textContent).toContain('Automatic picture-in-picture')
    expect(new Set([glyph('camera'), glyph('autoPictureInPicture'), glyph('midi')]).size).toBe(1)
    expect(settings?.querySelector('[data-permission="midi"]')).toHaveAttribute('data-decision', 'deny')
  })

  it('has the four top tiles: Share, Site tweaks, Screenshot, Capture text', () => {
    mount({ site: 'a.com' })
    const top = screen.getByRole('toolbar', { name: 'Page actions' })
    expect([...top.querySelectorAll('button')].map((b) => b.getAttribute('aria-label'))).toEqual(['Share', 'Site tweaks', 'Screenshot', 'Capture text'])
  })

  it('closes and starts the text capture from the Capture text tile', () => {
    const stub = mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'Capture text' }))
    expect(stub.calls.map((c) => c.name).filter((n) => n === 'overlay.close' || n === 'siteControls.captureText')).toEqual(['overlay.close', 'siteControls.captureText'])
  })

  it('closes and starts the region capture from the Screenshot tile', () => {
    const stub = mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'Screenshot' }))
    expect(stub.calls.map((c) => c.name).filter((n) => n === 'overlay.close' || n === 'siteControls.screenshot')).toEqual(['overlay.close', 'siteControls.screenshot'])
  })

  it('runs Site tweaks from the top row', () => {
    const stub = mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'Site tweaks' }))
    expect(stub.calls).toContainEqual({ name: 'commands.run', payload: { id: 'tweaks.open' } })
  })

  it('shows the count and every section for a site', () => {
    mount({ site: 'a.com', blockedCount: 2 })
    openAll()
    expect(screen.getByRole('dialog', { name: 'Site controls' })).toBeInTheDocument()
    expect(screen.getByText('2 blocked on this page')).toBeInTheDocument()
    expect(screen.getByRole('switch', { name: 'Block ads and trackers' })).toHaveAttribute('aria-checked', 'true')
    for (const name of ['tweaks', 'permissions', 'zoom']) expect(document.querySelector(`[data-slot="${name}"]`)).not.toBeNull()
  })

  it('never shows a negative count', () => {
    mount({ site: 'a.com', blockedCount: -1 })
    openAll()
    expect(screen.getByText('0 blocked on this page')).toBeInTheDocument()
  })

  it('reads "Off for" the site with no count, and the switch turns it back on', () => {
    const stub = mount({ site: 'a.com', blockedCount: 2, blockingEnabled: false })
    openAll()
    expect(screen.getByText('Off for a.com')).toBeInTheDocument()
    expect(screen.queryByText(/blocked on this page/)).toBeNull()
    fireEvent.click(screen.getByRole('switch', { name: 'Block ads and trackers' }))
    expect(stub.calls).toContainEqual({ name: 'blocking.setSiteEnabled', payload: { tabId: TAB, enabled: true } })
  })

  it('shows only zoom on a page without a site', () => {
    mount({ site: null })
    fireEvent.click(screen.getByRole('button', { name: 'More site controls' }))
    expect(screen.getByRole('menuitem', { name: 'Clear Cache' })).toHaveAttribute('aria-disabled', 'true')
    expect(screen.getByRole('menuitem', { name: 'Clear Cookies' })).toHaveAttribute('aria-disabled', 'true')
    fireEvent.click(screen.getByRole('menuitem', { name: 'All Site Settings…' }))
    expect(screen.queryByRole('switch', { name: 'Block ads and trackers' })).toBeNull()
    expect(document.querySelector('[data-slot="tweaks"]')).toBeNull()
    expect(document.querySelector('[data-slot="zoom"]')).not.toBeNull()
  })

  it('All Site Settings replaces the panel\'s content, and Back brings it back', () => {
    mount({ site: 'a.com' })
    openAll()
    const dialog = screen.getByRole('dialog', { name: 'Site controls' })
    expect(dialog.querySelector('[data-more]')).not.toBeNull()
    expect(screen.getByRole('heading', { name: 'All Site Settings' })).toBeInTheDocument()
    // Nothing of the popover stays under the settings: no page actions, no pill, no "…".
    expect(screen.queryByRole('toolbar', { name: 'Page actions' })).toBeNull()
    expect(screen.queryByText('Secure')).toBeNull()
    expect(screen.queryByRole('button', { name: 'More site controls' })).toBeNull()
    fireEvent.click(screen.getByRole('button', { name: 'Back to site controls' }))
    expect(dialog.querySelector('[data-more]')).toBeNull()
    expect(screen.getByRole('toolbar', { name: 'Page actions' })).toBeInTheDocument()
  })

  it('"…" holds the design\'s menu without its two page enhancement items', () => {
    mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'More site controls' }))
    const menu = screen.getByRole('menu', { name: 'More site controls' })
    expect([...menu.children].map((el) => (el.getAttribute('role') === 'separator' ? '—' : el.textContent))).toEqual([
      'Clear Cache',
      'Clear Cookies',
      '—',
      'Manage Extensions…',
      'Add Extension…',
      '—',
      'All Site Settings…'
    ])
    expect(MORE_MENU.some((it) => it.kind === 'item' && /Boost/.test(it.label))).toBe(false)
  })

  it.each([
    ['Clear Cache', 'page.clearCacheAndRefresh'],
    ['Clear Cookies', 'page.clearCookiesAndRefresh']
  ])('"…" ▸ %s closes the panel, then clears the site\'s data', (label, id) => {
    const stub = mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'More site controls' }))
    fireEvent.click(screen.getByRole('menuitem', { name: label }))
    expect(stub.calls.map((c) => c.name).filter((n) => n === 'overlay.close' || n === 'commands.run')).toEqual(['overlay.close', 'commands.run'])
    expect(stub.calls).toContainEqual({ name: 'commands.run', payload: { id } })
  })

  it('"…" ▸ Add Extension… opens the store and Manage Extensions… the extensions menu', () => {
    const stub = mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'More site controls' }))
    fireEvent.click(screen.getByRole('menuitem', { name: 'Manage Extensions…' }))
    expect(stub.calls.some((c) => c.name === 'extensions.openMenu')).toBe(true)
    expect(screen.queryByRole('menu')).toBeNull()
    fireEvent.click(screen.getByRole('button', { name: 'More site controls' }))
    fireEvent.click(screen.getByRole('menuitem', { name: 'Add Extension…' }))
    expect(stub.calls).toContainEqual({ name: 'tabs.create', payload: { url: 'https://chromewebstore.google.com/' } })
    expect(stub.calls).toContainEqual({ name: 'overlay.close', payload: { instanceId: INST } })
  })

  it('Esc closes the "…" menu first and leaves the panel open', () => {
    mount({ site: 'a.com' })
    fireEvent.click(screen.getByRole('button', { name: 'More site controls' }))
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'Escape' })
    expect(screen.queryByRole('menu')).toBeNull()
    expect(screen.getByRole('dialog', { name: 'Site controls' })).not.toHaveAttribute('data-closing')
  })

  afterEach(() => vi.useRealTimers())

  it('fades out on Esc, then closes', () => {
    vi.useFakeTimers()
    const stub = mount({ site: 'a.com' })
    fireEvent.keyDown(window, { key: 'Escape' })
    expect(screen.getByRole('dialog', { name: 'Site controls' })).toHaveAttribute('data-closing', 'true')
    act(() => vi.advanceTimersByTime(SITE_CONTROLS_CLOSE_MS - 1))
    expect(stub.calls.some((c) => c.name === 'overlay.close')).toBe(false)
    act(() => vi.advanceTimersByTime(1))
    expect(stub.calls).toContainEqual({ name: 'overlay.close', payload: { instanceId: INST } })
  })

  it('fades out on a click outside while it is the top light-dismiss surface, never on a click inside', () => {
    vi.useFakeTimers()
    const outside = document.createElement('div')
    document.body.append(outside)
    const stub = mount({ site: 'a.com' }, {}, [instance])
    const panel = screen.getByRole('dialog', { name: 'Site controls' })
    fireEvent.pointerDown(panel)
    expect(panel).not.toHaveAttribute('data-closing')
    fireEvent.pointerDown(outside)
    expect(panel).toHaveAttribute('data-closing', 'true')
    act(() => vi.advanceTimersByTime(SITE_CONTROLS_CLOSE_MS))
    expect(stub.calls).toContainEqual({ name: 'overlay.close', payload: { instanceId: INST } })
    outside.remove()
  })

  it('leaves an outside click to a menu opened over it', () => {
    const menu: OverlaySurfaceState = { ...instance, instanceId: '88888888-8888-4888-8888-888888888888', surface: 'menu' }
    mount({ site: 'a.com' }, {}, [instance, menu])
    fireEvent.pointerDown(document.body)
    expect(screen.getByRole('dialog', { name: 'Site controls' })).not.toHaveAttribute('data-closing')
  })

  it('acts on a Mini window page, which is not a tab', () => {
    const stub = mountMini({ site: 'a.com', blockedCount: 4 })
    expect(screen.getByText('4 blocked on this page')).toBeInTheDocument()
    fireEvent.click(screen.getByRole('switch', { name: 'Block ads and trackers' }))
    expect(stub.calls).toContainEqual({ name: 'blocking.setSiteEnabled', payload: { tabId: TAB, enabled: false } })
    expect(stub.calls.some((c) => c.name === 'overlay.close')).toBe(false)
  })

  it('closes when the window has no active tab left', () => {
    const stub = mount(null)
    expect(stub.calls).toContainEqual({ name: 'overlay.close', payload: { instanceId: INST } })
  })

  it('has the Developer Mode row under Settings for a site, which turns the site on and off', () => {
    const stub = mount({ site: 'a.com' })
    const row = document.querySelector<HTMLButtonElement>('[data-slot="settings"] [data-dev-mode]')
    expect(row?.textContent).toBe('Developer ModeOff')
    expect(row?.getAttribute('aria-pressed')).toBe('false')
    fireEvent.click(row as HTMLButtonElement)
    expect(stub.calls).toContainEqual({ name: 'siteControls.setDevMode', payload: { tabId: TAB, on: true } })
  })

  it('shows Developer Mode on and turns it off', () => {
    const stub = mount({ site: 'localhost', devMode: true })
    const row = document.querySelector<HTMLButtonElement>('[data-dev-mode]')
    expect(row?.textContent).toBe('Developer ModeOn')
    fireEvent.click(row as HTMLButtonElement)
    expect(stub.calls).toContainEqual({ name: 'siteControls.setDevMode', payload: { tabId: TAB, on: false } })
  })

  it('has no Developer Mode row for a page without a site', () => {
    mount({ site: null })
    expect(document.querySelector('[data-dev-mode]')).toBeNull()
  })
})
