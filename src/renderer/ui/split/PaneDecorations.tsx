// Per-pane decorations (PLAN 3.16 slot): in split view, a card per pane with its 32 px bar, the 10 px
// dividers in the gaps and the focus ring, all at the rects main laid the page views out with (splitView). On
// Chromium's native two-pane split (empty bars) Chromium draws those, and this slot stays empty. The
// frame renders this slot inside a box at layout.pageRect, so window rects are drawn relative to that origin.
// Dragging a pane bar onto another pane swaps the two (split.swap); Esc mid-drag drops nothing.
// The bar is the pane toolbar, as CluPaneBar draws it on the native two-pane split: back and
// forward (reload on the focused pane), the link glyph and the page URL centered, then on the focused pane site
// controls and split, and close on every pane.
import { useEffect, useRef, type PointerEvent as ReactPointerEvent } from 'react'
import { paneCard, type SplitDividerView, type SplitPaneView } from '../../../shared/split'
import type { Rect } from '../../../shared/types'
import { displayUrl } from '../../../shared/command/normalize'
import { BackIcon, ForwardIcon } from '../../common/icons'
import { IconButton } from '../../common/IconButton'
import { useCluState, useWindowId } from '../../common/useCluState'
import { dragSizes } from './drag'
import { hitSwap, sameHit, type DropHit } from './drop'
import { useTabDrop } from './useTabDrop'
import { CopyLinkIcon, ToolbarReloadIcon } from '../shell/NavIcons'
import './split.css'
import { clu } from '../../common/clu'

/** Pointer travel before a pane bar press becomes a drag. */
const DRAG_THRESHOLD = 4

/** Ring offset from the pane card, inside the 10 px gap. */
const RING_OUTSET = 3

// The pane toolbar glyphs (the same paths CluPaneBar draws on the native split): split 14.5 and close 12 across at
// 16, stroke 1.4 and 1.5.
const SplitIcon = () => (
  <svg width={16} height={16} viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth={1.4} strokeLinecap="round" strokeLinejoin="round" aria-hidden>
    <rect x="1.45" y="1.45" width="13.1" height="13.1" rx="3" />
    <path d="M8 1.45v13.1" />
  </svg>
)

const PaneCloseIcon = () => (
  <svg width={16} height={16} viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth={1.5} strokeLinecap="round" aria-hidden>
    <path d="M2.75 2.75l10.5 10.5M13.25 2.75 2.75 13.25" />
  </svg>
)

const SlidersIcon = () => (
  <svg width={16} height={16} viewBox="0 0 17 17" fill="none" stroke="currentColor" strokeWidth={1.4} strokeLinecap="round" strokeLinejoin="round" aria-hidden>
    <rect x="1.1" y="1.1" width="14.8" height="14.8" rx="4.1" />
    <path d="M4.4 6.2h4.5M11.9 6.2h0.7M4.4 10.8h0.7M8.1 10.8h4.5" />
    <circle cx="10.4" cy="6.2" r="1.5" />
    <circle cx="6.6" cy="10.8" r="1.5" />
  </svg>
)

/** The bar's URL: the page address without its scheme; empty for a blank page. */
export function paneUrlText(url: string): string {
  if (!url || url === 'about:blank') return ''
  return displayUrl(url).replace(/^https?:\/\//, '')
}

/** The bar's URL: the host darker, then its port, path and query in a lighter grey. A page that is not
 *  http(s) keeps one color. */
export function paneUrlParts(url: string): { host: string; rest: string } {
  const text = paneUrlText(url)
  if (!/^https?:\/\//i.test(url)) return { host: text, rest: '' }
  const close = text.startsWith('[') ? text.indexOf(']') : -1
  if (text.startsWith('[') && close < 0) return { host: text, rest: '' }
  const start = close + 1
  const end = text.slice(start).search(/[:/?#]/)
  return end < 0 ? { host: text, rest: '' } : { host: text.slice(0, start + end), rest: text.slice(start + end) }
}

function place(r: Rect, origin: { x: number; y: number }, outset = 0) {
  return { left: r.x - origin.x - outset, top: r.y - origin.y - outset, width: r.width + 2 * outset, height: r.height + 2 * outset }
}

/** Press, drag past the threshold and release over another pane to swap; Esc or a lost capture cancels. */
function useSwapDrag(tabId: string, panes: SplitPaneView[]) {
  const drag = useRef<{ x: number; y: number; dragging: boolean; hit: DropHit | null } | null>(null)
  const panesRef = useRef(panes)
  panesRef.current = panes
  const offKey = useRef<(() => void) | null>(null)

  const send = (phase: 'start' | 'hover' | 'end' | 'cancel', hit?: DropHit | null) =>
    void clu().split.dragState({ phase, tabId, ...(hit ? { targetTabId: hit.targetTabId } : {}) })
  const finish = (swap: boolean) => {
    const d = drag.current
    drag.current = null
    offKey.current?.()
    offKey.current = null
    if (!d?.dragging) return
    if (swap && d.hit) void clu().split.swap({ tabId, targetTabId: d.hit.targetTabId })
    send(swap && d.hit ? 'end' : 'cancel')
  }
  // A pane that unmounts mid-drag (closed or separated elsewhere) ends its drag.
  const finishRef = useRef(finish)
  finishRef.current = finish
  useEffect(() => () => finishRef.current(false), [])

  return {
    onPointerDown: (e: ReactPointerEvent<HTMLElement>) => {
      if (e.button !== 0) return
      void clu().split.focusPane({ tabId })
      e.currentTarget.setPointerCapture(e.pointerId)
      drag.current = { x: e.clientX, y: e.clientY, dragging: false, hit: null }
    },
    onPointerMove: (e: ReactPointerEvent<HTMLElement>) => {
      const d = drag.current
      if (!d) return
      if (!d.dragging) {
        if (Math.hypot(e.clientX - d.x, e.clientY - d.y) < DRAG_THRESHOLD) return
        d.dragging = true
        send('start')
        const onKey = (k: KeyboardEvent) => {
          if (k.key !== 'Escape') return
          k.preventDefault()
          finish(false)
        }
        window.addEventListener('keydown', onKey, true)
        offKey.current = () => window.removeEventListener('keydown', onKey, true)
      }
      const targets = panesRef.current.map((p) => ({ tabId: p.tabId, rect: paneCard(p) }))
      const hit = hitSwap(targets, e.clientX, e.clientY, tabId)
      if (sameHit(hit, d.hit)) return
      d.hit = hit
      send('hover', hit)
    },
    onPointerUp: () => finish(true),
    onPointerCancel: () => finish(false),
    onLostPointerCapture: () => finish(false)
  }
}

function PaneCard({ pane, panes, focused, origin }: { pane: SplitPaneView; panes: SplitPaneView[]; focused: boolean; origin: { x: number; y: number } }) {
  const tab = useCluState((s) => s.tabs[pane.tabId])
  const url = paneUrlText(tab?.url ?? '')
  const parts = paneUrlParts(tab?.url ?? '')
  const card = paneCard(pane)
  const swap = useSwapDrag(pane.tabId, panes)
  const tabId = pane.tabId
  // A button press acts on this pane and never starts a swap drag.
  const keep = (e: ReactPointerEvent) => e.stopPropagation()
  return (
    <>
      <div className="clu-split-pane" data-split-pane={tabId} data-focused={String(focused)} style={place(card, origin)}>
        <div className="clu-split-bar" data-split-pane-bar={tabId} style={{ height: pane.bar.height }} {...swap}>
          <div className="clu-split-bar-nav">
            <IconButton aria-label="Back" className="clu-split-bar-button" disabled={!tab?.canGoBack} onPointerDown={keep} onClick={() => void clu().nav.back({ tabId })}>
              <BackIcon width={14} height={14} />
            </IconButton>
            <IconButton aria-label="Forward" className="clu-split-bar-button" disabled={!tab?.canGoForward} onPointerDown={keep} onClick={() => void clu().nav.forward({ tabId })}>
              <ForwardIcon width={14} height={14} />
            </IconButton>
            {focused ? (
              <IconButton aria-label="Reload" className="clu-split-bar-button" onPointerDown={keep} onClick={() => void clu().nav.reload({ tabId })}>
                <ToolbarReloadIcon width={16} height={16} />
              </IconButton>
            ) : null}
          </div>
          <span className="clu-split-bar-url" data-split-pane-url>
            {url ? <CopyLinkIcon width={14} height={14} /> : null}
            <span className="clu-split-bar-url-text">
              {url ? (
                <>
                  {parts.host}
                  {parts.rest ? <span className="clu-split-bar-url-rest">{parts.rest}</span> : null}
                </>
              ) : (
                'Search or Enter URL...'
              )}
            </span>
          </span>
          <div className="clu-split-bar-end">
            {focused && url ? (
              <IconButton
                aria-label="Site controls"
                className="clu-split-bar-button"
                onPointerDown={keep}
                onClick={(e) => {
                  const r = e.currentTarget.getBoundingClientRect()
                  void clu().siteControls.open({ anchor: { x: r.x, y: r.y, width: r.width, height: r.height } })
                }}
              >
                <SlidersIcon />
              </IconButton>
            ) : null}
            {focused ? (
              <IconButton aria-label="Separate pane" className="clu-split-bar-button" onPointerDown={keep} onClick={() => void clu().split.removePane({ tabId, mode: 'separate' })}>
                <SplitIcon />
              </IconButton>
            ) : null}
            <IconButton aria-label="Close pane" className="clu-split-bar-button" onPointerDown={keep} onClick={() => void clu().split.removePane({ tabId, mode: 'close' })}>
              <PaneCloseIcon />
            </IconButton>
          </div>
        </div>
      </div>
      {focused ? <div className="clu-split-ring" data-split-focus-ring={tabId} style={place(card, origin, RING_OUTSET)} /> : null}
    </>
  )
}

function Divider({ splitId, divider, origin }: { splitId: string; divider: SplitDividerView; origin: { x: number; y: number } }) {
  const row = divider.dir === 'row'
  const drag = useRef<{ start: number; sizes: number[]; pending: number[] | null; frame: number } | null>(null)

  const send = () => {
    const d = drag.current
    if (!d) return
    d.frame = 0
    if (!d.pending) return
    void clu().split.resize({ splitId, path: divider.path, sizes: d.pending })
    d.pending = null
  }
  const end = (e: ReactPointerEvent) => {
    const d = drag.current
    if (!d) return
    if (d.frame) cancelAnimationFrame(d.frame)
    send()
    drag.current = null
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId)
  }

  return (
    <div
      className="clu-split-divider"
      role="separator"
      aria-orientation={row ? 'vertical' : 'horizontal'}
      data-split-divider={`${divider.path.join('.')}:${divider.index}`}
      data-dir={divider.dir}
      style={place(divider.rect, origin)}
      onPointerDown={(e) => {
        if (e.button !== 0) return
        // Capture keeps pointermove coming while the cursor crosses the page views on either side.
        e.currentTarget.setPointerCapture(e.pointerId)
        drag.current = { start: row ? e.clientX : e.clientY, sizes: divider.sizes, pending: null, frame: 0 }
      }}
      onPointerMove={(e) => {
        const d = drag.current
        if (!d) return
        d.pending = dragSizes(d.sizes, divider.index, (row ? e.clientX : e.clientY) - d.start, divider.avail)
        if (!d.frame) d.frame = requestAnimationFrame(send)
      }}
      onPointerUp={end}
      onPointerCancel={end}
      onLostPointerCapture={end}
      onDoubleClick={() => void clu().split.equalize({ splitId, path: divider.path })}
    />
  )
}

export function PaneDecorations(_props: { tabId: string }) {
  const windowId = useWindowId() ?? ''
  useTabDrop(windowId)
  const view = useCluState((s) => s.windows[windowId]?.splitView ?? null)
  const pageRect = useCluState((s) => s.windows[windowId]?.shell.layout.pageRect)
  const focusedTabId = useCluState((s) => s.windows[windowId]?.focusedTabId ?? null)
  if (!view || !pageRect) return null
  // Chromium's two-pane split draws each pane's bar, close button, divider and focus outline itself (M6):
  // its panes come with empty bars, and the frame draws nothing over them.
  if (view.panes.every((p) => p.bar.height === 0)) return <div className="clu-split-view" data-split-view={view.splitId} data-native="true" />
  const origin = { x: pageRect.x, y: pageRect.y }
  return (
    <div className="clu-split-view" data-split-view={view.splitId}>
      {view.panes.map((p) => (
        <PaneCard key={p.tabId} pane={p} panes={view.panes} focused={p.tabId === focusedTabId} origin={origin} />
      ))}
      {view.dividers.map((d) => (
        <Divider key={`${d.path.join('.')}:${d.index}`} splitId={view.splitId} divider={d} origin={origin} />
      ))}
    </div>
  )
}
