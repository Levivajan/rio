// Sidebar multi-select (PLAN 8 sidebar keys, 8.2 multi-select menu). Cmd+click toggles a row; Shift+click
// adds the rows from the anchor (the last Cmd+clicked or clicked row, else the active tab) to the clicked one in sidebar order:
// favorites, visible pinned rows through their folders, then Today, all of the window's space. A plain click and Esc
// clear it. Rows that leave the order (closed, archived, moved to another space, hidden in a collapsed folder) drop out.
// The reducer is pure and unit tested; the store below holds this window's selection.
import { useSyncExternalStore } from 'react'
import type { CluState } from '../../../shared/state'
import { MAX_BULK, sidebarOrder } from '../../../shared/tabs'
import { stateMirror } from '../../common/store'
import { clu } from '../../common/clu'

export interface Selection { ids: readonly string[]; anchor: string | null }

export type SelectionAction =
  | { type: 'toggle'; id: string; order: readonly string[] }
  | { type: 'range'; id: string; order: readonly string[]; active: string | null }
  | { type: 'clear' }
  /** A plain click: the selection clears and the clicked row anchors the next Shift+click before its activation lands. */
  | { type: 'click'; id: string }
  | { type: 'prune'; order: readonly string[] }

export const EMPTY_SELECTION: Selection = { ids: [], anchor: null }

/** `ids` in sidebar order, at most MAX_BULK. */
const ordered = (ids: Iterable<string>, order: readonly string[]): string[] => {
  const set = new Set(ids)
  return order.filter((id) => set.has(id)).slice(0, MAX_BULK)
}

export function selectionReducer(s: Selection, a: SelectionAction): Selection {
  switch (a.type) {
    case 'toggle': {
      if (!a.order.includes(a.id)) return s
      if (s.ids.includes(a.id)) return { ids: s.ids.filter((id) => id !== a.id), anchor: a.id }
      if (s.ids.length >= MAX_BULK) return s
      return { ids: ordered([...s.ids, a.id], a.order), anchor: a.id }
    }
    case 'range': {
      const to = a.order.indexOf(a.id)
      if (to < 0) return s
      const anchor = [s.anchor, a.active].find((id): id is string => !!id && a.order.includes(id)) ?? a.id
      const from = a.order.indexOf(anchor)
      const range = a.order.slice(Math.min(from, to), Math.max(from, to) + 1)
      return { ids: ordered([...s.ids, ...range], a.order), anchor }
    }
    case 'clear':
      return s.ids.length || s.anchor ? EMPTY_SELECTION : s
    case 'click':
      return s.ids.length || s.anchor !== a.id ? { ids: [], anchor: a.id } : s
    case 'prune': {
      const ids = s.ids.filter((id) => a.order.includes(id))
      const anchor = s.anchor && a.order.includes(s.anchor) ? s.anchor : null
      return ids.length === s.ids.length && anchor === s.anchor ? s : { ids, anchor }
    }
  }
}

/** Rows a window's sidebar can select: the F12 order without split members (a split shows as one row). */
export function selectableOrder(s: CluState, windowId: string): string[] {
  return sidebarOrder(s, windowId).filter((id) => !s.tabs[id]?.splitId)
}

// ---- this window's selection ------------------------------------------------------------------------------------

let current: Selection = EMPTY_SELECTION
const listeners = new Set<() => void>()

export function dispatchSelection(a: SelectionAction): void {
  const next = selectionReducer(current, a)
  if (next === current) return
  current = next
  for (const fn of listeners) fn()
}

const subscribe = (fn: () => void) => {
  listeners.add(fn)
  return () => void listeners.delete(fn)
}

export const useSelected = (tabId: string): boolean => useSyncExternalStore(subscribe, () => current.ids.includes(tabId))

const windowId = () => clu().windowId ?? ''

/** A row click: Cmd toggles and Shift extends the selection (true: the row is not activated); a plain click clears it. */
export function selectOnClick(e: { metaKey: boolean; shiftKey: boolean }, tabId: string): boolean {
  const s = stateMirror().get()
  const order = selectableOrder(s, windowId())
  if (e.metaKey) dispatchSelection({ type: 'toggle', id: tabId, order })
  else if (e.shiftKey) dispatchSelection({ type: 'range', id: tabId, order, active: s.windows[windowId()]?.activeTabId ?? null })
  else {
    dispatchSelection({ type: 'click', id: tabId })
    return false
  }
  return true
}

/** The tabs a press or right-click on `tabId` acts on, in sidebar order: the selection when the row is in one of two
 * or more, else the row alone. */
export function actingTabs(tabId: string): string[] {
  if (current.ids.length < 2 || !current.ids.includes(tabId)) return [tabId]
  return ordered(current.ids, selectableOrder(stateMirror().get(), windowId()))
}

/** What a right-click on `tabId` opens the menu for: the selection when the row is in one of two or more; a
 * right-click outside the selection clears it and gets the row's own menu. */
export function contextMenuFor(tabId: string): string[] | undefined {
  const ids = actingTabs(tabId)
  if (ids.length > 1) return ids
  if (!current.ids.includes(tabId)) dispatchSelection({ type: 'clear' })
  return undefined
}
