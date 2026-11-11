// Left-edge hot zone (docs/plan/01-shell.md F4 and F11): while the sidebar is hidden (or in focus mode)
// the layout leaves a strip at the window's left edge that page views do not cover. The pointer reaching the zone (x 0
// to 5, x 7 does not), or moving left fast enough to cross it between two
// samples, opens the hover peek after PEEK_DWELL_MS; any sample that does not reach it cancels. A held button never
// reveals. The browser runs the same rule on the frame's mouse events (CluSidebarPeek).
import { useEffect, useMemo } from 'react'
import { createDwell, reachesPeekZone, type EdgeSample } from '../../../shared/shell/peek'
import { useCluState, useWindowId } from '../../common/useCluState'
import { clu } from '../../common/clu'

export function EdgeHotZone() {
  const windowId = useWindowId() ?? ''
  const zone = useCluState((s) => {
    const shell = s.windows[windowId]?.shell
    return shell && !shell.sidebar.peeking ? shell.layout.hotZone : null
  })
  const dwell = useMemo(() => createDwell(() => void clu().shell.setSidebarPeek({ open: true })), [])
  const zx = zone?.x
  const zy = zone?.y
  const zw = zone?.width
  const zh = zone?.height
  useEffect(() => {
    if (zx === undefined || zy === undefined || zw === undefined || zh === undefined) {
      dwell.leave()
      return () => dwell.dispose()
    }
    const rect = { x: zx, y: zy, width: zw, height: zh }
    let previous: EdgeSample | null = null
    // The frame fills the window, so client coordinates are window coordinates.
    const sample = (e: PointerEvent) => {
      const point = { x: e.clientX, y: e.clientY, t: e.timeStamp }
      // The same sample seen twice (pointerover, then its pointermove) counts once.
      if (previous && previous.t === point.t && previous.x === point.x && previous.y === point.y) return
      if (reachesPeekZone(rect, point, previous, e.buttons)) dwell.enter()
      else dwell.leave()
      previous = point
    }
    const left = () => {
      previous = null
      dwell.leave()
    }
    // A pointer coming off a page frame inside this document may send only pointerover.
    const root = document.documentElement
    window.addEventListener('pointermove', sample)
    window.addEventListener('pointerover', sample)
    root.addEventListener('pointerleave', left)
    return () => {
      window.removeEventListener('pointermove', sample)
      window.removeEventListener('pointerover', sample)
      root.removeEventListener('pointerleave', left)
      dwell.dispose()
    }
  }, [zx, zy, zw, zh, dwell])
  if (!zone) return null
  return (
    <div
      className="clu-hot-zone"
      data-shell="hot-zone"
      aria-hidden="true"
      style={{ left: zone.x, top: zone.y, width: zone.width, height: zone.height }}
    />
  )
}
