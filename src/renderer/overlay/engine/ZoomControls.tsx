// Zoom controls: minus, percent, plus and reset, in the Site controls popover and the zoom pill.
import { IconButton } from '../../common/IconButton'
import { PlusIcon } from '../../common/icons'
import { useCluState, useWindowId } from '../../common/useCluState'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

const MIN = 0.25
const MAX = 5

const MinusIcon = () => (
  <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
    <path d="M3 7h8" />
  </svg>
)

export function ZoomControls({ tabId }: { tabId: string }) {
  const windowId = useWindowId() ?? ''
  const factor = useCluState((s) => s.tabs[tabId]?.zoomFactor ?? s.windows[windowId]?.peekPage?.zoomFactor ?? 1)
  const step = (dir: -1 | 0 | 1) => void clu().zoom.step({ tabId, dir })
  return (
    <div role="group" aria-label="Page zoom" className="clu-zoom">
      <IconButton aria-label="Zoom out" disabled={factor <= MIN} onClick={() => step(-1)}>
        <MinusIcon />
      </IconButton>
      <output className="clu-zoom-percent">{Math.round(factor * 100)}%</output>
      <IconButton aria-label="Zoom in" disabled={factor >= MAX} onClick={() => step(1)}>
        <PlusIcon />
      </IconButton>
      <Button onClick={() => step(0)}>
        Reset
      </Button>
    </div>
  )
}
