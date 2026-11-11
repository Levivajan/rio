// History page logic: day groups in local time, row hosts, checkbox selection with Shift ranges.
import type { HistoryRow } from '../../../shared/ipc/data'

export interface DayGroup { key: string; label: string; rows: HistoryRow[] }

const dayKey = (d: Date) => `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()}`

/** "Today", "Yesterday", else the full local date. */
export function dayLabel(at: number, now: number, locale?: string): string {
  const day = new Date(at)
  const today = new Date(now)
  if (dayKey(day) === dayKey(today)) return 'Today'
  const yesterday = new Date(today.getFullYear(), today.getMonth(), today.getDate() - 1)
  if (dayKey(day) === dayKey(yesterday)) return 'Yesterday'
  return day.toLocaleDateString(locale, { weekday: 'long', month: 'long', day: 'numeric', year: 'numeric' })
}

export function timeLabel(at: number, locale?: string): string {
  return new Date(at).toLocaleTimeString(locale, { hour: 'numeric', minute: '2-digit' })
}

/** Rows arrive newest first; consecutive rows of one local day share a group. */
export function groupByDay(rows: readonly HistoryRow[], now: number, locale?: string): DayGroup[] {
  const groups: DayGroup[] = []
  for (const row of rows) {
    const key = dayKey(new Date(row.visitedAt))
    const last = groups[groups.length - 1]
    if (last && last.key === key) last.rows.push(row)
    else groups.push({ key, label: dayLabel(row.visitedAt, now, locale), rows: [row] })
  }
  return groups
}

export function hostOf(url: string): string {
  try {
    return new URL(url).hostname
  } catch {
    return ''
  }
}

/** The search text for "More from this site". */
export const siteQuery = (host: string) => `site:${host}`

export interface Selection { ids: ReadonlySet<number>; anchor: number | null }
export const emptySelection: Selection = { ids: new Set(), anchor: null }

/**
 * A plain click toggles one row and makes it the anchor. A Shift+click selects every row from the anchor to the
 * clicked one (in list order) and keeps the anchor; with no anchor still listed it acts as a plain click.
 */
export function clickSelect(sel: Selection, order: readonly number[], id: number, shift: boolean): Selection {
  const from = sel.anchor === null ? -1 : order.indexOf(sel.anchor)
  const to = order.indexOf(id)
  if (shift && from >= 0 && to >= 0) {
    const ids = new Set(sel.ids)
    for (let i = Math.min(from, to); i <= Math.max(from, to); i++) ids.add(order[i] as number)
    return { ids, anchor: sel.anchor }
  }
  const ids = new Set(sel.ids)
  if (ids.has(id)) ids.delete(id)
  else ids.add(id)
  return { ids, anchor: id }
}

/** Drops selected ids that are no longer listed (deleted, or filtered out by a new search). */
export function pruneSelection(sel: Selection, order: readonly number[]): Selection {
  const listed = new Set(order)
  const ids = new Set([...sel.ids].filter((id) => listed.has(id)))
  if (ids.size === sel.ids.size && (sel.anchor === null || listed.has(sel.anchor))) return sel
  return { ids, anchor: sel.anchor !== null && listed.has(sel.anchor) ? sel.anchor : null }
}

/** history.deleteVisits takes at most 1,000 ids per call. */
export function chunks<T>(items: readonly T[], size = 1000): T[][] {
  const out: T[][] = []
  for (let i = 0; i < items.length; i += size) out.push(items.slice(i, i + size))
  return out
}
