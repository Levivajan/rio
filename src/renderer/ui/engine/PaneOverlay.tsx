// Per-pane overlay slot (seeded by the scaffold): the issue panel of each pane whose tab has an issue. The
// frame renders this inside a box at layout.pageRect, so a split pane's window rect is drawn relative to that origin.
import type { CSSProperties } from 'react'
import { useCluState, useWindowId } from '../../common/useCluState'
import { PageIssuePanel } from './PageIssuePanel'
import { UrlPillBypass } from './UrlPillBypass'

function PaneIssue({ tabId, style }: { tabId: string; style?: CSSProperties }) {
  const issue = useCluState((s) => s.tabs[tabId]?.issue ?? null)
  return issue ? <PageIssuePanel key={tabId} tabId={tabId} issue={issue} {...(style ? { style } : {})} /> : null
}

export function PaneOverlay({ tabId }: { tabId: string }) {
  const windowId = useWindowId() ?? ''
  const panes = useCluState((s) => s.windows[windowId]?.splitView?.panes ?? null)
  const origin = useCluState((s) => s.windows[windowId]?.shell.layout.pageRect)
  return (
    <>
      <UrlPillBypass tabId={tabId} />
      {panes && origin ? (
        panes.map((p) => <PaneIssue key={p.tabId} tabId={p.tabId} style={{ left: p.rect.x - origin.x, top: p.rect.y - origin.y, width: p.rect.width, height: p.rect.height }} />)
      ) : (
        <PaneIssue tabId={tabId} />
      )}
    </>
  )
}
