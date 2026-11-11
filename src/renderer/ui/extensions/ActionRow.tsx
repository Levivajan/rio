// Extension action row (docs/plan/08-extensions.md F1, docs/chromium/m7.md): the pinned actions of the
// window's profile under the URL field and in the Mini window toolbar, then the extensions menu button. Chromium runs
// the action: its popup hangs from the clicked button, a side panel opens next to the page card. Absent when the
// profile has no extension.
import type { MouseEvent } from 'react'
import type { ActionButton } from '../../../shared/state/extensions'
import { IconButton } from '../../common/IconButton'
import { invoke } from '../../common/invoke'
import { useCluState, useWindowId } from '../../common/useCluState'
import { clu } from '../../common/clu'
import './actionRow.css'

const NONE: ActionButton[] = []

const rectOf = (e: MouseEvent<HTMLElement>) => {
  const r = e.currentTarget.getBoundingClientRect()
  return { x: r.x, y: r.y, width: r.width, height: r.height }
}

/** The button's name: the action's title (its tooltip), the badge read out after it. */
export function actionLabel(a: ActionButton): string {
  const title = a.title.trim() || a.name
  return a.badge ? `${title}, ${a.badge}` : title
}

function PuzzleIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" aria-hidden="true">
      <path d="M5.5 2.5a1.25 1.25 0 0 1 2.5 0V3.5h2.5V6h1a1.25 1.25 0 0 1 0 2.5h-1V11H8V10a1.25 1.25 0 0 0-2.5 0v1H3V8.5h1a1.25 1.25 0 0 0 0-2.5H3V3.5h2.5Z" />
    </svg>
  )
}

export function ActionRow() {
  const windowId = useWindowId() ?? ''
  const row = useCluState((s) => s.windows[windowId]?.actionRow ?? null)
  const actions = row?.actions ?? NONE
  if (!row) return null
  const pinned = actions.filter((a) => a.pinned)
  return (
    <div className="clu-action-row" data-action-row role="toolbar" aria-label="Extensions">
      {pinned.map((a) => (
        <IconButton
          key={a.id}
          aria-label={actionLabel(a)}
          className="clu-action-button"
          data-action={a.id}
          data-badge={a.badge || undefined}
          aria-disabled={!a.enabled || undefined}
          onClick={(e) => void invoke(clu().extensions.runAction({ id: a.id, anchor: rectOf(e) }))}
        >
          {a.icon ? <img src={a.icon} alt="" width={16} height={16} draggable={false} /> : <span className="clu-action-letter">{a.name.slice(0, 1).toUpperCase()}</span>}
        </IconButton>
      ))}
      <IconButton aria-label="Extensions" aria-haspopup="dialog" className="clu-action-button" data-extensions-menu-button onClick={(e) => void invoke(clu().extensions.openMenu({ anchor: rectOf(e) }))}>
        <PuzzleIcon />
      </IconButton>
    </div>
  )
}
