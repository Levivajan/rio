// The sidebar's answer to a main menu command (Tabs ▸ New Folder…, Reveal Tab in Sidebar): the browser puts the ask in
// windows[id].sidebarRequest and this acts on each new seq once. The ask present when the frame first shows its
// sidebar is old (the frame reloaded), so it is skipped. The ask names its sidebar: with the floating sidebar open the
// browser asks it (`floating`), otherwise it shows the docked sidebar first and asks that. Each list marks the other's
// asks seen without acting, so neither replays one later. The warm-up copies never answer.
import { useEffect } from 'react'
import type { SidebarRequest } from '../../../shared/state'
import { useCluState } from '../../common/useCluState'
import { setRenaming } from './renaming'

// The last seq acted on, per window. It lives outside the tab list, which unmounts while the Library shows and mounts
// again from the very patch that closes the Library and holds the new ask.
const applied = new Map<string, number>()

function seed(windowId: string, seq: number): void {
  if (!applied.has(windowId)) applied.set(windowId, seq)
}

/** Marks the ask present when the sidebar first shows as old. The sidebar calls it, since it shows from the first
 *  state while the tab list may not (the Library replaces it). */
export function useSidebarRequestBaseline(windowId: string): void {
  seed(windowId, useCluState((s) => s.windows[windowId]?.sidebarRequest?.seq ?? 0))
}

/** Whether a rename ask for the floating sidebar is still waiting for its tab list to start the rename (the list
 *  mounts once the Library closes). The floating sidebar stays held from the ask until the rename starts, so the hold
 *  the browser put on it for the rename is not released in between. */
export function useFloatingRenamePending(windowId: string): boolean {
  const request = useCluState((s) => s.windows[windowId]?.sidebarRequest ?? null)
  const seen = applied.get(windowId)
  return request?.kind === 'rename' && request.floating === true && seen !== undefined && request.seq > seen
}

/** Starts the row's inline rename, or scrolls its row into view. */
export function applySidebarRequest(request: SidebarRequest, root: ParentNode = document): void {
  if (request.kind === 'rename') {
    setRenaming(request.nodeId)
    return
  }
  const id = CSS.escape(request.nodeId)
  // A tab in a split draws inside its split row, which is what scrolls.
  const row =
    root.querySelector(`[data-tab-id="${id}"], [data-folder-id="${id}"]`) ??
    root.querySelector(`[data-split-member="${id}"]`)?.closest('[data-split-id]')
  row?.scrollIntoView({ block: 'nearest' })
}

/** `answers` is false for a list that must never act on an ask (the warm-up copies); `floating` is set for the
 *  floating sidebar's list. */
export function useSidebarRequest(windowId: string, answers: boolean, floating = false): void {
  const request = useCluState((s) => s.windows[windowId]?.sidebarRequest ?? null)
  if (answers) seed(windowId, request?.seq ?? 0)
  useEffect(() => {
    if (!answers || !request || request.seq <= (applied.get(windowId) ?? 0)) return
    applied.set(windowId, request.seq)
    if ((request.floating === true) === floating) applySidebarRequest(request)
  }, [answers, floating, request, windowId])
}
