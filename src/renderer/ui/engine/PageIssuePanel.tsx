// Page issue panel (06 2.2, 2.12, 2.13): why a page is not showing, drawn in its pane's slot.
import { useState, type CSSProperties } from 'react'
import type { PageIssue } from '../../../shared/engine'
import { displayHost } from '../../../shared/url'
import './engine.css'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

type Action = 'reload' | 'back' | 'proceed'

function crashTitle(issue: Extract<PageIssue, { kind: 'crashed' }>): string {
  if (issue.repeated) return 'This page keeps crashing'
  return issue.reason === 'oom' ? 'This page ran out of memory' : 'This page crashed'
}

function CertificateIssue({ issue, act }: { issue: Extract<PageIssue, { kind: 'certificate' }>; act: (a: Action) => void }) {
  const [details, setDetails] = useState(false)
  const host = issue.host || 'this site'
  return (
    <>
      <h2 className="clu-issue-title">Your connection to {host} is not private</h2>
      <p className="clu-issue-code">{issue.name}</p>
      <div className="clu-issue-actions">
        <Button variant="primary" onClick={() => act('back')}>
          Go Back
        </Button>
        <Button aria-expanded={details} onClick={() => setDetails((d) => !d)}>
          Details
        </Button>
      </div>
      {details && issue.bypassable ? (
        <div className="clu-issue-details">
          <p>Rio can&apos;t verify this site&apos;s certificate. Someone may be trying to read what you send to it.</p>
          <button type="button" className="clu-issue-proceed" onClick={() => act('proceed')}>
            Proceed to {host} (unsafe)
          </button>
        </div>
      ) : null}
      {details && !issue.bypassable ? (
        <div className="clu-issue-details">
          <p>{host} only accepts secure connections, so Rio can&apos;t continue to it.</p>
        </div>
      ) : null}
    </>
  )
}

function IssueBody({ issue, act }: { issue: PageIssue; act: (a: Action) => void }) {
  switch (issue.kind) {
    case 'load-failed':
      return (
        <>
          <h2 className="clu-issue-title">This site can&apos;t be reached</h2>
          <p>{displayHost(issue.url) || 'The site'} didn&apos;t load.</p>
          <p className="clu-issue-code">{issue.name}</p>
          <Button variant="primary" onClick={() => act('reload')}>
            Try Again
          </Button>
        </>
      )
    case 'certificate':
      return <CertificateIssue key={`${issue.host} ${issue.name}`} issue={issue} act={act} />
    case 'crashed':
      return (
        <>
          <h2 className="clu-issue-title">{crashTitle(issue)}</h2>
          <Button variant="primary" onClick={() => act('reload')}>
            Reload
          </Button>
        </>
      )
  }
}

export function PageIssuePanel({ tabId, issue, style = { inset: 0 } }: { tabId: string; issue: PageIssue; style?: CSSProperties }) {
  const act = (action: Action) => void clu().issue.act({ tabId, action })
  return (
    <div className="clu-card clu-issue" role="alert" style={style} data-issue-tab-id={tabId} data-issue-kind={issue.kind}>
      <IssueBody issue={issue} act={act} />
    </div>
  )
}
