import { describe, expect, it } from 'vitest'
import { defaultProfileState, defaultSpaceState, defaultState, defaultTabState, defaultWindowState } from '../state'
import { contextRows } from './context'

const P = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
const S = '11111111-1111-4111-8111-111111111111'
const S2 = '22222222-2222-4222-8222-222222222222'
const W = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'
const F = 'ffffffff-ffff-4fff-8fff-ffffffffffff'
const T = '00000000-0000-4000-8000-000000000001'

function state(section: 'pinned' | 'today' | 'favorite', folderName: unknown = 'Rio folder') {
  const s = defaultState()
  s.profiles[P] = defaultProfileState({ id: P, name: 'One', isDefault: true, ephemeral: false })
  s.spaces[S] = { ...defaultSpaceState({ id: S, name: 'Rio test', icon: { kind: 'letter' }, color: 'blue', profileId: P, ephemeral: false }), accent: '#4247cf' }
  s.spaces[S2] = defaultSpaceState({ id: S2, name: 'Rio other', icon: { kind: 'letter' }, color: 'blue', profileId: P, ephemeral: false })
  s.tabs[T] = defaultTabState({ id: T, profileId: P, spaceId: section === 'favorite' ? null : S, section, url: 'https://apple.example/', title: 'Apple' })
  s.folders[F] = { id: F, spaceId: S, name: folderName as string, icon: null, collapsed: true, children: [{ kind: 'tab', id: '00000000-0000-4000-8000-000000000009' }] }
  s.folders['eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee'] = { id: 'eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee', spaceId: S2, name: 'Rio elsewhere', icon: null, collapsed: true, children: [] }
  s.windows[W] = { ...defaultWindowState({ id: W, kind: 'main', private: false, activeSpaceId: S }), activeTabId: T }
  return s
}

describe('contextRows', () => {
  it('lists Move to Today, Open and Pin to for a query naming the space and a folder', () => {
    const rows = contextRows(state('pinned'), W, 'rio')
    expect(rows.map((r) => [r.verb, r.chip])).toEqual([
      ['Move to Today in', 'Rio test'],
      ['Open', 'Rio test / Rio folder'],
      ['Pin to', 'Rio test / Rio folder']
    ])
    expect(rows[0]).toMatchObject({ dot: '#4247cf', icon: 'today', run: { kind: 'togglePin' } })
    expect(rows[1]?.run).toEqual({ kind: 'openFolder', folderId: F })
    expect(rows[2]?.run).toEqual({ kind: 'pinTo', tabId: T, spaceId: S, folderId: F, index: 1 })
  })

  it('offers Pin to the space for a Today tab, and nothing about the tab for a favorite', () => {
    expect(contextRows(state('today'), W, 'rio test')[0]).toMatchObject({ verb: 'Pin to', chip: 'Rio test', run: { kind: 'togglePin' } })
    expect(contextRows(state('today'), W, 'rio test')[0]?.dot).toBeUndefined()
    expect(contextRows(state('favorite'), W, 'rio').map((r) => r.verb)).toEqual(['Open'])
  })

  it('needs three characters at a word start, and survives junk names and a missing window', () => {
    expect(contextRows(state('pinned'), W, 'ri')).toEqual([])
    expect(contextRows(state('pinned'), W, 'io')).toEqual([])
    expect(contextRows(state('pinned'), W, 'weather')).toEqual([])
    expect(contextRows(state('pinned', null), W, 'untitled').map((r) => r.chip)).toEqual(['Rio test / Untitled', 'Rio test / Untitled'])
    expect(contextRows(state('pinned'), 'nope', 'rio')).toEqual([])
  })
})
