// Collapsed-folder popover: a
// search field ("Search <folder>…") over the folder's tabs, each with its favicon, title and how long ago it was last
// active. It fills its passive view beside the sidebar (folderPeekRect), less the room kept for its shadow. The
// pointer over it holds it open (folders.peek held); leaving it, Escape or picking a tab closes it. The browser gives
// it the keyboard, so the search field takes typing at once; arrow keys move the highlight, Return opens
// the highlighted tab.
import { useEffect, useRef, useState } from 'react'
import { FOLDER_PEEK, folderPeekTabs, timeAgo } from '../../../shared/tabs'
import type { CluState } from '../../../shared/state'
import { LetterTile } from '../../common/LetterTile'
import { useCluState } from '../../common/useCluState'
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'

interface PeekRow { id: string; title: string; url: string; favicon: string | null; lastActiveAt: number }

const sameRows = (a: PeekRow[], b: PeekRow[]) =>
  a.length === b.length && a.every((r, i) => { const o = b[i]; return !!o && o.id === r.id && o.title === r.title && o.favicon === r.favicon && o.lastActiveAt === r.lastActiveAt })

function rowsOf(s: CluState, folderId: string): PeekRow[] {
  return folderPeekTabs(s, folderId).flatMap((id) => {
    const t = s.tabs[id]
    return t ? [{ id, title: t.titleOverride || t.title || t.url, url: t.url, favicon: t.customIcon ? null : t.favicon, lastActiveAt: t.lastActiveAt }] : []
  })
}

const SearchIcon = () => (
  <svg width="13" height="13" viewBox="0 0 13 13" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
    <circle cx="5.6" cy="5.6" r="4.4" />
    <path d="m8.9 8.9 3.1 3.1" />
  </svg>
)

export function FolderPeek({ instance }: SurfaceProps) {
  const folderId = (instance.props as { folderId?: string } | null)?.folderId ?? ''
  const name = useCluState((s) => s.folders[folderId]?.name ?? '')
  const rows = useCluState((s) => rowsOf(s, folderId), sameRows)
  const [query, setQuery] = useState('')
  const [index, setIndex] = useState(0)
  const field = useRef<HTMLInputElement>(null)
  // Each folder's popover is a new instance: its field takes the caret as it opens.
  useEffect(() => field.current?.focus(), [])
  const q = query.trim().toLowerCase()
  const shown = q ? rows.filter((r) => r.title.toLowerCase().includes(q) || r.url.toLowerCase().includes(q)) : rows
  const at = Math.min(index, Math.max(0, shown.length - 1))
  const now = Date.now()
  const hold = (held: boolean) => void clu().folders.peek({ folderId, held })
  const pick = (tabId: string) => {
    void clu().tabs.activate({ tabId })
    hold(false)
  }
  const { top, right, bottom } = FOLDER_PEEK.shadow
  return (
    <div className="clu-folder-peek-view" style={{ padding: `${top}px ${right}px ${bottom}px 0` }}>
      <div
        className="clu-folder-peek"
        role="dialog"
        aria-label={name}
        onPointerEnter={() => hold(true)}
        onPointerLeave={() => hold(false)}
        onKeyDown={(e) => {
          if (e.key === 'Escape') hold(false)
          else if (e.key === 'ArrowDown') setIndex(Math.min(at + 1, shown.length - 1))
          else if (e.key === 'ArrowUp') setIndex(Math.max(at - 1, 0))
          else if (e.key === 'Enter' && shown[at]) pick(shown[at].id)
          else return
          e.preventDefault()
        }}
      >
        <label className="clu-folder-peek-search">
          <SearchIcon />
          <input
            ref={field}
            type="search"
            value={query}
            placeholder={`Search ${name}…`}
            aria-label={`Search ${name}`}
            maxLength={200}
            spellCheck={false}
            onChange={(e) => {
              setQuery(e.currentTarget.value)
              setIndex(0)
            }}
          />
        </label>
        <ul className="clu-folder-peek-rows" role="listbox" aria-label={name}>
          {shown.map((r, i) => (
            <li
              key={r.id}
              role="option"
              aria-selected={i === at}
              className="clu-folder-peek-row"
              data-tab-id={r.id}
              onPointerEnter={() => setIndex(i)}
              onClick={() => pick(r.id)}
            >
              {r.favicon ? <img className="clu-row-favicon" src={r.favicon} alt="" draggable={false} /> : <LetterTile text={r.title} />}
              <span className="clu-folder-peek-text">
                <span className="clu-folder-peek-title">{r.title}</span>
                <span className="clu-folder-peek-time">{timeAgo(r.lastActiveAt, now)}</span>
              </span>
            </li>
          ))}
        </ul>
      </div>
    </div>
  )
}
