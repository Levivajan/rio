// Archive panel model (02-tabs F7): day groups, row labels and page merging. Pure, so it is tested
// without a window.
import type { ArchiveRow } from '../../../shared/archive'

export interface DayGroup { key: string; label: string; rows: ArchiveRow[] }

const REASON_LABELS: Record<string, string> = {
  close: 'Closed',
  clear: 'Cleared',
  auto: 'Auto-archived',
  delete: 'Removed',
  mini: 'Mini window'
}

export function reasonLabel(reason: string): string {
  return REASON_LABELS[reason] ?? 'Archived'
}

/** The host shown under a row's title (no `www.`); the scheme for URLs without one, '' for junk. */
export function hostOf(url: string): string {
  try {
    const u = new URL(url)
    return u.hostname ? u.hostname.replace(/^www\./i, '') : u.protocol
  } catch {
    return ''
  }
}

function dayStart(ms: number): Date {
  const d = new Date(ms)
  d.setHours(0, 0, 0, 0)
  return d
}

/** Relative day buckets for the Library's groups: "Today", "1 day ago", "3 days ago", "1 week
 *  ago", "2 months ago", "1 year ago", counted in local calendar days. */
export function dayLabel(ms: number, now: number): string {
  // A row stamped ahead of the wall clock (a clock that ran ahead) still reads as today.
  const days = Math.max(0, Math.round((dayStart(now).getTime() - dayStart(ms).getTime()) / 86_400_000))
  if (days === 0) return 'Today'
  const [n, unit] = days < 7 ? [days, 'day'] : days < 30 ? [Math.floor(days / 7), 'week'] : days < 365 ? [Math.floor(days / 30), 'month'] : [Math.floor(days / 365), 'year']
  return `${n} ${unit}${n === 1 ? '' : 's'} ago`
}

/** Groups rows (already newest first) by their day label, keeping their order. */
export function groupByDay(rows: readonly ArchiveRow[], now: number): DayGroup[] {
  const groups: DayGroup[] = []
  for (const row of rows) {
    const label = dayLabel(Number.isFinite(row.archivedAt) ? row.archivedAt : now, now)
    const last = groups.at(-1)
    if (last && last.key === label) last.rows.push(row)
    else groups.push({ key: label, label, rows: [row] })
  }
  return groups
}

/** Appends a page, dropping rows already listed, so a page boundary that shifted never shows a row twice. */
export function mergePage(current: readonly ArchiveRow[], page: readonly ArchiveRow[]): ArchiveRow[] {
  const seen = new Set(current.map((r) => r.id))
  const out = [...current]
  for (const r of page) {
    if (seen.has(r.id)) continue
    seen.add(r.id)
    out.push(r)
  }
  return out
}
