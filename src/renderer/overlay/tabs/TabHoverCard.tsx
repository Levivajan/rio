// Tab hover card (02-tabs.md F15): a title pill, hung from the pointer. Its passive view is
// the pill's widest size plus a shadow margin (main sized it from the same model); the pill takes its text's width
// and cuts a long title with an ellipsis. Never focused; only Gmail's message list takes pointer input. A preview site's live page gets a
// 239 x 256 card: it draws the frame and shadow, and the browser lays a live view of the site's compact page, in the
// tab's own profile, inside it. The site's name shows until that page paints. Gmail's card is drawn from the account's
// unread mail: the no-unread card, or the newest unread messages. The
// message list takes the pointer inside its own rect: it holds itself open while the pointer is on it, its rows
// highlight, and a click opens that message in the row's own Gmail tab.
import { clu } from '../../common/clu'
import { HOVER_CARD, MAIL_CARD, type HoverCardModel, type MailItem } from '../../../shared/tabs'
import type { SurfaceProps } from '../core/types'

export function TabHoverCard({ instance }: SurfaceProps) {
  // `closing`: the browser keeps the title pill for its fade out (LOOK.tooltip.hoverCardOutMs), then removes it.
  const m = (instance.props ?? {}) as Partial<HoverCardModel> & { closing?: unknown; tabId?: unknown }
  if (!m.width || !m.height || typeof m.title !== 'string') return null
  if (m.kind === 'preview') {
    const site = typeof m.site === 'string' ? m.site.slice(0, 40) : ''
    return (
      <div className="clu-live-card" role="tooltip" aria-label={`${site || m.title} preview`} data-site={site} style={{ margin: HOVER_CARD.margin }}>
        <span className="clu-live-card-wait">{site || m.title}</span>
      </div>
    )
  }
  if (m.kind === 'mail') return <MailCard m={m} tabId={typeof m.tabId === 'string' ? m.tabId : ''} closing={m.closing === true} />
  return (
    <div className="clu-hover-card" role="tooltip" data-closing={m.closing === true || undefined} style={{ margin: HOVER_CARD.margin, maxWidth: HOVER_CARD.maxPill }}>
      {m.title}
    </div>
  )
}

/** A feed time as the card shows it: the time of day for today, else the month and day; nothing for junk. */
export function mailTime(issued: string, now: Date = new Date()): string {
  const at = new Date(issued.slice(0, 40))
  if (!issued || Number.isNaN(at.getTime())) return ''
  const today = at.toDateString() === now.toDateString()
  return today ? at.toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' }) : at.toLocaleDateString('en-US', { month: 'short', day: 'numeric' })
}

function cleanItems(raw: unknown): MailItem[] {
  if (!Array.isArray(raw)) return []
  const text = (v: unknown, max: number) => (typeof v === 'string' ? v.slice(0, max) : '')
  return raw
    .filter((i): i is Record<string, unknown> => typeof i === 'object' && i !== null)
    .slice(0, MAIL_CARD.maxRows)
    .map((i) => ({ author: text(i.author, 120), title: text(i.title, 300), issued: text(i.issued, 40), url: text(i.url, 2048) }))
}

/** A feed link the card may open: a Gmail page, nothing else. */
export function mailLink(url: string): string | null {
  try {
    const u = new URL(url)
    return u.protocol === 'https:' && u.hostname === 'mail.google.com' ? u.href : null
  } catch {
    return null
  }
}

/** Gmail's card: with no unread mail two centered lines and leaf;
 *  with unread mail and the feed's messages, a header with the count over the newest messages, newest first (sender
 *  and time, the subject under them); with only a count (the feed unavailable), the count alone. Never "No unread
 *  emails" without a source that says so. */
function MailCard({ m, tabId, closing }: { m: Partial<HoverCardModel>; tabId: string; closing: boolean }) {
  const n = typeof m.unread === 'number' && Number.isFinite(m.unread) && m.unread > 0 ? Math.floor(m.unread) : 0
  const source = m.mailSource === 'feed' || m.mailSource === 'title' ? m.mailSource : 'unknown'
  const items = n > 0 ? cleanItems(m.mailItems) : []
  const count = `${n.toLocaleString('en-US')} unread email${n === 1 ? '' : 's'}`
  if (items.length > 0) {
    const hold = (held: boolean) => void (tabId && clu().tabs.hover({ tabId, held }))
    return (
      <div
        className="clu-mail-card clu-mail-list"
        role="dialog"
        aria-label={count}
        data-unread={n}
        data-source={source}
        data-closing={closing || undefined}
        style={{ margin: HOVER_CARD.margin }}
        onPointerEnter={() => hold(true)}
        onPointerLeave={() => hold(false)}
      >
        <div className="clu-mail-head">{count}</div>
        <ul className="clu-mail-rows">
          {items.map((item, i) => {
            const link = mailLink(item.url)
            return (
            <li
              key={i}
              className="clu-mail-row"
              data-link={link ? 'true' : undefined}
              onClick={link && tabId ? () => void clu().tabs.openMail({ tabId, url: link }) : undefined}
            >
              <span className="clu-mail-line">
                <span className="clu-mail-from">{item.author || 'Unknown sender'}</span>
                <span className="clu-mail-time">{mailTime(item.issued)}</span>
              </span>
              <span className="clu-mail-subject">{item.title || '(no subject)'}</span>
            </li>
            )
          })}
        </ul>
      </div>
    )
  }
  const text = n > 0 ? count : source === 'unknown' ? 'Open Gmail to check for unread email' : null
  return (
    <div className="clu-mail-card" role="tooltip" data-unread={n} data-source={source} data-closing={closing || undefined} style={{ margin: HOVER_CARD.margin }}>
      {text ?? (
        <span className="clu-mail-exhale">
          No unread emails, you can exhale!
          <br />
          <span aria-hidden="true">{'\u{1F343}'}</span>
        </span>
      )}
    </div>
  )
}
