// Ctrl+Tab switcher (02-tabs F13): up to 5 recent tabs as cards with a preview (or a large favicon
// tile) and the title. A passive surface that never takes focus; Tab, Shift+Tab, Esc and the Ctrl release reach
// main through the page's key events. Clicking a card commits it, on the primary button's release: the hand still
// holds Control, and on Mac a Control click is a secondary click that never fires click.
import { safeIconSrc } from '../../common/favicon'
import { LetterTile } from '../../common/LetterTile'
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'

export interface SwitcherCardProps { tabId: string; title: string; host: string; favicon: string | null; thumb: string | null }

const str = (v: unknown): string => (typeof v === 'string' ? v : '')
const image = (v: unknown): string | null => safeIconSrc(v)

/** Reads the props defensively: a junk card is dropped, a bad selection reads as none. */
export function readSwitcherProps(props: unknown): { cards: SwitcherCardProps[]; selected: number } {
  const p = (props && typeof props === 'object' ? props : {}) as { cards?: unknown; selected?: unknown }
  const cards = (Array.isArray(p.cards) ? p.cards : []).flatMap((c: unknown) => {
    const o = (c && typeof c === 'object' ? c : {}) as Record<string, unknown>
    const tabId = str(o.tabId)
    return tabId ? [{ tabId, title: str(o.title), host: str(o.host), favicon: image(o.favicon), thumb: image(o.thumb) }] : []
  })
  const selected = typeof p.selected === 'number' && Number.isInteger(p.selected) ? p.selected : -1
  return { cards, selected }
}

export function TabSwitcher({ instance }: SurfaceProps) {
  const { cards, selected } = readSwitcherProps(instance.props)
  return (
    <div className="clu-switcher" role="listbox" aria-label="Recent tabs" data-count={cards.length}>
      {cards.map((c, i) => (
        <div
          key={c.tabId}
          role="option"
          aria-selected={i === selected}
          className="clu-switcher-card"
          data-tab-id={c.tabId}
          onPointerDown={(e) => e.preventDefault()}
          onPointerUp={(e) => {
            if (e.button === 0) void clu().switcher.respond({ action: 'commit', tabId: c.tabId })
          }}
          onContextMenu={(e) => e.preventDefault()}
        >
          <div className="clu-switcher-preview">
            {c.thumb ? (
              <img src={c.thumb} alt="" draggable={false} />
            ) : c.favicon ? (
              <img className="clu-switcher-favicon-large" src={c.favicon} alt="" draggable={false} />
            ) : (
              <LetterTile text={c.title || c.host} size={40} />
            )}
          </div>
          <div className="clu-switcher-label">
            {c.favicon ? <img className="clu-switcher-favicon" src={c.favicon} alt="" draggable={false} /> : <LetterTile text={c.title || c.host} />}
            <span className="clu-switcher-title">{c.title}</span>
          </div>
        </div>
      ))}
    </div>
  )
}
