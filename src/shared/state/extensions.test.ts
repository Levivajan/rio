import { describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState, type CluState } from '../state'
import type { MiniWindowState } from './multitask'
import { siteControlsPage } from './extensions'

const mini = (url: string): MiniWindowState => ({
  tabId: 'page', targetSpaceId: 's', targetSpaceName: 'Space', profileMismatch: false, confirming: false, url, title: '', favicon: null, loading: false, progress: 1, canGoBack: false
})

function state(): CluState {
  const main = defaultWindowState({ id: 'w1', kind: 'main', private: false, activeSpaceId: 's' })
  main.activeTabId = 't1'
  const miniWin = defaultWindowState({ id: 'w2', kind: 'mini', private: false, activeSpaceId: 's' })
  miniWin.mini = mini('https://a.com/x')
  const tab = { ...defaultTabState({ id: 't1', profileId: 'p', spaceId: 's', section: 'today', url: 'https://b.com/' }), site: 'b.com', blockedCount: 5 }
  return { ...defaultState(), windows: { w1: main, w2: miniWin }, tabs: { t1: tab } }
}

describe('siteControlsPage', () => {
  it('is the active tab in a main window', () => {
    expect(siteControlsPage(state(), 'w1')).toEqual({ id: 't1', url: 'https://b.com/', site: 'b.com', blockedCount: 5, blockingEnabled: true, devMode: false })
  })

  it('is the page of a Mini window, with defaults until its fields are written', () => {
    const s = state()
    expect(siteControlsPage(s, 'w2')).toEqual({ id: 'page', url: 'https://a.com/x', site: null, blockedCount: 0, blockingEnabled: true, devMode: false })
    const w2 = s.windows.w2
    if (w2) w2.miniPage = { site: 'a.com', blockedCount: 2, blockingEnabled: false, devMode: false }
    expect(siteControlsPage(s, 'w2')).toEqual({ id: 'page', url: 'https://a.com/x', site: 'a.com', blockedCount: 2, blockingEnabled: false, devMode: false })
  })

  it('is null for a missing window, no active tab or a tab record that is gone', () => {
    const s = state()
    expect(siteControlsPage(s, 'nope')).toBeNull()
    const w1 = s.windows.w1
    if (w1) w1.activeTabId = 'gone'
    expect(siteControlsPage(s, 'w1')).toBeNull()
    if (w1) w1.activeTabId = null
    expect(siteControlsPage(s, 'w1')).toBeNull()
  })
})
