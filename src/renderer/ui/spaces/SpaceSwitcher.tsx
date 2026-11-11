// Space switcher row (docs/plan/03-spaces.md "Switcher row"): left-aligned after the
// archive button, a 32 box per space holding its emoji, or an 8 pt dot for a space without one (no
// letter discs); inactive glyphs take the sidebar's tint, the active one shows in full (a dot goes dark), and the
// tooltip has the profile chip and the ⌃N keycaps. The boxes shrink evenly; past their minimums inactive spaces become
// dots, then the active space shows with its neighbors (switcherLayout.ts). Every icon is a tab drop target. Icons drag to
// reorder (4 px threshold), right-click opens the icon menu, and the + button at the end opens the sidebar's + menu.
import { Fragment, useLayoutEffect, useRef, useState, type CSSProperties, type PointerEvent as ReactPointerEvent } from 'react'
import { IconButton } from '../../common/IconButton'
import { CloseIcon, FooterPlusIcon } from '../../common/icons'
import { hasGlyph, SpaceIconGlyph } from '../../common/spaceSymbols'
import { useOpenPopover } from '../shell/TopRow'
import { Tooltip } from '../../common/Tooltip'
import { sameItems, useCluState, useWindowId } from '../../common/useCluState'
import { openMenuAt, openSpaceMenu, RENAME_SPACE_EVENT } from '../tabs/renaming'
import { useTabDropTarget } from '../tabs/useTabDropTarget'
import { DRAG_THRESHOLD, dropGap, reorderTarget, type IconSlot } from './reorderDrag'
import { switcherLayout } from './switcherLayout'
import './spaces.css'
import { clu } from '../../common/clu'

interface Drag { spaceId: string; from: number; startX: number; dx: number; gap: number | null; dragging: boolean }
type DropMark = 'before' | 'after' | undefined

const rectOf = (el: Element) => {
  const r = el.getBoundingClientRect()
  return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height) }
}

function SpaceButton({ spaceId, index, active, dot, dragDx, drop, onDragStart }: {
  spaceId: string; index: number; active: boolean; dot: boolean; dragDx: number | null; drop: DropMark
  onDragStart: (e: ReactPointerEvent<HTMLButtonElement>, spaceId: string, index: number) => void
}) {
  const space = useCluState((s) => s.spaces[spaceId])
  const profile = useCluState((s) => (space ? s.profiles[space.profileId]?.name : undefined))
  const dropTarget = useTabDropTarget({ spaceId })
  if (!space) return null
  // The tooltip pill: the name, the profile as a chip, then ⌃ and the number as keycaps.
  return (
    <Tooltip label={space.name} chip={profile} command={index < 9 ? `spaces.goto${index + 1}` : undefined}>
      <button
        type="button"
        className="clu-space-icon"
        aria-label={space.name}
        aria-pressed={active}
        {...(index < 9 ? { 'aria-keyshortcuts': `Control+${index + 1}` } : {})}
        data-space-id={spaceId}
        data-index={index}
        data-kind={dot ? 'dot' : 'icon'}
        {...(dragDx !== null ? { 'data-dragging': 'true' } : {})}
        {...(drop ? { 'data-drop': drop } : {})}
        style={dragDx !== null ? ({ transform: `translateX(${dragDx}px)` } as CSSProperties) : undefined}
        {...dropTarget}
        onPointerDown={(e) => onDragStart(e, spaceId, index)}
        onClick={(e) => {
          // A drag that just ended lands its click here; it is not a switch.
          if (e.currentTarget.dataset.dragged === 'true') {
            delete e.currentTarget.dataset.dragged
            return
          }
          void clu().spaces.switch({ spaceId })
        }}
        onContextMenu={(e) => {
          e.preventDefault()
          // The space name row's menu; Rename Space… renames in that row, so a
          // space not shown comes forward first.
          void openSpaceMenu(spaceId, { x: Math.round(e.clientX), y: Math.round(e.clientY) }, () => {
            void (active ? Promise.resolve() : clu().spaces.switch({ spaceId })).then(() =>
              requestAnimationFrame(() => window.dispatchEvent(new CustomEvent(RENAME_SPACE_EVENT, { detail: spaceId })))
            )
          })
        }}
      >
        {!dot && hasGlyph(space.icon) ? <SpaceIconGlyph icon={space.icon} size={14} className="clu-space-emoji" /> : <span className="clu-space-dot" aria-hidden="true" />}
      </button>
    </Tooltip>
  )
}

/** While the window's New Space panel is open, the bar marks where the new space will go and drops the "+". */
export function SpaceSwitcher({ creating = false }: { creating?: boolean }) {
  const windowId = useWindowId() ?? ''
  const order = useCluState((s) => s.spaceOrder, sameItems)
  const active = useCluState((s) => s.windows[windowId]?.activeSpaceId ?? '')
  const newOpen = useOpenPopover('new')
  const row = useRef<HTMLDivElement>(null)
  const [width, setWidth] = useState(0)

  useLayoutEffect(() => {
    const el = row.current
    if (!el) return
    setWidth(el.getBoundingClientRect().width)
    const ro = new ResizeObserver(([e]) => e && setWidth(e.contentRect.width))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  const [drag, setDrag] = useState<Drag | null>(null)
  const dragRef = useRef<Drag | null>(null)
  const update = (d: Drag | null) => {
    dragRef.current = d
    setDrag(d)
  }

  const slots = (): IconSlot[] =>
    [...(row.current?.querySelectorAll<HTMLElement>('.clu-space-icon[data-index]') ?? [])].map((el) => {
      const r = el.getBoundingClientRect()
      // The dragged icon is translated; its slot is where it sits at rest.
      const dx = el.dataset.dragging === 'true' ? (dragRef.current?.dx ?? 0) : 0
      return { index: Number(el.dataset.index), left: r.left - dx, right: r.right - dx }
    })

  const onDragStart = (e: ReactPointerEvent<HTMLButtonElement>, spaceId: string, index: number) => {
    if (e.button !== 0 || order.length < 2) return
    const el = e.currentTarget
    const startX = e.clientX
    update({ spaceId, from: index, startX, dx: 0, gap: null, dragging: false })
    const move = (ev: PointerEvent) => {
      const d = dragRef.current
      if (!d) return
      const dx = ev.clientX - d.startX
      if (!d.dragging && Math.abs(dx) < DRAG_THRESHOLD && Math.abs(ev.clientY - e.clientY) < DRAG_THRESHOLD) return
      update({ ...d, dragging: true, dx, gap: dropGap(ev.clientX, slots()) })
    }
    const finish = (commit: boolean) => (ev: PointerEvent) => {
      el.removeEventListener('pointermove', move)
      el.removeEventListener('pointerup', up)
      el.removeEventListener('pointercancel', cancel)
      const d = dragRef.current
      update(null)
      if (!d?.dragging) return
      el.dataset.dragged = 'true'
      setTimeout(() => delete el.dataset.dragged, 0)
      const to = commit ? reorderTarget(d.from, dropGap(ev.clientX, slots())) : null
      if (to !== null) void clu().spaces.reorder({ spaceId: d.spaceId, toIndex: to })
    }
    const up = finish(true)
    const cancel = finish(false)
    try {
      el.setPointerCapture(e.pointerId)
    } catch {
      // The pointer may already be gone; moves still arrive while it is over the icon.
    }
    el.addEventListener('pointermove', move)
    el.addEventListener('pointerup', up)
    el.addEventListener('pointercancel', cancel)
  }

  const layout = switcherLayout(order.length, order.indexOf(active), width)
  const shown = layout.items.map((i) => i.index)
  const lastShown = shown[shown.length - 1] ?? -1
  // The drop line shows only where the drop would move the space.
  const markGap = drag?.dragging && reorderTarget(drag.from, drag.gap) !== null ? drag.gap : null
  const markFor = (index: number): DropMark => (markGap === index ? 'before' : markGap === index + 1 && index === lastShown ? 'after' : undefined)
  return (
    <div className="clu-space-bar">
      <div ref={row} className="clu-space-switcher" role="group" aria-label="Spaces" data-dragging={String(!!drag?.dragging)}>
        {layout.items.map(({ index, kind }) => {
          const id = order[index] as string
          return (
            <Fragment key={id}>
              <SpaceButton
                spaceId={id}
                index={index}
                active={id === active && !creating}
                dot={kind === 'dot'}
                dragDx={drag?.dragging && drag.spaceId === id ? drag.dx : null}
                drop={markFor(index)}
                onDragStart={onDragStart}
              />
              {/* The space being made goes right after the current one; it shows there as the active dot. */}
              {creating && id === active ? (
                <span className="clu-space-placeholder" aria-hidden="true">
                  <span className="clu-space-dot" />
                </span>
              ) : null}
            </Fragment>
          )
        })}
      </div>
      {creating ? null : (
        <IconButton aria-label={newOpen ? 'Close' : 'New'} aria-haspopup="menu" aria-expanded={newOpen} data-open={newOpen || undefined} className="clu-space-new" onClick={(e) => void openMenuAt('new', rectOf(e.currentTarget))}>
          {newOpen ? <CloseIcon /> : <FooterPlusIcon />}
        </IconButton>
      )}
    </div>
  )
}
