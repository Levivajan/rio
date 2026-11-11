// History page data: history.query pages of 100 over the (visited_at, id) cursor. A new search
// starts over; a history change (historyRevision) reloads as many rows as are listed, so deleted rows go and new visits
// show without losing the scroll depth. Replies to an older request are dropped.
import { useCallback, useEffect, useRef, useState } from 'react'
import type { HistoryRow } from '../../../shared/ipc/data'
import { clu } from '../../common/clu'

export const PAGE = 100
/** Bounds one load when a `site:` filter drops rows from a page. */
const MAX_PAGES_PER_LOAD = 50

type Cursor = { visitedAt: number; id: number }
export type HistoryStatus = 'loading' | 'ready' | 'error'

async function fetchRows(text: string, cursor: Cursor | null, atLeast: number): Promise<{ rows: HistoryRow[]; cursor: Cursor | null }> {
  const rows: HistoryRow[] = []
  let next = cursor
  for (let i = 0; i < MAX_PAGES_PER_LOAD; i++) {
    const r = await clu().history.query({ ...(text ? { text } : {}), ...(next ? { cursor: next } : {}), limit: PAGE })
    if (!r.ok) throw new Error(r.error.code)
    rows.push(...r.value.rows)
    next = r.value.nextCursor
    if (!next || rows.length >= atLeast) break
  }
  return { rows, cursor: next }
}

export function useDebounced<T>(value: T, ms: number): T {
  const [out, setOut] = useState(value)
  useEffect(() => {
    const t = setTimeout(() => setOut(value), ms)
    return () => clearTimeout(t)
  }, [value, ms])
  return out
}

export function useHistory(text: string, revision: number) {
  const [rows, setRows] = useState<HistoryRow[]>([])
  const [cursor, setCursor] = useState<Cursor | null>(null)
  const [status, setStatus] = useState<HistoryStatus>('loading')
  const gen = useRef(0)
  const shown = useRef({ text, count: 0 })
  const loadingMore = useRef(false)
  const [reloads, setReloads] = useState(0)

  useEffect(() => {
    const mine = ++gen.current
    const sameSearch = shown.current.text === text
    const atLeast = sameSearch ? Math.max(PAGE, shown.current.count) : PAGE
    if (!sameSearch) setStatus('loading')
    fetchRows(text, null, atLeast).then(
      (r) => {
        if (mine !== gen.current) return
        shown.current = { text, count: r.rows.length }
        setRows(r.rows)
        setCursor(r.cursor)
        setStatus('ready')
      },
      () => {
        if (mine === gen.current) setStatus('error')
      }
    )
  }, [text, revision, reloads])

  const loadMore = useCallback(() => {
    if (!cursor || loadingMore.current) return
    const mine = gen.current
    loadingMore.current = true
    fetchRows(text, cursor, 1)
      .then(
        (r) => {
          if (mine !== gen.current) return
          setRows((prev) => {
            const seen = new Set(prev.map((x) => x.id))
            const merged = [...prev, ...r.rows.filter((x) => !seen.has(x.id))]
            shown.current = { text, count: merged.length }
            return merged
          })
          setCursor(r.cursor)
        },
        () => undefined
      )
      .finally(() => {
        loadingMore.current = false
      })
  }, [cursor, text])

  /** Hides rows right away; the revision bump that follows the delete reloads from the store. */
  const hide = useCallback((gone: (row: HistoryRow) => boolean) => setRows((prev) => prev.filter((r) => !gone(r))), [])
  /** Reloads what is listed, for a delete main refused (no revision bump follows it). */
  const reload = useCallback(() => setReloads((n) => n + 1), [])

  return { rows, status, hasMore: cursor !== null, loadMore, hide, reload }
}
