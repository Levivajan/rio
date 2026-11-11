// Tooltip (design.md section 1): 450 ms delay, 100 ms after
// the first one closes, never native title. The pill hangs 9.5 right of and below the
// pointer with its tight corner toward it, a command's shortcut follows the label as keycaps, and it fades out over
// 280 ms on a decelerating curve. A pill that would pass the window's bottom or right edge (the bottom bar's)
// or of a scrolling panel it sits in hangs above the pointer instead and slides left, so it is never
// cut off. In the frame the page card covers the frame's own document, so there the pill is laid out but not drawn,
// and the overlay draws it at the same place above the card (shell.tooltip), as it draws menus.
import { useEffect, useId, useLayoutEffect, useRef, useState, type CSSProperties, type PointerEvent, type ReactElement, type ReactNode } from 'react'
import { chordKeys } from '../../shared/commands'
import { effectiveEntry, SHORTCUTS_KEY } from '../../shared/shortcuts'
import { LOOK } from '../../shared/look/tokens'
import { clu } from './clu'
import { useCluState } from './useCluState'

let lastClosedAt = 0
const selectCustom = (s: { settings: { global: Record<string, unknown> } }) => s.settings.global[SHORTCUTS_KEY]

type Phase = 'closed' | 'open' | 'closing'

/** How close a pill may come to the window's edge. */
const EDGE = 4
/** The room around the overlay's pill for its shadow, less than the pill's 9.5 offset so the view never covers the
 *  pointer (it would take the hover from the button and the pill would flicker). */
export const TIP_MARGIN = 8

// The frame turns this on: its pills go to the overlay. A browser that refuses (an old build) turns it back off.
let overlayTips = false
// The tooltip the overlay shows now, so a late hide from the previous one leaves the next alone.
let overlayOwner: string | null = null

export function drawTooltipsInOverlay(on: boolean): void {
  overlayTips = on
}

function sendTip(owner: string, tip: { label: string; keys: string[]; rect: DOMRect; above: boolean; closing: boolean } | null, refused: () => void) {
  if (!tip) {
    if (overlayOwner !== owner) return
    overlayOwner = null
    void clu().shell.tooltip({})
    return
  }
  overlayOwner = owner
  const m = TIP_MARGIN
  const rect = { x: Math.round(tip.rect.left - m), y: Math.round(tip.rect.top - m), width: Math.ceil(tip.rect.width + 2 * m), height: Math.ceil(tip.rect.height + 2 * m) }
  void clu()
    .shell.tooltip({ label: tip.label.slice(0, 200), keys: tip.keys.slice(0, 8).map((k) => k.slice(0, 16)), rect, above: tip.above, closing: tip.closing })
    .then((r) => {
      if (r.ok) return
      overlayTips = false
      overlayOwner = null
      refused()
    })
}

/** The keycaps of a command's shortcut (the user's own when they changed it), or none for a command without one. */
export function shortcutKeys(command: string | undefined, custom?: unknown): string[] {
  const keys = command ? effectiveEntry(command, custom)?.accelerator : undefined
  if (!keys) return []
  try {
    return chordKeys(keys)
  } catch {
    return []
  }
}

/** `chip` is a pill between the label and the keycaps (a space's profile, in a space's tooltip). */
export function Tooltip({ label, chip, command, children }: { label: string; chip?: string | undefined; command?: string | undefined; children: ReactElement | ReactNode }) {
  const [phase, setPhase] = useState<Phase>('closed')
  const [at, setAt] = useState<{ x: number; y: number } | null>(null)
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const anchor = useRef<HTMLSpanElement>(null)
  const tip = useRef<HTMLSpanElement>(null)
  // The shift that keeps the open pill inside the window, measured once per placement.
  const [fit, setFit] = useState<{ dx: number; dy: number } | null>(null)
  const id = useId()
  const clear = () => {
    if (timer.current) clearTimeout(timer.current)
    timer.current = null
  }
  const point = (e: PointerEvent) => {
    const r = anchor.current?.getBoundingClientRect()
    if (r && phase !== 'open') {
      setAt({ x: e.clientX - r.left + LOOK.tooltip.offset, y: e.clientY - r.top + LOOK.tooltip.offset })
      setFit(null)
    }
  }
  const show = () => {
    clear()
    if (phase !== 'open') setFit(null)
    const delay = Date.now() - lastClosedAt < 1000 ? 100 : LOOK.tooltip.delayMs
    timer.current = setTimeout(() => setPhase('open'), delay)
  }
  const hide = () => {
    clear()
    if (phase !== 'open') {
      setPhase('closed')
      return
    }
    lastClosedAt = Date.now()
    setPhase('closing')
    // The fade's own end unmounts it; the timer covers a document that never runs the animation.
    timer.current = setTimeout(() => setPhase('closed'), LOOK.tooltip.outMs + 50)
  }
  useEffect(() => clear, [])
  useLayoutEffect(() => {
    const el = tip.current
    const a = anchor.current
    if (phase !== 'open' || !el || !a) return
    const r = el.getBoundingClientRect()
    const view = el.ownerDocument.documentElement
    // Above: the pill's bottom 9.5 over the pointer, or 6 over the anchor for keyboard focus.
    const up = at ? r.height + 2 * LOOK.tooltip.offset : r.height + a.getBoundingClientRect().height + 12
    // A scrolling ancestor (the sidebar's tab list, the Site controls panel's settings) clips the pill too,
    // so it slides left and hangs above inside that as well.
    let right = view.clientWidth
    let bottom = view.clientHeight
    for (let p = a.parentElement; p && p !== view; p = p.parentElement) {
      const css = getComputedStyle(p)
      if (css.overflowX === 'visible' && css.overflowY === 'visible') continue
      const box = p.getBoundingClientRect()
      right = Math.min(right, box.right)
      bottom = Math.min(bottom, box.bottom)
      break
    }
    setFit({ dx: Math.min(0, right - EDGE - r.right), dy: r.bottom > bottom - EDGE ? -up : 0 })
  }, [phase, at])
  const custom = useCluState(selectCustom)
  const keys = shortcutKeys(command, custom)
  const [remote, setRemote] = useState(overlayTips)
  const keysKey = JSON.stringify(keys)
  // The overlay's copy follows the laid-out pill: once it fits, again as it starts to fade, gone when it closes.
  useEffect(() => {
    if (!remote) return
    const el = tip.current
    if (phase === 'closed' || !el || (phase === 'open' && !fit)) {
      sendTip(id, null, () => setRemote(false))
      return
    }
    sendTip(id, { label, keys: JSON.parse(keysKey) as string[], rect: el.getBoundingClientRect(), above: !!fit?.dy, closing: phase === 'closing' }, () => setRemote(false))
  }, [remote, phase, fit, label, keysKey, id])
  useEffect(() => () => sendTip(id, null, () => undefined), [id])
  // Keyboard focus has no pointer: the pill sits under the anchor's left edge.
  const style: CSSProperties = {
    ...(at ? { left: at.x, top: at.y } : {}),
    ...(fit && (fit.dx || fit.dy) ? { translate: `${fit.dx}px ${fit.dy}px` } : {}),
  }
  return (
    <span
      ref={anchor}
      className="clu-tooltip-anchor"
      onPointerEnter={(e) => {
        point(e)
        show()
      }}
      onPointerMove={point}
      onPointerLeave={hide}
      // Keyboard focus only: focus the window hands back after a menu or popover (the first button of the frame) shows
      // no pill.
      onFocus={(e) => {
        if (e.target instanceof Element && e.target.matches(':focus-visible')) show()
      }}
      onBlur={hide}
    >
      {children}
      {phase !== 'closed' ? (
        <span
          ref={tip}
          role="tooltip"
          id={id}
          className="clu-tooltip"
          style={style}
          data-placed={at ? 'pointer' : 'anchor'}
          data-above={fit?.dy ? true : undefined}
          data-closing={phase === 'closing' || undefined}
          data-remote={remote || undefined}
          onAnimationEnd={() => setPhase((p) => (p === 'closing' ? 'closed' : p))}
        >
          {label}
          {chip ? <span className="clu-tooltip-chip">{chip}</span> : null}
          {keys.length ? (
            <span className="clu-keycaps" aria-label={effectiveEntry(command ?? '', custom)?.accelerator}>
              {keys.map((k, i) => (
                <kbd key={i} className="clu-keycap">
                  {k}
                </kbd>
              ))}
            </span>
          ) : null}
        </span>
      ) : null}
    </span>
  )
}
