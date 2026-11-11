import { act, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultTabState, type TabState } from '../../../shared/state'
import { RowIcon } from './RowIcon'
import { hasUnread, rowView } from './rowModel'

const tab = (patch: Partial<TabState> = {}): TabState => ({
  ...defaultTabState({ id: crypto.randomUUID(), profileId: crypto.randomUUID(), spaceId: crypto.randomUUID(), section: 'today', url: 'https://example.com/', title: 'Example' }),
  ...patch
})

describe('rowView', () => {
  it('prefers the rename, then the title, then the host, then the URL', () => {
    expect(rowView(tab({ titleOverride: 'Mine' })).title).toBe('Mine')
    expect(rowView(tab()).title).toBe('Example')
    expect(rowView(tab({ title: '' })).title).toBe('example.com')
    expect(rowView(tab({ title: '', url: 'about:blank' })).title).toBe('about:blank')
  })
  it('reads "Untitled" while a page loads with only its URL for a title', () => {
    expect(rowView(tab({ loading: true, title: '', url: 'https://httpbin.org/delay/6' })).title).toBe('Untitled')
    expect(rowView(tab({ loading: true, title: 'httpbin.org/delay/6', url: 'https://httpbin.org/delay/6' })).title).toBe('Untitled')
    expect(rowView(tab({ loading: true, title: 'httpbin.org', url: 'https://httpbin.org/delay/6' })).title).toBe('Untitled')
    expect(rowView(tab({ loading: true, title: 'Delay page', url: 'https://httpbin.org/delay/6' })).title).toBe('Delay page')
    expect(rowView(tab({ loading: true, title: '', titleOverride: 'Mine' })).title).toBe('Mine')
    expect(rowView(tab({ loading: false, title: '', url: 'https://httpbin.org/delay/6' })).title).toBe('httpbin.org')
  })
  it('shows a speaker while audible and a crossed speaker while muted, even when silent', () => {
    expect(rowView(tab()).audio).toBeNull()
    expect(rowView(tab({ audible: true }))).toMatchObject({ audio: 'playing', audioLabel: 'Mute this tab' })
    expect(rowView(tab({ muted: true }))).toMatchObject({ audio: 'muted', audioLabel: 'Unmute this tab' })
    expect(rowView(tab({ muted: true, audible: true })).audio).toBe('muted')
  })
  it('finds an unread count in the page title, never in the rename or a zero', () => {
    expect(hasUnread('Inbox (3) - me@example.com - Mail')).toBe(true)
    expect(hasUnread('(12) Home / Feed')).toBe(true)
    expect(hasUnread('(99+) Notifications')).toBe(true)
    expect(hasUnread('Inbox (0) - Mail')).toBe(false)
    expect(hasUnread('Pricing (beta)')).toBe(false)
    expect(hasUnread('f(3)x')).toBe(false)
    expect(hasUnread('')).toBe(false)
    expect(rowView(tab({ title: 'Inbox (3) - Mail', titleOverride: 'Mail' })).unread).toBe(true)
    expect(rowView(tab({ title: 'Mail', titleOverride: 'Inbox (3)' })).unread).toBe(false)
  })
  it('a loaded pinned or favorite row unloads ("−"), an unloaded one removes ("×"), Today always closes', () => {
    for (const section of ['pinned', 'favorite'] as const) {
      expect(rowView(tab({ section, lifecycle: 'live' })).close).toBe('unload')
      expect(rowView(tab({ section, lifecycle: 'restoring' })).close).toBe('unload')
      expect(rowView(tab({ section, lifecycle: 'unloaded' })).close).toBe('remove')
      expect(rowView(tab({ section, lifecycle: 'hibernated' })).close).toBe('remove')
    }
    expect(rowView(tab({ section: 'today', lifecycle: 'live' })).close).toBe('close')
    expect(rowView(tab({ section: 'today', lifecycle: 'unloaded' })).close).toBe('close')
    // Junk from a corrupt row: an unknown lifecycle never hides the control.
    expect(rowView(tab({ section: 'pinned', lifecycle: 'zzz' as never })).close).toBe('unload')
  })

  it('marks anchored rows, and off-pinned only for anchored rows', () => {
    expect(rowView(tab({ section: 'pinned', pinnedUrl: 'https://example.com/', offPinned: true }))).toMatchObject({ anchored: true, offPinned: true })
    expect(rowView(tab({ section: 'favorite' })).anchored).toBe(true)
    expect(rowView(tab({ offPinned: true }))).toMatchObject({ anchored: false, offPinned: false })
  })
})

describe('RowIcon', () => {
  it('never spins: a loading page keeps its favicon, and one without a favicon shows the accent square', () => {
    const t = tab({ favicon: 'clu://favicon/abc' })
    const { container, rerender } = render(<RowIcon tab={{ ...t, loading: true }} title="Example" />)
    expect(container.querySelector('img')?.getAttribute('src')).toBe('clu://favicon/abc')
    expect(container.querySelector('[data-loading-icon]')).toBeNull()
    rerender(<RowIcon tab={{ ...t, favicon: null, loading: true }} title="Example" />)
    expect(container.querySelector('[data-loading-icon]')).not.toBeNull()
    expect(container.querySelector('.clu-letter-tile')).toBeNull()
    rerender(<RowIcon tab={{ ...t, favicon: null, loading: false }} title="Example" />)
    expect(container.querySelector('[data-loading-icon]')).toBeNull()
    expect(container.querySelector('.clu-letter-tile')?.textContent).toBe('E')
  })
  it('shows the square, never the default globe, until a loading page has its own icon', () => {
    const generic = 'chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=https%3A%2F%2Fhttpbin.org%2F&allowGoogleServerFallback=0'
    const { container, rerender } = render(<RowIcon tab={tab({ loading: true, favicon: generic })} title="Untitled" />)
    expect(container.querySelector('[data-loading-icon]')).not.toBeNull()
    rerender(<RowIcon tab={tab({ loading: true, favicon: `${generic}&cluIcon=123` })} title="Untitled" />)
    expect(container.querySelector('img')).not.toBeNull()
    rerender(<RowIcon tab={tab({ loading: false, favicon: generic })} title="httpbin.org" />)
    expect(container.querySelector('img')).not.toBeNull()
  })
  it('falls back to a letter tile without a favicon or when the favicon fails, and shows a custom icon first', () => {
    const { container, rerender } = render(<RowIcon tab={tab()} title="example.com" />)
    expect(container.querySelector('.clu-letter-tile')?.textContent).toBe('E')
    rerender(<RowIcon tab={tab({ favicon: 'clu://favicon/bad' })} title="example.com" />)
    act(() => {
      container.querySelector('img')?.dispatchEvent(new Event('error'))
    })
    expect(container.querySelector('img')).toBeNull()
    expect(container.querySelector('.clu-letter-tile')).not.toBeNull()
    rerender(<RowIcon tab={tab({ customIcon: '🚀', favicon: 'clu://favicon/x' })} title="example.com" />)
    expect(screen.getByText('🚀')).toBeInTheDocument()
  })

  it('fades an icon in only when it changes under a mounted row, never on mount or a re-render (motion.md)', () => {
    const t = tab({ favicon: 'clu://favicon/a' })
    const { container, rerender, unmount } = render(<RowIcon tab={t} title="Example" />)
    expect(container.querySelector('img')?.hasAttribute('data-fresh')).toBe(false)
    rerender(<RowIcon tab={{ ...t, title: 'Echo' }} title="Echo" />)
    expect(container.querySelector('img')?.hasAttribute('data-fresh')).toBe(false)
    rerender(<RowIcon tab={{ ...t, favicon: 'clu://favicon/b' }} title="Example" />)
    expect(container.querySelector('img')?.hasAttribute('data-fresh')).toBe(true)
    unmount()
    // The same row mounted again in its new list (pinned, unpinned, a folder that opened) does not fade.
    const again = render(<RowIcon tab={{ ...t, favicon: 'clu://favicon/b' }} title="Example" />)
    expect(again.container.querySelector('img')?.hasAttribute('data-fresh')).toBe(false)
  })
})
