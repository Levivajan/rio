// The Library: while the archive panel is open the docked sidebar widens
// to 417 (computeLayout's libraryOpen) and shows a section rail, Media, Downloads, Spaces, Site tweaks and Archived
// Tabs (LibrarySections.tsx), icon over label,
// beside the list column; the back arrow at the rail's foot closes it; Esc does not.
// From the Library button the rail shows the section last shown in this window, Downloads at first;
// the archive command opens it on Archived Tabs.
// Downloads lists Rio's downloads and the Mac's Downloads folder, Media the screenshot folder, each with
// the search field on top and day headers.
import { useEffect, useMemo, useState, type ReactNode, type SVGProps } from 'react'
import type { LibrarySource } from '../../../shared/ipc/capabilities'
import { BackIcon } from '../../common/icons'
import { IconButton } from '../../common/IconButton'
import { Tooltip } from '../../common/Tooltip'
import { useWindowId } from '../../common/useCluState'
import { useWindowDownloads } from '../capabilities/useWindowDownloads'
import { ArchiveIcon } from './icons'
import { ArchiveView } from './ArchivePanel'
import { FileThumb } from './FileThumb'
import { dragTargetOf, startFileDrag } from './fileDrag'
import { LibraryFilter } from './LibraryFilter'
import { useLibraryItemMenu } from './LibraryItemMenu'
import { SpacesGlyph, TweaksGlyph, TweaksView } from './LibrarySections'
import { SpacesView } from './LibrarySpaces'
import { downloadItems, groupItems, itemSubtitle, mediaItems, type LibraryItem } from './libraryModel'
import { useLibraryFiles } from './useLibraryFiles'
import { clu } from '../../common/clu'

type Section = 'media' | 'downloads' | 'spaces' | 'tweaks' | 'archive'

const glyph = { width: 22, height: 22, viewBox: '0 0 22 22', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
const DownloadsGlyph = (p: SVGProps<SVGSVGElement>) => (
  <svg {...glyph} {...p}>
    <circle cx="11" cy="11" r="8.25" />
    <path d="M11 6.5v8M7.75 11.5 11 14.75l3.25-3.25" />
  </svg>
)
const MediaGlyph = (p: SVGProps<SVGSVGElement>) => (
  <svg {...glyph} {...p}>
    <rect x="3" y="4.5" width="16" height="13" rx="2.5" />
    <circle cx="8" cy="9" r="1.5" />
    <path d="m3.5 15.5 4.25-4 3.25 3 2.5-2.25 5 4.25" />
  </svg>
)
const RevealGlyph = () => (
  <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <circle cx="6" cy="6" r="3.75" />
    <path d="m8.75 8.75 3 3" />
  </svg>
)

const act = (item: LibraryItem, action: 'open' | 'reveal') =>
  item.type === 'download'
    ? void clu().download.act({ id: item.download.id, action })
    : void clu().download.fileAct({ source: item.source, name: item.name, action })

const canOpen = (item: LibraryItem) => item.type === 'file' || (item.download.state === 'completed' && item.download.exists)

function LibrarySearch({ label, value, onChange }: { label: string; value: string; onChange: (v: string) => void }) {
  return (
    <label className="clu-archive-search-field">
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" aria-hidden="true">
        <circle cx="7" cy="7" r="4.75" />
        <path d="m10.5 10.5 3.25 3.25" />
      </svg>
      <input
        className="clu-archive-search"
        type="search"
        aria-label={`Search ${label}`}
        placeholder={`Search ${label}…`}
        value={value}
        maxLength={200}
        spellCheck={false}
        autoFocus
        onChange={(e) => onChange(e.target.value)}
      />
    </label>
  )
}

const DOWNLOAD_FILTERS = [
  { id: 'all', label: 'All Downloads' },
  { id: 'file', label: 'Documents' },
  { id: 'image', label: 'Images' },
  { id: 'video', label: 'Videos' }
] as const
const MEDIA_FILTERS = [
  { id: 'all', label: 'All Media' },
  { id: 'image', label: 'Screenshots' },
  { id: 'video', label: 'Recordings' }
] as const
const byKind = (items: LibraryItem[], kind: string) => (kind === 'all' ? items : items.filter((i) => i.kind === kind))

function DownloadRow({ item, onChanged }: { item: LibraryItem; onChanged: () => void }) {
  const meta = itemSubtitle(item)
  const menu = useLibraryItemMenu(item, (a) => act(item, a), onChanged)
  return (
    <li
      className="clu-archive-row"
      data-library-item={item.key}
      data-download-id={item.type === 'download' ? item.download.id : undefined}
      data-state={item.type === 'download' ? item.download.state : 'completed'}
      data-renaming={menu.renaming || undefined}
      onContextMenu={menu.onContextMenu}
      onKeyDown={menu.onKeyDown}
    >
      <button
        type="button"
        className="clu-archive-open"
        disabled={!canOpen(item)}
        draggable={canOpen(item)}
        onDragStart={(e) => startFileDrag(e, dragTargetOf(item))}
        onClick={() => act(item, 'open')}
      >
        <FileThumb item={item} className="clu-library-icon" />
        <span className="clu-archive-text">
          <span className="clu-archive-title">{item.name}</span>
          <span className="clu-archive-meta">{meta}</span>
        </span>
      </button>
      {canOpen(item) ? (
        <IconButton aria-label="Show in Finder" className="clu-archive-action" onClick={() => act(item, 'reveal')}>
          <RevealGlyph />
        </IconButton>
      ) : null}
      {menu.renameNode}
      {menu.menuNode}
    </li>
  )
}

/** A Media tile: the framed thumbnail on its tinted tile, the full name in a tooltip on hover, the item menu on right-click. */
function MediaTile({ item, onChanged }: { item: LibraryItem; onChanged: () => void }) {
  const menu = useLibraryItemMenu(item, (a) => act(item, a), onChanged)
  return (
    <li className="clu-library-tile" data-library-item={item.key} data-kind={item.kind} data-renaming={menu.renaming || undefined} onContextMenu={menu.onContextMenu} onKeyDown={menu.onKeyDown}>
      <Tooltip label={item.name}>
        <button
          type="button"
          className="clu-library-tile-open"
          aria-label={item.name}
          draggable
          onDragStart={(e) => startFileDrag(e, dragTargetOf(item))}
          onClick={() => act(item, 'open')}
        >
          <FileThumb item={item} className="clu-library-thumb" />
        </button>
      </Tooltip>
      <IconButton aria-label="Show in Finder" className="clu-library-tile-reveal" onClick={() => act(item, 'reveal')}>
        <RevealGlyph />
      </IconButton>
      {menu.renameNode}
      {menu.menuNode}
    </li>
  )
}

function DownloadsView() {
  const windowId = useWindowId()
  const rows = useWindowDownloads(windowId)
  const finished = rows.filter((d) => d.state === 'completed').length
  // A rename or a move to the Trash from the item menu reads the folder again.
  const [changed, setChanged] = useState(0)
  const onChanged = () => setChanged((n) => n + 1)
  const files = useLibraryFiles('downloads', `${finished}:${changed}`)
  const [query, setQuery] = useState('')
  const [kind, setKind] = useState('all')
  const groups = useMemo(() => groupItems(byKind(downloadItems(rows, files, query), kind), Date.now()), [rows, files, query, kind])
  return (
    <section className="clu-archive clu-library-downloads" aria-label="Downloads">
      <div className="clu-library-head">
        <LibrarySearch label="Downloads" value={query} onChange={setQuery} />
        <LibraryFilter options={DOWNLOAD_FILTERS} value={kind} onChange={setKind} />
      </div>
      <div className="clu-archive-list">
        {groups.map((g) => (
          <section key={g.key} className="clu-archive-day" aria-label={g.label}>
            <h3 className="clu-section-label">{g.label}</h3>
            <ul className="clu-archive-rows">
              {g.items.map((item) => (
                <DownloadRow key={item.key} item={item} onChanged={onChanged} />
              ))}
            </ul>
          </section>
        ))}
        {groups.length === 0 ? <p className="clu-archive-empty">{query.trim() ? 'No matches' : 'No downloads'}</p> : null}
      </div>
    </section>
  )
}

function MediaView() {
  const [changed, setChanged] = useState(0)
  const onChanged = () => setChanged((n) => n + 1)
  const files = useLibraryFiles('media' satisfies LibrarySource, changed)
  const [query, setQuery] = useState('')
  const [kind, setKind] = useState('all')
  const groups = useMemo(() => groupItems(byKind(mediaItems(files, query), kind), Date.now()), [files, query, kind])
  return (
    <section className="clu-archive clu-library-media" aria-label="Media">
      <div className="clu-library-head">
        <LibrarySearch label="Media" value={query} onChange={setQuery} />
        <LibraryFilter options={MEDIA_FILTERS} value={kind} onChange={setKind} />
      </div>
      <div className="clu-archive-list">
        {groups.map((g) => (
          <section key={g.key} className="clu-archive-day" aria-label={g.label}>
            <h3 className="clu-section-label">{g.label}</h3>
            <ul className="clu-library-grid">
              {g.items.map((item) => (
                <MediaTile key={item.key} item={item} onChanged={onChanged} />
              ))}
            </ul>
          </section>
        ))}
        {groups.length === 0 ? <p className="clu-archive-empty">{query.trim() ? 'No matches' : 'No screenshots or recordings'}</p> : null}
      </div>
    </section>
  )
}

const LABELS: Record<Section, string> = { media: 'Media', downloads: 'Downloads', spaces: 'Spaces', tweaks: 'Site tweaks', archive: 'Archived Tabs' }

let lastSection: Section = 'downloads'
let fromButton = false

/** The Library button opened it: it shows the last section; "Show Archived Tabs" (Cmd+Option+A) shows the archive. */
export function libraryOpenedFromButton(): void {
  fromButton = true
}

export function Library() {
  const [section, setShown] = useState<Section>(() => (fromButton ? lastSection : 'archive'))
  useEffect(() => {
    fromButton = false
  }, [])
  const setSection = (next: Section) => {
    lastSection = next
    setShown(next)
  }
  const items: { id: Section; icon: ReactNode }[] = [
    { id: 'media', icon: <MediaGlyph /> },
    { id: 'downloads', icon: <DownloadsGlyph /> },
    { id: 'spaces', icon: <SpacesGlyph /> },
    { id: 'tweaks', icon: <TweaksGlyph /> },
    { id: 'archive', icon: <ArchiveIcon width={22} height={22} /> }
  ]
  return (
    <div className="clu-library" data-section={section}>
      <div className="clu-library-rail" role="tablist" aria-label="Library" aria-orientation="vertical">
        <div className="clu-library-rail-items">
          {items.map((it) => (
            <button
              key={it.id}
              type="button"
              role="tab"
              aria-selected={section === it.id}
              className="clu-library-rail-item"
              data-section={it.id}
              onClick={() => setSection(it.id)}
            >
              {it.icon}
              <span>{LABELS[it.id]}</span>
            </button>
          ))}
        </div>
        <div className="clu-library-rail-foot">
          <IconButton aria-label="Back to tabs" onClick={() => void clu().archive.close()}>
            <BackIcon />
          </IconButton>
        </div>
      </div>
      <div className="clu-library-list" role="tabpanel" aria-label={LABELS[section]}>
        {section === 'archive' ? (
          <ArchiveView filter />
        ) : section === 'media' ? (
          <MediaView />
        ) : section === 'spaces' ? (
          <SpacesView />
        ) : section === 'tweaks' ? (
          <TweaksView />
        ) : (
          <DownloadsView />
        )}
      </div>
    </div>
  )
}
