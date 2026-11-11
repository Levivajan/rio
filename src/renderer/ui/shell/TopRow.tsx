// Top row (docs/plan/01-shell.md F1, docs/chromium/parity-sidebar.md): the traffic-light spacer
// (collapsed in full screen), then the controls the sidebar's width allows. From 212
// the sidebar toggle sits after the lights and back, forward and reload sit at the right on a 34 pitch, reload 6 from
// the card; up to 211 the toggle and reload fold into a "..." button after back and forward on a 28 pitch. The row
// drags the window; its buttons do not (design.md section 3).
import { IconButton } from '../../common/IconButton'
import { CloseIcon } from '../../common/icons'
import { useCluState, useWindowId } from '../../common/useCluState'
import { topRowMode } from '../../../shared/shell/sidebarMetrics'
import { SIDEBAR_WIDTH } from '../../../shared/shell/layout'
import { openMenuAt } from '../tabs/renaming'
import { NavButtons } from './NavButtons'
import { MoreIcon } from './NavIcons'
import { SidebarIcon } from './SidebarIcon'
import { clu } from '../../common/clu'

/** The toggle box starts 77 from the window's edge when there is room: 3 past the lights' spacer. */
const TOGGLE_NUDGE = 3

/** Which of the "…" and "+" popovers is open in this window: their buttons show × meanwhile. */
export function useOpenPopover(menu: 'more' | 'new'): boolean {
  const windowId = useWindowId() ?? ''
  return useCluState(
    (s) => s.windows[windowId]?.overlay.surfaces.some((i) => i.surface === 'menu-popover' && (i.props as { menu?: string } | null)?.menu === menu) ?? false
  )
}

function MoreButton() {
  const open = useOpenPopover('more')
  return (
    <IconButton
      aria-label={open ? 'Close' : 'More options'}
      aria-haspopup="menu"
      aria-expanded={open}
      className="clu-nav-button"
      data-shell="more"
      data-open={open || undefined}
      onClick={(e) => {
        const r = e.currentTarget.getBoundingClientRect()
        void openMenuAt('more', { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height) })
      }}
    >
      {open ? <CloseIcon /> : <MoreIcon />}
    </IconButton>
  )
}

/** How far the top row moves down for the revealed full-screen bar: its covered height, junk as 0. */
export function titlebarShift(covered: unknown): number {
  return typeof covered === 'number' && Number.isFinite(covered) ? Math.min(200, Math.max(0, Math.round(covered))) : 0
}

/** The shift for this window's revealed full-screen bar. */
export function useTitlebarShift(): number {
  const windowId = useWindowId() ?? ''
  return useCluState((s) => titlebarShift(s.windows[windowId]?.shell.titlebarReveal))
}

/** The sidebar's padding while the full-screen bar shows: the whole column (top row, address pill, favorites, the
 *  list) moves down together by the bar's height, so nothing slides under anything, and the footer keeps its place. */
export function titlebarPad(reveal: number, base = 'var(--top-row-y)'): { paddingTop: string } | undefined {
  return reveal > 0 ? { paddingTop: `calc(${base} + ${reveal}px)` } : undefined
}

export function TopRow() {
  const windowId = useWindowId() ?? ''
  const spacer = useCluState((s) => s.windows[windowId]?.shell.layout.trafficSpacer ?? 66)
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  const hidden = useCluState((s) => {
    const shell = s.windows[windowId]?.shell
    return !!shell && (shell.sidebar.hidden || shell.focusMode)
  })
  // In split view each pane has its own back, forward and reload (CluPaneBar), so the row keeps only the toggle.
  const split = useCluState((s) => (s.windows[windowId]?.splitView?.panes.length ?? 0) > 1)
  // In full screen the bar macOS slides down at the screen's top (its menu bar and the window's buttons) would
  // cover the row; the sidebar column moves down with it, step for step (titlebarPad), and back up as it hides.
  const reveal = useTitlebarShift()
  const mode = topRowMode(width)
  return (
    <div className="clu-top-row" data-surface="top-row" data-mode={mode} data-titlebar-reveal={reveal}>
      <div className="clu-traffic-spacer" data-shell="traffic-spacer" style={{ width: spacer ? spacer + TOGGLE_NUDGE : 0 }} />
      {mode === 'full' ? (
        <IconButton aria-label={hidden ? 'Show sidebar' : 'Hide sidebar'} data-shell="sidebar-toggle" command="sidebar.toggle" onClick={() => void clu().shell.toggleSidebar()}>
          <SidebarIcon />
        </IconButton>
      ) : null}
      <div className="clu-top-row-fill" />
      {split ? (
        mode === 'compact' ? (
          <MoreButton />
        ) : null
      ) : (
        <div className="clu-nav-group">
          <NavButtons reload={mode === 'full'} />
          {mode === 'compact' ? <MoreButton /> : null}
        </div>
      )}
    </div>
  )
}
