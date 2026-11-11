// Library hover peek:
// resting on the archive button for 150 ms lists the 4 newest files of the Library right on the sidebar, above the
// bottom bar, with no panel: Rio's downloads, the Downloads folder and Media together, each a 40 x 40 thumbnail
//, the file name and how long ago it was saved (or its status while it runs). Rows rise into place bottom first
// while the tab list behind fades out; leaving the button and the list drops them again. A row opens its file, or the
// downloads bubble for a download that is not on disk. archive.css fades the tab list over the rows' height.
import { useSyncExternalStore, type CSSProperties } from 'react'
import { downloadStatus } from '../../../shared/capabilities'
import { timeAgo } from '../../../shared/tabs'
import { useWindowDownloads } from '../capabilities/useWindowDownloads'
import { useCluState, useWindowId } from '../../common/useCluState'
import './archive.css'
import { clu } from '../../common/clu'
import { FileThumb } from './FileThumb'
import { itemSubtitle, peekItems, type LibraryItem } from './libraryModel'
import { dragTargetOf, startFileDrag } from './fileDrag'
import { useLibraryFiles } from './useLibraryFiles'

/** The timing: 150 ms before it opens; rows close 70 ms each, 33 ms apart. */
export const LIBRARY_PEEK = { delayMs: 150, leaveMs: 120, rows: 4, pitch: 56, closeMs: 70 + 33 * 3 } as const

type PeekState = 'closed' | 'open' | 'closing'
let peek: PeekState = 'closed'
let timer: ReturnType<typeof setTimeout> | null = null
const listeners = new Set<() => void>()

function set(next: PeekState): void {
  if (peek === next) return
  peek = next
  for (const fn of listeners) fn()
}
const later = (ms: number, fn: () => void) => {
  if (timer) clearTimeout(timer)
  timer = setTimeout(() => {
    timer = null
    fn()
  }, ms)
}

// The bottom-left pop-up holds the peek shut while one of its rows has a menu or Rename open: the peek takes the
// pop-up's place, and unmounting it would drop the field and what was typed in it.
let held = false
export function holdLibraryPeek(on: boolean): void {
  if (held !== on) {
    held = on
    for (const fn of listeners) fn()
  }
  if (on && peek === 'closed' && timer) {
    clearTimeout(timer)
    timer = null
  }
}

/** The pointer entered the archive button or the list. */
export function libraryPeekEnter(): void {
  if (held && peek === 'closed') return
  if (peek === 'open') {
    if (timer) clearTimeout(timer)
    timer = null
    return
  }
  later(peek === 'closing' ? 0 : LIBRARY_PEEK.delayMs, () => set('open'))
}

/** The pointer left the archive button or the list; `now` (a press) skips the grace period. */
export function libraryPeekLeave(now = false): void {
  if (peek === 'closed') {
    if (timer) clearTimeout(timer)
    timer = null
    return
  }
  later(now ? 0 : LIBRARY_PEEK.leaveMs, () => {
    set('closing')
    later(LIBRARY_PEEK.closeMs, () => set('closed'))
  })
}

const subscribe = (fn: () => void) => {
  listeners.add(fn)
  return () => void listeners.delete(fn)
}
export const useLibraryPeek = (): PeekState => useSyncExternalStore(subscribe, () => peek)
/** A pop-up row's menu or Rename is open: the floating sidebar stays out under it. */
export const useLibraryPeekHeld = (): boolean => useSyncExternalStore(subscribe, () => held)

function subtitle(item: LibraryItem, now: number): string {
  if (item.type === 'download' && !(item.download.state === 'completed' && item.download.exists)) return downloadStatus(item.download)
  return timeAgo(item.at, now) || itemSubtitle(item)
}

function open(item: LibraryItem): void {
  libraryPeekLeave(true)
  if (item.type === 'file') void clu().download.fileAct({ source: item.source, name: item.name, action: 'open' })
  else if (item.download.state === 'completed' && item.download.exists) void clu().download.act({ id: item.download.id, action: 'open' })
  else void clu().download.openPopover({})
}

export function LibraryPeek() {
  const windowId = useWindowId()
  const state = useLibraryPeek()
  const archiveOpen = useCluState((s) => s.archivePanel[windowId ?? ''] === true)
  const downloads = useWindowDownloads(windowId)
  const reading = state !== 'closed'
  const downloadFiles = useLibraryFiles('downloads', null, reading)
  const mediaFiles = useLibraryFiles('media', null, reading)
  const rows = peekItems(downloads, downloadFiles, mediaFiles, LIBRARY_PEEK.rows)
  const shown = reading && !archiveOpen && rows.length > 0
  if (!shown) return null
  const now = Date.now()
  return (
    <div className="clu-library-peek-anchor">
      <ul className="clu-library-peek" aria-label="Recent files" data-state={state} onPointerEnter={libraryPeekEnter} onPointerLeave={() => libraryPeekLeave()}>
        {rows.map((item, i) => (
          <li key={item.key} style={{ '--i': rows.length - 1 - i } as CSSProperties}>
            <button
              type="button"
              className="clu-library-peek-row"
              data-library-item={item.key}
              data-download-id={item.type === 'download' ? item.download.id : undefined}
              draggable={dragTargetOf(item) !== null}
              onDragStart={(e) => startFileDrag(e, dragTargetOf(item))}
              onClick={() => open(item)}
            >
              <FileThumb item={item} className="clu-library-peek-tile" />
              <span className="clu-library-peek-text">
                <span className="clu-library-peek-title">{item.name}</span>
                <span className="clu-library-peek-time">{subtitle(item, now)}</span>
              </span>
            </button>
          </li>
        ))}
      </ul>
    </div>
  )
}
