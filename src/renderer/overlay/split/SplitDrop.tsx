// Drop preview: while a sidebar tab or a pane bar is dragged over the page, a translucent card shows
// where it lands (half of the target pane toward the edge, or the whole pane for a swap). Input stays with the UI.
import { useCluState } from '../../common/useCluState'
import type { SurfaceProps } from '../core/types'
import './split.css'

export function SplitDrop({ windowId }: SurfaceProps) {
  const drop = useCluState((s) => s.windows[windowId]?.splitDrop ?? null)
  const zone = drop?.zone
  if (!drop || !zone) return null
  return (
    <div
      className="clu-split-drop"
      data-split-drop-zone={drop.targetTabId ?? ''}
      data-edge={drop.edge ?? 'swap'}
      aria-hidden
      style={{ left: zone.x, top: zone.y, width: zone.width, height: zone.height }}
    />
  )
}
