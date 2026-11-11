// Archive panel (02-tabs F7): replaces the tab list while open (Cmd+Option+A, the footer button or
// "Show Archived Tabs"). Rows are grouped by day, newest first, with title, host and reason; a click or Restore brings
// the tab back at the top of Today, right-click or Delete deletes the row. Search matches title and URL literally;
// scrolling near the end loads the next 100 rows. In the hover peek Esc closes it; in the Library the back arrow does.
import { useEffect, useMemo, useState, type ReactNode } from 'react'
import type { ArchiveRow } from '../../../shared/archive'
import { hostAndPath } from '../../../shared/tabs'
import { MenuList } from '../../common/MenuList'
import { useCluState, useWindowId } from '../../common/useCluState'
import './archive.css'
import { LibraryFilter } from './LibraryFilter'
import { groupByDay, reasonLabel } from './model'
import { useArchivePages } from './useArchivePages'
import { clu } from '../../common/clu'

const SEARCH_DEBOUNCE_MS = 150
/** Distance from the end of the list at which the next page loads. */
const LOAD_AHEAD_PX = 400

// Archive rows carry no buttons: a click restores, right-click (or the context menu key) offers
// Restore Tab and Delete from Archive, and Delete or Backspace on a focused row deletes it.
const ROW_MENU_W = 180

function ArchiveItem({ row }: { row: ArchiveRow }) {
  const restore = () => void clu().archive.restore({ archiveId: row.id })
  const remove = () => void clu().archive.delete({ archiveId: row.id })
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null)
  return (
    <li
      className="clu-archive-row"
      data-archive-id={row.id}
      data-reason={row.reason}
      onContextMenu={(e) => {
        e.preventDefault()
        // The page view covers everything right of the sidebar: the menu stays inside it.
        const right = e.currentTarget.closest('nav')?.getBoundingClientRect().right ?? window.innerWidth
        setMenu({ x: Math.max(0, Math.min(Math.round(e.clientX), Math.floor(right) - ROW_MENU_W - 4)), y: Math.round(e.clientY) })
      }}
    >
      <button
        type="button"
        className="clu-archive-open"
        onClick={restore}
        onKeyDown={(e) => {
          if (e.key !== 'Delete' && e.key !== 'Backspace') return
          e.preventDefault()
          remove()
        }}
      >
        {/* No favicon: a blank rounded square. */}
        {row.favicon ? <img className="clu-row-favicon" src={row.favicon} alt="" draggable={false} /> : <span className="clu-archive-blank" aria-hidden="true" />}
        <span className="clu-archive-text">
          <span className="clu-archive-title">{row.title}</span>
          {/* The URL under the title; the reason stays for assistive tech. */}
          <span className="clu-archive-meta" title={reasonLabel(row.reason)}>
            <span className="clu-archive-host">{hostAndPath(row.url)}</span>
            <span className="clu-archive-reason">, {reasonLabel(row.reason)}</span>
          </span>
        </span>
      </button>
      {menu ? (
        <div className="clu-library-filter-layer" onPointerDown={(e) => e.target === e.currentTarget && setMenu(null)}>
          <MenuList
            label="Archived tab"
            style={{ position: 'fixed', left: menu.x, top: menu.y, width: ROW_MENU_W }}
            items={[
              { kind: 'item', id: 'restore', label: 'Restore Tab' },
              { kind: 'separator' },
              { kind: 'item', id: 'delete', label: 'Delete from Archive', destructive: true }
            ]}
            onClose={() => setMenu(null)}
            onPick={(id) => {
              setMenu(null)
              if (id === 'restore') restore()
              else if (id === 'delete') remove()
            }}
          />
        </div>
      ) : null}
    </li>
  )
}

/** Rows a space filter shows before the next page is fetched without a scroll. */
const FILTER_FILL = 30

/** `filter`: the Library's Filter button beside the search field, by space of this profile. */
export function ArchiveView({ filter = false }: { filter?: boolean }) {
  const windowId = useWindowId() ?? ''
  const profileId = useCluState((s) => s.spaces[s.windows[windowId]?.activeSpaceId ?? '']?.profileId ?? null)
  const spaceNames = useCluState((s) => s.spaceOrder.filter((id) => s.spaces[id]?.profileId === profileId).map((id) => `${id}\t${s.spaces[id]?.name ?? ''}`).join('\n'))
  const spaces = useMemo(() => (spaceNames ? spaceNames.split('\n').map((l) => ({ id: l.slice(0, l.indexOf('\t')), label: l.slice(l.indexOf('\t') + 1) || 'Space' })) : []), [spaceNames])
  const [spaceId, setSpaceId] = useState('all')
  const revision = useCluState((s) => s.archiveRevision)
  const [text, setText] = useState('')
  const [query, setQuery] = useState('')
  useEffect(() => {
    const t = setTimeout(() => setQuery(text.trim()), SEARCH_DEBOUNCE_MS)
    return () => clearTimeout(t)
  }, [text])
  const pages = useArchivePages(profileId, query, revision)
  const shown = useMemo(() => (spaceId === 'all' ? pages.rows : pages.rows.filter((r) => r.spaceId === spaceId)), [pages.rows, spaceId])
  const groups = useMemo(() => groupByDay(shown, Date.now()), [shown])
  // A space filter that leaves the list short reads on without waiting for a scroll.
  useEffect(() => {
    if (spaceId !== 'all' && shown.length < FILTER_FILL && pages.hasMore && !pages.loading) pages.loadMore()
  }, [spaceId, shown.length, pages])

  const empty = !pages.loading && shown.length === 0
  return (
    <section className="clu-archive" aria-label="Archive">
      <div className="clu-library-head">
      <label className="clu-archive-search-field">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" aria-hidden="true">
          <circle cx="7" cy="7" r="4.75" />
          <path d="m10.5 10.5 3.25 3.25" />
        </svg>
      <input
        className="clu-archive-search"
        type="search"
        aria-label="Search Archive"
        placeholder="Search Archive…"
        value={text}
        maxLength={200}
        spellCheck={false}
        autoFocus
        onChange={(e) => setText(e.target.value)}
      />
      </label>
      {filter ? <LibraryFilter options={[{ id: 'all', label: 'All Spaces' }, ...spaces]} value={spaceId} onChange={setSpaceId} /> : null}
      </div>
      <div
        className="clu-archive-list"
        data-loading={String(pages.loading)}
        onScroll={(e) => {
          const el = e.currentTarget
          if (pages.hasMore && el.scrollTop + el.clientHeight >= el.scrollHeight - LOAD_AHEAD_PX) pages.loadMore()
        }}
      >
        {groups.map((g) => (
          <section key={g.key} className="clu-archive-day" aria-label={g.label}>
            <h3 className="clu-section-label">{g.label}</h3>
            <ul className="clu-archive-rows">
              {g.rows.map((row) => (
                <ArchiveItem key={row.id} row={row} />
              ))}
            </ul>
          </section>
        ))}
        {empty ? <p className="clu-archive-empty">{pages.failed ? "The Archive couldn't be loaded" : query ? 'No matches' : 'No archived tabs'}</p> : null}
      </div>
    </section>
  )
}

/** The archive inside a sidebar that cannot widen (the hidden sidebar's hover peek): it replaces the tab list. */
export function ArchivePanel({ children }: { children: ReactNode }) {
  const windowId = useWindowId() ?? ''
  const open = useCluState((s) => s.archivePanel[windowId] === true)
  // Esc closes this one; the Library ignores Esc (its back arrow closes it).
  useEffect(() => {
    if (!open) return
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape') return
      e.preventDefault()
      void clu().archive.close()
    }
    document.addEventListener('keydown', onKey)
    return () => document.removeEventListener('keydown', onKey)
  }, [open])
  return open ? <ArchiveView /> : <>{children}</>
}
