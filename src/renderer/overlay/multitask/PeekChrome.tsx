// Peek chrome in the under-peek overlay view (05 Peek): the dimmed scrim over the page card shrunk to 0.983, the card frame under the
// Peek page and the button rail just outside its top-right corner (the host shows in the sidebar's URL pill, M11). The
// card unfolds from a band at the click point and reports peek.animationDone so main attaches the page; the rail shows
// 108 ms after the card starts. On close the card shrinks main's capture of the page back toward the click point. A scrim click closes the Peek. The rail holds Close, Open as tab and
// Open in split; the Peek page's own keys go back and reload it.
import { useLayoutEffect, useRef, type ReactNode } from 'react'
import { LOOK } from '../../../shared/look/tokens'
import type { PeekSurfaceProps, PeekClosingFrame } from '../../../shared/multitask'
import type { Rect } from '../../../shared/types'
import { IconButton } from '../../common/IconButton'
import { CloseIcon } from '../../common/icons'
import { useCluState } from '../../common/useCluState'
import { useReducedMotion } from '../../common/useReducedMotion'
import { ThemeBackground } from '../../ui/theme/ThemeBackground'
import type { SurfaceProps } from '../core/types'
import { CLOSE_MS, closeMotion, OPEN_EASE, OPEN_MS, openMotion, play, RAIL_DELAY_MS, RAIL_FADE_MS, tokens } from './peek-motion'
import { clu } from '../../common/clu'

// The rail: three 34 buttons on a 43 pitch, 13 right of the card, the first centered 24.25 below its top.
const RAIL_BUTTON = LOOK.peek.buttonSize
const CARD_GAP = LOOK.peek.buttonGap
const RAIL_GAP = LOOK.peek.buttonPitch - LOOK.peek.buttonSize
const RAIL_TOP = LOOK.peek.firstButtonCenterY - LOOK.peek.buttonSize / 2
/** A hover preview's summary strip at the card's top; the browser lays the page below it (CluPeek::kSummaryStrip). */
export const PEEK_SUMMARY_STRIP = 76

/** The on-device model's two or three sentences about a hover-previewed page, above the page in the card. */
function Summary({ text }: { text: string }) {
  return (
    <div
      data-peek-summary
      role="note"
      aria-label="Summary"
      style={{
        position: 'absolute',
        inset: `0 0 auto 0`,
        height: PEEK_SUMMARY_STRIP,
        boxSizing: 'border-box',
        display: 'flex',
        gap: 10,
        padding: '12px 16px',
        borderBottom: '1px solid var(--divider)',
        color: 'var(--label)',
        fontSize: 13,
        lineHeight: '17px',
        overflow: 'hidden'
      }}
    >
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="var(--accent-strong)" strokeWidth="1.4" strokeLinejoin="round" aria-hidden="true" style={{ flex: 'none', marginTop: 1 }}>
        <path d="M8 1.5 9.4 6.6 14.5 8 9.4 9.4 8 14.5 6.6 9.4 1.5 8 6.6 6.6Z" />
      </svg>
      <span style={{ display: '-webkit-box', WebkitLineClamp: 3, WebkitBoxOrient: 'vertical', overflow: 'hidden' }}>{text}</span>
    </div>
  )
}

function cardStyle(card: Rect, origin: string): React.CSSProperties {
  return {
    position: 'fixed',
    left: card.x,
    top: card.y,
    width: card.width,
    height: card.height,
    transformOrigin: origin,
    borderRadius: 'var(--peek-r)',
    background: 'var(--page-bg)',
    boxShadow: 'var(--shadow-peek)'
  }
}

/** A rounded rect as a path, clockwise from the top edge. */
function roundedRect({ x, y, width: w, height: h }: Rect, r: number): string {
  const n = (v: number) => +v.toFixed(2)
  return `M${n(x + r)} ${n(y)}H${n(x + w - r)}A${r} ${r} 0 0 1 ${n(x + w)} ${n(y + r)}V${n(y + h - r)}A${r} ${r} 0 0 1 ${n(x + w - r)} ${n(y + h)}H${n(x + r)}A${r} ${r} 0 0 1 ${n(x)} ${n(y + h - r)}V${n(y + r)}A${r} ${r} 0 0 1 ${n(x + r)} ${n(y)}Z`
}

/** The page card shrunk to LOOK.peek.backdropScale around its center (x 223 to 1261, y 19 to 781 in the design). */
export function backdropCard(page: Rect): Rect {
  const scale = LOOK.peek.backdropScale
  const width = page.width * scale
  const height = page.height * scale
  return { x: page.x + (page.width - width) / 2, y: page.y + (page.height - height) / 2, width, height }
}

/**
 * The backdrop: the page card shrinks to 0.983 around its center and is washed with the window's tint (the sidebar
 * stays as it is); a press on it closes. The page is a native view under this one, so the shrink is the window's own
 * background drawn over the ring between the card and its shrunk rect, the frame's ThemeBackground clipped to it.
 */
function Scrim({ windowId, onClose, scrimRef }: { windowId: string; onClose?: () => void; scrimRef: React.RefObject<HTMLDivElement | null> }) {
  const page = useCluState((s) => s.windows[windowId]?.shell.layout.pageRect ?? null)
  const shrunk = page ? backdropCard(page) : null
  const r = LOOK.card.radius
  return (
    <div ref={scrimRef} data-peek-scrim aria-hidden="true" style={{ position: 'fixed', inset: 0 }} onPointerDown={onClose}>
      {page && shrunk ? (
        <>
          <div data-peek-ring className="*:absolute *:inset-0" style={{ position: 'fixed', inset: 0, clipPath: `path(evenodd, '${roundedRect(page, r)}${roundedRect(shrunk, r)}')` }}>
            <ThemeBackground />
          </div>
          <div
            data-peek-wash
            style={{ position: 'fixed', left: shrunk.x, top: shrunk.y, width: shrunk.width, height: shrunk.height, borderRadius: 'var(--card-r)', background: 'color-mix(in srgb, var(--tint) var(--peek-wash), transparent)' }}
          />
        </>
      ) : null}
    </div>
  )
}

function RailButton({ label, onClick, disabled, children }: { label: string; onClick: () => void; disabled?: boolean; children: ReactNode }) {
  return (
    <IconButton
      aria-label={label}
      disabled={disabled}
      onPointerDown={(e) => e.stopPropagation()}
      onClick={onClick}
      className="rounded-full! transition-transform active:scale-[var(--press-scale)]"
      style={{ width: RAIL_BUTTON, height: RAIL_BUTTON, background: 'var(--peek-button-bg)', color: 'var(--accent-strong)', boxShadow: 'var(--shadow-peek-button)' }}
    >
      {children}
    </IconButton>
  )
}

function Rail({ card, railRef }: { card: Rect; railRef: React.RefObject<HTMLDivElement | null> }) {
  // Outside the card (the Peek view takes input over its whole rect), kept inside the window on narrow layouts.
  const left = Math.min(card.x + card.width + CARD_GAP, window.innerWidth - RAIL_BUTTON - 4)
  return (
    <div
      ref={railRef}
      role="toolbar"
      aria-label="Peek"
      aria-orientation="vertical"
      data-peek-rail
      className="fixed flex flex-col [&_.clu-tooltip]:top-0! [&_.clu-tooltip]:left-[calc(100%+6px)]! [&_.clu-tooltip]:transform-none!"
      style={{ left, top: card.y + RAIL_TOP, gap: RAIL_GAP }}
    >
      <RailButton label="Close Peek" onClick={() => void clu().peek.close({})}>
        <CloseIcon width={14} height={14} />
      </RailButton>
      <RailButton label="Open as tab" onClick={() => void clu().peek.expand({})}>
        {/* Two arrows out to opposite corners, 14.5 across (the expand glyph). */}
        <svg width="15" height="15" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
          <path d="M9.5 2.25h4.25V6.5M13.75 2.25 9.25 6.75M6.5 13.75H2.25V9.5M2.25 13.75l4.5-4.5" />
        </svg>
      </RailButton>
      <RailButton label="Open in split" onClick={() => void clu().peek.toSplit({})}>
        <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinejoin="round" aria-hidden="true">
          <rect x="2.5" y="3" width="11" height="10" rx="2" />
          <path d="M8 3v10" />
        </svg>
      </RailButton>
    </div>
  )
}

function OpenPeek({ windowId, seq, originRect, cardRect, summary }: { windowId: string; seq: number; originRect: Rect; cardRect: Rect; summary: string | null }) {
  const reduced = useReducedMotion()
  const scrim = useRef<HTMLDivElement>(null)
  const card = useRef<HTMLDivElement>(null)
  const rail = useRef<HTMLDivElement>(null)
  const motion = openMotion(originRect, cardRect)

  // The scrim fades in once per Peek; a replacement Peek keeps it dimmed.
  useLayoutEffect(() => {
    const { base, ease } = tokens()
    const anim = play(scrim.current, [{ opacity: 0 }, { opacity: 1 }], { duration: base, easing: ease })
    return anim.cancel
  }, [])

  // Every seq (a new Peek or a replacement) replays the card from its own origin, then main attaches the page.
  useLayoutEffect(() => {
    const anim = play(card.current, motion.keyframes, { duration: reduced ? 0 : OPEN_MS, easing: OPEN_EASE, fill: 'both' })
    const buttons = play(rail.current, [{ opacity: 0 }, { opacity: 1 }], { delay: reduced ? 0 : RAIL_DELAY_MS, duration: reduced ? 0 : RAIL_FADE_MS, fill: 'backwards' })
    let live = true
    void anim.done.then((finished) => {
      if (live && finished) void clu().peek.animationDone({ seq })
    })
    return () => {
      live = false
      anim.cancel()
      buttons.cancel()
    }
    // The animation belongs to one seq; rect changes after it (window resize) just move the card.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [seq])

  return (
    <>
      <Scrim windowId={windowId} scrimRef={scrim} onClose={() => void clu().peek.close({})} />
      <div ref={card} data-peek-card data-seq={seq} style={{ ...cardStyle(cardRect, motion.transformOrigin), overflow: 'hidden' }}>
        {summary ? <Summary text={summary} /> : null}
      </div>
      <Rail card={cardRect} railRef={rail} />
    </>
  )
}

function ClosingPeek({ windowId, frame }: { windowId: string; frame: PeekClosingFrame }) {
  const reduced = useReducedMotion()
  const scrim = useRef<HTMLDivElement>(null)
  const card = useRef<HTMLDivElement>(null)
  const motion = closeMotion(frame.originRect, frame.cardRect)

  // Until main has the capture the Peek page is still on screen, so the frame holds still under it.
  useLayoutEffect(() => {
    if (!frame.shrinking) return
    const { ease } = tokens()
    const duration = reduced ? 0 : CLOSE_MS
    const fade = play(scrim.current, [{ opacity: 1 }, { opacity: 0 }], { duration, easing: ease, fill: 'forwards' })
    const anim = play(card.current, motion.keyframes, { duration, easing: ease, fill: 'forwards' })
    let live = true
    void anim.done.then((finished) => {
      if (live && finished) void clu().peek.animationDone({ seq: frame.seq })
    })
    return () => {
      live = false
      anim.cancel()
      fade.cancel()
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [frame.seq, frame.shrinking])

  return (
    <>
      <Scrim windowId={windowId} scrimRef={scrim} />
      <div ref={card} data-peek-card data-peek-closing data-seq={frame.seq} style={{ ...cardStyle(frame.cardRect, motion.transformOrigin), overflow: 'hidden' }}>
        {frame.image ? <img data-peek-snapshot src={frame.image} alt="" draggable={false} style={{ display: 'block', width: '100%', height: '100%' }} /> : null}
      </div>
    </>
  )
}

export function PeekChrome({ windowId, instance }: SurfaceProps) {
  const peek = useCluState((s) => s.windows[windowId]?.peek ?? null)
  const closing = (instance.props as PeekSurfaceProps | null)?.closing ?? null
  if (closing) return <ClosingPeek windowId={windowId} frame={closing} />
  if (!peek) return null
  return <OpenPeek windowId={windowId} seq={peek.seq} originRect={peek.originRect} cardRect={peek.cardRect} summary={peek.summary ?? null} />
}
