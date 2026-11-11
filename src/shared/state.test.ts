import { describe, expect, it } from 'vitest'
import { applyPatch, defaultState, defaultTabState, defaultWindowState, delIn, setIn } from './state'

const ID = '11111111-1111-4111-8111-111111111111'

describe('composed defaults', () => {
  it('give a tab every field of PLAN 6', () => {
    const t = defaultTabState({ id: ID, profileId: ID, spaceId: ID, section: 'today', url: 'https://a.test' })
    const fields = [
      'lifecycle', 'pendingUrl', 'loading', 'progress', 'canGoBack', 'canGoForward', 'audible', 'zoomFactor', 'find', 'issue', 'security', 'fullscreen', 'unresponsive',
      'id', 'profileId', 'spaceId', 'section', 'parentId', 'url', 'pinnedUrl', 'offPinned', 'title', 'titleOverride', 'subtitle', 'favicon', 'iconColor', 'unreadBadge', 'customIcon', 'muted', 'lastActiveAt', 'splitId', 'internal',
      'mediaPlaying', 'pip', 'site', 'blockedCount', 'blockingEnabled', 'tweak', 'zapMode'
    ]
    for (const f of fields) expect(t, f).toHaveProperty(f)
  })
  it('give a window every field of PLAN 6', () => {
    const w = defaultWindowState({ id: ID, kind: 'main', private: false, activeSpaceId: ID })
    const fields = [
      'id', 'kind', 'private', 'focused', 'fullscreen', 'activeSpaceId', 'swipe', 'activeTabId', 'shell', 'overlay', 'toasts', 'prompts', 'command',
      'activeSplitId', 'focusedTabId', 'splitDrop', 'peek', 'peekPage', 'mini', 'installPrompt'
    ]
    for (const f of fields) expect(w, f).toHaveProperty(f)
    expect(w.shell.sidebar.width).toBe(214)
  })
  it('give the snapshot every top-level field', () => {
    const s = defaultState()
    for (const f of ['rev', 'app', 'settings', 'profiles', 'spaceOrder', 'spaces', 'favorites', 'folders', 'tabs', 'splits', 'windows', 'downloads', 'archiveRevision', 'historyRevision']) {
      expect(s, f).toHaveProperty(f)
    }
    for (const f of ['version', 'isPackaged', 'appearance', 'onboarding', 'dataHealth', 'import', 'defaultBrowser']) expect(s.app, f).toHaveProperty(f)
  })
})

describe('applyPatch', () => {
  it('round-trips set and del with structural sharing', () => {
    const s = defaultState()
    const a = applyPatch(s, [{ op: 'set', path: ['tabs', ID], value: { id: ID } }, { op: 'set', path: ['historyRevision'], value: 3 }])
    expect(a.tabs[ID]).toEqual({ id: ID })
    expect(a.windows).toBe(s.windows)
    const b = applyPatch(a, [{ op: 'del', path: ['tabs', ID] }])
    expect(b.tabs[ID]).toBeUndefined()
    expect(s.tabs[ID]).toBeUndefined()
  })
  it('ignores prototype paths', () => {
    const s = applyPatch(defaultState(), [{ op: 'set', path: ['__proto__', 'x'], value: 1 }])
    expect(({} as Record<string, unknown>).x).toBeUndefined()
    expect(s).toBeTruthy()
  })
  it('setIn and delIn copy only along the path', () => {
    const root = { a: { b: 1 }, c: { d: 2 } }
    const next = setIn(root, ['a', 'b'], 5)
    expect(next.c).toBe(root.c)
    expect(delIn(next, ['a', 'b'])).toEqual({ a: {}, c: { d: 2 } })
  })
})
