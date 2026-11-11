// Page context menu (06 2.8): opens at the click, kept inside the window. Picks go back by id only.
import { useLayoutEffect, useRef, useState } from 'react'
import type { MenuItem } from '../../../shared/types'
import { MenuList } from '../../common/MenuList'
import { closeInstance, type SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'

export function ContextMenu({ instance }: SurfaceProps) {
  const { menuId, items, x, y } = instance.props as { menuId: string; items: MenuItem[]; x: number; y: number }
  const ref = useRef<HTMLDivElement>(null)
  const [pos, setPos] = useState({ left: x, top: y })
  useLayoutEffect(() => {
    const el = ref.current
    if (!el) return
    setPos({ left: Math.max(0, Math.min(x, window.innerWidth - el.offsetWidth)), top: Math.max(0, Math.min(y, window.innerHeight - el.offsetHeight)) })
  }, [x, y])
  return (
    <div ref={ref} className="clu-page-menu" style={{ left: pos.left, top: pos.top }} onPointerDown={(e) => e.stopPropagation()}>
      <MenuList items={items} label="Page menu" onPick={(itemId) => void clu().menu.invoke({ menuId, itemId })} onClose={() => closeInstance(instance.instanceId)} />
    </div>
  )
}
