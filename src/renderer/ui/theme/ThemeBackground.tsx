// Frame background slot (PLAN decision 20): the active space's theme (flat fill or gradient, grain),
// opaque (M11). A space switch cross-fades it over 120 ms (none with reduced motion); during
// a sidebar swipe it follows the frame's swipe blend (swipeBlend.ts) toward the target, or windows[id].swipe without one. Edits to the shown space's theme (the
// picker's live preview) apply at once. The main color (first stop) is published as --tint (the empty page, tooltips);
// the color the sidebar sits on picks the frame's chrome set (useTintChrome: a pastel is light chrome in dark
// mode, a dark color dark chrome in light mode) and, with light chrome, a near-black in its hue as --ink.
import { useEffect, useLayoutEffect, useRef, type CSSProperties } from 'react'
import { stateMirror } from '../../common/store'
import { useCluState, useWindowId } from '../../common/useCluState'
import { useReducedMotion } from '../../common/useReducedMotion'
import { chromeForTint, inkForTint } from '../../../shared/look/derive'
import type { ResolvedMode } from '../../../shared/theme/appearance'
import { glyphOnTint, hexToRgb, rgbToHex } from '../../../shared/theme/space'
import type { CluState } from '../../../shared/state'
import { CARD_GRAIN, grainAlpha, grainTileUrl } from './grain'
import { CROSS_FADE_MS, frameLayer, mainTint, sameLayer, sidebarTint, strongAccent, type LayerInput, type LayerSpace } from './layer'
import { getSwipeBlend, subscribeSwipeBlend, useSwipeTarget, type SwipeBlend } from './swipeBlend'
import './theme.css'

const FADED = ['--clu-theme-c0', '--clu-theme-c1', '--clu-theme-c2', 'opacity']

function spaceOf(s: CluState, id: string | null | undefined): LayerSpace | undefined {
  const space = id ? s.spaces[id] : undefined
  return space ? { theme: space.theme, color: space.color } : undefined
}

/**
 * The swipe the frame's layer blends toward: the sidebar drag's own blend (the drag never round-trips to the
 * browser), else the browser's windows[id].swipe. A blend whose target is already the active space (the switch landed)
 * is over, so the switch's cross-fade finishes it.
 */
export function layerSwipe(s: CluState, windowId: string, blend: SwipeBlend | null): LayerInput['swipe'] {
  const w = s.windows[windowId]
  if (blend && blend.targetSpaceId !== w?.activeSpaceId) return { target: spaceOf(s, blend.targetSpaceId), fraction: blend.fraction }
  if (blend) return null
  return w?.swipe ? { target: spaceOf(s, w.swipe.targetSpaceId), fraction: w.swipe.fraction } : null
}

/** The chrome set the frame wears for the active space's tint; the frame's App applies it as data-theme. */
export function useTintChrome(): ResolvedMode {
  const windowId = useWindowId() ?? ''
  return useCluState((s) => {
    const resolved = s.app.appearance.resolved
    const w = s.windows[windowId]
    const active = spaceOf(s, w?.activeSpaceId)
    const layer = frameLayer({ active, swipe: null, resolved, reduceTransparency: s.app.appearance.reduceTransparency, focused: w?.focused ?? false })
    const rgb = hexToRgb(sidebarTint(layer.stops, active?.theme?.colors.length ?? 0))
    return rgb ? chromeForTint(rgb, resolved) : resolved
  })
}

function layerOf(s: CluState, windowId: string, blend: SwipeBlend | null) {
  const w = s.windows[windowId]
  return frameLayer({
    active: spaceOf(s, w?.activeSpaceId),
    swipe: layerSwipe(s, windowId, blend),
    resolved: s.app.appearance.resolved,
    reduceTransparency: s.app.appearance.reduceTransparency,
    focused: w?.focused ?? false
  })
}

export function ThemeBackground() {
  const windowId = useWindowId() ?? ''
  const reduced = useReducedMotion()
  // Renders when the drag's target changes; each drag frame writes the blend to the style below without a render.
  useSwipeTarget()
  const blend = getSwipeBlend()
  const layer = useCluState((s) => layerOf(s, windowId, blend), sameLayer)
  const swiping = useCluState((s) => !!layerSwipe(s, windowId, blend))
  // A swipe toward another space keeps the diagonal; three colors at rest take the corner layout.
  const colors = useCluState((s) => {
    const w = s.windows[windowId]
    return layerSwipe(s, windowId, blend) ? 0 : (s.spaces[w?.activeSpaceId ?? '']?.theme?.colors.length ?? 0)
  })
  const spaceId = useCluState((s) => s.windows[windowId]?.activeSpaceId ?? null)
  // The cross-fade runs for a space switch only; the render that sees the new space starts it.
  const shown = useRef(spaceId)
  const fadeUntil = useRef(0)
  if (shown.current !== spaceId) {
    shown.current = spaceId
    fadeUntil.current = performance.now() + CROSS_FADE_MS
  }
  const accent = useCluState((s) => {
    const spaceId = s.windows[windowId]?.activeSpaceId
    return spaceId ? s.spaces[spaceId]?.accent : undefined
  })

  const tint = mainTint(layer)
  const sidebar = sidebarTint(layer.stops, colors)
  const light = useCluState((s) => s.app.appearance.resolved === 'light')
  useEffect(() => {
    const root = document.documentElement.style
    if (accent) root.setProperty('--accent', accent)
    const strong = accent ? strongAccent(accent, light) : null
    if (strong) root.setProperty('--accent-strong', strong)
    else root.removeProperty('--accent-strong')
  }, [accent, light])
  useEffect(() => {
    const root = document.documentElement.style
    if (hexToRgb(tint)) root.setProperty('--tint', tint)
    const rgb = hexToRgb(sidebar)
    if (!rgb) return
    // The chrome follows the color, not macOS: a pastel sidebar gets dark text in dark mode too, a dark
    // color keeps light text in light mode.
    if (chromeForTint(rgb, light ? 'light' : 'dark') === 'light') root.setProperty('--ink', inkForTint(rgb))
    else root.removeProperty('--ink')
  }, [tint, sidebar, light])

  // Glyphs drawn in the accent (folder rows) keep their contrast on the sidebar.
  useEffect(() => {
    const root = document.documentElement.style
    const a = accent ? hexToRgb(accent) : null
    const t = hexToRgb(sidebar)
    if (a && t) root.setProperty('--accent-glyph', rgbToHex(glyphOnTint(a, t)))
    else root.removeProperty('--accent-glyph')
  }, [accent, sidebar])

  // The window always carries grain (at least sd 0.83); the empty page card draws the same tile as
  // overlay, which moves a bright card less, so at CARD_GRAIN times the window's alpha it carries about a third of
  // the window's spread (shell.css).
  const tile = grainTileUrl()
  const grainOpacity = grainAlpha(layer.grain)
  const cardOpacity = Math.round(grainOpacity * CARD_GRAIN * 1000) / 1000
  useEffect(() => {
    const root = document.documentElement.style
    if (tile) root.setProperty('--clu-grain-tile', `url(${tile})`)
    root.setProperty('--clu-grain-opacity', String(cardOpacity))
  }, [tile, cardOpacity])

  // The drag's frames: the same call that moves the panels writes the blended stops, so the colors
  // follow the fingers with no React render per frame. A render (a new target, the switch) draws the same values.
  const bgRef = useRef<HTMLDivElement>(null)
  const grainRef = useRef<HTMLDivElement>(null)
  useLayoutEffect(
    () =>
      subscribeSwipeBlend(() => {
        const el = bgRef.current
        const next = getSwipeBlend()
        if (!el || !next) return
        const l = layerOf(stateMirror().get(), windowId, next)
        el.style.setProperty('--clu-theme-c0', l.stops[0])
        el.style.setProperty('--clu-theme-c1', l.stops[1])
        el.style.setProperty('--clu-theme-c2', l.stops[2])
        el.style.opacity = String(l.alpha)
        if (grainRef.current) grainRef.current.style.opacity = String(grainAlpha(l.grain))
      }),
    [windowId]
  )

  const fade = swiping || reduced || performance.now() >= fadeUntil.current ? 0 : CROSS_FADE_MS
  const transition = fade ? FADED.map((p) => `${p} ${fade}ms var(--ease-tint)`).join(', ') : 'none'
  const style = {
    '--clu-theme-c0': layer.stops[0],
    '--clu-theme-c1': layer.stops[1],
    '--clu-theme-c2': layer.stops[2],
    opacity: layer.alpha,
    transition
  } as CSSProperties

  return (
    <div ref={bgRef} className="clu-theme-bg" aria-hidden="true" data-opaque={layer.alpha === 1} data-swiping={swiping} data-colors={colors || undefined} style={style}>
      {tile ? (
        <div
          ref={grainRef}
          className="clu-theme-grain"
          style={{ backgroundImage: `url(${tile})`, opacity: grainOpacity, transition: fade ? `opacity ${fade}ms var(--ease-tint)` : 'none' }}
        />
      ) : null}
    </div>
  )
}
