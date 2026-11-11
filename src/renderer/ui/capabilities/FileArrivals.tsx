// The moment a download finishes or a screenshot lands, a row rises at the bottom left of the sidebar, right
// above the bottom bar's Library button, in the Library hover peek's row look: the file's
// 40 x 40 thumbnail, its name, and "5 seconds ago" under it, newest on top, on the sidebar with no card. Click opens
// the file, the x dismisses it, dragging it drags the file out and right-click opens the Library's menu for it, and each drops away on its own after FILE_ARRIVALS.showMs. The pointer moving over the
// list holds them; the hold lapses FILE_ARRIVALS.showMs after the last move, so a pointer that rests there, or left
// for another app or the page without a pointerleave, never keeps them up. While the list shows, the Library button sits on its tile. Only what arrives while this sidebar is
// up shows, never what was there before.
import { useCallback, useEffect, useRef, useState } from 'react'
import { sameItems, useCluState, useWindowId } from '../../common/useCluState'
import type { CluState } from '../../../shared/state'
import { useWindowDownloads } from './useWindowDownloads'
import { FILE_ARRIVALS, arrivalAgo, stackArrivals, takeArrivals, type FileArrival } from './arrivalRules'
import { clu } from '../../common/clu'
import { FileThumb } from '../archive/FileThumb'
import { startFileDrag } from '../archive/fileDrag'
import { useLibraryItemMenu } from '../archive/LibraryItemMenu'
import { holdLibraryPeek, useLibraryPeek } from '../archive/LibraryPeek'
import type { LibraryItem } from '../archive/libraryModel'
import '../archive/archive.css'
import './arrivals.css'

const selectScreenshots = (s: CluState) => s.screenshotArrivals ?? []

function open(a: FileArrival): void {
  if (a.source === 'downloads' && a.downloadId) void clu().download.act({ id: a.downloadId, action: 'open' })
  else void clu().download.fileAct({ source: a.source, name: a.name, action: 'open' })
}

function reveal(a: FileArrival): void {
  if (a.source === 'downloads' && a.downloadId) void clu().download.act({ id: a.downloadId, action: 'reveal' })
  else void clu().download.fileAct({ source: a.source, name: a.name, action: 'reveal' })
}

/** The arrival as a Library item, so its tile is the Library's QuickLook thumbnail (FileThumb). */
function asItem(a: FileArrival): LibraryItem {
  const source = a.source === 'media' ? 'media' : 'downloads'
  return { key: a.key, type: 'file', name: a.name, at: a.at, kind: a.kind, source, file: { name: a.name, size: 0, modifiedAt: a.at, kind: a.kind } }
}

/** One arrival: click opens, drag drags the file, right-click opens the Library's menu for it. */
function ArrivalRow({ arrival: a, now, leaving, onDismiss, onBusy }: { arrival: FileArrival; now: number; leaving: boolean; onDismiss: () => void; onBusy: (key: string, busy: boolean) => void }) {
  // A rename or a move to the Trash leaves this row naming a file that is no longer there, so it goes.
  const menu = useLibraryItemMenu(asItem(a), (action) => (action === 'open' ? open(a) : reveal(a)), onDismiss)
  // An open menu or rename holds the rows, even under a pointer that rests on the menu, and keeps this row when a
  // new arrival fills the list.
  const busy = menu.open || menu.renaming
  const key = a.key
  useEffect(() => {
    if (!busy) return
    onBusy(key, true)
    return () => onBusy(key, false)
  }, [busy, key, onBusy])
  return (
    <li className="clu-arrival" data-arrival={a.source} data-leaving={leaving || undefined} data-renaming={menu.renaming || undefined} onContextMenu={menu.onContextMenu} onKeyDown={menu.onKeyDown}>
      <button
        type="button"
        className="clu-library-peek-row clu-arrival-open"
        title={a.name}
        draggable
        onDragStart={(e) => startFileDrag(e, a.source === 'downloads' && a.downloadId ? { downloadId: a.downloadId } : { source: a.source, name: a.name })}
        onClick={() => {
          open(a)
          onDismiss()
        }}
      >
        <FileThumb item={asItem(a)} className="clu-library-peek-tile" />
        <span className="clu-library-peek-text">
          <span className="clu-library-peek-title">{a.name}</span>
          <span className="clu-library-peek-time">{arrivalAgo(a.at, now)}</span>
        </span>
      </button>
      <button type="button" className="clu-arrival-close" aria-label={`Dismiss ${a.name}`} onClick={onDismiss}>
        <svg width="8" height="8" viewBox="0 0 8 8" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden="true">
          <path d="m1 1 6 6M7 1 1 7" />
        </svg>
      </button>
      {menu.renameNode}
      {menu.menuNode}
    </li>
  )
}

export function FileArrivals() {
  const windowId = useWindowId()
  const downloads = useWindowDownloads(windowId)
  const screenshots = useCluState(selectScreenshots, sameItems)
  const [since] = useState(() => Date.now())
  const seen = useRef(new Set<string>())
  const [shown, setShown] = useState<FileArrival[]>([])
  const [leaving, setLeaving] = useState<ReadonlySet<string>>(() => new Set())
  const [held, setHeld] = useState(false)
  const [busy, setBusy] = useState(false)
  // The rows whose menu or rename is open: a new arrival never pushes them off.
  const busyKeys = useRef(new Set<string>())
  const holdLapse = useRef<ReturnType<typeof setTimeout> | undefined>(undefined)
  // While the pointer is on the list, a row that left keeps its slot as a gap, so the rows above never slide down
  // under the pointer and a right-click lands on the file the pointer was aimed at.
  const inside = useRef(false)
  const [gaps, setGaps] = useState<ReadonlySet<string>>(() => new Set())
  const gapsNow = useRef(gaps)
  gapsNow.current = gaps
  // When each card's time started (it arrived, or the pointer left the stack).
  const startedAt = useRef(new Map<string, number>())
  const [now, setNow] = useState(() => Date.now())
  // The hover peek lists the same files; it takes the place while it is up.
  const peekUp = useLibraryPeek() !== 'closed'

  useEffect(() => {
    const fresh = takeArrivals(downloads, screenshots, seen.current, since)
    if (fresh.length === 0) return
    const now = Date.now()
    for (const a of fresh) startedAt.current.set(a.key, now)
    setShown((s) => stackArrivals(s, fresh, busyKeys.current))
  }, [downloads, screenshots, since])

  const dismiss = useCallback((key: string) => {
    setLeaving((l) => (l.has(key) ? l : new Set(l).add(key)))
  }, [])

  // A card that is leaving goes after the drop.
  useEffect(() => {
    if (leaving.size === 0) return
    const t = setTimeout(() => {
      if (inside.current) setGaps((g) => new Set([...g, ...leaving]))
      else setShown((s) => s.filter((a) => !leaving.has(a.key)))
      setLeaving(new Set())
      for (const key of leaving) startedAt.current.delete(key)
    }, FILE_ARRIVALS.leaveMs)
    return () => clearTimeout(t)
  }, [leaving])

  // Each card leaves FILE_ARRIVALS.showMs after its time started, unless the pointer holds the stack.
  useEffect(() => {
    if (held || busy) return
    const now = Date.now()
    const timers = shown
      .filter((a) => !leaving.has(a.key) && !gaps.has(a.key))
      .map((a) => setTimeout(() => dismiss(a.key), Math.max(0, (startedAt.current.get(a.key) ?? now) + FILE_ARRIVALS.showMs - now)))
    return () => timers.forEach(clearTimeout)
  }, [shown, leaving, gaps, held, busy, dismiss])

  // Releasing the hold: a pointer that left restarts every card's time; a hold that lapsed lets them go now.
  const release = useCallback((restart: boolean) => {
    clearTimeout(holdLapse.current)
    if (restart) {
      const now = Date.now()
      for (const key of startedAt.current.keys()) startedAt.current.set(key, now)
    }
    setHeld(false)
  }, [])
  // The pointer left the list: the gaps close.
  const leave = useCallback(() => {
    inside.current = false
    const g = gapsNow.current
    if (g.size > 0) {
      setShown((s) => s.filter((a) => !g.has(a.key)))
      setGaps(new Set())
    }
    release(true)
  }, [release])
  useEffect(() => () => clearTimeout(holdLapse.current), [])
  // A row's menu or rename holds them all; when it ends, every card's time starts again.
  const onBusy = useCallback((key: string, b: boolean) => {
    if (b) busyKeys.current.add(key)
    else busyKeys.current.delete(key)
    if (!b) {
      const now = Date.now()
      for (const key of startedAt.current.keys()) startedAt.current.set(key, now)
    }
    setBusy(busyKeys.current.size > 0)
  }, [])

  // Cards pushed off the top of the stack forget their time; a stack of nothing but gaps goes.
  useEffect(() => {
    for (const key of [...startedAt.current.keys()]) if (!shown.some((a) => a.key === key)) startedAt.current.delete(key)
    if (shown.length > 0 && shown.every((a) => gaps.has(a.key))) {
      setShown([])
      setGaps(new Set())
    } else if ([...gaps].some((key) => !shown.some((a) => a.key === key))) setGaps((g) => new Set([...g].filter((key) => shown.some((a) => a.key === key))))
  }, [shown, gaps])

  // The hover peek takes the pop-up's place, so it stays shut while a row's menu or Rename is open.
  useEffect(() => {
    holdLibraryPeek(busy)
    return () => holdLibraryPeek(false)
  }, [busy])

  // "N seconds ago" counts up while rows show.
  useEffect(() => {
    if (shown.length === 0) return
    setNow(Date.now())
    const t = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(t)
  }, [shown.length])

  if (shown.length === 0 || peekUp) return null
  return (
    <div className="clu-arrivals-anchor">
      <ul
        className="clu-arrivals"
        aria-label="Just added"
        aria-live="polite"
        onPointerEnter={() => {
          inside.current = true
        }}
        onPointerMove={() => {
          inside.current = true
          setHeld(true)
          clearTimeout(holdLapse.current)
          holdLapse.current = setTimeout(() => release(false), FILE_ARRIVALS.showMs)
        }}
        onPointerLeave={leave}
      >
        {shown.map((a) =>
          gaps.has(a.key) ? (
            <li key={a.key} className="clu-arrival" data-gap aria-hidden="true" />
          ) : (
          <ArrivalRow key={a.key} arrival={a} now={now} leaving={leaving.has(a.key)} onDismiss={() => dismiss(a.key)} onBusy={onBusy} />
          )
        )}
      </ul>
    </div>
  )
}
