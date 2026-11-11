// Where a dragged sidebar node lands (02-tabs.md F8). Pure: rows in visual order with their rects, the
// pointer, and the dragged node give a DropTarget for tabs.move plus what the drag layer draws, or null for no drop.
// Tab rows split in halves; folder rows in thirds (top before, middle inside, bottom after). Below the last row of a
// folder the indent X picks the level. Indices count the target list without the dragged node (TabStore.move).
import { MAX_FAVORITES, type DropTarget } from '../../../shared/tabs'

export interface Rect { x: number; y: number; width: number; height: number }
/** One level of a tree row's path from the section root: the node, its parent folder and its index there. */
export interface PathStep { id: string; parentId: string | null; index: number }

export type FlatRow =
  /** A split's one row stands for its members, which sit together in one list: `path` is the first member's, `span`
   * how many list places the row covers (a drop after it goes after the last member). */
  | { kind: 'tab' | 'folder'; id: string; section: 'pinned' | 'today'; spaceId: string; level: number; path: PathStep[]; rect: Rect; expanded?: boolean; childCount?: number; span?: number }
  | { kind: 'favorite'; id: string; index: number; rect: Rect }
  /** favorites (the zero-favorites zone), pinned (the label or empty-Pinned zone), today (label, New Tab row) and
   * today-end (the empty list area below the rows, at index = Today's length). */
  | { kind: 'zone'; zone: 'favorites' | 'pinned' | 'today' | 'today-end'; spaceId: string; index: number; rect: Rect }

export interface DraggedNode {
  id: string
  kind: 'tab' | 'folder'
  section: 'favorite' | 'pinned' | 'today'
  spaceId: string | null
  parentId: string | null
  index: number
}

export type DropIndicator =
  | { kind: 'line'; rect: Rect; edge: 'top' | 'bottom'; level: number }
  | { kind: 'inside'; rect: Rect }
  | { kind: 'zone'; rect: Rect }
  | { kind: 'tile'; rect: Rect; edge: 'left' | 'right' }

export interface Drop { target: DropTarget; indicator: DropIndicator }

/** Indent per folder level, as FolderRow's `indent` (--folder-indent). */
export const INDENT_PX = 16

const inside = (r: Rect, x: number, y: number) => x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height

/** Rows sit a 5 px gap apart and a drop rect takes only 1 px of it (collectRows), so a pointer in the rest of the gap,
 *  say between two pinned folders, goes to the nearer tree row: above a row's top it drops before it, under its bottom
 *  after it. -1 when no tree row is within a gap of the pointer. */
const ROW_GAP_PX = 5
function nearestTreeRow(rows: readonly FlatRow[], x: number, y: number): number {
  let best = -1
  let bestDistance = ROW_GAP_PX
  rows.forEach((r, i) => {
    if (r.kind === 'favorite' || r.kind === 'zone' || x < r.rect.x || x >= r.rect.x + r.rect.width) return
    const distance = y < r.rect.y ? r.rect.y - y : y - (r.rect.y + r.rect.height)
    if (distance >= 0 && distance <= bestDistance) {
      best = i
      bestDistance = distance
    }
  })
  return best
}

/** Shifts the index past the dragged node when it leaves the same list earlier; null when nothing would change. */
function settle(target: DropTarget, d: DraggedNode, indicator: DropIndicator): Drop | null {
  const sameList =
    target.section === d.section &&
    (target.section === 'favorite' || (target.spaceId === d.spaceId && (target.section !== 'pinned' || target.parentId === d.parentId)))
  if (!sameList) return { target, indicator }
  const index = d.index < target.index ? target.index - 1 : target.index
  if (index === d.index) return null
  return { target: { ...target, index }, indicator }
}

/**
 * `indentX` is the left edge of the dragged row's ghost (defaults to pointerX): its offset from a row's left edge in
 * steps of INDENT_PX picks the level at a folder's end.
 */
export function computeDrop(rows: readonly FlatRow[], pointerX: number, pointerY: number, dragged: DraggedNode, indentX = pointerX): Drop | null {
  const hit = rows.findIndex((r) => inside(r.rect, pointerX, pointerY))
  const at = hit >= 0 ? hit : nearestTreeRow(rows, pointerX, pointerY)
  const row = rows[at]
  if (!row) return null

  if (row.kind === 'favorite') {
    if (row.id === dragged.id) return null
    // A full grid takes no new tile, so it draws no drop line that would do nothing on release.
    if (dragged.section !== 'favorite' && rows.filter((r) => r.kind === 'favorite').length >= MAX_FAVORITES) return null
    const right = pointerX >= row.rect.x + row.rect.width / 2
    return settle({ section: 'favorite', index: row.index + (right ? 1 : 0) }, dragged, { kind: 'tile', rect: row.rect, edge: right ? 'right' : 'left' })
  }

  if (row.kind === 'zone') {
    const indicator: DropIndicator = { kind: 'zone', rect: row.rect }
    if (row.zone === 'favorites') return settle({ section: 'favorite', index: 0 }, dragged, indicator)
    if (row.zone === 'pinned') return settle({ section: 'pinned', spaceId: row.spaceId, parentId: null, index: 0 }, dragged, indicator)
    return settle({ section: 'today', spaceId: row.spaceId, index: row.zone === 'today' ? 0 : row.index }, dragged, indicator)
  }

  // A row cannot take itself or its own descendants.
  if (row.path.some((s) => s.id === dragged.id)) return null
  const self = row.path[row.path.length - 1]
  if (!self) return null
  const rel = (pointerY - row.rect.y) / row.rect.height
  const place = (parentId: string | null, index: number): DropTarget =>
    row.section === 'today' ? { section: 'today', spaceId: row.spaceId, index } : { section: 'pinned', spaceId: row.spaceId, parentId, index }

  const zone = row.kind === 'folder' ? (rel < 1 / 3 ? 'before' : rel > 2 / 3 ? 'after' : 'inside') : rel < 0.5 ? 'before' : 'after'
  if (zone === 'before') return settle(place(self.parentId, self.index), dragged, { kind: 'line', rect: row.rect, edge: 'top', level: row.level })
  if (zone === 'inside') return settle(place(row.id, row.childCount ?? 0), dragged, { kind: 'inside', rect: row.rect })

  // After the row: any level from the next row's level up to this row's (one deeper into an open folder).
  const next = rows[at + 1]
  const max = row.level + (row.kind === 'folder' && row.expanded ? 1 : 0)
  const min = next && next.kind !== 'favorite' && next.kind !== 'zone' && next.section === row.section ? Math.min(next.level, max) : 1
  const wanted = 1 + Math.round((indentX - row.rect.x) / INDENT_PX)
  const level = Math.max(min, Math.min(max, wanted))
  const indicator: DropIndicator = { kind: 'line', rect: row.rect, edge: 'bottom', level }
  if (level > row.level) return settle(place(row.id, 0), dragged, indicator)
  const step = row.path[level - 1]
  if (!step) return null
  return settle(place(step.parentId, step.index + (level === row.level ? (row.span ?? 1) : 1)), dragged, indicator)
}
