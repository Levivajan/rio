import { describe, expect, it } from 'vitest'
import { MAX_FAVORITES } from '../../../shared/tabs'
import { computeDrop, type DraggedNode, type FlatRow, type PathStep } from './computeDrop'

const S = 'space-s'
const H = 34
const W = 240
const FAV_Y = 0

// Favorites f0 f1, then Pinned: a, F (open: b, G (open: c)), H (collapsed, 3 children); Today label; t0 t1 t2; empty area.
const favs: FlatRow[] = [
  { kind: 'favorite', id: 'f0', index: 0, rect: { x: 0, y: FAV_Y, width: 50, height: 44 } },
  { kind: 'favorite', id: 'f1', index: 1, rect: { x: 56, y: FAV_Y, width: 50, height: 44 } }
]
const p = (id: string, parentId: string | null, index: number): PathStep => ({ id, parentId, index })
type TreeSpec = { kind: 'tab' | 'folder'; id: string; section: 'pinned' | 'today'; path: PathStep[]; expanded?: boolean; childCount?: number }
const tree: TreeSpec[] = [
  { kind: 'tab', id: 'a', section: 'pinned', path: [p('a', null, 0)] },
  { kind: 'folder', id: 'F', section: 'pinned', path: [p('F', null, 1)], expanded: true, childCount: 2 },
  { kind: 'tab', id: 'b', section: 'pinned', path: [p('F', null, 1), p('b', 'F', 0)] },
  { kind: 'folder', id: 'G', section: 'pinned', path: [p('F', null, 1), p('G', 'F', 1)], expanded: true, childCount: 1 },
  { kind: 'tab', id: 'c', section: 'pinned', path: [p('F', null, 1), p('G', 'F', 1), p('c', 'G', 0)] },
  { kind: 'folder', id: 'H', section: 'pinned', path: [p('H', null, 2)], expanded: false, childCount: 3 }
]
const today: TreeSpec[] = ['t0', 't1', 't2'].map((id, i) => ({ kind: 'tab', id, section: 'today', path: [p(id, null, i)] }))

const rows: FlatRow[] = [...favs]
let y = 60
const add = (spec: TreeSpec) => {
  rows.push({ ...spec, spaceId: S, level: spec.path.length, rect: { x: 0, y, width: W, height: H } })
  y += H
}
tree.forEach(add)
rows.push({ kind: 'zone', zone: 'today', spaceId: S, index: 0, rect: { x: 0, y, width: W, height: 24 } })
y += 24
today.forEach(add)
rows.push({ kind: 'zone', zone: 'today-end', spaceId: S, index: 3, rect: { x: 0, y, width: W, height: 200 } })

const rowOf = (id: string) => {
  const r = rows.find((x) => x.kind !== 'zone' && x.id === id)
  if (!r) throw new Error(id)
  return r.rect
}
/** Pointer at a fraction of a row's height (and an x inside it). */
const at = (id: string, frac: number, x = 100) => ({ x, y: rowOf(id).y + rowOf(id).height * frac })

const todayTab = (id: string, index: number, spaceId = S): DraggedNode => ({ id, kind: 'tab', section: 'today', spaceId, parentId: null, index })
const pinnedNode = (id: string, kind: 'tab' | 'folder', parentId: string | null, index: number): DraggedNode => ({ id, kind, section: 'pinned', spaceId: S, parentId, index })
const favorite = (id: string, index: number): DraggedNode => ({ id, kind: 'tab', section: 'favorite', spaceId: null, parentId: null, index })
const pinned = (parentId: string | null, index: number) => ({ section: 'pinned', spaceId: S, parentId, index })
const todayAt = (index: number) => ({ section: 'today', spaceId: S, index })

const zero: FlatRow[] = [{ kind: 'zone', zone: 'favorites', spaceId: S, index: 0, rect: { x: 0, y: 0, width: W, height: 44 } }]
const pinZone: FlatRow[] = [{ kind: 'zone', zone: 'pinned', spaceId: S, index: 0, rect: { x: 0, y: 0, width: W, height: 32 } }]

type Case = [name: string, pointer: { x: number; y: number }, dragged: DraggedNode, expected: unknown, indentX?: number, list?: FlatRow[]]
const cases: Case[] = [
  ['top half of the next row is a no-op for the row above', at('t1', 0.2), todayTab('t0', 0), null],
  ['top half of the first Today row inserts before it', at('t0', 0.2), todayTab('t2', 2), todayAt(0)],
  ['bottom half of the last row counts the list without the dragged row', at('t2', 0.8), todayTab('t0', 0), todayAt(2)],
  ['a row over itself is a no-op', at('t1', 0.5), todayTab('t1', 1), null],
  ['Today row over the top of a pinned row pins it there', at('a', 0.1), todayTab('t0', 0), pinned(null, 0)],
  ['middle third of a folder goes inside at its end', at('F', 0.5), todayTab('t0', 0), pinned('F', 2)],
  ['top third of a folder inserts before it', at('F', 0.2), todayTab('t0', 0), pinned(null, 1)],
  ['bottom third of an open folder with children goes inside first, whatever the X', at('F', 0.9), todayTab('t0', 0), pinned('F', 0), 0],
  ['below a folder\'s last row, X at level 3 stays in the inner folder', at('c', 0.9), todayTab('t0', 0), pinned('G', 1), 32],
  ['below a folder\'s last row, X at level 2 lands after the inner folder', at('c', 0.9), todayTab('t0', 0), pinned('F', 2), 16],
  ['below a folder\'s last row, X at level 1 lands after the outer folder', at('c', 0.9), todayTab('t0', 0), pinned(null, 2), 0],
  ['X past the deepest level clamps to the row\'s own level', at('c', 0.9), todayTab('t0', 0), pinned('G', 1), 200],
  ['a folder over its own child is null', at('b', 0.5), pinnedNode('F', 'folder', null, 1), null],
  ['a folder over a grandchild is null', at('c', 0.2), pinnedNode('F', 'folder', null, 1), null],
  ['a folder over Today yields a Today target that main rejects with a toast', at('t1', 0.2), pinnedNode('F', 'folder', null, 1), todayAt(1)],
  ['nothing under the pointer is null', { x: 100, y: 50 }, todayTab('t0', 0), null],
  ['right of the window is null', { x: W + 40, y: rowOf('t1').y + 4 }, todayTab('t0', 0), null],
  ['right half of a favorite inserts after it', { x: 40, y: 20 }, todayTab('t0', 0), { section: 'favorite', index: 1 }],
  ['a favorite onto the left half of the first favorite', { x: 10, y: 20 }, favorite('f1', 1), { section: 'favorite', index: 0 }],
  ['a favorite over itself is null', { x: 40, y: 20 }, favorite('f0', 0), null],
  ['a favorite next to itself is a no-op', { x: 60, y: 20 }, favorite('f0', 0), null],
  ['the zero-favorites zone takes index 0', { x: 100, y: 20 }, todayTab('t0', 0), { section: 'favorite', index: 0 }, undefined, zero],
  ['the empty-Pinned zone pins at the top of Pinned', { x: 100, y: 10 }, todayTab('t0', 0), pinned(null, 0), undefined, pinZone],
  ['the empty area below the rows goes to the end of Today', { x: 100, y: rowOf('t2').y + 100 }, todayTab('t0', 0), todayAt(2)],
  ['the Today label goes to the top of Today', { x: 100, y: rowOf('t0').y - 10 }, todayTab('t2', 2), todayAt(0)],
  ['middle of a collapsed folder goes inside at its end', at('H', 0.5), todayTab('t0', 0), pinned('H', 3)],
  ['bottom of the last pinned row, before the Today label, stays at the root', at('H', 0.9), todayTab('t0', 0), pinned(null, 3), 60],
  ['a folder\'s child dropped inside its own folder moves to the end', at('F', 0.5), pinnedNode('b', 'tab', 'F', 0), pinned('F', 1)],
  ['a folder\'s last child dropped inside its own folder is a no-op', at('F', 0.5), pinnedNode('G', 'folder', 'F', 1), null],
  ['a root pinned row below a level 2 row with a level 2 neighbor goes into that folder', at('b', 0.9), pinnedNode('a', 'tab', null, 0), pinned('F', 1), 0],
  ['a row from another space keeps the target index', at('t1', 0.2), todayTab('x0', 0, 'space-other'), todayAt(1)]
]

// A pinned split row over members s0 and s1 at pinned 0 and 1, then x at 2.
const split: FlatRow[] = [
  { kind: 'tab', id: 's0', section: 'pinned', spaceId: S, level: 1, path: [p('s0', null, 0)], span: 2, rect: { x: 0, y: 0, width: W, height: H } },
  { kind: 'tab', id: 'x', section: 'pinned', spaceId: S, level: 1, path: [p('x', null, 2)], rect: { x: 0, y: H, width: W, height: H } }
]
cases.push(
  ['top half of a pinned split row pins before its first member', { x: 100, y: 4 }, todayTab('t0', 0), pinned(null, 0), undefined, split],
  ['bottom half of a split row pins after its last member', { x: 100, y: H - 4 }, todayTab('t0', 0), pinned(null, 2), 0, split]
)

describe('computeDrop', () => {
  it.each(cases)('%s', (_name, pt, dragged, expected, indentX, list = rows) => {
    const drop = computeDrop(list, pt.x, pt.y, dragged, indentX)
    expect(drop?.target ?? null).toEqual(expected)
  })

  it('draws a line at the chosen level, a folder highlight for inside, a zone and a tile edge', () => {
    expect(computeDrop(rows, at('c', 0.9).x, at('c', 0.9).y, todayTab('t0', 0), 12)?.indicator).toMatchObject({ kind: 'line', edge: 'bottom', level: 2 })
    expect(computeDrop(rows, at('a', 0.1).x, at('a', 0.1).y, todayTab('t0', 0))?.indicator).toMatchObject({ kind: 'line', edge: 'top', level: 1 })
    expect(computeDrop(rows, at('F', 0.5).x, at('F', 0.5).y, todayTab('t0', 0))?.indicator).toEqual({ kind: 'inside', rect: rowOf('F') })
    expect(computeDrop(zero, 100, 20, todayTab('t0', 0))?.indicator.kind).toBe('zone')
    expect(computeDrop(rows, 40, 20, todayTab('t0', 0))?.indicator).toMatchObject({ kind: 'tile', edge: 'right' })
  })

  it('offers no tile drop on a full favorites grid, but still reorders inside it', () => {
    const full: FlatRow[] = Array.from({ length: MAX_FAVORITES }, (_, i) => ({ kind: 'favorite', id: `f${i}`, index: i, rect: { x: i * 56, y: FAV_Y, width: 50, height: 44 } }))
    expect(computeDrop(full, 40, 20, todayTab('t0', 0))).toBeNull()
    expect(computeDrop(full, 10, 20, favorite('f1', 1))?.target).toEqual({ section: 'favorite', index: 0 })
  })

  it('drops a Today tab into the gap between two pinned folders with a line there', () => {
    // As collectRows measures them: 34 tall rows 5 apart, each rect taking 1 px of the gap below it.
    const gapped: FlatRow[] = [
      { kind: 'folder', id: 'P', section: 'pinned', spaceId: S, level: 1, path: [p('P', null, 0)], rect: { x: 0, y: 100, width: W, height: 35 }, expanded: false, childCount: 1 },
      { kind: 'folder', id: 'Q', section: 'pinned', spaceId: S, level: 1, path: [p('Q', null, 1)], rect: { x: 0, y: 139, width: W, height: 35 }, expanded: true, childCount: 2 },
      { kind: 'tab', id: 't0', section: 'today', spaceId: S, level: 1, path: [p('t0', null, 0)], rect: { x: 0, y: 300, width: W, height: 35 } }
    ]
    for (const y of [135.5, 137, 138.5]) {
      const drop = computeDrop(gapped, 100, y, todayTab('t0', 0))
      expect(drop?.target, `pointer at ${y}`).toEqual(pinned(null, 1))
      expect(drop?.indicator).toMatchObject({ kind: 'line', level: 1 })
    }
    // Over the folder's middle third it still drops inside, and far from any row nothing happens.
    expect(computeDrop(gapped, 100, 117, todayTab('t0', 0))?.indicator.kind).toBe('inside')
    expect(computeDrop(gapped, 100, 250, todayTab('t0', 0))).toBeNull()
    expect(computeDrop(gapped, W + 20, 137, todayTab('t0', 0))).toBeNull()
  })
})
