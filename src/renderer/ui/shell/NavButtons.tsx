// Back, forward and reload in the top row (docs/plan/01-shell.md F7). Engine's nav.* commands do the
// navigating. Back and forward open the nav-history menu on a 450 ms press or a right-click; reload becomes stop
// while the page loads and Cmd+click duplicates the tab.
import { useEffect, useMemo, useRef, useState, type MouseEvent, type PointerEvent } from 'react'
import { createLongPress, type NavDirection } from '../../../shared/shell/nav'
import { BackIcon, ForwardIcon } from '../../common/icons'
import { IconButton } from '../../common/IconButton'
import { useCluState, useWindowId } from '../../common/useCluState'
import { StopIcon, ToolbarReloadIcon } from './NavIcons'
import { clu } from '../../common/clu'

/** The arrows' 16 pt and reload's 18 pt boxes (back and forward drawn
 *  14.7 x 11.6, reload 14 x 16.5). */
const ARROW = 16
const RELOAD = 18

function openHistory(direction: NavDirection, el: HTMLElement): void {
  const r = el.getBoundingClientRect()
  void clu().shell.openNavHistory({ direction, anchor: { x: r.x, y: r.y, width: r.width, height: r.height } })
}

function HistoryButton({ direction, tabId, enabled }: { direction: NavDirection; tabId: string | null; enabled: boolean }) {
  const target = useRef<HTMLElement | null>(null)
  const press = useMemo(() => createLongPress(() => target.current && openHistory(direction, target.current)), [direction])
  useEffect(() => () => press.dispose(), [press])
  const end = () => press.up()
  return (
    <IconButton
      aria-label={direction === 'back' ? 'Go back' : 'Go forward'}
      command={direction === 'back' ? 'nav.back' : 'nav.forward'}
      className="clu-nav-button"
      data-shell={`nav-${direction}`}
      disabled={!enabled}
      onPointerDown={(e: PointerEvent<HTMLButtonElement>) => {
        if (e.button !== 0) return
        target.current = e.currentTarget
        press.down()
      }}
      onPointerUp={end}
      onPointerLeave={end}
      onPointerCancel={end}
      onContextMenu={(e: MouseEvent<HTMLButtonElement>) => {
        e.preventDefault()
        if (enabled) openHistory(direction, e.currentTarget)
      }}
      onClick={() => {
        // The pointer up after a long press is not a click on the button.
        if (press.consume() || !tabId) return
        void (direction === 'back' ? clu().nav.back({ tabId }) : clu().nav.forward({ tabId }))
      }}
    >
      {direction === 'back' ? <BackIcon width={ARROW} height={ARROW} /> : <ForwardIcon width={ARROW} height={ARROW} />}
    </IconButton>
  )
}

/**
 * Reload turns once per click. The turn lives in this button's own state and ends on its animationend:
 * the reload starts the page loading, which swaps this button for stop, and the button that comes back when the load
 * ends starts at rest instead of replaying the turn.
 */
function ReloadButton({ tabId }: { tabId: string | null }) {
  const [spin, setSpin] = useState(0)
  const [spinning, setSpinning] = useState(false)
  return (
    <IconButton
      aria-label="Reload"
      command="nav.reload"
      className="clu-nav-button"
      data-shell="nav-reload"
      disabled={!tabId}
      onClick={(e) => {
        if (!tabId) return
        if (e.metaKey) return void clu().tabs.duplicate({ tabId })
        setSpin((n) => n + 1)
        setSpinning(true)
        void clu().nav.reload({ tabId })
      }}
    >
      <ToolbarReloadIcon key={spin} className={spinning ? 'clu-reload-spin' : undefined} onAnimationEnd={() => setSpinning(false)} width={RELOAD} height={RELOAD} />
    </IconButton>
  )
}

/** `reload` false: the narrow top row folds reload (and stop) into its "..." button. */
export function NavButtons({ reload = true }: { reload?: boolean }) {
  const windowId = useWindowId() ?? ''
  const tabId = useCluState((s) => s.windows[windowId]?.activeTabId ?? null)
  const canBack = useCluState((s) => (tabId ? (s.tabs[tabId]?.canGoBack ?? false) : false))
  const canForward = useCluState((s) => (tabId ? (s.tabs[tabId]?.canGoForward ?? false) : false))
  const loading = useCluState((s) => (tabId ? (s.tabs[tabId]?.loading ?? false) : false))
  return (
    <>
      <HistoryButton direction="back" tabId={tabId} enabled={canBack} />
      <HistoryButton direction="forward" tabId={tabId} enabled={canForward} />
      {!reload ? null : loading ? (
        <IconButton aria-label="Stop" className="clu-nav-button" data-shell="nav-stop" disabled={!tabId} onClick={() => tabId && void clu().nav.stop({ tabId })}>
          <StopIcon />
        </IconButton>
      ) : (
        <ReloadButton tabId={tabId} />
      )}
    </>
  )
}
