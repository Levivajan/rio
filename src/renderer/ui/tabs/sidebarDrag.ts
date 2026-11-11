// Sidebar pointer drag (02-tabs.md F8, PLAN decision 25). A press becomes a drag after 4 px; it emits
// start, move (inside the sidebar), outside (beyond it), end and cancel on the dragBus with the pointer position. Over a
// space icon (data-space-drop) for 500 ms the sidebar switches to that space and the drag goes on there; releasing on
// the icon puts Today rows at the top of its Today and anchored rows and folders at the end of its pinned root. Over a
// collapsed folder's middle for 500 ms the folder expands. Released outside the sidebar, nothing moves (split may
// claim the drop from the 'end' event). A press on a row of a multi-selection drags every selected tab: the events carry
// their ids and the drop moves them together in order. The DOM side lives in useSidebarDrag.ts.
import type { DragEvent, DragPhase } from '../../common/dragBus'
import type { DropTarget } from '../../../shared/tabs'
import { computeDrop, type DraggedNode, type Drop, type FlatRow, type Rect } from './computeDrop'

export const DRAG_THRESHOLD_PX = 4
export const SPRING_LOAD_MS = 500

export interface DragEnv {
  rows(): FlatRow[]
  sidebarRect(): Rect | null
  /** The space drop target under the point, if any. */
  spaceAt(x: number, y: number): string | null
  activeSpaceId(): string
  pinnedCount(spaceId: string): number
  move(nodeId: string, target: DropTarget): void
  /** A multi-selection drop: `target` is computed for the pressed row alone (see bulkDropIndex). */
  moveTabs(tabIds: string[], pressedId: string, target: DropTarget): void
  switchSpace(spaceId: string): void
  expandFolder(folderId: string): void
  emit(e: DragEvent): void
  setTimer(fn: () => void, ms: number): unknown
  clearTimer(handle: unknown): void
}

export interface DragView {
  dragging: boolean
  node: DraggedNode | null
  tabIds: string[]
  x: number
  y: number
  /** Pointer offset inside the pressed row, so the ghost keeps its grab point. */
  grabX: number
  grabY: number
  source: Rect | null
  drop: Drop | null
  spaceHover: string | null
}

const IDLE: DragView = { dragging: false, node: null, tabIds: [], x: 0, y: 0, grabX: 0, grabY: 0, source: null, drop: null, spaceHover: null }

const contains = (r: Rect, x: number, y: number) => x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height

/** F8: a release on a space icon puts a Today row at the top of Today, anything else at the end of the pinned root. */
export function spaceIconTarget(node: DraggedNode, spaceId: string, pinnedCount: number): DropTarget {
  return node.section === 'today' && node.kind === 'tab'
    ? { section: 'today', spaceId, index: 0 }
    : { section: 'pinned', spaceId, parentId: null, index: pinnedCount }
}

/** Turns a drop index computed for the pressed row alone (its list without it, as computeDrop counts) into the index
 * tabs.bulk counts (the list without every moved tab): they land before the first row at or after the slot that is
 * not moving. */
export function bulkDropIndex(list: readonly string[], pressedId: string, moving: readonly string[], index: number): number {
  const anchor = list.filter((id) => id !== pressedId).slice(Math.max(0, index)).find((id) => !moving.includes(id))
  const without = list.filter((id) => !moving.includes(id))
  return anchor === undefined ? without.length : without.indexOf(anchor)
}

export function createSidebarDrag(env: DragEnv) {
  let view: DragView = IDLE
  let pressed: { node: DraggedNode; tabIds: string[]; x: number; y: number; source: Rect } | null = null
  let spring: { key: string; handle: unknown } | null = null
  const listeners = new Set<() => void>()

  const set = (next: DragView) => {
    view = next
    for (const fn of listeners) fn()
  }
  const emit = (phase: DragPhase, x: number, y: number) => {
    const node = view.node ?? pressed?.node
    if (node) env.emit({ phase, x, y, tabId: node.id, tabIds: view.tabIds.length ? view.tabIds : (pressed?.tabIds ?? []) })
  }
  const stopSpring = () => {
    if (spring) env.clearTimer(spring.handle)
    spring = null
  }
  /** Runs fn after 500 ms unless the pointer leaves `key` first. */
  const springOn = (key: string | null, fn: () => void) => {
    if (spring?.key === key) return
    stopSpring()
    if (key) spring = { key, handle: env.setTimer(() => ((spring = null), fn()), SPRING_LOAD_MS) }
  }

  /** Recomputes the drop for the pointer and reports whether it is inside the sidebar. */
  function track(x: number, y: number): boolean {
    const node = view.node
    if (!node) return false
    const bounds = env.sidebarRect()
    const inside = !!bounds && contains(bounds, x, y)
    if (!inside) {
      stopSpring()
      set({ ...view, x, y, drop: null, spaceHover: null })
      return false
    }
    const space = env.spaceAt(x, y)
    if (space) {
      springOn(`space:${space}`, () => {
        if (view.dragging && space !== env.activeSpaceId()) env.switchSpace(space)
      })
      set({ ...view, x, y, drop: null, spaceHover: space })
      return true
    }
    const rows = env.rows()
    const indentX = x - view.grabX
    const drop = computeDrop(rows, x, y, node, indentX)
    const folderId = drop?.indicator.kind === 'inside' && drop.target.section === 'pinned' ? drop.target.parentId : null
    const collapsed = folderId ? rows.some((r) => r.kind === 'folder' && r.id === folderId && r.expanded === false) : false
    springOn(collapsed && folderId ? `folder:${folderId}` : null, () => {
      if (view.dragging && folderId) env.expandFolder(folderId)
    })
    set({ ...view, x, y, drop, spaceHover: null })
    return true
  }

  function reset() {
    stopSpring()
    pressed = null
    set(IDLE)
  }

  return {
    get: (): DragView => view,
    subscribe(fn: () => void): () => void {
      listeners.add(fn)
      return () => void listeners.delete(fn)
    },
    /** A primary-button press on a row; nothing shows until the pointer travels 4 px. */
    press(node: DraggedNode, tabIds: string[], x: number, y: number, source: Rect): void {
      if (view.dragging) return
      pressed = { node, tabIds, x, y, source }
    },
    move(x: number, y: number): void {
      if (!view.dragging) {
        const p = pressed
        if (!p || Math.hypot(x - p.x, y - p.y) < DRAG_THRESHOLD_PX) return
        set({ ...IDLE, dragging: true, node: p.node, tabIds: p.tabIds, x, y, grabX: p.x - p.source.x, grabY: p.y - p.source.y, source: p.source })
        emit('start', x, y)
      }
      emit(track(x, y) ? 'move' : 'outside', x, y)
    },
    /** Ends a press or a drag. Returns true when it was a drag, so the click that follows is swallowed. */
    release(x: number, y: number): boolean {
      if (!view.dragging) {
        pressed = null
        return false
      }
      const inside = track(x, y)
      const { node, drop, spaceHover, tabIds } = view
      const target = !inside || !node ? null : spaceHover ? spaceIconTarget(node, spaceHover, env.pinnedCount(spaceHover)) : (drop?.target ?? null)
      if (node && target) {
        if (tabIds.length > 1) env.moveTabs(tabIds, node.id, target)
        else env.move(node.id, target)
      }
      emit('end', x, y)
      reset()
      return true
    },
    /** Esc, pointercancel or losing focus: nothing moves. */
    cancel(): void {
      if (view.dragging) emit('cancel', view.x, view.y)
      reset()
    }
  }
}

export type SidebarDrag = ReturnType<typeof createSidebarDrag>
