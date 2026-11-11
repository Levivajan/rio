// What just landed. A download that finishes and a screenshot that lands in the screenshot folder
// (state.screenshotArrivals, from the browser's folder watcher) each show as a row at the bottom of the sidebar,
// right above the bottom bar. Pure rules here; FileArrivals.tsx shows them (arrivals.css).
import type { DownloadState, ScreenshotArrival } from '../../../shared/state/capabilities'
import { timeAgo } from '../../../shared/tabs'

export interface FileArrival {
  /** 'd:<download id>' or 'm:<file name>', unique per arrival. */
  key: string
  source: 'downloads' | 'media'
  name: string
  kind: 'image' | 'video' | 'file'
  /** The download row's id, for download.act open. */
  downloadId?: string
  at: number
}

/** Up to 4 rows list, as the Library peek does; each stays 10 s (longer while the pointer rests on the list), then
 * drops away in 160 ms. Every row reads "5 seconds ago" for its first minute, so a row outlives 5 s. */
export const FILE_ARRIVALS = { maxShown: 4, showMs: 10_000, leaveMs: 160 } as const

const IMAGE = /\.(png|jpe?g|gif|heic|webp|tiff?|bmp|avif)$/i
const VIDEO = /\.(mov|mp4|m4v|webm)$/i

export function kindOf(name: string): FileArrival['kind'] {
  return IMAGE.test(name) ? 'image' : VIDEO.test(name) ? 'video' : 'file'
}

const cleanName = (name: unknown): string | null =>
  typeof name === 'string' && name.trim() !== '' && !name.includes('/') && !name.startsWith('.') ? name.slice(0, 255) : null

/**
 * The arrivals among the window's downloads and the screenshot rows that are not in `seen` and happened at or after
 * `since` (when this sidebar started), oldest first. A download arrives once it is completed and on disk. Every key
 * returned joins `seen`, so an arrival shows once. Junk rows (no name, a path, a bad time) are skipped.
 */
export function takeArrivals(
  downloads: readonly DownloadState[],
  screenshots: readonly ScreenshotArrival[] | undefined,
  seen: Set<string>,
  since: number
): FileArrival[] {
  const out: FileArrival[] = []
  for (const d of downloads) {
    const at = d.endedAt ?? 0
    const name = cleanName(d.filename)
    const key = `d:${d.id}`
    if (d.state !== 'completed' || !d.exists || !name || !Number.isFinite(at) || at < since || seen.has(key)) continue
    seen.add(key)
    out.push({ key, source: 'downloads', name, kind: kindOf(name), downloadId: d.id, at })
  }
  for (const s of screenshots ?? []) {
    const name = cleanName(s?.name)
    if (!name || !Number.isFinite(s.at) || s.at < since) continue
    const key = `m:${name}`
    if (seen.has(key)) continue
    seen.add(key)
    out.push({ key, source: 'media', name, kind: s.kind === 'video' ? 'video' : 'image', at: s.at })
  }
  return out.sort((a, b) => a.at - b.at)
}

/**
 * The list after `fresh` (oldest first) arrive: newest on top, at most FILE_ARRIVALS.maxShown. A row in `keep` (its
 * menu or rename is open) never drops: the oldest rows that are not kept go instead.
 */
export function stackArrivals(shown: readonly FileArrival[], fresh: readonly FileArrival[], keep: ReadonlySet<string> = new Set()): FileArrival[] {
  if (fresh.length === 0) return shown as FileArrival[]
  const all = [...[...fresh].reverse(), ...shown]
  let over = all.length - FILE_ARRIVALS.maxShown
  if (over <= 0) return all
  const out: FileArrival[] = []
  for (let i = all.length - 1; i >= 0; i--) {
    const a = all[i] as FileArrival
    if (over > 0 && !keep.has(a.key)) over--
    else out.push(a)
  }
  return out.reverse()
}

/** The row's second line: "5 seconds ago" under a minute, then the Library's timeAgo. */
export function arrivalAgo(at: number, now: number): string {
  if (!Number.isFinite(at) || !Number.isFinite(now)) return ''
  const s = Math.floor(Math.max(0, now - at) / 1000)
  if (s >= 60) return timeAgo(at, now)
  return s <= 1 ? '1 second ago' : `${s} seconds ago`
}
