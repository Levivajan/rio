// Archive pages (02-tabs F7): archive.list 100 rows at a time. A new profile or query starts over; a
// new archiveRevision refetches as many rows as were loaded, so a restore or delete keeps the scroll position.
import { useCallback, useEffect, useRef, useState } from 'react'
import type { ArchiveRow } from '../../../shared/archive'
import { invoke } from '../../common/invoke'
import { mergePage } from './model'
import { clu } from '../../common/clu'

export const ARCHIVE_PAGE = 100

export interface ArchivePages {
  rows: ArchiveRow[]
  hasMore: boolean
  loading: boolean
  failed: boolean
  loadMore(): void
}

interface Loaded { rows: ArchiveRow[]; nextCursor: string | null; loading: boolean; failed: boolean }

export function useArchivePages(profileId: string | null, query: string, revision: number): ArchivePages {
  const [loaded, setLoaded] = useState<Loaded>({ rows: [], nextCursor: null, loading: true, failed: false })
  const latest = useRef(loaded)
  const gen = useRef(0)
  const busy = useRef(false)
  const listKey = useRef<string | null>(null)

  const update = useCallback((next: Loaded) => {
    latest.current = next
    setLoaded(next)
  }, [])

  const fetchPage = useCallback(
    (cursor: string | null) =>
      invoke(
        clu().archive.list({
          profileId: profileId ?? '',
          ...(query ? { query } : {}),
          ...(cursor ? { cursor } : {}),
          limit: ARCHIVE_PAGE
        })
      ),
    [profileId, query]
  )

  useEffect(() => {
    const g = ++gen.current
    busy.current = true
    const key = `${profileId ?? ''}\u0000${query}`
    const fresh = listKey.current !== key
    listKey.current = key
    const want = fresh ? ARCHIVE_PAGE : Math.max(ARCHIVE_PAGE, latest.current.rows.length)
    if (fresh) update({ rows: [], nextCursor: null, loading: true, failed: false })
    if (!profileId) {
      busy.current = false
      update({ rows: [], nextCursor: null, loading: false, failed: false })
      return
    }
    void (async () => {
      let rows: ArchiveRow[] = []
      let cursor: string | null = null
      do {
        const r = await fetchPage(cursor)
        if (g !== gen.current) return
        if (!r.ok) {
          busy.current = false
          update({ ...latest.current, loading: false, failed: true })
          return
        }
        rows = mergePage(rows, r.value.rows)
        cursor = r.value.nextCursor
      } while (cursor && rows.length < want)
      busy.current = false
      update({ rows, nextCursor: cursor, loading: false, failed: false })
    })()
  }, [profileId, query, revision, fetchPage, update])

  const loadMore = useCallback(() => {
    const cur = latest.current
    if (busy.current || !cur.nextCursor) return
    busy.current = true
    const g = gen.current
    update({ ...cur, loading: true })
    void fetchPage(cur.nextCursor).then((r) => {
      if (g !== gen.current) return
      busy.current = false
      const now = latest.current
      if (!r.ok) update({ ...now, loading: false, failed: true })
      else update({ rows: mergePage(now.rows, r.value.rows), nextCursor: r.value.nextCursor, loading: false, failed: false })
    })
  }, [fetchPage, update])

  return { rows: loaded.rows, hasMore: loaded.nextCursor !== null, loading: loaded.loading, failed: loaded.failed, loadMore }
}
