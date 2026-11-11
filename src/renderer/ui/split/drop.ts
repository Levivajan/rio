// Drop hit testing: which pane, and which edge band of it, a drag over the page area points at. Bands
// are 25% of the pane's width or height, capped at 160 DIP, so a wide pane keeps a wide center that drops nothing.
import { paneCard, type SplitEdge, type SplitView } from '../../../shared/split'
import type { Rect } from '../../../shared/types'

export const EDGE_BAND_SHARE = 0.25
export const EDGE_BAND_MAX = 160

/** A pane (or the single unsplit page) a drag can land on, as a card rect in window DIP. */
export interface DropTarget { tabId: string; rect: Rect }
export interface DropHit { targetTabId: string; edge: SplitEdge | null }

export function edgeBand(size: number): number {
  return Number.isFinite(size) && size > 0 ? Math.min(EDGE_BAND_MAX, size * EDGE_BAND_SHARE) : 0
}

export function inside(r: Rect, x: number, y: number): boolean {
  return Number.isFinite(x) && Number.isFinite(y) && x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height
}

/** The edge band of `r` under the point, or null in the center or outside. Where two bands meet, the nearer edge
 * (relative to its band) wins. */
export function hitEdge(r: Rect, x: number, y: number): SplitEdge | null {
  if (!inside(r, x, y)) return null
  const bx = edgeBand(r.width)
  const by = edgeBand(r.height)
  const bands: [SplitEdge, number, number][] = [
    ['left', x - r.x, bx],
    ['right', r.x + r.width - x, bx],
    ['top', y - r.y, by],
    ['bottom', r.y + r.height - y, by]
  ]
  let best: SplitEdge | null = null
  let bestRatio = Infinity
  for (const [edge, dist, band] of bands) {
    if (band <= 0 || dist >= band) continue
    const ratio = dist / band
    if (ratio < bestRatio) {
      best = edge
      bestRatio = ratio
    }
  }
  return best
}

/** Drop targets of a window: every pane card of the split on screen, or the active tab at the page rect. */
export function dropTargets(view: SplitView | null, pageRect: Rect | undefined, activeTabId: string | null): DropTarget[] {
  if (view) return view.panes.map((p) => ({ tabId: p.tabId, rect: paneCard(p) }))
  return activeTabId && pageRect ? [{ tabId: activeTabId, rect: pageRect }] : []
}

/** A sidebar tab over a pane's edge band; the dragged tab's own pane is never a target. */
export function hitTabDrop(targets: readonly DropTarget[], x: number, y: number, tabId: string): DropHit | null {
  const t = targets.find((c) => inside(c.rect, x, y))
  if (!t || t.tabId === tabId) return null
  const edge = hitEdge(t.rect, x, y)
  return edge ? { targetTabId: t.tabId, edge } : null
}

/** A pane bar dragged anywhere over another pane: a swap. */
export function hitSwap(targets: readonly DropTarget[], x: number, y: number, tabId: string): DropHit | null {
  const t = targets.find((c) => inside(c.rect, x, y))
  return t && t.tabId !== tabId ? { targetTabId: t.tabId, edge: null } : null
}

export const sameHit = (a: DropHit | null, b: DropHit | null): boolean => a?.targetTabId === b?.targetTabId && a?.edge === b?.edge
