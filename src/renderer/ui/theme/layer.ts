// Frame theme layer (PLAN decision 20): the colors, alpha and grain the window background shows for the
// active space, interpolated toward the swipe target by windows[id].swipe.fraction.
import type { ResolvedMode } from '../../../shared/theme/appearance'
import { defaultThemeFor, hexToRgb, hslToRgb, mixRgb, rgbToHex, rgbToHsl, themeStops, type Rgb } from '../../../shared/theme/space'
import type { SpaceTheme } from '../../../shared/theme/types'
import type { SpaceColor } from '../../../shared/types'
import { LOOK } from '../../../shared/look/tokens'

export interface LayerSpace { theme: SpaceTheme | null; color: SpaceColor }

export interface LayerInput {
  active: LayerSpace | undefined
  /** The space a sidebar swipe is moving to and how far (0 at the active space, 1 at the target). */
  swipe: { target: LayerSpace | undefined; fraction: number } | null
  resolved: ResolvedMode
  reduceTransparency: boolean
  focused: boolean
}

export interface Layer { stops: [string, string, string]; alpha: number; grain: number }

interface Parts { stops: Rgb[]; alpha: number; grain: number }

/** The layer is opaque (M11): the tint plus grain, never the desktop through the window. */
export const LAYER_ALPHA = 1

function partsOf(space: LayerSpace | undefined, input: LayerInput): Parts {
  // A space with no theme row keeps the muted default look; a theme from the editor follows the color wheel.
  const legacy = !space?.theme
  const theme = space ? (space.theme ?? defaultThemeFor(space.color)) : defaultThemeFor('blue')
  return { stops: themeStops(theme, input.resolved, legacy), alpha: LAYER_ALPHA, grain: theme.grain }
}

const lerp = (a: number, b: number, t: number) => a + (b - a) * t
const round3 = (v: number) => Math.round(v * 1000) / 1000

/** The layer for the active space, or the sRGB midway toward the swipe target at the swipe's fraction. */
export function frameLayer(input: LayerInput): Layer {
  const from = partsOf(input.active, input)
  const raw = input.swipe && input.swipe.target ? Math.abs(input.swipe.fraction) : 0
  const t = Number.isFinite(raw) ? Math.min(1, raw) : 0
  const to = t > 0 ? partsOf(input.swipe?.target, input) : from
  return {
    stops: from.stops.map((c, i) => rgbToHex(mixRgb(c, to.stops[i] as Rgb, t))) as Layer['stops'],
    alpha: round3(lerp(from.alpha, to.alpha, t)),
    grain: round3(lerp(from.grain, to.grain, t))
  }
}

/**
 * The color the UI tints with (tooltips, keycaps, popovers, menus, the empty page, Site controls): the theme's MAIN
 * color, its first stop. The editor keeps the main handle first and places the others around it (the
 * complement for two colors, -150 and +150 degrees for three), and its tooltips and panels take the main color only,
 * never a gradient's later stops.
 */
export const mainTint = (layer: Layer): string => layer.stops[0]

/**
 * The strong accent (folder glyph, split focus ring, Peek buttons) in the main color's hue and saturation: the
 * default theme pairs accent #9095fb with #2028f2 in light mode and #6a70ff in dark (lightness 54% and
 * 71% at the same hue), so other themes take their own hue at those lightnesses. Null for a malformed accent.
 */
export function strongAccent(accent: string, light: boolean): string | null {
  const rgb = hexToRgb(accent)
  if (!rgb) return null
  const { h, s } = rgbToHsl(rgb)
  return rgbToHex(hslToRgb(h, s, light ? 0.54 : 0.71))
}

/**
 * The selected row of the command bar and its menus: the main color as a muted tone, never the neon a saturated accent
 * gives. A light pink space selects in #dd808d (HSL 352, 58%,
 * 68%) and in #9b3a45 on a dark red one (HSL 353, 46%, 42%), so other themes keep their hue at the
 * lightness of their chrome set with saturation at most 58%. Null for a malformed accent.
 */
export function selectionTint(accent: string, light: boolean): string | null {
  const rgb = hexToRgb(accent)
  if (!rgb) return null
  const { h, s } = rgbToHsl(rgb)
  return rgbToHex(hslToRgb(h, Math.min(s, 0.58), light ? 0.68 : 0.418))
}

/** The color the sidebar sits on: the middle stop, or with three colors in corners (the sidebar runs from the first
 * at the top to the third at the bottom) their midpoint. */
export function sidebarTint(stops: readonly string[], colors: number): string {
  const [a, c] = [hexToRgb(stops[0] ?? ''), hexToRgb(stops[2] ?? '')]
  return colors === 3 && a && c ? rgbToHex(mixRgb(a, c, 0.5)) : (stops[1] ?? '')
}

export const sameLayer = (a: Layer, b: Layer) =>
  a.alpha === b.alpha && a.grain === b.grain && a.stops.every((s, i) => s === b.stops[i])

/** Cross-fade of the background on a space switch (M11: a 120 ms tint fade); none while a swipe drives it or with reduced motion. */
export const CROSS_FADE_MS = LOOK.motion.spaceTintMs
