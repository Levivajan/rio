// The overlay's copy of the history index (04-command "History index sync"). The overlay view lives as long as its
// window, so the first open fetches everything and later opens apply only the delta since the last rev.
import type { IndexRow } from '../../../shared/ipc/command'
import { buildHistoryIndex, type HistoryIndex } from '../../../shared/command/rank'

export interface IndexDelta { rev: number; full: boolean; upserts: IndexRow[]; deletes: string[] }
export interface HistoryCache { rev: number; rows: Map<string, IndexRow>; built: HistoryIndex | null }

export const emptyHistoryCache = (): HistoryCache => ({ rev: 0, rows: new Map(), built: null })

/** Applies a sync result; the built index is reused when nothing changed. */
export function applyIndexDelta(cache: HistoryCache, delta: IndexDelta): HistoryIndex {
  const changed = delta.full || delta.rev !== cache.rev || delta.upserts.length > 0 || delta.deletes.length > 0
  if (delta.full) cache.rows = new Map()
  for (const key of delta.deletes) cache.rows.delete(key)
  for (const row of delta.upserts) if (row.kind === 'history') cache.rows.set(row.key, row)
  cache.rev = delta.rev
  if (changed || !cache.built) cache.built = buildHistoryIndex([...cache.rows.values()])
  return cache.built
}

/** Drops history rows deleted from the bar, so they neither list nor complete before the next sync drops them. */
export function dropUrls(cache: HistoryCache, urls: Iterable<string>): HistoryIndex {
  let changed = false
  for (const url of urls) changed = cache.rows.delete(url) || changed
  if (changed || !cache.built) cache.built = buildHistoryIndex([...cache.rows.values()])
  return cache.built
}
