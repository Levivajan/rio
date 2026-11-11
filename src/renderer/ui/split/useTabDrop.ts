// Drag a sidebar tab onto a page edge. Tabs owns the row drag and emits it on the dragBus; this hook
// follows that drag (and the window's own pointer events, since the row only sees moves while the pointer is over
// it), hit-tests the pane edge bands and tells main through split.dragState only when the zone changes. Releasing
// over a band sends split.dropTab; the center, the sidebar, Esc or a lost window focus drop nothing.
import { useEffect } from 'react'
import type { CluState } from '../../../shared/state'
import type { Rect } from '../../../shared/types'
import { dragBus, type DragEvent } from '../../common/dragBus'
import { stateMirror } from '../../common/store'
import { dropTargets, hitTabDrop, inside, sameHit, type DropHit, type DropTarget } from './drop'
import { clu } from '../../common/clu'

/** Drop targets of the window now, read at event time so a drag never acts on a stale layout. */
export function windowTargets(s: CluState, windowId: string): { page: Rect | undefined; targets: DropTarget[] } {
  const w = s.windows[windowId]
  const page = w?.shell.layout.pageRect
  return { page, targets: w ? dropTargets(w.splitView, page, w.activeTabId) : [] }
}

export function useTabDrop(windowId: string): void {
  useEffect(() => {
    if (!windowId) return
    let drag: { tabId: string; open: boolean; hit: DropHit | null } | null = null

    const send = (phase: 'start' | 'hover' | 'end' | 'cancel', tabId: string, hit?: DropHit | null) =>
      void clu().split.dragState({ phase, tabId, ...(hit ? { targetTabId: hit.targetTabId, ...(hit.edge ? { edge: hit.edge } : {}) } : {}) })

    const track = (x: number, y: number) => {
      const d = drag
      if (!d) return
      const { page, targets } = windowTargets(stateMirror().get(), windowId)
      const overPage = !!page && inside(page, x, y)
      if (overPage && !d.open) {
        d.open = true
        send('start', d.tabId)
      }
      const hit = overPage ? hitTabDrop(targets, x, y, d.tabId) : null
      if (sameHit(hit, d.hit)) return
      d.hit = hit
      if (d.open) send('hover', d.tabId, hit)
    }

    const finish = (drop: boolean) => {
      const d = drag
      drag = null
      window.removeEventListener('pointermove', onMove, true)
      window.removeEventListener('pointerup', onUp, true)
      window.removeEventListener('keydown', onKey, true)
      window.removeEventListener('blur', onBlur)
      if (!d) return
      if (drop && d.hit?.edge) void clu().split.dropTab({ tabId: d.tabId, targetTabId: d.hit.targetTabId, edge: d.hit.edge })
      if (d.open) send(drop && d.hit ? 'end' : 'cancel', d.tabId)
    }

    const onMove = (e: PointerEvent) => track(e.clientX, e.clientY)
    const onUp = (e: PointerEvent) => {
      track(e.clientX, e.clientY)
      finish(true)
    }
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape') return
      e.preventDefault()
      finish(false)
    }
    const onBlur = () => finish(false)

    const mine = (e: DragEvent) => drag?.tabId === e.tabId
    const offs = [
      dragBus.on('start', (e) => {
        finish(false)
        drag = { tabId: e.tabId, open: false, hit: null }
        window.addEventListener('pointermove', onMove, true)
        window.addEventListener('pointerup', onUp, true)
        window.addEventListener('keydown', onKey, true)
        window.addEventListener('blur', onBlur)
        track(e.x, e.y)
      }),
      dragBus.on('move', (e) => mine(e) && track(e.x, e.y)),
      dragBus.on('outside', (e) => mine(e) && track(e.x, e.y)),
      dragBus.on('end', (e) => {
        if (!mine(e)) return
        track(e.x, e.y)
        finish(true)
      }),
      dragBus.on('cancel', (e) => mine(e) && finish(false))
    ]
    return () => {
      for (const off of offs) off()
      finish(false)
    }
  }, [windowId])
}
