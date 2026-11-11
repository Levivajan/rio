// Back and forward history menu (docs/plan/01-shell.md F7), surface nav-history: up to 15 entries of the
// tab's history in that direction, nearest first, each with its favicon or a letter glyph. Picking one calls
// nav.goToIndex. Opened by main under the long-pressed or right-clicked button; light-dismiss, Esc closes.
import { useEffect, useLayoutEffect, useRef, useState, type KeyboardEvent } from 'react'
import type { NavHistoryRow } from '../../../shared/ipc/engine'
import { letterOf, sanitizeHistory, type NavHistoryProps } from '../../../shared/shell/nav'
import { closeInstance, type SurfaceProps } from '../core/types'
import './nav-history.css'
import { clu } from '../../common/clu'

function readProps(raw: unknown): NavHistoryProps | null {
  if (!raw || typeof raw !== 'object') return null
  const p = raw as Record<string, unknown>
  const a = p.anchor as Record<string, unknown> | null | undefined
  if (typeof p.tabId !== 'string' || (p.direction !== 'back' && p.direction !== 'forward') || !a || typeof a !== 'object') return null
  const n = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : 0)
  return { tabId: p.tabId, direction: p.direction, anchor: { x: n(a.x), y: n(a.y), width: n(a.width), height: n(a.height) } }
}

function Glyph({ row }: { row: NavHistoryRow }) {
  const [broken, setBroken] = useState(false)
  if (row.favicon && !broken) return <img className="clu-nav-history-icon" src={row.favicon} alt="" onError={() => setBroken(true)} />
  return (
    <span className="clu-nav-history-icon clu-nav-history-letter" aria-hidden>
      {letterOf(row.title)}
    </span>
  )
}

export function NavHistoryMenu({ instance }: SurfaceProps) {
  const props = readProps(instance.props)
  const id = instance.instanceId
  const [rows, setRows] = useState<NavHistoryRow[] | null>(null)
  const [focus, setFocus] = useState(0)
  const [pos, setPos] = useState<{ left: number; top: number } | null>(null)
  const ref = useRef<HTMLDivElement>(null)
  const items = useRef<(HTMLDivElement | null)[]>([])
  const tabId = props?.tabId
  const direction = props?.direction

  useEffect(() => {
    if (!tabId || !direction) return closeInstance(id)
    let live = true
    void clu().nav.getHistory({ tabId, direction }).then((r) => {
      if (!live) return
      const list = r.ok ? sanitizeHistory(r.value) : []
      if (list.length) setRows(list)
      else closeInstance(id)
    })
    return () => {
      live = false
    }
  }, [id, tabId, direction])

  const anchor = props?.anchor
  useLayoutEffect(() => {
    const el = ref.current
    if (!el || !rows || !anchor) return
    const w = el.offsetWidth
    const h = el.offsetHeight
    const left = Math.max(0, Math.min(anchor.x, window.innerWidth - w))
    const below = anchor.y + anchor.height + 4
    setPos({ left, top: below + h > window.innerHeight ? Math.max(0, anchor.y - h - 4) : below })
  }, [rows, anchor?.x, anchor?.y, anchor?.width, anchor?.height]) // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (pos) items.current[focus]?.focus()
  }, [focus, pos])

  if (!rows || !tabId) return null
  const pick = (row: NavHistoryRow) => {
    void clu().nav.goToIndex({ tabId, index: row.index }).finally(() => closeInstance(id))
  }
  const onKey = (e: KeyboardEvent) => {
    if (e.key === 'ArrowDown') setFocus((f) => (f + 1) % rows.length)
    else if (e.key === 'ArrowUp') setFocus((f) => (f - 1 + rows.length) % rows.length)
    else if (e.key === 'Home') setFocus(0)
    else if (e.key === 'End') setFocus(rows.length - 1)
    else if (e.key === 'Enter' || e.key === ' ') {
      const row = rows[focus]
      if (row) pick(row)
    } else if (e.key === 'Escape') closeInstance(id)
    else return
    e.preventDefault()
    e.stopPropagation()
  }
  return (
    <div
      ref={ref}
      role="menu"
      aria-label={direction === 'back' ? 'Back history' : 'Forward history'}
      tabIndex={-1}
      className="clu-menu clu-nav-history"
      data-shell="nav-history"
      style={{ position: 'fixed', left: pos?.left ?? 0, top: pos?.top ?? 0, visibility: pos ? 'visible' : 'hidden' }}
      onPointerDown={(e) => e.stopPropagation()}
      onKeyDown={onKey}
    >
      {rows.map((row, i) => (
        <div
          key={row.index}
          ref={(el) => {
            items.current[i] = el
          }}
          role="menuitem"
          tabIndex={-1}
          className="clu-menu-item"
          data-index={row.index}
          onPointerEnter={() => setFocus(i)}
          onClick={() => pick(row)}
        >
          <Glyph row={row} />
          <span className="clu-nav-history-title">{row.title}</span>
        </div>
      ))}
    </div>
  )
}
