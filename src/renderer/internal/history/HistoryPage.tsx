// History page: visits newest first under local day headings, search as you type (`site:<host>`
// filters by host), checkbox selection with Shift ranges, delete with one in-page confirm, a row menu, paging near the
// bottom. Rows are links with target _blank, so a click opens a new Today tab and Cmd+click opens it in the background.
import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent, type MouseEvent } from 'react'
import type { HistoryRow } from '../../../shared/ipc/data'
import { clu } from '../../common/clu'
import { Dialog } from '../../common/Dialog'
import { IconButton } from '../../common/IconButton'
import { LetterTile } from '../../common/LetterTile'
import { MenuList } from '../../common/MenuList'
import { useCluState } from '../../common/useCluState'
import { chunks, clickSelect, emptySelection, groupByDay, hostOf, pruneSelection, siteQuery, timeLabel, type Selection } from './model'
import { useHistory } from './useHistory'
import { Button } from '../../common/controls'

const DEBOUNCE_MS = 150
const FAVICON_PREFIX = 'clu://favicon/'

const MoreIcon = () => (
  <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">
    <circle cx="3.5" cy="8" r="1.3" />
    <circle cx="8" cy="8" r="1.3" />
    <circle cx="12.5" cy="8" r="1.3" />
  </svg>
)

function Favicon({ row, host }: { row: HistoryRow; host: string }) {
  const [broken, setBroken] = useState(false)
  // Icons come only from Clu's own favicon store, so the page never fetches from the network.
  if (!row.favicon?.startsWith(FAVICON_PREFIX) || broken) return <LetterTile text={host || row.title} />
  return <img className="clu-history-favicon" src={row.favicon} alt="" width={16} height={16} onError={() => setBroken(true)} />
}

export function HistoryPage({ now = Date.now }: { now?: () => number }) {
  const [input, setInput] = useState('')
  const [text, setText] = useState('')
  const revision = useCluState((s) => s.historyRevision)
  const { rows, status, hasMore, loadMore, hide, reload } = useHistory(text.trim(), revision)
  const [selection, setSelection] = useState<Selection>(emptySelection)
  const [confirming, setConfirming] = useState(false)
  const [menu, setMenu] = useState<{ row: HistoryRow; x: number; y: number; opener: HTMLElement | null } | null>(null)
  const sentinel = useRef<HTMLDivElement>(null)
  const search = useRef<HTMLInputElement>(null)

  useEffect(() => {
    if (input === text) return
    const t = setTimeout(() => setText(input), DEBOUNCE_MS)
    return () => clearTimeout(t)
  }, [input, text])

  const order = useMemo(() => rows.map((r) => r.id), [rows])
  useEffect(() => setSelection((s) => pruneSelection(s, order)), [order])
  const groups = useMemo(() => groupByDay(rows, now()), [rows, now])

  useEffect(() => {
    const el = sentinel.current
    if (!el || !hasMore) return
    const io = new IntersectionObserver((entries) => {
      if (entries.some((e) => e.isIntersecting)) loadMore()
    }, { rootMargin: '600px' })
    io.observe(el)
    return () => io.disconnect()
  }, [hasMore, loadMore])

  const deleteSelected = useCallback(async () => {
    const ids = [...selection.ids]
    setConfirming(false)
    if (!ids.length) return
    const gone = new Set(ids)
    hide((r) => gone.has(r.id))
    setSelection(emptySelection)
    // A refused delete brings the hidden rows back.
    for (const part of chunks(ids)) {
      if (!(await clu().history.deleteVisits({ ids: part })).ok) return reload()
    }
  }, [selection, hide, reload])

  const requestDelete = () => {
    if (selection.ids.size > 1) setConfirming(true)
    else void deleteSelected()
  }

  const showSite = (host: string) => {
    const q = siteQuery(host)
    setInput(q)
    setText(q)
    search.current?.focus()
  }

  const removeUrl = (row: HistoryRow) => {
    hide((r) => r.url === row.url)
    void clu().history.deleteUrl({ url: row.url }).then((r) => {
      if (!r.ok) reload()
    })
  }

  const onPageKey = (e: KeyboardEvent) => {
    if (confirming || menu) return
    const inField = e.target instanceof HTMLInputElement && e.target.type !== 'checkbox'
    if (e.key === 'Escape' && selection.ids.size) {
      e.preventDefault()
      setSelection(emptySelection)
    } else if ((e.key === 'Delete' || e.key === 'Backspace') && selection.ids.size && !inField) {
      e.preventDefault()
      requestDelete()
    }
  }

  const openMenu = (row: HistoryRow, e: MouseEvent<HTMLButtonElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    setMenu({ row, x: Math.max(8, Math.min(r.right - 220, window.innerWidth - 228)), y: Math.min(r.bottom + 4, window.innerHeight - 76), opener: e.currentTarget })
  }
  const closeMenu = () => {
    menu?.opener?.focus()
    setMenu(null)
  }

  const selecting = selection.ids.size > 0
  const query = text.trim()
  return (
    <main className="clu-internal clu-history" onKeyDown={onPageKey} data-selecting={selecting || undefined}>
      <header className="clu-history-header">
        {selecting ? (
          <div className="clu-history-bar" role="toolbar" aria-label="Selection">
            <span className="clu-history-count" aria-live="polite">{selection.ids.size} selected</span>
            <span className="clu-history-spacer" />
            <Button onClick={() => setSelection(emptySelection)}>Cancel</Button>
            <Button variant="danger" onClick={requestDelete}>Delete</Button>
          </div>
        ) : (
          <div className="clu-history-bar">
            <h1>History</h1>
            <span className="clu-history-spacer" />
            <Button onClick={() => void clu().browsingData.open({})}>Clear browsing data…</Button>
          </div>
        )}
        <input
          ref={search}
          className="clu-history-search"
          type="search"
          aria-label="Search history"
          placeholder="Search history"
          value={input}
          maxLength={200}
          spellCheck={false}
          onChange={(e) => setInput(e.target.value)}
        />
      </header>

      {status === 'error' ? (
        <p className="clu-history-empty" role="status">History could not be loaded.</p>
      ) : status === 'ready' && rows.length === 0 ? (
        <p className="clu-history-empty" role="status">{query ? `No results for ‘${query}’` : 'No history yet'}</p>
      ) : (
        groups.map((g) => (
          <section key={g.key} className="clu-history-day" aria-labelledby={`day-${g.key}`}>
            <h2 id={`day-${g.key}`}>{g.label}</h2>
            <ul>
              {g.rows.map((row) => {
                const host = hostOf(row.url)
                const title = row.title || row.url
                const checked = selection.ids.has(row.id)
                return (
                  <li key={row.id} className="clu-history-row" data-visit-id={row.id} data-selected={checked || undefined}>
                    <input
                      type="checkbox"
                      className="clu-history-check"
                      aria-label={`Select ${title}`}
                      checked={checked}
                      onChange={() => undefined}
                      onClick={(e) => setSelection((s) => clickSelect(s, order, row.id, e.shiftKey))}
                    />
                    <time className="clu-history-time" dateTime={new Date(row.visitedAt).toISOString()}>{timeLabel(row.visitedAt)}</time>
                    <a className="clu-history-link" href={row.url} target="_blank" rel="noopener noreferrer">
                      <Favicon row={row} host={host} />
                      <span className="clu-history-title">{title}</span>
                      <span className="clu-history-host">{host}</span>
                    </a>
                    <IconButton aria-label="More actions" aria-haspopup="menu" aria-expanded={menu?.row.id === row.id} className="clu-history-more" onClick={(e) => openMenu(row, e)}>
                      <MoreIcon />
                    </IconButton>
                  </li>
                )
              })}
            </ul>
          </section>
        ))
      )}
      <div ref={sentinel} aria-hidden="true" />

      {menu ? (
        <div className="clu-history-layer" onPointerDown={(e) => e.target === e.currentTarget && closeMenu()}>
          <MenuList
            label="History item"
            style={{ position: 'fixed', left: menu.x, top: menu.y }}
            items={[
              { kind: 'item', id: 'site', label: 'More from this site', enabled: hostOf(menu.row.url) !== '' },
              { kind: 'item', id: 'remove', label: 'Remove from history', destructive: true }
            ]}
            onClose={closeMenu}
            onPick={(id) => {
              const row = menu.row
              setMenu(null)
              if (id === 'site') showSite(hostOf(row.url))
              else if (id === 'remove') {
                menu.opener?.focus()
                removeUrl(row)
              }
            }}
          />
        </div>
      ) : null}

      {confirming ? (
        <div className="clu-history-layer is-scrim">
          <Dialog title={`Remove ${selection.ids.size} items from history?`} onCancel={() => setConfirming(false)}>
            <div className="clu-history-actions">
              <Button onClick={() => setConfirming(false)}>Cancel</Button>
              <Button variant="danger" onClick={() => void deleteSelected()}>Remove</Button>
            </div>
          </Dialog>
        </div>
      ) : null}
    </main>
  )
}
