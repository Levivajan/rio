// Window frame: background, the card at layout.pageRect, per-pane slots, empty state,
// and the sidebar wrapped by shell's slots.
import type { ReactNode } from 'react'
import { useCluState, useWindowId } from '../../common/useCluState'
import { PaneOverlay } from '../engine/PaneOverlay'
import { CardLoading } from '../shell/CardLoading'
import { EdgeHotZone } from '../shell/EdgeHotZone'
import { ResizeHandle } from '../shell/ResizeHandle'
import { SidebarFrame } from '../shell/SidebarFrame'
import { Toolbar } from '../shell/Toolbar'
import { PaneDecorations } from '../split/PaneDecorations'
import { ThemeBackground } from '../theme/ThemeBackground'

export function Frame({ children }: { children: ReactNode }) {
  const windowId = useWindowId() ?? ''
  const rect = useCluState((s) => s.windows[windowId]?.shell.layout.pageRect)
  const activeTabId = useCluState((s) => s.windows[windowId]?.activeTabId ?? null)
  return (
    <div className="clu-frame">
      <ThemeBackground />
      {rect ? (
        // With no page the card is the bare tinted surface, no text.
        <div className="clu-card" data-surface="page-card" data-empty={!activeTabId} style={{ left: rect.x, top: rect.y, width: rect.width, height: rect.height }} />
      ) : null}
      <CardLoading />
      <Toolbar />
      {activeTabId && rect ? (
        <div style={{ position: 'absolute', left: rect.x, top: rect.y, width: rect.width, height: rect.height }} data-pane-tab-id={activeTabId}>
          <PaneDecorations tabId={activeTabId} />
          <PaneOverlay tabId={activeTabId} />
        </div>
      ) : null}
      <SidebarFrame>{children}</SidebarFrame>
      <ResizeHandle />
      <EdgeHotZone />
    </div>
  )
}
