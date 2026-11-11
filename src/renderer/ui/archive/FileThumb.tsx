// A Library file's thumbnail: QuickLook's rendering of the file (download.thumb), such as a
// PDF's first page, a sheet or a screenshot; the kind glyph until it arrives, and for a file QuickLook cannot draw.
// Thumbnails are kept per file and modification time, so reopening the Library or the hover peek asks once.
import { useEffect, useState } from 'react'
import type { LibrarySource } from '../../../shared/ipc/capabilities'
import { clu } from '../../common/clu'
import { invoke } from '../../common/invoke'
import type { LibraryItem } from './libraryModel'

const MAX_CACHED = 300
const cache = new Map<string, string>()

export const FileGlyph = ({ kind }: { kind: LibraryItem['kind'] }) => (
  <svg width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {kind === 'image' ? (
      <>
        <rect x="2.75" y="4" width="14.5" height="12" rx="2" />
        <circle cx="7.25" cy="8" r="1.25" />
        <path d="m3.25 14.25 4-3.5 3 2.75 2.25-2 4.25 3.5" />
      </>
    ) : kind === 'video' ? (
      <>
        <rect x="2.75" y="4" width="14.5" height="12" rx="2" />
        <path d="M8.5 7.5v5l4-2.5Z" />
      </>
    ) : (
      <path d="M5 2.75h6.5L15.25 6.5v10.75H5ZM11.25 2.75V6.75h4" />
    )}
  </svg>
)

/** Where the item's file is, by folder and bare name; null for a download that is not a finished file. */
export function thumbTarget(item: LibraryItem): { source: LibrarySource; name: string } | null {
  if (item.type === 'file') return { source: item.source, name: item.name }
  return item.download.state === 'completed' && item.download.exists && item.name ? { source: 'downloads', name: item.name } : null
}

/** A PNG or SVG data URL the browser sent, or '' for anything else. */
const safeThumb = (v: unknown) => (typeof v === 'string' && /^data:image\/(png|svg\+xml)[;,]/.test(v) && v.length < 2_000_000 ? v : '')

export function useThumb(item: LibraryItem): string {
  const target = thumbTarget(item)
  const source = target?.source
  const name = target?.name ?? ''
  const key = source ? `${source}\n${name}\n${item.at}` : ''
  const [url, setUrl] = useState(() => cache.get(key) ?? '')
  useEffect(() => {
    if (!source) return setUrl('')
    const hit = cache.get(key)
    if (hit !== undefined) return setUrl(hit)
    let live = true
    void invoke(clu().download.thumb({ source, name })).then((r) => {
      const next = r.ok ? safeThumb(r.value) : ''
      if (cache.size >= MAX_CACHED) cache.delete(cache.keys().next().value as string)
      cache.set(key, next)
      if (live) setUrl(next)
    })
    return () => {
      live = false
    }
  }, [key, source, name])
  return url
}

export function FileThumb({ item, className }: { item: LibraryItem; className: string }) {
  const url = useThumb(item)
  return (
    <span className={className} data-kind={item.kind} data-thumb={url ? 'image' : 'glyph'} aria-hidden="true">
      {url ? <img src={url} alt="" draggable={false} /> : <FileGlyph kind={item.kind} />}
    </span>
  )
}
