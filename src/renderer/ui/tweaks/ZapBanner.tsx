// Zap banner at the bottom of the sidebar (docs/plan/08-extensions.md F11): shows while the window's
// active tab is in zap mode, with the site's hidden count, the last result, Undo and Done.
import { invoke } from '../../common/invoke'
import { useCluState, useWindowId } from '../../common/useCluState'
import './zap.css'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

export function ZapBanner() {
  const windowId = useWindowId() ?? ''
  const tabId = useCluState((s) => s.windows[windowId]?.activeTabId ?? null)
  const zap = useCluState((s) => (tabId ? (s.tabs[tabId]?.zapMode ?? null) : null))
  const count = useCluState((s) => (tabId ? (s.tabs[tabId]?.tweak?.zapCount ?? 0) : 0))
  if (!tabId || !zap) return null
  return (
    <section className="clu-zap-banner" aria-label="Zap mode" data-zap-banner={tabId}>
      <div className="clu-zap-text">
        <strong>Zap mode</strong>
        <span role="status">{zap.lastResult ?? 'Click an element to hide it'}</span>
      </div>
      <div className="clu-zap-actions">
        <span className="clu-zap-count">{count} hidden</span>
        <Button onClick={() => void invoke(clu().tweaks.undoZap({ tabId }))} aria-keyshortcuts="Meta+Z">
          Undo
        </Button>
        <Button variant="primary" onClick={() => void invoke(clu().tweaks.stopZap({ tabId }))} aria-keyshortcuts="Escape">
          Done
        </Button>
      </div>
    </section>
  )
}
