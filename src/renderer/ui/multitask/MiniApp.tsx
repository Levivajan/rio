// Mini window UI (05 Mini window): the toolbar over the page card. Main attaches the page view at the card's rect.
import { MINI_CONFIRM_H, MINI_INSET, MINI_TOOLBAR_H } from '../../../shared/multitask'
import { useCluState, useWindowId } from '../../common/useCluState'
import { PageIssuePanel } from '../engine/PageIssuePanel'
import { MiniToolbar } from './MiniToolbar'
import { MoveConfirmStrip } from './MoveConfirmStrip'

export function MiniApp() {
  const windowId = useWindowId() ?? ''
  const mini = useCluState((s) => s.windows[windowId]?.mini ?? null)
  // Engine hides the page view while its page has an issue, so this shows through.
  const issue = useCluState((s) => s.windows[windowId]?.peekPage?.issue ?? null)
  const card = { left: MINI_INSET, right: MINI_INSET, top: MINI_TOOLBAR_H + (mini?.confirming ? MINI_CONFIRM_H : 0), bottom: MINI_INSET }
  return (
    <main className="clu-frame" aria-label="Mini window" data-mini-window style={{ background: 'var(--surface-2)' }}>
      <MiniToolbar mini={mini} />
      {mini?.confirming ? <MoveConfirmStrip mini={mini} /> : null}
      <div
        className="clu-card"
        data-surface="page-card"
        aria-hidden="true"
        style={card}
      />
      {issue && mini ? <PageIssuePanel key={mini.tabId} tabId={mini.tabId} issue={issue} style={card} /> : null}
    </main>
  )
}
