// Toolbar mode: View > Show Toolbar (⇧⌘D) puts the URL bar across the top of the page.
// The band spans the page card's width above it (TOOLBAR_H in layout.ts) and drags the window; its field is the URL
// pill, 30 tall and centered on the top row's line. Absent while the mode is off or in focus mode. A page in Developer
// Mode takes the band for the Developer Mode strip instead.
import { useCluState, useWindowId } from '../../common/useCluState'
import { DevStrip, useDevStripPage } from '../extensions/DevStrip'
import { UrlPill } from './UrlPill'

export function Toolbar() {
  const windowId = useWindowId() ?? ''
  const focus = useCluState((s) => !!s.windows[windowId]?.shell.focusMode)
  const shown = useCluState((s) => !!s.windows[windowId]?.shell.toolbar)
  const dev = useDevStripPage(windowId) !== null
  const rect = useCluState((s) => s.windows[windowId]?.shell.layout.pageRect)
  if (focus || !rect || (!shown && !dev)) return null
  if (dev) return <DevStrip rect={rect} />
  return (
    <div className="clu-toolbar" data-shell="toolbar" style={{ left: rect.x, width: rect.width, height: rect.y }}>
      <UrlPill placement="toolbar" />
    </div>
  )
}
