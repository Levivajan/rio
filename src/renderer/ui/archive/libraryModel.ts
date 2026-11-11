// The Library's Downloads and Media lists: Rio's own downloads and the files of the Mac's Downloads folder
// in one list, newest first, grouped by day; Media is the screenshot folder's images and videos. A folder file that is
// one of Rio's downloads shows once, as the download.
import type { LibraryFile, LibrarySource } from '../../../shared/ipc/capabilities'
import type { DownloadState } from '../../../shared/state/capabilities'
import { downloadStatus } from '../../../shared/capabilities'
import { dayLabel } from './model'

export type LibraryItem =
  | { key: string; type: 'download'; name: string; at: number; kind: LibraryFile['kind']; download: DownloadState }
  | { key: string; type: 'file'; name: string; at: number; kind: LibraryFile['kind']; source: LibrarySource; file: LibraryFile }

export interface LibraryGroup { key: string; label: string; items: LibraryItem[] }

const IMAGE = /\.(png|jpe?g|gif|heic|webp|tiff?|bmp)$/i
const VIDEO = /\.(mov|mp4|m4v|webm)$/i

export function kindOf(name: string): LibraryFile['kind'] {
  return IMAGE.test(name) ? 'image' : VIDEO.test(name) ? 'video' : 'file'
}

const finite = (n: unknown, fallback: number) => (typeof n === 'number' && Number.isFinite(n) ? n : fallback)

/** Rows from the browser, junk dropped: no name, a path in the name, or not an object. */
export function cleanFiles(rows: unknown): LibraryFile[] {
  if (!Array.isArray(rows)) return []
  const out: LibraryFile[] = []
  for (const r of rows as Partial<LibraryFile>[]) {
    if (!r || typeof r.name !== 'string' || !r.name || r.name.includes('/')) continue
    const kind = r.kind === 'image' || r.kind === 'video' || r.kind === 'file' ? r.kind : kindOf(r.name)
    out.push({ name: r.name.slice(0, 255), size: Math.max(0, finite(r.size, 0)), modifiedAt: finite(r.modifiedAt, 0), kind })
  }
  return out
}

const matches = (name: string, query: string) => !query || name.toLowerCase().includes(query.toLowerCase())

/** Downloads: Rio's downloads plus the Downloads folder's files it did not make, newest first, filtered by `query`. */
export function downloadItems(downloads: readonly DownloadState[], files: readonly LibraryFile[], query = ''): LibraryItem[] {
  const q = query.trim()
  const ours = new Set(downloads.map((d) => d.filename))
  const items: LibraryItem[] = [
    ...downloads.map((d): LibraryItem => ({ key: `d:${d.id}`, type: 'download', name: d.filename, at: finite(d.endedAt, finite(d.startedAt, 0)), kind: kindOf(d.filename), download: d })),
    ...files
      .filter((f) => !ours.has(f.name))
      .map((f): LibraryItem => ({ key: `f:${f.name}`, type: 'file', name: f.name, at: f.modifiedAt, kind: f.kind, source: 'downloads', file: f }))
  ]
  return items.filter((i) => matches(i.name, q)).sort((a, b) => b.at - a.at || a.name.localeCompare(b.name))
}

/** Media: the screenshot folder's images and videos, newest first, filtered by `query`. */
export function mediaItems(files: readonly LibraryFile[], query = ''): LibraryItem[] {
  const q = query.trim()
  return files
    .filter((f) => f.kind !== 'file' && matches(f.name, q))
    .map((f): LibraryItem => ({ key: `m:${f.name}`, type: 'file', name: f.name, at: f.modifiedAt, kind: f.kind, source: 'media', file: f }))
    .sort((a, b) => b.at - a.at || a.name.localeCompare(b.name))
}

/** Groups items (already newest first) by relative day: "Today", "1 day ago", "1 week ago" (model.ts dayLabel). */
export function groupItems(items: readonly LibraryItem[], now: number): LibraryGroup[] {
  const groups: LibraryGroup[] = []
  for (const item of items) {
    const label = dayLabel(item.at > 0 ? item.at : now, now)
    const last = groups.at(-1)
    if (last && last.label === label) last.items.push(item)
    else groups.push({ key: label, label, items: [item] })
  }
  return groups
}

/** The site a download came from: the registrable part of the host ("googleusercontent.com"
 *  for doc-0s.googleusercontent.com), '' for junk or a URL without a host. */
export function sourceSite(url: string): string {
  let host: string
  try {
    host = new URL(url).hostname.replace(/^www\./i, '').replace(/\.$/, '')
  } catch {
    return ''
  }
  if (!host || /^[\d.]+$/.test(host) || host.startsWith('[')) return host
  const parts = host.split('.')
  // A two-letter country code under a short second level (co.uk, com.au) keeps three labels.
  const keep = parts.length > 2 && (parts.at(-1) ?? '').length === 2 && (parts.at(-2) ?? '').length <= 3 ? 3 : 2
  return parts.slice(-keep).join('.')
}

const KIND_NAMES: Record<LibraryFile['kind'], string> = { image: 'Image', video: 'Video', file: 'Download' }

/** The line under a finished file's name: its kind ("PDF", "Image", "Video", or
 *  "Download") and, for Rio's downloads, the site it came from ("PDF from googleusercontent.com"). */
export function fileKindLine(name: string, kind: LibraryFile['kind'], url = ''): string {
  const what = /\.pdf$/i.test(name) ? 'PDF' : KIND_NAMES[kind]
  const site = url ? sourceSite(url) : ''
  return site ? `${what} from ${site}` : what
}

/** A row's subtitle: kind and source once finished, the download's status (progress, failed, deleted) otherwise. */
export function itemSubtitle(item: LibraryItem): string {
  if (item.type === 'file') return fileKindLine(item.name, item.kind)
  const d = item.download
  return d.state === 'completed' && d.exists ? fileKindLine(item.name, item.kind, d.url) : downloadStatus(d)
}

/** The hover peek's rows: the newest LIBRARY_PEEK rows among Rio's downloads, the Downloads folder and Media, newest
 *  last so it sits by the Library button. */
export function peekItems(downloads: readonly DownloadState[], downloadFiles: readonly LibraryFile[], mediaFiles: readonly LibraryFile[], count: number): LibraryItem[] {
  const seen = new Set<string>()
  // A screenshot folder that is the Downloads folder lists its files once.
  return [...downloadItems(downloads, downloadFiles), ...mediaItems(mediaFiles)]
    .filter((i) => !seen.has(`${i.name}\n${i.at}`) && Boolean(seen.add(`${i.name}\n${i.at}`)))
    .sort((a, b) => b.at - a.at || a.name.localeCompare(b.name))
    .slice(0, Math.max(0, count))
    .reverse()
}
