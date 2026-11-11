// Space theme editor (docs/chromium/parity-theme.md): a 356 x 508 popover beside the sidebar. A dotted
// canvas carries the Auto, Light and Dark buttons at its top, none (minus removed every color) to three color handles on a color wheel (angle is
// hue, distance how deep the color is; each handle moves on its own) and minus and plus at its bottom; below it 5 sliding pages of 9 preset swatches, the wavy intensity slider and the grain dial.
// Drags preview through theme.previewSpaceTheme once a frame (state only), so the sidebar, frame and bottom bar
// repaint while the pointer moves; pointer up, a key or a click commits through theme.setSpaceTheme. The browser puts
// back an uncommitted preview when the editor closes. Onboarding's "Pick a look" shows the same editor inline
// (ThemeEditor with `inline`), scaled to fit its card.
import { useEffect, useLayoutEffect, useMemo, useRef, useState, type CSSProperties, type KeyboardEvent, type PointerEvent as ReactPointerEvent, type ReactElement } from 'react'
import { useCluState } from '../../common/useCluState'
import { useReducedMotion } from '../../common/useReducedMotion'
import type { AppearanceMode, SpaceTheme } from '../../../shared/theme/types'
import {
  CANVAS, DIAL, HANDLE, MAX_COLORS, PICKER_SIZE, PREVIEW_MS, SLIDER, SWATCHES, SWATCH_PAGES, SWATCH_SLIDE_EASING, addColor, canvasToColor,
  colorToCanvas, dialAngle, dialValue, handleFill, moveColor, nudge, placePicker, removeColor, roundIntensity,
  sameTheme, sliderValue, sliderX, snap, step, swatchColor, swatchLabel, swatchMatches, thumbSize, throttle, waveAmplitude, wavePath, type NudgeKey
} from '../../../shared/theme/picker'
import { editorThemeFor } from '../../../shared/theme/space'
import type { Rect } from '../../../shared/types'
import { cachedGrainTile, grainTileUrl } from '../../ui/theme/grain'
import { closeInstance, type SurfaceProps } from '../core/types'
import './picker.css'
import { clu } from '../../common/clu'

const NUDGE_KEYS = new Set(['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'])
const pct = (v: number) => `${Math.round(v * 100)}%`
const sameOrNull = (a: SpaceTheme | null, b: SpaceTheme | null) => (a && b ? sameTheme(a, b) : a === b)
const HALF = CANVAS.size / 2
/** The editor shrinks toward the sidebar in 150 ms on Esc (picker.css, docs/chromium/motion.md). */
export const CLOSE_MS = 150

const isRect = (r: unknown): r is Rect =>
  !!r && typeof r === 'object' && ['x', 'y', 'width', 'height'].every((k) => Number.isFinite((r as Record<string, unknown>)[k]))

const icon = { width: 22, height: 22, viewBox: '0 0 18 18', fill: 'none', 'aria-hidden': true } as const
const AutoIcon = () => (
  <svg {...icon}>
    <path d="M7 2.5c.4 2.6 1.4 3.6 4 4-2.6.4-3.6 1.4-4 4-.4-2.6-1.4-3.6-4-4 2.6-.4 3.6-1.4 4-4Z" fill="currentColor" />
    <path className="clu-tp-spark" d="M13 9.5c.25 1.6.9 2.25 2.5 2.5-1.6.25-2.25.9-2.5 2.5-.25-1.6-.9-2.25-2.5-2.5 1.6-.25 2.25-.9 2.5-2.5Z" fill="currentColor" />
    <circle className="clu-tp-spark" cx="13.5" cy="3.5" r="1" fill="currentColor" />
  </svg>
)
const SunIcon = () => (
  <svg {...icon}>
    <circle cx="9" cy="9" r="3.2" fill="currentColor" />
    {[0, 45, 90, 135, 180, 225, 270, 315].map((a) => (
      <circle key={a} className="clu-tp-spark" cx={9 + 6.2 * Math.sin((a * Math.PI) / 180)} cy={9 - 6.2 * Math.cos((a * Math.PI) / 180)} r="1" fill="currentColor" />
    ))}
  </svg>
)
const MoonIcon = () => (
  <svg {...icon}>
    <path d="M10.4 3.4a6 6 0 1 0 4.2 9.3A5.2 5.2 0 0 1 10.4 3.4Z" fill="currentColor" />
    <path className="clu-tp-spark" d="M14 2.5l.45 1.05L15.5 4l-1.05.45L14 5.5l-.45-1.05L12.5 4l1.05-.45Z" fill="currentColor" />
  </svg>
)
const MODES: { mode: AppearanceMode; label: string; Icon: () => ReactElement }[] = [
  { mode: 'auto', label: 'Auto', Icon: AutoIcon },
  { mode: 'light', label: 'Light', Icon: SunIcon },
  { mode: 'dark', label: 'Dark', Icon: MoonIcon }
]
const MinusIcon = () => (
  <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">
    <path d="M1 8h14" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
  </svg>
)
const PlusIcon = () => (
  <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">
    <path d="M1 8h14M8 1v14" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
  </svg>
)
const Chevron = ({ dir }: { dir: 'left' | 'right' }) => (
  <svg width="10" height="18" viewBox="0 0 10 18" aria-hidden="true">
    <path d={dir === 'left' ? 'M8 2 2 9l6 7' : 'M2 2l6 7-6 7'} fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
  </svg>
)

const NO_ANCHOR: Rect = { x: 0, y: 0, width: 0, height: 0 }

export function ThemePicker({ instance, windowId }: SurfaceProps) {
  const props = (instance.props ?? {}) as { spaceId?: unknown; anchor?: unknown }
  const spaceId = typeof props.spaceId === 'string' ? props.spaceId : ''
  const anchor = isRect(props.anchor) ? props.anchor : NO_ANCHOR
  return <ThemeEditor spaceId={spaceId} windowId={windowId} anchor={anchor} onClose={() => closeInstance(instance.instanceId)} />
}

/**
 * The editor itself. As the overlay's popover it sits beside the sidebar, opens and closes with its scale and closes
 * on Esc; `inline` (onboarding) lays it out where it is placed, always shown, and leaves Esc and focus to the host.
 */
export function ThemeEditor({ spaceId, windowId, anchor = NO_ANCHOR, inline = false, onClose }: {
  spaceId: string
  windowId: string
  anchor?: Rect
  inline?: boolean
  onClose?: () => void
}) {
  // A space with no theme row opens with the handles of the default theme its sidebar already shows; the
  // canvas is empty ("Tap to pick a color for this space") only once minus removed every color.
  const stored = useCluState((s) => {
    const space = s.spaces[spaceId]
    return space ? (space.theme ?? editorThemeFor(space.color, s.app.appearance.resolved)) : null
  }, sameOrNull)
  const spaceName = useCluState((s) => s.spaces[spaceId]?.name ?? '')
  const mode = useCluState((s) => s.app.appearance.mode)
  const resolved = useCluState((s) => s.app.appearance.resolved)
  const sidebarRight = useCluState((s) => {
    const shell = s.windows[windowId]?.shell
    if (!shell || shell.sidebar.hidden) return 0
    const r = shell.layout.sidebarRect
    return r.width > 0 ? r.x + r.width : shell.sidebar.width
  })
  const reduced = useReducedMotion()

  const [draft, setDraft] = useState<SpaceTheme | null>(stored)
  const [page, setPage] = useState(0)
  const [closingNow, setClosingNow] = useState(false)
  /** Set once, after the first style pass, so the opening transition runs exactly once per open. */
  const [shown, setShown] = useState(false)
  /** The mode a click picked: only that click plays its icon's turn, never an echo or the editor opening. */
  const [pickedMode, setPickedMode] = useState<AppearanceMode | null>(null)
  /** A tap, a swatch, minus or plus slides the handles to their new places; a drag moves them under the pointer. */
  const [glide, setGlide] = useState(false)
  const draftRef = useRef(draft)
  draftRef.current = draft
  /** True while a pointer drags a handle, the slider or the dial: state echoes of the editor's own previews are ignored. */
  const dragging = useRef(false)
  /** True once the current gesture sent a preview; a press and release with no change (choosing a handle) writes nothing. */
  const previewed = useRef(false)
  const canvasRef = useRef<HTMLDivElement>(null)
  const sliderRef = useRef<HTMLDivElement>(null)
  const dialRef = useRef<HTMLDivElement>(null)
  const handleRefs = useRef<(HTMLButtonElement | null)[]>([])
  /** The handle the current drag moves; the others stay where they are. */
  const dragIndex = useRef(0)
  /** True once the editor is shown: a main handle created after that (the first tap on an empty canvas) plays its entry. */
  const live = useRef(false)
  const seenHandles = useRef(new WeakSet<HTMLElement>())
  /** True once the canvas was empty: the hint stays mounted so it can fade out under the first tap. */
  const [hintMounted, setHintMounted] = useState(false)

  const previewer = useMemo(() => throttle((t: SpaceTheme) => void clu().theme.previewSpaceTheme({ spaceId, theme: t }), PREVIEW_MS), [spaceId])
  useEffect(() => () => previewer.cancel(), [previewer])
  /** Set on Esc: a pointer released after it neither previews nor commits. */
  const closing = useRef(false)
  const onCloseRef = useRef(onClose)
  onCloseRef.current = onClose
  const close = () => {
    if (closing.current || inline) return
    closing.current = true
    previewer.cancel()
    setClosingNow(true)
  }
  // Esc shrinks the editor toward the sidebar over 150 ms before it goes.
  useEffect(() => {
    if (!closingNow) return
    const timer = setTimeout(() => onCloseRef.current?.(), reduced ? 0 : CLOSE_MS)
    return () => clearTimeout(timer)
  }, [closingNow, reduced])

  useEffect(() => {
    if (!stored) onCloseRef.current?.()
    else if (!dragging.current) setDraft(stored)
  }, [stored])

  const count = draft?.colors.length ?? 0
  const emptyCanvas = draft !== null && count === 0
  useEffect(() => {
    if (emptyCanvas) setHintMounted(true)
  }, [emptyCanvas])

  const viewport = () => ({ width: window.innerWidth, height: window.innerHeight })
  const [pos, setPos] = useState(() => placePicker(anchor, viewport(), sidebarRight))
  useLayoutEffect(() => {
    setPos(placePicker(anchor, viewport(), sidebarRight))
  }, [anchor.x, anchor.y, anchor.width, anchor.height, sidebarRight]) // eslint-disable-line react-hooks/exhaustive-deps

  const rootRef = useRef<HTMLDivElement>(null)
  // The first style pass computes the closed state (scale 0.1, transparent); flipping data-shown after it starts the
  // opening transition from there before the first paint.
  const hasDraft = draft !== null
  useLayoutEffect(() => {
    if (!hasDraft || !rootRef.current) return
    rootRef.current.getBoundingClientRect()
    live.current = true
    setShown(true)
  }, [hasDraft])
  // Keys go to the main handle, or to the editor itself while the canvas is empty, so Esc always closes it.
  useEffect(() => {
    if (!inline) (handleRefs.current[0] ?? rootRef.current)?.focus({ preventScroll: true })
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  // The knob shows the grain itself; the tile is drawn once and shared with the frame's layer.
  const tile = (draft?.grain ?? 0) > 0 ? grainTileUrl() : cachedGrainTile()

  if (!draft) return null

  const preview = (t: SpaceTheme) => {
    if (closing.current) return
    previewed.current = true
    setDraft(t)
    previewer.call(t)
  }
  const commit = (t: SpaceTheme) => {
    if (closing.current) return
    previewer.cancel()
    dragging.current = false
    previewed.current = false
    setDraft(t)
    void clu().theme.setSpaceTheme({ spaceId, theme: t })
  }
  /** Pointer up: commits what the gesture previewed; nothing changed means nothing to write. */
  const endGesture = () => {
    if (previewed.current && draftRef.current) commit(draftRef.current)
    else dragging.current = false
  }
  const capture = (e: ReactPointerEvent<HTMLElement>): boolean => {
    if (e.button !== 0) return false
    e.preventDefault()
    e.stopPropagation()
    e.currentTarget.setPointerCapture(e.pointerId)
    dragging.current = true
    return true
  }
  const captured = (e: ReactPointerEvent<HTMLElement>) => dragging.current && e.currentTarget.hasPointerCapture(e.pointerId)
  const release = (e: ReactPointerEvent<HTMLElement>) => {
    if (!e.currentTarget.hasPointerCapture(e.pointerId)) return
    e.currentTarget.releasePointerCapture(e.pointerId)
    endGesture()
  }

  const colorAt = (e: { clientX: number; clientY: number }) => {
    const r = canvasRef.current?.getBoundingClientRect()
    if (!r) return null
    // Onboarding shows the editor scaled down: pointer offsets go back to canvas points before they become a color.
    const k = r.width > 0 ? CANVAS.size / r.width : 1
    return canvasToColor({ x: (e.clientX - (r.left + r.width / 2)) * k, y: (e.clientY - (r.top + r.height / 2)) * k })
  }
  // A press on the bare canvas moves the main handle there; a press on a handle grabs that one. Either way the drag
  // moves only that handle.
  const startHandle = (e: ReactPointerEvent<HTMLElement>, index: number) => {
    if (!capture(e)) return
    dragIndex.current = index
    if (e.currentTarget === canvasRef.current) {
      const c = colorAt(e)
      setGlide(true)
      const current = draftRef.current ?? draft
      // The first tap on an empty canvas creates the main handle there.
      if (c) preview(current.colors.length ? moveColor(current, 0, c) : { ...current, colors: [c] })
    }
    requestAnimationFrame(() => handleRefs.current[index]?.focus({ preventScroll: true, focusVisible: false }))
  }
  const moveHandle = (e: ReactPointerEvent<HTMLElement>) => {
    if (!captured(e)) return
    setGlide(false)
    const c = colorAt(e)
    if (c) preview(moveColor(draftRef.current ?? draft, dragIndex.current, c))
  }

  const onHandleKey = (e: KeyboardEvent, index: number) => {
    if (!NUDGE_KEYS.has(e.key)) return
    e.preventDefault()
    const c = draft.colors[index]
    if (c) commit(moveColor(draft, index, nudge(c, e.key as NudgeKey, e.shiftKey ? 5 : 1)))
  }

  const onSlider = (e: ReactPointerEvent<HTMLDivElement>, phase: 'down' | 'move') => {
    if (phase === 'down' ? !capture(e) : !captured(e)) return
    // Keys reach it after a press; a pointer press shows no focus ring.
    if (phase === 'down') e.currentTarget.focus({ preventScroll: true, focusVisible: false })
    const r = sliderRef.current?.getBoundingClientRect()
    if (!r) return
    const intensity = sliderValue(((e.clientX - r.left) / (r.width || 1)) * SLIDER.width)
    if (intensity !== draftRef.current?.intensity) preview({ ...(draftRef.current ?? draft), intensity })
  }
  const onDial = (e: ReactPointerEvent<HTMLDivElement>, phase: 'down' | 'move') => {
    if (phase === 'down' ? !capture(e) : !captured(e)) return
    if (phase === 'down') e.currentTarget.focus({ preventScroll: true, focusVisible: false })
    const r = dialRef.current?.getBoundingClientRect()
    if (!r) return
    const prev = draftRef.current?.grain
    const grain = dialValue(e.clientX - (r.left + r.width / 2), e.clientY - (r.top + r.height / 2), prev)
    if (grain !== prev) preview({ ...(draftRef.current ?? draft), grain })
  }
  const onStepKey = (e: KeyboardEvent, field: 'grain' | 'intensity') => {
    const v = draft[field]
    const next =
      e.key === 'ArrowRight' || e.key === 'ArrowUp' ? step(v, 1) : e.key === 'ArrowLeft' || e.key === 'ArrowDown' ? step(v, -1)
        : e.key === 'PageUp' ? step(v, 1, 2) : e.key === 'PageDown' ? step(v, -1, 2) : e.key === 'Home' ? 0 : e.key === 'End' ? 1 : null
    if (next === null) return
    e.preventDefault()
    commit({ ...draft, [field]: field === 'intensity' ? roundIntensity(next) : snap(next) })
  }

  const swatches = SWATCH_PAGES[page] ?? SWATCH_PAGES[0]
  const pages = SWATCH_PAGES.length
  const angle = dialAngle(draft.grain)
  const thumbX = sliderX(draft.intensity)
  const thumb = thumbSize(draft.intensity)
  const wave = wavePath(waveAmplitude(draft.intensity))
  const main = draft.colors[0]
  const picked = count === 1 && main ? (hex: string) => swatchMatches(hex, main) : () => false
  const rad = (a: number) => (a * Math.PI) / 180

  return (
    <div
      ref={rootRef}
      tabIndex={-1}
      className="clu-theme-picker"
      // Inline it is one part of onboarding's own dialog.
      role={inline ? 'group' : 'dialog'}
      aria-label={spaceName ? `Theme for ${spaceName}` : 'Theme'}
      data-surface="theme-picker"
      data-colors={count}
      data-shown={shown ? '' : undefined}
      data-closing={closingNow ? '' : undefined}
      data-inline={inline ? '' : undefined}
      // The editor is neutral gray glass in every theme, light in light mode (#5f5f5f in dark, #f3eae8 in light).
      data-resolved={resolved}
      style={inline ? { width: PICKER_SIZE.width, height: PICKER_SIZE.height } : { left: pos.x, top: pos.y, width: PICKER_SIZE.width, height: PICKER_SIZE.height }}
      onPointerDown={(e) => e.stopPropagation()}
      onKeyDown={(e) => {
        if (e.key !== 'Escape') return
        e.preventDefault()
        close()
      }}
    >
      <div
        ref={canvasRef}
        className="clu-tp-canvas"
        data-testid="theme-field"
        data-glide={glide && !reduced ? '' : undefined}
        style={{ left: CANVAS.x, top: CANVAS.y, width: CANVAS.size, height: CANVAS.size }}
        onPointerDown={(e) => startHandle(e, 0)}
        onPointerMove={moveHandle}
        onPointerUp={release}
        onPointerCancel={release}
      >
        <div className="clu-tp-modes" role="radiogroup" aria-label="Appearance" onPointerDown={(e) => e.stopPropagation()}>
          {MODES.map((m, i) => (
            <button
              key={m.mode}
              type="button"
              role="radio"
              aria-checked={mode === m.mode}
              aria-label={m.label}
              tabIndex={mode === m.mode ? 0 : -1}
              className="clu-tp-mode"
              data-mode={m.mode}
              data-picked={pickedMode === m.mode && mode === m.mode ? '' : undefined}
              onClick={() => {
                if (mode !== m.mode) setPickedMode(m.mode)
                void clu().theme.setMode({ mode: m.mode })
              }}
              onKeyDown={(e) => {
                const d = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0
                if (!d) return
                e.preventDefault()
                const next = MODES[(i + d + MODES.length) % MODES.length] as (typeof MODES)[number]
                setPickedMode(next.mode)
                void clu().theme.setMode({ mode: next.mode })
                ;(e.currentTarget.parentElement?.querySelector(`[data-mode="${next.mode}"]`) as HTMLElement | null)?.focus()
              }}
            >
              <m.Icon />
            </button>
          ))}
        </div>

        {draft.colors.map((c, i) => {
          const p = colorToCanvas(c)
          const size = count === 1 ? HANDLE.single : i === 0 ? HANDLE.primary : HANDLE.secondary
          return (
            <button
              key={i}
              ref={(el) => {
                handleRefs.current[i] = el
                // Set once on the new node, outside React, so no later render restarts it.
                if (el && !seenHandles.current.has(el)) {
                  seenHandles.current.add(el)
                  if (live.current && i === 0 && count === 1) el.setAttribute('data-enter', '')
                }
              }}
              type="button"
              className="clu-tp-handle"
              data-index={i}
              data-main={i === 0}
              aria-label={`${i === 0 ? 'Main color' : `Color ${i + 1}`}: hue ${Math.round(c.h)} degrees, depth ${pct(c.s)}`}
              style={{
                // Placed by transform so a glide runs on the compositor.
                left: HALF,
                top: HALF,
                transform: `translate(${p.x}px, ${p.y}px)`,
                width: size,
                height: size,
                margin: -size / 2,
                borderWidth: i === 0 ? HANDLE.primaryRing : HANDLE.secondaryRing,
                background: handleFill(c),
                zIndex: i === 0 ? 3 : 2
              }}
              onPointerDown={(e) => startHandle(e, i)}
              onPointerMove={moveHandle}
              onPointerUp={release}
              onPointerCancel={release}
              onKeyDown={(e) => onHandleKey(e, i)}
            />
          )
        })}

        {count === 0 || hintMounted ? (
          <p className="clu-tp-hint" data-hidden={count > 0 ? '' : undefined} aria-hidden={count > 0}>
            Tap to pick a color for this space
          </p>
        ) : null}

        <div className="clu-tp-count" onPointerDown={(e) => e.stopPropagation()}>
          <button
            type="button"
            className="clu-tp-glyph"
            aria-label="Remove color"
            disabled={count <= 1}
            onClick={() => {
              // Removing or adding a color takes one frame: no glide.
              setGlide(false)
              commit(removeColor(draft))
              // Focus moves to the main handle, so a button that turns disabled under focus does not keep focus.
              requestAnimationFrame(() => handleRefs.current[0]?.focus({ preventScroll: true }))
            }}
          >
            <MinusIcon />
          </button>
          <button
            type="button"
            className="clu-tp-glyph"
            aria-label="Add color"
            disabled={count === 0 || count >= MAX_COLORS}
            onClick={() => {
              setGlide(false)
              commit(addColor(draft))
              if (count + 1 >= MAX_COLORS) requestAnimationFrame(() => handleRefs.current[0]?.focus({ preventScroll: true }))
            }}
          >
            <PlusIcon />
          </button>
        </div>
      </div>

      <div className="clu-tp-presets" role="group" aria-label={`Presets, ${swatches?.name ?? ''}`} data-page={page}>
        <button type="button" className="clu-tp-glyph clu-tp-arrow" aria-label="Previous presets" disabled={page === 0} onClick={() => setPage((p) => Math.max(0, p - 1))}>
          <Chevron dir="left" />
        </button>
        <div className="clu-tp-swatches">
          <div className="clu-tp-strip" style={{ transform: `translateX(${-page * 9 * SWATCHES.pitch}px)`, transitionDuration: reduced ? '0ms' : `${SWATCHES.slideMs}ms`, transitionTimingFunction: SWATCH_SLIDE_EASING }}>
            {SWATCH_PAGES.map((p, pi) =>
              p.colors.map((hex, i) => (
                <button
                  key={`${p.name}-${i}`}
                  type="button"
                  className="clu-tp-swatch"
                  aria-label={swatchLabel(pi, i)}
                  aria-pressed={picked(hex)}
                  aria-hidden={pi !== page}
                  tabIndex={pi === page ? 0 : -1}
                  data-swatch={hex}
                  data-page={pi}
                  style={{ background: hex }}
                  // A swatch replaces the theme with that one color.
                  onClick={() => {
                    setGlide(true)
                    commit({ ...draft, colors: [swatchColor(hex)] })
                  }}
                />
              ))
            )}
          </div>
        </div>
        <button type="button" className="clu-tp-glyph clu-tp-arrow" aria-label="Next presets" disabled={page >= pages - 1} onClick={() => setPage((p) => Math.min(pages - 1, p + 1))}>
          <Chevron dir="right" />
        </button>
      </div>

      <div
        ref={sliderRef}
        className="clu-tp-slider"
        role="slider"
        tabIndex={0}
        aria-label="Intensity"
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={draft.intensity}
        aria-valuetext={pct(draft.intensity)}
        style={{ left: SLIDER.x, top: SLIDER.centerY - SLIDER.height / 2, width: SLIDER.width, height: SLIDER.height }}
        onPointerDown={(e) => onSlider(e, 'down')}
        onPointerMove={(e) => onSlider(e, 'move')}
        onPointerUp={release}
        onPointerCancel={release}
        onKeyDown={(e) => onStepKey(e, 'intensity')}
      >
        <svg className="clu-tp-wave" width={SLIDER.width} height={SLIDER.height} viewBox={`0 0 ${SLIDER.width} ${SLIDER.height}`} aria-hidden="true">
          <defs>
            <clipPath id="clu-tp-wave-on">
              <rect x="-8" y="-12" width={thumbX + 8} height={SLIDER.height + 24} />
            </clipPath>
            <clipPath id="clu-tp-wave-off">
              <rect x={thumbX} y="-12" width={SLIDER.width - thumbX + 8} height={SLIDER.height + 24} />
            </clipPath>
          </defs>
          <path d={wave} className="clu-tp-wave-off" strokeWidth={SLIDER.stroke} clipPath="url(#clu-tp-wave-off)" />
          <path d={wave} className="clu-tp-wave-on" strokeWidth={SLIDER.stroke} clipPath="url(#clu-tp-wave-on)" />
        </svg>
        <span className="clu-tp-thumb" style={{ left: thumbX - thumb.width / 2, width: thumb.width, height: thumb.height, top: (SLIDER.height - thumb.height) / 2 }} />
      </div>

      <div
        ref={dialRef}
        className="clu-tp-dial"
        role="slider"
        tabIndex={0}
        aria-label="Grain"
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={draft.grain}
        aria-valuetext={pct(draft.grain)}
        style={{ left: DIAL.centerX - (DIAL.pinTo + 2), top: DIAL.centerY - (DIAL.pinTo + 2), width: 2 * (DIAL.pinTo + 2), height: 2 * (DIAL.pinTo + 2) }}
        onPointerDown={(e) => onDial(e, 'down')}
        onPointerMove={(e) => onDial(e, 'move')}
        onPointerUp={release}
        onPointerCancel={release}
        onKeyDown={(e) => onStepKey(e, 'grain')}
      >
        <span
          className="clu-tp-knob"
          style={{
            width: DIAL.knob,
            height: DIAL.knob,
            ...(tile ? { '--clu-tp-grain': `url(${tile})`, '--clu-tp-grain-opacity': String(Math.round((0.08 + 0.5 * draft.grain) * 1000) / 1000) } : {})
          } as CSSProperties}
        />
        <svg className="clu-tp-ring" width={2 * (DIAL.pinTo + 2)} height={2 * (DIAL.pinTo + 2)} aria-hidden="true">
          {Array.from({ length: DIAL.dots }, (_, i) => {
            const a = (i * 360) / DIAL.dots
            // Dots before the pin are lit; the one under it (the top one at a full turn) is covered by the pin.
            const on = a < angle - 1e-6 && !(a === 0 && angle >= 360 - 1e-6)
            const c = DIAL.pinTo + 2
            return <circle key={i} className={on ? 'clu-tp-dot-on' : 'clu-tp-dot-off'} data-on={on} cx={c + DIAL.ring * Math.sin(rad(a))} cy={c - DIAL.ring * Math.cos(rad(a))} r={(on ? DIAL.dotOn : DIAL.dotOff) / 2} />
          })}
          <line
            className="clu-tp-pin"
            x1={DIAL.pinTo + 2}
            y1={DIAL.pinTo + 2 - DIAL.pinFrom - DIAL.pinWidth / 2}
            x2={DIAL.pinTo + 2}
            y2={2 + DIAL.pinWidth / 2}
            strokeWidth={DIAL.pinWidth}
            transform={`rotate(${angle} ${DIAL.pinTo + 2} ${DIAL.pinTo + 2})`}
          />
        </svg>
      </div>
    </div>
  )
}
