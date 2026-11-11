// Overlay root: renders the open instances of this view's own layer.
// A passive view renders its one surface at its own origin. Transparent root.
import { useEffect, useRef } from 'react'
import type { OverlaySurfaceState } from '../../shared/state'
import { markAfterPaint } from '../common/perf'
import { useAppearance, useReducedMotion } from '../common/useReducedMotion'
import { useCluState, useHasSnapshot, useWindowId } from '../common/useCluState'
import { registry } from './core/registry'
import { closeInstance } from './core/types'
import { useSpaceColors } from './core/useSpaceColors'
import { clu } from '../common/clu'
import { PEEK_SLOT } from '../../shared/shell/peek'
import { SidebarPeekWarmup } from './shell/SidebarPeek'

const NONE: OverlaySurfaceState[] = []

export function App() {
  const windowId = useWindowId() ?? ''
  const layer = clu().layer ?? 'top'
  const slot = clu().slot
  const hasSnapshot = useHasSnapshot()
  const all = useCluState((s) => s.windows[windowId]?.overlay.surfaces ?? NONE)
  const mine = all.filter((i) => i.layer === layer && (layer !== 'passive' || i.slot === slot))
  const marked = useRef(false)
  useReducedMotion()
  useAppearance(useSpaceColors(windowId))

  useEffect(() => {
    if (!hasSnapshot || marked.current) return
    marked.current = true
    void clu().ui.ready()
    if (layer === 'top') markAfterPaint('overlay:ready')
  }, [hasSnapshot, layer])

  const dismissible = mine.filter((i) => i.modality === 'light-dismiss').at(-1)
  const modal = mine.some((i) => i.modality === 'modal')
  return (
    <div
      className="clu-overlay-root"
      data-layer={layer}
      onPointerDown={() => {
        if (dismissible) closeInstance(dismissible.instanceId)
      }}
      data-scrim={modal ? 'modal' : dismissible ? 'light' : 'none'}
    >
      {layer === 'passive' && slot === PEEK_SLOT && hasSnapshot && mine.length === 0 ? <SidebarPeekWarmup windowId={windowId} /> : null}
      {mine.map((instance) => {
        const Surface = registry[instance.surface]
        if (!Surface) return null
        return (
          <div key={instance.instanceId} data-surface={instance.surface} data-instance-id={instance.instanceId} data-layer={layer}>
            <Surface instance={instance} windowId={windowId} />
          </div>
        )
      })}
    </div>
  )
}
