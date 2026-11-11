// Sidebar resize handle (docs/plan/01-shell.md F5, docs/chromium/parity-sidebar.md): an
// 8 px band centered on the sidebar's edge (4 in the sidebar, 4 over the card's gutters; the page view covers the
// rest). Pointer capture; the first 1 px after the press is ignored, then the width follows the pointer 1:1, one
// setSidebarWidth per animation frame; main clamps to 164..500 and places the page at once, and the pointer-up call
// commits (persists) the width. Double-click resets. Resting on the band for 250 ms shows a 4 px bar 2 inside the
// edge that stays for the whole drag. The width locks at 228 (where the favorites become 4 square columns):
// a drag that reaches it holds there, the browser ticks the trackpad, and leaving takes 6 more points of drag.
import { useEffect, useRef, useState, type PointerEvent } from 'react'
import { useCluState, useWindowId } from '../../common/useCluState'
import { clu } from '../../common/clu'
import { SIDEBAR_WIDTH } from '../../../shared/shell/layout'

const HANDLE_IN = 4
const HANDLE_OUT = 4
/** Moves within this distance of the press do not resize. */
const START_SLOP = 1
export const EDGE_BAR_DELAY_MS = 250
/** The width the drag locks at. */
export const LOCK_WIDTH = 228
/** Points of drag past the lock before the width follows the pointer again. */
export const LOCK_ESCAPE = 6

/** The width for a drag at `raw` that was at `prev`: reaching or crossing the lock stops there, and the width stays
 *  until the pointer is more than LOCK_ESCAPE past it. */
export function lockedWidth(prev: number, raw: number): number {
  if (prev === LOCK_WIDTH) return Math.abs(raw - LOCK_WIDTH) > LOCK_ESCAPE ? raw : LOCK_WIDTH
  const crossed = (prev < LOCK_WIDTH && raw >= LOCK_WIDTH) || (prev > LOCK_WIDTH && raw <= LOCK_WIDTH)
  return crossed ? LOCK_WIDTH : raw
}

export function ResizeHandle() {
  const windowId = useWindowId() ?? ''
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  const visible = useCluState((s) => {
    const shell = s.windows[windowId]?.shell
    return !!shell && !shell.sidebar.hidden && !shell.focusMode && shell.sidebar.anim === null
  })
  const drag = useRef<{ start: number; startX: number; latest: number; raf: number; moved: boolean } | null>(null)
  const [hot, setHot] = useState(false)
  const [dragging, setDragging] = useState(false)
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const cancelHot = () => {
    if (timer.current) clearTimeout(timer.current)
    timer.current = null
  }
  useEffect(() => cancelHot, [])

  if (!visible) return null

  const send = (w: number, commit: boolean) => void clu().shell.setSidebarWidth(commit ? { width: w, commit } : { width: w })

  const onPointerDown = (e: PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return
    e.preventDefault()
    e.currentTarget.setPointerCapture(e.pointerId)
    drag.current = { start: width, startX: e.clientX, latest: width, raf: 0, moved: false }
    setDragging(true)
  }
  const onPointerMove = (e: PointerEvent<HTMLDivElement>) => {
    const d = drag.current
    if (!d) return
    const dx = e.clientX - d.startX
    if (!d.moved && Math.abs(dx) <= START_SLOP) return
    d.moved = true
    d.latest = lockedWidth(d.latest, d.start + dx)
    if (d.raf) return
    d.raf = requestAnimationFrame(() => {
      d.raf = 0
      if (drag.current === d) send(d.latest, false)
    })
  }
  const end = (e: PointerEvent<HTMLDivElement>) => {
    const d = drag.current
    if (!d) return
    drag.current = null
    setDragging(false)
    if (d.raf) cancelAnimationFrame(d.raf)
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId)
    if (e.type === 'pointerup' && (d.moved || Math.abs(e.clientX - d.startX) > START_SLOP)) d.latest = lockedWidth(d.latest, d.start + e.clientX - d.startX)
    // A press that never moved past the slop changes nothing.
    if (d.latest !== d.start) send(d.latest, true)
  }

  return (
    <>
      <div className="clu-resize-bar" data-shell="resize-bar" aria-hidden="true" data-shown={String(hot || dragging)} style={{ left: width - 6 }} />
      <div
        className="clu-resize-handle"
        data-shell="resize-handle"
        role="separator"
        aria-orientation="vertical"
        aria-label="Resize sidebar"
        aria-valuenow={width}
        style={{ left: width - HANDLE_IN, width: HANDLE_IN + HANDLE_OUT }}
        onPointerEnter={() => {
          cancelHot()
          timer.current = setTimeout(() => setHot(true), EDGE_BAR_DELAY_MS)
        }}
        onPointerLeave={() => {
          cancelHot()
          setHot(false)
        }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={end}
        onPointerCancel={end}
        onDoubleClick={() => void clu().shell.resetSidebarWidth()}
      />
    </>
  )
}
