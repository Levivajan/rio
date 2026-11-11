// Inline rename of the tabs module (02-tabs.md F3): which sidebar row shows its rename field. Set by a double-click or by a
// menu answer (Rename, New Folder, New Folder Inside), so a folder created in main enters rename as soon as its row mounts.
import { useSyncExternalStore } from 'react'
import { clu } from '../../common/clu'
import { menuLight } from '../../common/menuChrome'

let renaming: string | null = null
const listeners = new Set<() => void>()

export function setRenaming(nodeId: string | null): void {
  if (renaming === nodeId) return
  renaming = nodeId
  for (const fn of listeners) fn()
}

const subscribe = (fn: () => void) => {
  listeners.add(fn)
  return () => void listeners.delete(fn)
}

export const useRenaming = (nodeId: string): boolean => useSyncExternalStore(subscribe, () => renaming === nodeId)
/** Whether any row of this document shows its rename field. */
export const useAnyRenaming = (): boolean => useSyncExternalStore(subscribe, () => renaming !== null)

/** Opens the row menu of a tab or folder, or the multi-select menu when `tabIds` holds two or more tabs; a Rename,
 * New Folder or New Folder Inside pick starts inline rename. */
export async function openRowMenu(nodeId: string, x: number, y: number, tabIds?: string[]): Promise<void> {
  const r = await clu().tabs.openMenu({ menu: 'row', nodeId, anchor: { x, y }, light: menuLight(), ...(tabIds && tabIds.length > 1 ? { tabIds } : {}) })
  if (r.ok && r.value.rename) setRenaming(r.value.rename)
}

/** Opens the empty-area, + or "..." menu; New Folder answers with the folder to rename. */
export async function openMenuAt(menu: 'empty' | 'new' | 'more', anchor: { x: number; y: number; width?: number; height?: number }): Promise<void> {
  const r = await clu().tabs.openMenu({ menu, light: menuLight(), anchor: anchor.width === undefined ? { x: anchor.x, y: anchor.y } : { x: anchor.x, y: anchor.y, width: anchor.width, height: anchor.height ?? 0 } })
  if (r.ok && r.value.rename) setRenaming(r.value.rename)
}

/** Asks the space header of the event's `detail` space id to start its inline rename. */
export const RENAME_SPACE_EVENT = 'clu-rename-space'

/** Opens the space header's menu; New Folder starts the folder's rename, and Rename Space… calls `renameSpace`. */
export async function openSpaceMenu(spaceId: string, anchor: { x: number; y: number }, renameSpace: () => void): Promise<void> {
  const r = await clu().tabs.openMenu({ menu: 'space', nodeId: spaceId, anchor, light: menuLight() })
  if (!r.ok || !r.value.rename) return
  if (r.value.rename === spaceId) renameSpace()
  else setRenaming(r.value.rename)
}
