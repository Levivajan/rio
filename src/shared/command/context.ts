// Command bar contextual actions: rows about the active tab and the active space's folders, listed
// for a query that names the space or a folder: "Move to Today in [space] ⌘D", "Open [space / folder]" and
// "Pin to [space / folder]". Pure: the overlay runs what a row asks for.
import type { CluState } from '../state'
import { queryTokens } from './match'

/** A query needs this many characters before contextual rows join the list. */
export const CONTEXT_MIN_QUERY = 3
/** Most contextual rows in one list. */
export const MAX_CONTEXT_ROWS = 3
/** Longest space or folder name drawn in a chip. */
const MAX_CHIP_PART = 60

export type ContextRun =
  | { kind: 'togglePin' }
  | { kind: 'openFolder'; folderId: string }
  | { kind: 'pinTo'; tabId: string; spaceId: string; folderId: string; index: number }

export interface ContextRow {
  key: string
  /** The verb before the chip: "Move to Today in", "Open", "Pin to". */
  verb: string
  /** The grey path chip: the space name, or "space / folder". */
  chip: string
  /** The space's accent for the chip's dot (Move to Today only). */
  dot?: string
  icon: 'today' | 'folder' | 'pin'
  run: ContextRun
}

const COLOR = /^(#[0-9a-f]{3,8}|(rgb|hsl)a?\([0-9.,%\s/]+\))$/i
const folded = (s: string) => queryTokens(s).join(' ')
const part = (name: unknown, fallback: string) => (typeof name === 'string' && name.trim() ? name.trim().slice(0, MAX_CHIP_PART) : fallback)

/** True when every word of the query starts a word of one of the names, in order ("rio fo" for "Rio folder"). */
function names(query: string, list: readonly string[]): boolean {
  const q = folded(query)
  if (q.length < CONTEXT_MIN_QUERY) return false
  return list.some((n) => ` ${folded(n)}`.includes(` ${q}`))
}

export function contextRows(state: Pick<CluState, 'tabs' | 'spaces' | 'windows' | 'folders'>, windowId: string, query: string): ContextRow[] {
  const w = state.windows[windowId]
  const spaceId = w?.activeSpaceId ?? null
  const space = spaceId ? state.spaces[spaceId] : undefined
  if (!spaceId || !space) return []
  const spaceName = part(space.name, 'Untitled')
  const tab = w?.activeTabId ? state.tabs[w.activeTabId] : undefined
  // Favorites and tabs of another space stay out: Cmd+D and a folder pin act on the space's own rows.
  const own = tab && tab.section !== 'favorite' && tab.spaceId === spaceId ? tab : undefined
  const rows: ContextRow[] = []

  if (own) {
    const pinned = own.section === 'pinned'
    const verb = pinned ? 'Move to Today in' : 'Pin to'
    if (names(query, [verb, spaceName, `${verb} ${spaceName}`])) {
      rows.push({ key: 'context:togglePin', verb, chip: spaceName, ...(pinned && typeof space.accent === 'string' && COLOR.test(space.accent) ? { dot: space.accent } : {}), icon: pinned ? 'today' : 'pin', run: { kind: 'togglePin' } })
    }
  }

  for (const folder of Object.values(state.folders)) {
    if (rows.length >= MAX_CONTEXT_ROWS) break
    if (!folder || folder.spaceId !== spaceId) continue
    const folderName = part(folder.name, 'Untitled')
    const chip = `${spaceName} / ${folderName}`
    if (!names(query, [folderName, `Open ${folderName}`, `Pin to ${folderName}`])) continue
    rows.push({ key: `context:open:${folder.id}`, verb: 'Open', chip, icon: 'folder', run: { kind: 'openFolder', folderId: folder.id } })
    const inFolder = own && folder.children.some((c) => c.kind === 'tab' && c.id === own.id)
    if (own && !inFolder && rows.length < MAX_CONTEXT_ROWS) {
      const index = Array.isArray(folder.children) ? folder.children.length : 0
      rows.push({ key: `context:pin:${folder.id}`, verb: 'Pin to', chip, icon: 'pin', run: { kind: 'pinTo', tabId: own.id, spaceId, folderId: folder.id, index } })
    }
  }
  return rows.slice(0, MAX_CONTEXT_ROWS)
}
