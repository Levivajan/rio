// DOM side of the sidebar drag (02-tabs.md F8): pointer events only, no HTML5 drag. A press listens on the
// window; once it is a drag the root takes pointer capture, so a drag that crosses the page view keeps tracking (macOS
// keeps delivering a mouse drag to the view that got the mouse-down). Rows come from the DOM (rects) and the state
// (tree paths). Esc cancels, the list auto-scrolls within 32 px of its edges, and a moved row animates from where it
// was dropped (FLIP, 70 ms, none under reduced motion). tabs.move is not optimistic: main applies or toasts.
import { useLayoutEffect, useSyncExternalStore, type PointerEvent as ReactPointerEvent } from 'react'
import type { CluState, NodeRef } from '../../../shared/state'
import type { DropTarget } from '../../../shared/tabs'
import { dragBus } from '../../common/dragBus'
import { stateMirror } from '../../common/store'
import { INDENT_PX, type DraggedNode, type FlatRow, type PathStep, type Rect } from './computeDrop'
import { hoverLeave } from './hoverCard'
import { bulkDropIndex, createSidebarDrag, type DragView } from './sidebarDrag'
import { clu } from '../../common/clu'

export const AUTO_SCROLL_ZONE_PX = 32
const AUTO_SCROLL_MAX_PX = 12
/** A dropped row settles in about 70 ms. */
const FLIP_MS = 70
const FLIP_WAIT_MS = 1000
/** The bands of the zone markers: half the 36 pt space header, and the 8 pt gap beside it. */
const ZONE_BAND_HEADER = 18
const ZONE_BAND_GAP = 8

const rectOf = (el: Element): Rect => {
  const r = el.getBoundingClientRect()
  return { x: r.x, y: r.y, width: r.width, height: r.height }
}

function containerOf(s: CluState, spaceId: string, id: string): readonly NodeRef[] {
  for (const f of Object.values(s.folders)) if (f.spaceId === spaceId && f.children.some((c) => c.id === id)) return f.children
  return s.spaces[spaceId]?.pinned ?? []
}

/** Where a tab or folder sits now (section, list and index), or null when it is gone. */
export function draggedNode(s: CluState, id: string): DraggedNode | null {
  const t = s.tabs[id]
  if (t) {
    if (t.section === 'favorite') return { id, kind: 'tab', section: 'favorite', spaceId: null, parentId: null, index: (s.favorites[t.profileId] ?? []).indexOf(id) }
    const spaceId = t.spaceId ?? ''
    if (t.section === 'today') return { id, kind: 'tab', section: 'today', spaceId, parentId: null, index: (s.spaces[spaceId]?.today ?? []).indexOf(id) }
    const list = t.parentId ? (s.folders[t.parentId]?.children ?? []) : (s.spaces[spaceId]?.pinned ?? [])
    return { id, kind: 'tab', section: 'pinned', spaceId, parentId: t.parentId, index: list.findIndex((r) => r.id === id) }
  }
  const f = s.folders[id]
  if (!f) return null
  const parent = Object.values(s.folders).find((p) => p.spaceId === f.spaceId && p.children.some((c) => c.id === id))
  return { id, kind: 'folder', section: 'pinned', spaceId: f.spaceId, parentId: parent?.id ?? null, index: containerOf(s, f.spaceId, id).findIndex((r) => r.id === id) }
}

interface TreeInfo { section: 'pinned' | 'today'; path: PathStep[]; expanded?: boolean; childCount?: number }

/** Paths of every pinned and Today node of a space (a folder cycle is walked once). */
export function treeInfo(s: CluState, spaceId: string): Map<string, TreeInfo> {
  const out = new Map<string, TreeInfo>()
  const walk = (refs: readonly NodeRef[], parentId: string | null, above: PathStep[]) => {
    refs.forEach((r, index) => {
      if (out.has(r.id)) return
      const path = [...above, { id: r.id, parentId, index }]
      const f = r.kind === 'folder' ? s.folders[r.id] : undefined
      out.set(r.id, f ? { section: 'pinned', path, expanded: !f.collapsed, childCount: f.children.length } : { section: 'pinned', path })
      if (f) walk(f.children, f.id, path)
    })
  }
  walk(s.spaces[spaceId]?.pinned ?? [], null, [])
  ;(s.spaces[spaceId]?.today ?? []).forEach((id, index) => out.set(id, { section: 'today', path: [{ id, parentId: null, index }] }))
  return out
}

/** The drop rows of a sidebar in visual order: favorites, then the tab tree clipped to the visible list. */
export function collectRows(root: HTMLElement, spaceId: string, s: CluState): FlatRow[] {
  const out: FlatRow[] = []
  root.querySelectorAll('.clu-favorites .clu-favorite[data-tab-id]').forEach((el, index) => {
    out.push({ kind: 'favorite', id: el.getAttribute('data-tab-id') ?? '', index, rect: rectOf(el) })
  })
  // The zero-favorites and empty-Pinned zones are zero-height markers, so nothing moves when a drag starts. Each
  // takes a band around its marker instead: Favorites the gap under the pill and the space header's top half, Pinned
  // the header's bottom half and the first few points under it.
  const favZone = root.querySelector('[data-drop-zone="favorites"]')
  if (favZone) {
    const m = rectOf(favZone)
    out.push({ kind: 'zone', zone: 'favorites', spaceId, index: 0, rect: { x: m.x, y: m.y - ZONE_BAND_GAP, width: m.width, height: ZONE_BAND_GAP + ZONE_BAND_HEADER } })
  }
  const list = root.querySelector('.clu-tab-list')
  const tree = list?.querySelector('.clu-tab-tree')
  if (!list || !tree) return out
  const view = rectOf(list)
  const clip = (r: Rect): Rect | null => {
    const top = Math.max(r.y, view.y)
    const bottom = Math.min(r.y + r.height, view.y + view.height)
    return bottom > top ? { x: r.x, y: top, width: r.width, height: bottom - top } : null
  }
  const info = treeInfo(s, spaceId)
  for (const el of tree.querySelectorAll('[role="treeitem"][data-tab-id], [role="treeitem"][data-folder-id], [role="treeitem"][data-split-id], [data-drop-zone]')) {
    const zone = el.getAttribute('data-drop-zone')
    if (zone === 'pinned') {
      const m = rectOf(el)
      if (m.y >= view.y && m.y <= view.y + view.height) out.push({ kind: 'zone', zone, spaceId, index: 0, rect: { x: m.x, y: m.y - ZONE_BAND_HEADER, width: m.width, height: ZONE_BAND_HEADER + ZONE_BAND_GAP } })
      continue
    }
    // Each row takes 1 px of the 5 px gap below it; computeDrop gives the rest of the gap to the nearer row. A row in a
    // folder reports the top-level row's left edge: computeDrop and the drop line add the indent per level themselves.
    const box = rectOf(el)
    const shift = (Math.max(1, Number(el.getAttribute('aria-level')) || 1) - 1) * INDENT_PX
    const r = clip({ x: box.x - shift, y: box.y, width: box.width + shift, height: box.height + 1 })
    if (!r) continue
    if (zone === 'today') {
      out.push({ kind: 'zone', zone, spaceId, index: 0, rect: r })
      continue
    }
    if (el.hasAttribute('data-split-id')) {
      // A split row: a drop before it goes before its first member, after it after its last.
      const members = [...el.querySelectorAll('[data-split-member]')]
        .map((m) => info.get(m.getAttribute('data-split-member') ?? ''))
        .filter((t): t is TreeInfo => !!t)
        .sort((a, b) => (a.path.at(-1)?.index ?? 0) - (b.path.at(-1)?.index ?? 0))
      const first = members[0]
      const last = members.at(-1)
      const firstStep = first?.path.at(-1)
      if (!first || !last || !firstStep) continue
      const span = (last.path.at(-1)?.index ?? firstStep.index) - firstStep.index + 1
      out.push({ kind: 'tab', id: firstStep.id, section: first.section, spaceId, level: first.path.length, path: first.path, rect: r, span })
      continue
    }
    const id = el.getAttribute('data-tab-id') ?? el.getAttribute('data-folder-id') ?? ''
    const t = info.get(id)
    if (!t) continue
    out.push({ kind: el.hasAttribute('data-folder-id') ? 'folder' : 'tab', id, section: t.section, spaceId, level: t.path.length, path: t.path, rect: r, ...(t.expanded === undefined ? {} : { expanded: t.expanded, childCount: t.childCount ?? 0 }) })
  }
  const treeRect = rectOf(tree)
  const below = clip({ x: view.x, y: treeRect.y + treeRect.height, width: view.width, height: view.y + view.height - (treeRect.y + treeRect.height) })
  if (below) out.push({ kind: 'zone', zone: 'today-end', spaceId, index: s.spaces[spaceId]?.today.length ?? 0, rect: below })
  return out
}

/** The ids of the list a drop target names, as the state has it now. */
export function targetList(s: CluState, target: DropTarget, activeSpaceId: string): readonly string[] {
  if (target.section === 'favorite') return s.favorites[s.spaces[activeSpaceId]?.profileId ?? ''] ?? []
  if (target.section === 'today') return s.spaces[target.spaceId]?.today ?? []
  return (target.parentId ? (s.folders[target.parentId]?.children ?? []) : (s.spaces[target.spaceId]?.pinned ?? [])).map((r) => r.id)
}

// ---- the window's drag --------------------------------------------------------------------------------------------

let root: HTMLElement | null = null
let ghost: HTMLElement | null = null
let flip: { id: string; from: Rect; before: string; at: number } | null = null
const spaceOf = () => root?.dataset.activeSpace ?? ''
const placeKey = (id: string) => JSON.stringify(draggedNode(stateMirror().get(), id))

export const sidebarDrag = createSidebarDrag({
  rows: () => (root ? collectRows(root, spaceOf(), stateMirror().get()) : []),
  sidebarRect: () => (root?.isConnected ? rectOf(root) : null),
  spaceAt: (x, y) => document.elementFromPoint(x, y)?.closest('[data-space-drop]')?.getAttribute('data-space-drop') ?? null,
  activeSpaceId: spaceOf,
  pinnedCount: (spaceId) => stateMirror().get().spaces[spaceId]?.pinned.length ?? 0,
  move(nodeId, target) {
    const v = sidebarDrag.get()
    if (v.source) flip = { id: nodeId, from: { ...v.source, x: v.x - v.grabX, y: v.y - v.grabY }, before: placeKey(nodeId), at: performance.now() }
    void clu().tabs.move({ nodeId, target, ask: true, drop: true })
  },
  moveTabs(tabIds, pressedId, target) {
    const v = sidebarDrag.get()
    if (v.source) flip = { id: pressedId, from: { ...v.source, x: v.x - v.grabX, y: v.y - v.grabY }, before: placeKey(pressedId), at: performance.now() }
    const index = bulkDropIndex(targetList(stateMirror().get(), target, spaceOf()), pressedId, tabIds, target.index)
    void clu().tabs.bulk({ tabIds, action: 'move', target: { ...target, index }, ask: true, drop: true })
  },
  switchSpace: (spaceId) => void clu().spaces.switch({ spaceId }),
  expandFolder: (folderId) => void clu().folders.setCollapsed({ folderId, collapsed: false }),
  emit: (e) => dragBus.emit(e),
  setTimer: (fn, ms) => setTimeout(fn, ms),
  clearTimer: (h) => clearTimeout(h as ReturnType<typeof setTimeout>)
})

/** A copy of the pressed row for the ghost: no ids, data attributes or roles, so it never matches a row query. The
 *  ghost sits at the row's own box, so the copy drops the folder indent margin it would otherwise add again. */
function cloneForGhost(el: HTMLElement): HTMLElement {
  const copy = el.cloneNode(true) as HTMLElement
  for (const n of [copy, ...copy.querySelectorAll<HTMLElement>('*')]) {
    for (const a of [...n.attributes]) if (a.name === 'id' || a.name === 'role' || a.name === 'tabindex' || a.name.startsWith('data-') || a.name.startsWith('aria-')) n.removeAttribute(a.name)
  }
  copy.style.width = `${el.getBoundingClientRect().width}px`
  copy.style.marginInlineStart = '0'
  return copy
}

function swallowNextClick(): void {
  const stop = (e: MouseEvent) => {
    e.stopPropagation()
    e.preventDefault()
  }
  window.addEventListener('click', stop, { capture: true, once: true })
  setTimeout(() => window.removeEventListener('click', stop, true), 0)
}

function autoScroll(): void {
  const v = sidebarDrag.get()
  const list = root?.querySelector('.clu-tab-list')
  if (!v.dragging || !list) return
  const r = list.getBoundingClientRect()
  if (v.x >= r.left && v.x < r.right) {
    const up = r.top + AUTO_SCROLL_ZONE_PX - v.y
    const down = v.y - (r.bottom - AUTO_SCROLL_ZONE_PX)
    const step = up > 0 && v.y >= r.top ? -up : down > 0 && v.y < r.bottom ? down : 0
    if (step) {
      const before = list.scrollTop
      list.scrollTop += Math.max(-AUTO_SCROLL_MAX_PX, Math.min(AUTO_SCROLL_MAX_PX, Math.round(step / 2) || Math.sign(step)))
      if (list.scrollTop !== before) sidebarDrag.move(v.x, v.y)
    }
  }
  requestAnimationFrame(autoScroll)
}

/** Starts listening for a drag of `nodeId` from a primary press; tabIds go on the dragBus. */
export function pressRow(e: ReactPointerEvent<HTMLElement>, nodeId: string, tabIds: string[]): void {
  if (e.button !== 0 || e.ctrlKey || e.metaKey || e.shiftKey || sidebarDrag.get().dragging) return
  if ((e.target as Element).closest('input, textarea')) return
  const el = e.currentTarget
  const sidebar = el.closest<HTMLElement>('.clu-sidebar')
  // No drag in the hover-peek sidebar (PLAN 3.16).
  if (!sidebar || sidebar.dataset.mode === 'peek') return
  const node = draggedNode(stateMirror().get(), nodeId)
  if (!node || node.index < 0) return
  root = sidebar
  const pointerId = e.pointerId
  sidebarDrag.press(node, tabIds, e.clientX, e.clientY, rectOf(el))

  const onMove = (ev: PointerEvent) => {
    if (ev.pointerId !== pointerId) return
    // The button came up where this window never saw the pointerup (another app took the mouse): the press is over.
    if ((ev.buttons & 1) === 0) {
      onCancel()
      return
    }
    const was = sidebarDrag.get().dragging
    sidebarDrag.move(ev.clientX, ev.clientY)
    if (was || !sidebarDrag.get().dragging) return
    hoverLeave()
    ghost = cloneForGhost(el)
    try {
      document.documentElement.setPointerCapture(pointerId)
    } catch {
      // The pointer is already gone; the window listeners still end the drag.
    }
    requestAnimationFrame(autoScroll)
  }
  const done = () => {
    window.removeEventListener('pointermove', onMove, true)
    window.removeEventListener('pointerup', onUp, true)
    window.removeEventListener('pointercancel', onCancel, true)
    window.removeEventListener('keydown', onKey, true)
    if (document.documentElement.hasPointerCapture(pointerId)) document.documentElement.releasePointerCapture(pointerId)
    ghost = null
  }
  const onUp = (ev: PointerEvent) => {
    if (ev.pointerId !== pointerId) return
    done()
    if (sidebarDrag.release(ev.clientX, ev.clientY)) swallowNextClick()
  }
  const onCancel = () => {
    done()
    sidebarDrag.cancel()
  }
  const onKey = (ev: KeyboardEvent) => {
    if (ev.key !== 'Escape' || !sidebarDrag.get().dragging) return
    ev.preventDefault()
    ev.stopPropagation()
    onCancel()
  }
  window.addEventListener('pointermove', onMove, true)
  window.addEventListener('pointerup', onUp, true)
  window.addEventListener('pointercancel', onCancel, true)
  window.addEventListener('keydown', onKey, true)
  // No cancel on the window's blur. Pressing a row that is not selected activates it, and Chromium then
  // gives the page focus (BrowserView::OnActiveTabChanged calls RestoreFocus), so the frame blurs a few ms into every
  // such press and the drag never started. macOS keeps sending the drag to the frame that got the mouse-down.
}

/** One field of the drag view; re-renders only when it changes. */
export function useDrag<T>(pick: (v: DragView) => T): T {
  return useSyncExternalStore(sidebarDrag.subscribe, () => pick(sidebarDrag.get()))
}

export const ghostElement = (): HTMLElement | null => ghost

/** After a drop, animates the moved row from the ghost's spot once its new place is in the state (FLIP). Runs after
 * every render of a list that shows it, which is when a state push lands. */
export function useDropFlip(): void {
  useLayoutEffect(() => {
    const f = flip
    if (!f) return
    if (performance.now() - f.at > FLIP_WAIT_MS) {
      flip = null
      return
    }
    if (placeKey(f.id) === f.before) return
    flip = null
    if (document.documentElement.dataset.reducedMotion === 'true') return
    const el = root?.querySelector<HTMLElement>(`[data-tab-id="${CSS.escape(f.id)}"][role="treeitem"], .clu-favorite[data-tab-id="${CSS.escape(f.id)}"], [data-folder-id="${CSS.escape(f.id)}"]`)
    if (!el || typeof el.animate !== 'function') return
    const to = el.getBoundingClientRect()
    const dx = f.from.x - to.x
    const dy = f.from.y - to.y
    if (Math.abs(dx) + Math.abs(dy) < 1) return
    el.animate([{ transform: `translate(${dx}px, ${dy}px)` }, { transform: 'none' }], { duration: FLIP_MS, easing: 'cubic-bezier(0.2, 0, 0, 1)' })
  })
}
