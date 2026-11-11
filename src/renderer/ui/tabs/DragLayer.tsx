// What a sidebar drag draws (02-tabs.md F8): the ghost row in the
// selected style at 0.8 opacity under the pointer (0.93 of its size over the favorites), the drop line (2 px
// in the space accent, centered in the 5 px gap, with a 7 px ring at its left end) at the chosen indent, an accent band
// the full sidebar width behind a folder the row would drop into, or an outline for a zone. Over the favorites only the
// ghost shrinks: no line, slot or shift.
// A multi-selection drag shows the number of tabs on the ghost; the layer carries the ids of the dragBus start event.
// Fixed on the body, never hit-tested.
import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import { dragBus } from '../../common/dragBus'
import { createPortal } from 'react-dom'
import { INDENT_PX } from './computeDrop'
import { ghostElement, sidebarDrag, useDrag } from './useSidebarDrag'

/** The ring's center sits 11.5 into the row (x 19.5 at the 8 inset), the line ends 3 short of the row's right edge,
 *  and both center in the 5 px gap between rows. */
const DROP_RING_X = 11.5
const DROP_RING = 7
const DROP_LINE_END = 3
const DROP_GAP = 5

export function DragLayer() {
  const dragging = useDrag((v) => v.dragging)
  const drop = useDrag((v) => v.drop)
  const ghostRef = useRef<HTMLDivElement>(null)
  const rowRef = useRef<HTMLDivElement>(null)
  const [tabIds, setTabIds] = useState<string[]>([])
  useEffect(() => dragBus.on('start', (e) => setTabIds(e.tabIds)), [])
  // The folder the row would drop into takes the band behind its own row (data-drop-into, tabs.css), so the band sits
  // under the folder's glyph and name.
  const into = dragging && drop?.indicator.kind === 'inside' && drop.target.section === 'pinned' ? drop.target.parentId : null
  useLayoutEffect(() => {
    if (!into) return
    const row = document.querySelector<HTMLElement>(`[data-folder-id="${CSS.escape(into)}"]`)
    row?.setAttribute('data-drop-into', 'true')
    return () => row?.removeAttribute('data-drop-into')
  }, [into])

  // The rows under the ghost drop their hover close while a drag runs (tabs.css), so only the ghost shows one.
  useLayoutEffect(() => {
    if (!dragging) return
    document.documentElement.toggleAttribute('data-sidebar-dragging', true)
    return () => document.documentElement.removeAttribute('data-sidebar-dragging')
  }, [dragging])

  // The ghost follows the pointer outside React, so a move re-renders nothing but the drop indicator.
  useLayoutEffect(() => {
    const box = ghostRef.current
    if (!dragging || !box) return
    const copy = ghostElement()
    if (copy) rowRef.current?.replaceChildren(copy)
    const follow = () => {
      const v = sidebarDrag.get()
      box.style.transform = `translate(${v.x - v.grabX}px, ${v.y - v.grabY}px)`
    }
    follow()
    return sidebarDrag.subscribe(follow)
  }, [dragging])

  if (!dragging) return null
  const i = drop?.indicator
  return createPortal(
    <div className="clu-drag-layer" data-drag-layer="true" data-drag-tab-ids={tabIds.join(' ')} aria-hidden="true">
      <div ref={ghostRef} className="clu-drag-ghost" data-drag-ghost="true" data-over-favorites={drop?.target.section === 'favorite' || undefined}>
        <div ref={rowRef} className="clu-drag-ghost-row" />
        {tabIds.length > 1 ? (
          <span className="clu-drag-count" data-drag-count={tabIds.length}>
            {tabIds.length}
          </span>
        ) : null}
      </div>
      {i?.kind === 'line' ? (
        <div
          className="clu-drop-line"
          data-drop-line={i.level}
          style={{
            left: i.rect.x + DROP_RING_X - DROP_RING / 2 + (i.level - 1) * INDENT_PX,
            width: i.rect.width - DROP_RING_X + DROP_RING / 2 - DROP_LINE_END - (i.level - 1) * INDENT_PX,
            // A row's drop rect takes 1 px of the gap below it (collectRows).
            top: (i.edge === 'top' ? i.rect.y - DROP_GAP / 2 : i.rect.y + i.rect.height - 1 + DROP_GAP / 2) - DROP_RING / 2
          }}
        />
      ) : null}
      {i?.kind === 'zone' ? (
        <div className="clu-drop-box" data-drop-box={i.kind} style={{ left: i.rect.x, top: i.rect.y, width: i.rect.width, height: i.rect.height - 1 }} />
      ) : null}
    </div>,
    document.body
  )
}
