// Site controls button (docs/plan/08-extensions.md): a sliders glyph (16.2 x 16.2 drawn,
// no box at rest; a 28 x 26 rounded box on hover and while its panel is open) at the right end of the URL pill and in the Mini window toolbar. The page's blocked count is in its name and
// in the popover, not drawn (the glyph shows alone). Opens the Site controls popover under itself.
import { siteControlsPage } from '../../../shared/state/extensions'
import { IconButton } from '../../common/IconButton'
import { useCluState, useWindowId } from '../../common/useCluState'
import './siteControlsButton.css'
import { clu } from '../../common/clu'

const MAX_COUNT = 99

/** The count the button shows: nothing at 0 or below, capped at 99+. */
export function blockedBadge(count: number): string {
  if (!Number.isFinite(count) || count <= 0) return ''
  return count > MAX_COUNT ? `${MAX_COUNT}+` : String(Math.floor(count))
}

// The knobs are rings: the track stops at each ring. It draws no mark while blocking is off.
// Drawn at 16.2 pt: 14.8 plus its 1.4 stroke.
export const SLIDERS_SIZE = 17
function SlidersIcon() {
  return (
    <svg width={SLIDERS_SIZE} height={SLIDERS_SIZE} viewBox="0 0 17 17" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.1" y="1.1" width="14.8" height="14.8" rx="4.1" />
      <path d="M4.4 6.2h4.5M11.9 6.2h0.7M4.4 10.8h0.7M8.1 10.8h4.5" />
      <circle cx="10.4" cy="6.2" r="1.5" />
      <circle cx="6.6" cy="10.8" r="1.5" />
    </svg>
  )
}

export function SiteControlsButton() {
  const windowId = useWindowId() ?? ''
  const url = useCluState((s) => siteControlsPage(s, windowId)?.url ?? '')
  const count = useCluState((s) => siteControlsPage(s, windowId)?.blockedCount ?? 0)
  const enabled = useCluState((s) => siteControlsPage(s, windowId)?.blockingEnabled ?? true)
  // Pressed while its panel is open.
  const open = useCluState((s) => s.windows[windowId]?.overlay.surfaces.some((o) => o.surface === 'site-controls') ?? false)
  if (!url || url === 'about:blank') return null
  const badge = enabled ? blockedBadge(count) : ''
  return (
    <IconButton
      aria-label={badge ? `Site controls, ${badge} blocked` : 'Site controls'}
      aria-haspopup="dialog"
      aria-expanded={open}
      className="clu-site-controls-button"
      data-site-controls-button
      onClick={(e) => {
        const r = e.currentTarget.getBoundingClientRect()
        void clu().siteControls.open({ anchor: { x: r.x, y: r.y, width: r.width, height: r.height } })
      }}
    >
      <SlidersIcon />
      {badge ? (
        <span data-blocked-count>{badge}</span>
      ) : null}
    </IconButton>
  )
}
