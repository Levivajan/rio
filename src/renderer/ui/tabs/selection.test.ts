// Multi-select reducer (tabs-t7): Cmd+click toggle, Shift+click range within one space across sections and folders,
// Esc clear, rows that leave the sidebar dropped, the 200-tab cap, and tabs.bulk refusing a 201st id.
import { describe, expect, it } from 'vitest'
import { tabsIpc } from '../../../shared/ipc/tabs'
import { defaultSpaceState, defaultState, defaultTabState, defaultWindowState, type CluState, type NodeRef } from '../../../shared/state'
import { MAX_BULK } from '../../../shared/tabs'
import { EMPTY_SELECTION, selectableOrder, selectionReducer, type Selection, type SelectionAction } from './selection'

const P = crypto.randomUUID()
const Q = crypto.randomUUID()
const S = crypto.randomUUID()
const OTHER = crypto.randomUUID()
const W = crypto.randomUUID()

/** Favorites f1; pinned: p1, folder A [a1, folder B [b1]], p2; Today t1 t2 t3; another space on another profile: x1. */
function fixture() {
  const s: CluState = defaultState()
  const ids = Object.fromEntries(['f1', 'p1', 'a1', 'b1', 'p2', 't1', 't2', 't3', 'x1'].map((k) => [k, crypto.randomUUID()])) as Record<string, string>
  const A = crypto.randomUUID()
  const B = crypto.randomUUID()
  const id = (k: string) => ids[k] ?? ''
  const tab = (k: string): NodeRef => ({ kind: 'tab', id: id(k) })
  for (const v of Object.values(ids)) s.tabs[v] = defaultTabState({ id: v, profileId: P, spaceId: S, section: 'today', url: 'https://a.test/' })
  s.spaces[S] = { ...defaultSpaceState({ id: S, name: 'S', icon: { kind: 'letter' }, color: 'blue', profileId: P, ephemeral: false }), pinned: [tab('p1'), { kind: 'folder', id: A }, tab('p2')], today: [id('t1'), id('t2'), id('t3')] }
  s.spaces[OTHER] = { ...defaultSpaceState({ id: OTHER, name: 'O', icon: { kind: 'letter' }, color: 'red', profileId: Q, ephemeral: false }), today: [id('x1')] }
  s.folders[A] = { id: A, spaceId: S, name: 'A', icon: null, collapsed: false, children: [tab('a1'), { kind: 'folder', id: B }] }
  s.folders[B] = { id: B, spaceId: S, name: 'B', icon: null, collapsed: false, children: [tab('b1')] }
  s.favorites[P] = [id('f1')]
  s.windows[W] = { ...defaultWindowState({ id: W, kind: 'main', private: false, activeSpaceId: S }), activeTabId: null }
  const order = () => selectableOrder(s, W)
  const names = (sel: Selection) => sel.ids.map((v) => Object.entries(ids).find(([, x]) => x === v)?.[0])
  const run = (actions: ((o: string[]) => SelectionAction)[], from: Selection = EMPTY_SELECTION) => actions.reduce((sel, a) => selectionReducer(sel, a(order())), from)
  const toggle = (k: string) => (o: string[]): SelectionAction => ({ type: 'toggle', id: id(k), order: o })
  const range = (k: string, active: string | null = null) => (o: string[]): SelectionAction => ({ type: 'range', id: id(k), order: o, active: active ? id(active) : null })
  return { s, id, A, B, order, names, run, toggle, range }
}

describe('selection reducer', () => {
  it('Cmd+click toggles a row in and out; the ids stay in sidebar order', () => {
    const f = fixture()
    const sel = f.run([f.toggle('t2'), f.toggle('p1'), f.toggle('f1')])
    expect(f.names(sel)).toEqual(['f1', 'p1', 't2'])
    expect(sel.anchor).toBe(f.id('f1'))
    expect(f.names(f.run([f.toggle('p1')], sel))).toEqual(['f1', 't2'])
    // A row of another space is not in this sidebar and changes nothing.
    expect(f.run([f.toggle('x1')], sel)).toBe(sel)
  })

  it('Shift+click adds the range from the anchor across sections and folders, in either direction', () => {
    const f = fixture()
    // From p1 (pinned root) through folder A and its subfolder B into Today.
    expect(f.names(f.run([f.toggle('p1'), f.range('t1')]))).toEqual(['p1', 'a1', 'b1', 'p2', 't1'])
    // Backwards from Today into the folder, keeping a Cmd+clicked row outside the range.
    expect(f.names(f.run([f.toggle('f1'), f.toggle('t2'), f.range('b1')]))).toEqual(['f1', 'b1', 'p2', 't1', 't2'])
    // Cmd+click two rows, then Shift+click the next one: exactly those three.
    expect(f.names(f.run([f.toggle('t1'), f.toggle('t2'), f.range('t3')]))).toEqual(['t1', 't2', 't3'])
  })

  it('Shift+click with no anchor starts at the active tab, else at the clicked row alone', () => {
    const f = fixture()
    expect(f.names(f.run([f.range('t2', 'a1')]))).toEqual(['a1', 'b1', 'p2', 't1', 't2'])
    expect(f.names(f.run([f.range('t2')]))).toEqual(['t2'])
    // An anchor or active tab outside this sidebar is ignored, and so is a click on a row outside it.
    expect(f.names(f.run([f.range('t1', 'x1')]))).toEqual(['t1'])
    expect(f.run([f.range('x1')])).toBe(EMPTY_SELECTION)
  })

  it('a plain click clears the selection and anchors the next Shift+click at the clicked row, before its activation lands', () => {
    const f = fixture()
    const clicked = selectionReducer(f.run([f.toggle('t1'), f.toggle('t3')]), { type: 'click', id: f.id('p2') })
    expect(clicked).toEqual({ ids: [], anchor: f.id('p2') })
    expect(selectionReducer(clicked, { type: 'click', id: f.id('p2') })).toBe(clicked)
    // The window still reports the previous active tab (a1); the range starts at the clicked row.
    expect(f.names(f.run([f.range('t2', 'a1')], clicked))).toEqual(['p2', 't1', 't2'])
  })

  it('a collapsed folder leaves its rows out of a range', () => {
    const f = fixture()
    const folder = f.s.folders[f.A]
    if (folder) folder.collapsed = true
    expect(f.names(f.run([f.toggle('p1'), f.range('t1')]))).toEqual(['p1', 'p2', 't1'])
  })

  it('Esc clears the selection and its anchor', () => {
    const f = fixture()
    const sel = f.run([f.toggle('t1'), f.toggle('t3')])
    expect(selectionReducer(sel, { type: 'clear' })).toEqual(EMPTY_SELECTION)
    expect(selectionReducer(EMPTY_SELECTION, { type: 'clear' })).toBe(EMPTY_SELECTION)
  })

  it('drops rows that are closed or leave the sidebar, and the anchor with them', () => {
    const f = fixture()
    const sel = f.run([f.toggle('p1'), f.toggle('t1'), f.toggle('t3')])
    // t3 closes.
    f.s.tabs = Object.fromEntries(Object.entries(f.s.tabs).filter(([k]) => k !== f.id('t3')))
    const space = f.s.spaces[S]
    if (space) space.today = [f.id('t1'), f.id('t2')]
    const pruned = selectionReducer(sel, { type: 'prune', order: f.order() })
    expect(f.names(pruned)).toEqual(['p1', 't1'])
    expect(pruned.anchor).toBeNull()
    // Nothing gone: the same object comes back.
    expect(selectionReducer(pruned, { type: 'prune', order: f.order() })).toBe(pruned)
    // A split member is one split row, not a selectable tab row.
    const t1 = f.s.tabs[f.id('t1')]
    if (t1) t1.splitId = crypto.randomUUID()
    expect(f.names(selectionReducer(pruned, { type: 'prune', order: f.order() }))).toEqual(['p1'])
  })

  it('stops at 200 tabs, and tabs.bulk refuses a 201st id', () => {
    const order = Array.from({ length: MAX_BULK + 5 }, () => crypto.randomUUID())
    const full = selectionReducer(EMPTY_SELECTION, { type: 'range', id: order.at(-1) ?? '', order, active: order[0] ?? null })
    expect(full.ids).toHaveLength(MAX_BULK)
    expect(selectionReducer(full, { type: 'toggle', id: order.at(-1) ?? '', order })).toBe(full)

    const schema = tabsIpc['tabs.bulk'].input
    const ids = (n: number) => Array.from({ length: n }, () => crypto.randomUUID())
    expect(schema.safeParse({ tabIds: ids(200), action: 'archive' }).success).toBe(true)
    expect(schema.safeParse({ tabIds: ids(201), action: 'archive' }).success).toBe(false)
    expect(schema.safeParse({ tabIds: ids(201), action: 'move', target: { section: 'today', spaceId: S, index: 0 } }).success).toBe(false)
    expect(schema.safeParse({ tabIds: [], action: 'copyUrls' }).success).toBe(false)
    expect(schema.safeParse({ tabIds: ['not-an-id'], action: 'close' }).success).toBe(false)
    expect(schema.safeParse({ tabIds: ids(2), action: 'delete' }).success).toBe(false)
  })
})
