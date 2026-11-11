// One command bar row: icon (favicon, letter tile, magnifier, globe or action glyph), the title and URL
// with matched ranges in bold, and on the right the switch badge, the action's shortcut or the remove control.
// Titles and URLs render as text nodes only, never as HTML.
import { memo, type ReactNode } from 'react'
import { assistantById, middleEllipsis } from '../../../shared/command/ask'
import type { Range } from '../../../shared/command/match'
import { formatChord } from '../../../shared/commands'
import { CloseIcon } from '../../common/icons'
import { LetterTile } from '../../common/LetterTile'
import { hasGlyph, SpaceIconGlyph } from '../../common/spaceSymbols'
import type { BarRow, Disposition } from './useCommandBar'
import { safeIconSrc } from '../../common/favicon'
import { AssistantIcon } from './AssistantIcon'

/** The badge's label, shown beside its arrow pill. */
const BADGE_LABEL = { 'Switch to tab': 'Switch to Tab', 'Switch to split': 'Switch to Split' } as const

/** Longest title or URL drawn; the rest is clipped by the row anyway. */
const MAX_TEXT = 512

const glyph = { width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
const SearchGlyph = () => (
  <svg {...glyph}><circle cx="7" cy="7" r="4.25" /><path d="m10.25 10.25 3 3" /></svg>
)
const GlobeGlyph = () => (
  <svg {...glyph}><circle cx="8" cy="8" r="5.75" /><path d="M2.5 8h11M8 2.25c1.6 1.6 2.4 3.5 2.4 5.75S9.6 12.15 8 13.75C6.4 12.15 5.6 10.25 5.6 8S6.4 3.85 8 2.25" /></svg>
)
const ArrowGlyph = () => (
  <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d="M1.75 6h8.5M6.75 2.5 10.25 6l-3.5 3.5" /></svg>
)
const ChevronGlyph = () => (
  <svg width="9" height="9" viewBox="0 0 10 10" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d="m2.25 3.75 2.75 2.75 2.75-2.75" /></svg>
)
const ChatGlyph = () => (
  <svg {...glyph}><path d="M8 2.75c3.2 0 5.5 2.05 5.5 4.6s-2.3 4.6-5.5 4.6c-.65 0-1.27-.08-1.85-.24L3.2 13.1l.75-2.45C3.05 9.8 2.5 8.65 2.5 7.35c0-2.55 2.3-4.6 5.5-4.6Z" /><path d="M5.6 7.4h.01M8 7.4h.01M10.4 7.4h.01" strokeWidth="2" /></svg>
)
/** The compass drawn for an action on the current tab (Move to Today). */
const CompassGlyph = () => (
  <svg {...glyph}><circle cx="8" cy="8" r="5.75" /><path d="m10.25 5.75-1.4 3.1-3.1 1.4 1.4-3.1 3.1-1.4Z" /></svg>
)
const FolderGlyph = () => (
  <svg {...glyph}><path d="M2.5 4.75c0-.7.55-1.25 1.25-1.25h2.6l1.4 1.5h4.5c.7 0 1.25.55 1.25 1.25v5.5c0 .7-.55 1.25-1.25 1.25h-8.5c-.7 0-1.25-.55-1.25-1.25v-7Z" /><path d="M2.5 7h11" /></svg>
)
const PinToGlyph = () => (
  <svg {...glyph}><path d="M11.5 11.5 4.5 4.5M4.5 9.5v-5h5" /></svg>
)
const CONTEXT_GLYPH = { today: CompassGlyph, folder: FolderGlyph, pin: PinToGlyph } as const
const ActionGlyph = () => (
  <svg {...glyph}><rect x="2.5" y="2.5" width="11" height="11" rx="3" /><path d="m6.5 5.75 2.5 2.25-2.5 2.25" /></svg>
)

/** Text with the matched ranges in bold, as text nodes only. Ranges outside the text are ignored. */
export function Highlight({ text, ranges }: { text: string; ranges: readonly Range[] }) {
  if (ranges.length === 0) return <>{text}</>
  const parts: ReactNode[] = []
  let at = 0
  for (const [start, end] of ranges) {
    const s = Math.max(start, at)
    const e = Math.min(end, text.length)
    if (e <= s) continue
    if (s > at) parts.push(text.slice(at, s))
    parts.push(<strong key={s}>{text.slice(s, e)}</strong>)
    at = e
  }
  if (at < text.length) parts.push(text.slice(at))
  return <>{parts}</>
}

/** Only Chromium's favicon2 icons, Clu's favicon store and inline images load; anything else falls back to the letter tile. */
export const safeFavicon = (src: string | null | undefined): string | null => safeIconSrc(src)

/** A history row's icon in the browser: Chromium's favicon2 for the page, as CluUrlPolicy's FaviconUrlFor() builds it.
 *  Outside chrome:// (the fake harness) there is no favicon service, so the letter tile stays. */
export function historyFavicon(url: string, protocol = globalThis.location?.protocol): string | null {
  if (protocol !== 'chrome:' || !/^https?:\/\//i.test(url) || url.length > 4096) return null
  return `chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=${encodeURIComponent(url)}&allowGoogleServerFallback=0`
}

function shortcut(accelerator: string | undefined): string | null {
  if (!accelerator) return null
  try {
    return formatChord(accelerator)
  } catch {
    return null
  }
}

export function RowIcon({ row, favicon, onFaviconError }: { row: BarRow; favicon: string | null; onFaviconError: () => void }) {
  if (row.ask) return <AssistantIcon id={row.ask} />
  if (row.group === 'help') return <ChatGlyph />
  if (row.context) {
    const Glyph = CONTEXT_GLYPH[row.context.icon]
    return <Glyph />
  }
  // Switch to <space> and Move tab to <space>: the space's emoji or symbol, else its letter.
  if (row.space) return hasGlyph(row.space.icon) ? <SpaceIconGlyph icon={row.space.icon} size={16} className="clu-cmd-space-icon" /> : <LetterTile text={row.space.name} />
  switch (row.result.kind) {
    case 'search':
      return <SearchGlyph />
    case 'navigate':
      return <GlobeGlyph />
    case 'command':
    case 'provided':
    case 'action':
      return <ActionGlyph />
    default:
      return favicon ? <img className="clu-cmd-favicon" src={favicon} alt="" draggable={false} onError={onFaviconError} /> : <LetterTile text={row.title || row.url} />
  }
}

export interface ResultRowProps {
  row: BarRow
  index: number
  id: string
  selected: boolean
  favicon: string | null
  onHover(index: number): void
  onRun(index: number, disposition: Disposition): void
  onRemove(index: number): void
  onFaviconError(src: string): void
  /** The Ask row's assistant menu is open. */
  pickerOpen?: boolean
  onPicker?(): void
}

export const ResultRow = memo(function ResultRow({ row, index, id, selected, favicon, onHover, onRun, onRemove, onFaviconError, pickerOpen, onPicker }: ResultRowProps) {
  const keys = shortcut(row.accelerator)
  // Long addresses lose their middle; the shortened text drops the match ranges. Tab rows (Switch to Tab)
  // show the title alone.
  const url = row.result.kind === 'tab' ? '' : row.url.slice(0, MAX_TEXT)
  const shortUrl = middleEllipsis(url)
  return (
    <div
      role="option"
      id={id}
      className="clu-cmd-row"
      aria-selected={selected}
      data-group={row.group}
      data-kind={row.result.kind}
      onPointerMove={() => onHover(index)}
      onClick={(e) => onRun(index, e.metaKey ? 'new-tab' : e.altKey ? 'split' : 'default')}
    >
      <span className="clu-cmd-icon" data-tile={row.result.kind === 'tab' || row.result.kind === 'reload' || row.ask !== undefined || undefined} data-help={row.group === 'help' || undefined} data-assistant={row.ask}>
        <RowIcon row={row} favicon={favicon} onFaviconError={() => favicon && onFaviconError(favicon)} />
      </span>
      {/* Title, then an em dash and the URL in the secondary color, as one line ("GitHub — github.com"). */}
      <span className="clu-cmd-text">
        <span className="clu-cmd-title">
          <Highlight text={row.title.slice(0, MAX_TEXT)} ranges={row.titleRanges} />
        </span>
        {row.context ? (
          // The grey path chip: "● Rio test" or "Rio test / Rio folder".
          <span className="clu-cmd-path">
            {row.context.dot ? <span className="clu-cmd-path-dot" style={{ background: row.context.dot }} aria-hidden="true" /> : null}
            {row.context.chip}
          </span>
        ) : null}
        {url ? (
          <span className="clu-cmd-url">
            <Highlight text={shortUrl} ranges={shortUrl === url ? row.urlRanges : []} />
          </span>
        ) : null}
      </span>
      <span className="clu-cmd-end">
        {row.ask ? (
          <>
            {/* "Ask ChatGPT" opens the assistant menu (Option+Down from the keyboard); the arrow pill asks. */}
            <span
              className="clu-cmd-ask"
              data-ask-picker
              data-open={pickerOpen || undefined}
              onPointerDown={(e) => {
                e.preventDefault()
                e.stopPropagation()
              }}
              onClick={(e) => {
                e.stopPropagation()
                onPicker?.()
              }}
            >
              Ask {assistantById(row.ask).name}
              <ChevronGlyph />
            </span>
            <span className="clu-cmd-arrow" aria-hidden="true">
              <ArrowGlyph />
            </span>
          </>
        ) : null}
        {row.badge ? (
          <>
            <span className="clu-cmd-badge">{BADGE_LABEL[row.badge]}</span>
            <span className="clu-cmd-arrow" aria-hidden="true">
              <ArrowGlyph />
            </span>
          </>
        ) : null}
        {keys ? <kbd className="clu-cmd-keys">{keys}</kbd> : null}
        {row.historyUrl ? (
          // Mouse twin of Cmd+Option+Backspace; hidden from assistive tech, which uses the shortcut.
          <span
            aria-hidden="true"
            className="clu-cmd-remove"
            data-remove
            onPointerDown={(e) => e.preventDefault()}
            onClick={(e) => {
              e.stopPropagation()
              onRemove(index)
            }}
          >
            <CloseIcon width={12} height={12} />
          </span>
        ) : null}
      </span>
    </div>
  )
})
