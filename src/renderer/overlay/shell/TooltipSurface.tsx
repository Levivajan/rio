// The frame's tooltip pill, drawn here so the page card never covers it: the passive view is the pill plus
// its shadow margin, placed where the frame laid the pill out (shell.tooltip). Never focused and takes no pointer input.
import type { SurfaceProps } from '../core/types'
import { TIP_MARGIN } from '../../common/Tooltip'

export function TooltipSurface({ instance }: SurfaceProps) {
  const p = (instance.props ?? {}) as { label?: unknown; keys?: unknown; above?: unknown; closing?: unknown }
  if (typeof p.label !== 'string' || !p.label) return null
  const keys = Array.isArray(p.keys) ? p.keys.filter((k): k is string => typeof k === 'string') : []
  return (
    <span
      role="tooltip"
      className="clu-tooltip"
      style={{ position: 'relative', left: 0, top: 0, margin: TIP_MARGIN }}
      data-placed="overlay"
      data-above={p.above === true || undefined}
      data-closing={p.closing === true || undefined}
    >
      {p.label}
      {keys.length ? (
        <span className="clu-keycaps">
          {keys.map((k, i) => (
            <kbd key={i} className="clu-keycap">
              {k}
            </kbd>
          ))}
        </span>
      ) : null}
    </span>
  )
}
