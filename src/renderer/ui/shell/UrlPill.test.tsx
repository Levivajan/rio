import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { COPIED_CHECK_MS, UrlPill } from './UrlPill'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-000000000002'
const RECT = { x: 0, y: 0, width: 1, height: 1 }

function mount(peekUrl: string | null, copied: string | null = null, url = 'https://www.a.com/x', toolbar = false, placement: 'sidebar' | 'toolbar' = 'sidebar', global: Record<string, boolean> = {}) {
  const stub = stubClu({ 'command.copyUrl': () => ({ copied }), 'shell.openToolbarMenu': () => null })
  window.clu = { ...stub.clu, windowId: WIN }
  render(<UrlPill placement={placement} />)
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.activeTabId = TAB
  win.shell.toolbar = toolbar
  if (peekUrl !== null) win.peek = { seq: 1, url: peekUrl, host: '', title: '', favicon: null, canGoBack: false, originRect: RECT, cardRect: RECT, summary: null }
  const tabs = { [TAB]: defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url, title: 'Settings' }) }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win }, tabs, settings: { global, byProfile: {} } } }))
  return stub
}

describe('UrlPill', () => {
  it('shows the active tab host with a copy button', () => {
    mount(null)
    expect(document.querySelector('[data-shell="url-host"]')).toHaveTextContent('a.com')
    expect(screen.getByRole('button', { name: 'Copy URL' })).toBeInTheDocument()
  })

  it('leaves the sidebar for the toolbar in toolbar mode', () => {
    mount(null, null, undefined, true)
    expect(document.querySelector('[data-shell="url-pill"]')).toBeNull()
  })

  it('draws in the toolbar only in toolbar mode; its right-click opens the menu at the pointer', () => {
    const stub = mount(null, null, undefined, true, 'toolbar')
    const pill = document.querySelector('[data-shell="url-pill"]')
    expect(pill).toHaveAttribute('data-placement', 'toolbar')
    fireEvent.contextMenu(pill as Element, { clientX: 40.4, clientY: 20.6 })
    expect(stub.calls.filter((c) => c.name === 'shell.openToolbarMenu').map((c) => c.payload)).toEqual([{ anchor: { x: 40, y: 21 } }])
  })

  it('shows the whole URL in the toolbar with Show full URL on', () => {
    mount(null, null, 'https://www.a.com/x?q=1', true, 'toolbar', { 'toolbar.fullUrl': true })
    expect(document.querySelector('[data-shell="url-host"]')).toHaveTextContent('https://www.a.com/x?q=1')
  })

  it('shows the host in the toolbar with Show full URL off', () => {
    mount(null, null, 'https://www.a.com/x?q=1', true, 'toolbar')
    expect(document.querySelector('[data-shell="url-host"]')).toHaveTextContent(/^a\.com$/)
  })

  it('keeps the host in the sidebar pill with Show full URL on', () => {
    mount(null, null, 'https://www.a.com/x?q=1', false, 'sidebar', { 'toolbar.fullUrl': true })
    expect(document.querySelector('[data-shell="url-host"]')).toHaveTextContent(/^a\.com$/)
  })

  it('shows Rio\'s settings by their rio: URL, not the page title', () => {
    mount(null, null, 'rio://settings/appearance')
    expect(document.querySelector('[data-shell="url-host"]')).toHaveTextContent('rio://settings/appearance')
  })

  it('swaps the copy icon for a check for a second after a copy, and keeps it after a failed copy', async () => {
    vi.useFakeTimers()
    try {
      mount(null, 'https://www.a.com/x')
      const button = screen.getByRole('button', { name: 'Copy URL' })
      await act(async () => fireEvent.click(button))
      expect(button).toHaveAttribute('data-copied', 'true')
      act(() => vi.advanceTimersByTime(COPIED_CHECK_MS))
      expect(button).not.toHaveAttribute('data-copied')
    } finally {
      vi.useRealTimers()
    }
  })

  it('shows no check when nothing was copied', async () => {
    mount(null, null)
    const button = screen.getByRole('button', { name: 'Copy URL' })
    await act(async () => fireEvent.click(button))
    expect(button).not.toHaveAttribute('data-copied')
  })

  it('shows the peeked host while a Peek is open, and no copy button', () => {
    mount('http://b.org/page')
    expect(document.querySelector('[data-shell="url-host"]')).toHaveTextContent('b.org')
    expect(document.querySelector('[data-shell="url-pill"]')).toHaveAttribute('data-security', 'insecure')
    expect(screen.queryByRole('button', { name: 'Copy URL' })).toBeNull()
  })

  it('ignores a junk Peek URL instead of crashing', () => {
    mount('')
    expect(document.querySelector('[data-shell="url-host"]')).toHaveTextContent('a.com')
  })
})
