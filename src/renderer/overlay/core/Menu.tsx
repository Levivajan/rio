// Menu surface: items, separators, disabled items, two submenu levels with edge flipping,
// keyboard navigation, Esc. Positioned at its anchor, kept inside the window.
import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import { LOOK } from '../../../shared/look/tokens'
import { useReducedMotion } from '../../common/useReducedMotion'
import type { MenuItem, Rect } from '../../../shared/types'
import { MenuList } from '../../common/MenuList'
import { closeInstance, respond, type SurfaceProps } from './types'

export function MenuSurface({ instance }: SurfaceProps) {
  const props = (instance.props ?? {}) as { items?: MenuItem[]; anchor?: Rect }
  const anchor = props.anchor ?? { x: 0, y: 0, width: 0, height: 0 }
  const ref = useRef<HTMLDivElement>(null)
  const [pos, setPos] = useState({ left: anchor.x, top: anchor.y + anchor.height })
  // Esc fades the menu (and its submenu) out over 190 ms before it closes, as a native macOS menu does.
  const [closing, setClosing] = useState(false)
  const reduced = useReducedMotion()
  useEffect(() => {
    if (!closing) return
    const timer = setTimeout(() => closeInstance(instance.instanceId), reduced ? 0 : LOOK.motion.menuCloseMs)
    return () => clearTimeout(timer)
  }, [closing, reduced, instance.instanceId])
  useLayoutEffect(() => {
    const el = ref.current
    if (!el) return
    const w = el.offsetWidth
    const h = el.offsetHeight
    const left = anchor.x + w > window.innerWidth ? Math.max(0, anchor.x + anchor.width - w) : anchor.x
    const below = anchor.y + anchor.height
    const top = below + h > window.innerHeight ? Math.max(0, anchor.y - h) : below
    setPos({ left, top })
  }, [anchor.x, anchor.y, anchor.width, anchor.height])
  return (
    <div ref={ref} style={{ position: 'fixed', left: pos.left, top: pos.top }} data-surface="menu" onPointerDown={(e) => e.stopPropagation()}>
      <MenuList items={props.items ?? []} focusFirst={false} closing={closing} onPick={(id) => respond(instance.instanceId, id)} onClose={() => setClosing(true)} />
    </div>
  )
}
