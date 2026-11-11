import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { MediaCard } from './MediaCard'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '22222222-2222-4222-8222-222222222222'

function mount(tab: Partial<ReturnType<typeof defaultTabState>> | null) {
  const stub = stubClu({ 'media.toggle': () => null, 'media.focus': () => null, 'media.previous': () => null, 'media.next': () => null, 'tabs.setMuted': () => null })
  window.clu = stub.clu
  render(<MediaCard />)
  const state: CluState = { ...defaultState(), rev: 1 }
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  if (tab) {
    win.mediaTabId = TAB
    state.tabs = { [TAB]: { ...defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url: 'https://radio.test/' }), title: 'Radio', mediaPlaying: true, ...tab } }
  }
  state.windows = { [WIN]: win }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  return stub
}

describe('MediaCard', () => {
  it('renders nothing without a media tab', () => {
    mount(null)
    expect(screen.queryByRole('region', { name: 'Now playing' })).toBeNull()
  })

  it('is the design media bar: favicon, previous, play or pause, next and mute, in that order', () => {
    const stub = mount({})
    const card = screen.getByRole('region', { name: 'Now playing' })
    expect([...card.querySelectorAll('button')].map((b) => b.getAttribute('aria-label'))).toEqual(['Radio', 'Previous track', 'Pause', 'Next track', 'Mute tab'])
    fireEvent.click(screen.getByRole('button', { name: 'Previous track' }))
    fireEvent.click(screen.getByRole('button', { name: 'Next track' }))
    expect(stub.calls.filter((c) => c.name.startsWith('media.'))).toEqual([
      { name: 'media.previous', payload: { tabId: TAB } },
      { name: 'media.next', payload: { tabId: TAB } }
    ])
  })

  it('favicon named by the title focuses the tab, the middle button toggles, the last mutes', () => {
    const stub = mount({})
    const card = screen.getByRole('region', { name: 'Now playing' })
    expect(card).toHaveAttribute('data-state', 'playing')
    expect(card).toHaveAttribute('data-muted', 'false')
    // No title text on the bar.
    expect(card).not.toHaveTextContent('Radio')
    fireEvent.click(screen.getByRole('button', { name: 'Pause' }))
    fireEvent.click(screen.getByRole('button', { name: 'Radio' }))
    fireEvent.click(screen.getByRole('button', { name: 'Mute tab' }))
    expect(stub.calls.filter((c) => c.name.startsWith('media.') || c.name === 'tabs.setMuted')).toEqual([
      { name: 'media.toggle', payload: { tabId: TAB } },
      { name: 'media.focus', payload: { tabId: TAB } },
      { name: 'tabs.setMuted', payload: { tabId: TAB, muted: true } }
    ])
  })

  it('a paused tab offers Play, a muted tab shows a muted card, a missing title falls back to the URL', () => {
    mount({ mediaPlaying: false, muted: true, title: '' })
    const card = screen.getByRole('region', { name: 'Now playing' })
    expect(card).toHaveAttribute('data-state', 'paused')
    expect(card).toHaveAttribute('data-muted', 'true')
    expect(screen.getByRole('button', { name: 'Unmute tab' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Play' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'https://radio.test/' })).toBeInTheDocument()
  })
})
