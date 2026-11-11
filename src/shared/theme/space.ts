// Space theme model (docs/plan/01-shell.md F9, PLAN decision 20): HSL helpers, the 12 presets, the
// default theme derived from a space's named color, the accent, and CSS for the frame background and its ink. The
// validator is in schema.ts.
import { SPACE_COLOR_HEX } from '../spaces'
import type { SpaceColor } from '../types'
import { DEFAULT_INTENSITY, translucencyAlpha, type ResolvedMode } from './appearance'
import type { SpaceTheme, ThemeColor } from './types'

export type Rgb = [number, number, number]

const clamp01 = (v: number) => Math.min(1, Math.max(0, v))

/** HSL (h in degrees, s and l in [0,1]) to sRGB channels in [0,255], rounded. */
export function hslToRgb(h: number, s: number, l: number): Rgb {
  const hue = ((h % 360) + 360) % 360
  const sat = clamp01(s)
  const lig = clamp01(l)
  const c = (1 - Math.abs(2 * lig - 1)) * sat
  const x = c * (1 - Math.abs(((hue / 60) % 2) - 1))
  const m = lig - c / 2
  const [r, g, b] =
    hue < 60 ? [c, x, 0] : hue < 120 ? [x, c, 0] : hue < 180 ? [0, c, x] : hue < 240 ? [0, x, c] : hue < 300 ? [x, 0, c] : [c, 0, x]
  return [Math.round((r + m) * 255), Math.round((g + m) * 255), Math.round((b + m) * 255)]
}

/** sRGB channels in [0,255] to HSL; h in [0,360), 0 for grays. */
export function rgbToHsl([r, g, b]: Rgb): { h: number; s: number; l: number } {
  const [rr, gg, bb] = [r / 255, g / 255, b / 255]
  const max = Math.max(rr, gg, bb)
  const min = Math.min(rr, gg, bb)
  const l = (max + min) / 2
  const d = max - min
  if (d === 0) return { h: 0, s: 0, l }
  const s = d / (1 - Math.abs(2 * l - 1))
  const h = max === rr ? ((gg - bb) / d) % 6 : max === gg ? (bb - rr) / d + 2 : (rr - gg) / d + 4
  return { h: ((h * 60) % 360 + 360) % 360, s: clamp01(s), l }
}

export function hexToRgb(hex: string): Rgb | null {
  const m = /^#([0-9a-f]{6})$/i.exec(hex)
  if (!m) return null
  const n = parseInt(m[1] as string, 16)
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255]
}

export function rgbToHex([r, g, b]: Rgb): string {
  return `#${[r, g, b].map((v) => Math.round(Math.min(255, Math.max(0, v))).toString(16).padStart(2, '0')).join('')}`
}

/** Linear sRGB mix: t = 0 gives a, t = 1 gives b. */
export function mixRgb(a: Rgb, b: Rgb, t: number): Rgb {
  const k = clamp01(t)
  return [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k]
}

/** Brightness is fixed by the resolved mode (HSL lightness). */
export const THEME_LIGHTNESS: Record<ResolvedMode, number> = { light: 0.9, dark: 0.17 }
/** Share of a color's saturation each mode keeps: a dark tint is a muted gray of the hue, never a saturated navy
 * (dark sidebar fills are white at low alphas over a near-neutral dark). */
export const THEME_SATURATION: Record<ResolvedMode, number> = { light: 1, dark: 0.3 }
/** The window's vibrancy material the theme layer is painted over. */
export const VIBRANCY_BASE: Record<ResolvedMode, string> = { light: '#ececec', dark: '#1e1e1e' }
/** Text colors (tokens.css `--ink` light and dark). */
export const INK = { dark: '#1d1d1f', light: '#f2f2f7' } as const
/** The accent made from a theme color: the main color as a soft tint (a full-saturation
 * pick at lightness 50% would give neon #00ff52 rings, sliders and drop lines). A mint pick gives the sidebar's (126, 221, 157). */
const ACCENT_LIGHTNESS = 0.68
const ACCENT_MAX_SATURATION = 0.58

/** How far lightness moves at intensity 1 (light darkens, dark lightens); none at or below the default 0.5. */
export const INTENSITY_LIGHTNESS: Record<ResolvedMode, number> = { light: -0.12, dark: 0.05 }
/** Below the default the slider turns the default's color into a lighter pastel in OKLab, linearly down to the minimum:
 * lightness rises by `lift` and chroma keeps `keep` of itself at 0.
 * In light mode a pink reaches `#f2b6b8` at the minimum. */
export const INTENSITY_SOFTEN: Record<ResolvedMode, { lift: number; keep: number }> = {
  light: { lift: 0.054, keep: 0.92 },
  dark: { lift: 0.073, keep: 0.38 },
}

const toLinear = (v: number) => (v <= 0.04045 * 255 ? v / 255 / 12.92 : ((v / 255 + 0.055) / 1.055) ** 2.4)
const fromLinear = (v: number) => {
  const c = clamp01(v)
  return 255 * (c <= 0.0031308 ? 12.92 * c : 1.055 * c ** (1 / 2.4) - 0.055)
}

/** sRGB channels in [0,255] to OKLab [L, a, b]. */
export function rgbToOklab([r, g, b]: Rgb): Rgb {
  const [lr, lg, lb] = [toLinear(r), toLinear(g), toLinear(b)]
  const l = Math.cbrt(0.4122214708 * lr + 0.5363325363 * lg + 0.0514459929 * lb)
  const m = Math.cbrt(0.2119034982 * lr + 0.6806995451 * lg + 0.1073969566 * lb)
  const s = Math.cbrt(0.0883024619 * lr + 0.2817188376 * lg + 0.6299787005 * lb)
  return [
    0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s,
  ]
}

/** OKLab to sRGB channels in [0,255], clipped to the gamut and rounded. */
export function oklabToRgb([L, a, b]: Rgb): Rgb {
  const l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
  const m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
  const s = (L - 0.0894841775 * a - 1.291485548 * b) ** 3
  return [
    Math.round(fromLinear(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s)),
    Math.round(fromLinear(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s)),
    Math.round(fromLinear(-0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s)),
  ]
}

/** The default's color at an intensity below the default: lighter and nearly as colorful, never a gray. */
export function softenBelowDefault(rgb: Rgb, mode: ResolvedMode, intensity: number): Rgb {
  const t = clamp01(1 - 2 * intensity)
  if (t === 0) return rgb
  const { lift, keep } = INTENSITY_SOFTEN[mode]
  const [L, a, b] = rgbToOklab(rgb)
  const k = 1 - (1 - keep) * t
  return oklabToRgb([Math.min(1, L + lift * t), a * k, b * k])
}

/** Above the default the slider deepens the default's color in OKLab, linearly up to the maximum: lightness drops by
 * `drop` and chroma grows to `gain` times itself at 1. The design's sidebar runs `#959ccf` (L 0.706, C 0.075) at
 * the middle to `#7d87e4` (L 0.656, C 0.138) at the maximum. */
export const INTENSITY_DEEPEN = { drop: 0.05, gain: 1.84 } as const

const inGamut = ([L, a, b]: Rgb) => {
  const l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
  const m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
  const s = (L - 0.0894841775 * a - 1.291485548 * b) ** 3
  return [
    4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
    -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s,
  ].every((v) => v >= -1e-4 && v <= 1 + 1e-4)
}

/** The default's color at an intensity above the default: darker and more colorful, the hue held. Chroma the sRGB
 * gamut can't show is cut back (not clipped per channel, which would shift the hue). */
export function deepenAboveDefault(rgb: Rgb, intensity: number): Rgb {
  const t = clamp01(2 * intensity - 1)
  if (t === 0) return rgb
  const [L0, a, b] = rgbToOklab(rgb)
  const L = Math.max(0, L0 - INTENSITY_DEEPEN.drop * t)
  let lo = 0
  let hi = 1 + (INTENSITY_DEEPEN.gain - 1) * t
  if (!inGamut([L, a * hi, b * hi])) {
    for (let n = 0; n < 20; n++) {
      const k = (lo + hi) / 2
      if (inGamut([L, a * k, b * k])) lo = k
      else hi = k
    }
    hi = lo
  }
  return oklabToRgb([L, a * hi, b * hi])
}

/** A legacy theme color: the mode fixes the lightness. Kept for spaces
 * with no theme row (defaultThemeFor), whose muted look new spaces keep. */
export function legacyColorRgb(c: ThemeColor, mode: ResolvedMode, intensity: number = DEFAULT_INTENSITY): Rgb {
  const i = Number.isFinite(intensity) ? clamp01(intensity) : DEFAULT_INTENSITY
  // Below the default the default's color softens into a lighter pastel, never a gray.
  const k = Math.max(1, 2 * i)
  const l = THEME_LIGHTNESS[mode] + INTENSITY_LIGHTNESS[mode] * clamp01(k - 1)
  return softenBelowDefault(hslToRgb(c.h, clamp01(c.s * THEME_SATURATION[mode] * k), Math.round(l * 1000) / 1000), mode, i)
}

/** The gray the chrome mixes a theme color into, and the color's share: 0.64 (`#df0001` gives `#ce3e3f`, `#050001`
 * gives `#423f40`, `#ffbaba` gives `#e1b5b6`). Below the default softenBelowDefault lightens the mix, above it
 * deepenAboveDefault deepens it. */
export const CHROME_GRAY: Rgb = [171, 171, 171]
export const COLOR_SHARE = 0.64
/** Light mode lifts the chrome toward white (`#959ccf` dark against `#a6acdf` light). */
export const LIGHT_LIFT = 0.15

/** A theme color as the frame paints it: the wheel's color (picker.ts) mixed into a gray, then moved by the
 * intensity, so the chrome follows the color's lightness. */
export function colorRgb(c: ThemeColor, mode: ResolvedMode, intensity: number = DEFAULT_INTENSITY): Rgb {
  const h = ((c.h % 360) + 360) % 360
  const wheel = hslToRgb(h, 1, 0.01 + 0.87 * clamp01(c.s))
  const mixed = mixRgb(CHROME_GRAY, wheel, COLOR_SHARE)
  const out = (mode === 'light' ? mixRgb(mixed, [255, 255, 255], LIGHT_LIFT) : mixed).map((v) => Math.round(v)) as Rgb
  const i = Number.isFinite(intensity) ? clamp01(intensity) : DEFAULT_INTENSITY
  return i > DEFAULT_INTENSITY ? deepenAboveDefault(out, i) : softenBelowDefault(out, mode, i)
}

/** The theme's gradient as three stops: one color repeats, two colors put their sRGB midpoint in the middle. */
export function themeStops(theme: SpaceTheme, mode: ResolvedMode, legacy = false): [Rgb, Rgb, Rgb] {
  const paint = legacy ? legacyColorRgb : colorRgb
  const rgb = theme.colors.map((c) => paint(c, mode, theme.intensity))
  const [a, b, c] = rgb as [Rgb, Rgb | undefined, Rgb | undefined]
  if (!b) return [a, a, a]
  if (!c) return [a, mixRgb(a, b, 0.5), b]
  return [a, b, c]
}

/** WCAG relative luminance of an sRGB color. */
export function luminance([r, g, b]: Rgb): number {
  const lin = (v: number) => {
    const c = v / 255
    return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4
  }
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)
}

export function contrast(a: Rgb, b: Rgb): number {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x) as [number, number]
  return (hi + 0.05) / (lo + 0.05)
}

/** Contrast a glyph in the accent keeps against the sidebar (a folder glyph in a red space vanished). */
export const GLYPH_CONTRAST = 2.2

/**
 * The accent as a glyph on the sidebar `tint`: the accent itself when it stands out, else the accent mixed toward
 * white on a dark sidebar (toward black on a light one) in tenths until it does.
 */
export function glyphOnTint(accent: Rgb, tint: Rgb): Rgb {
  const toward: Rgb = luminance(tint) < 0.35 ? [255, 255, 255] : [0, 0, 0]
  for (let t = 0; t <= 1; t += 0.1) {
    const c = mixRgb(accent, toward, Math.min(1, t))
    if (contrast(c, tint) >= GLYPH_CONTRAST) return c
  }
  return toward
}

/** The color a stop shows on screen: the theme layer at `alpha` over the vibrancy base. */
export function effectiveRgb(stop: Rgb, mode: ResolvedMode, alpha: number): Rgb {
  return mixRgb(hexToRgb(VIBRANCY_BASE[mode]) as Rgb, stop, alpha)
}

/** Dark or light text, whichever contrasts more with every given background (the gradient's stops). */
export function inkFor(bg: Rgb | readonly Rgb[]): string {
  const list = (Array.isArray(bg[0]) ? bg : [bg]) as readonly Rgb[]
  const worst = (ink: string) => Math.min(...list.map((c) => contrast(hexToRgb(ink) as Rgb, c)))
  return worst(INK.dark) >= worst(INK.light) ? INK.dark : INK.light
}

/** Secondary text: the ink faded toward the background as far as contrast 4.5 allows on every stop. */
function mutedInk(ink: string, list: readonly Rgb[]): string {
  const inkRgb = hexToRgb(ink) as Rgb
  const avg = [0, 1, 2].map((i) => list.reduce((sum, c) => sum + c[i as 0 | 1 | 2], 0) / list.length) as Rgb
  for (let t = 0.4; t > 0; t -= 0.02) {
    const c = mixRgb(inkRgb, avg, t)
    if (list.every((bg) => contrast(c, bg) >= 4.5)) return rgbToHex(c)
  }
  return ink
}

export function accentOf(c: ThemeColor): string {
  return rgbToHex(hslToRgb(c.h, Math.min(c.s, ACCENT_MAX_SATURATION), ACCENT_LIGHTNESS))
}

export interface ThemeCss { background: string; ink: string; inkMuted: string; accent: string }

/** CSS for a theme in a mode: a flat fill for 1 color, a 120 degree gradient for 2 or 3; ink chosen by contrast. */
export function themeToCss(theme: SpaceTheme, mode: ResolvedMode, legacy = false): ThemeCss {
  const stops = themeStops(theme, mode, legacy)
  const paint = legacy ? legacyColorRgb : colorRgb
  const hex = theme.colors.map((c) => rgbToHex(paint(c, mode, theme.intensity)))
  const background = hex.length === 1 ? (hex[0] as string) : `linear-gradient(120deg, ${hex.join(', ')})`
  const alpha = translucencyAlpha({ resolved: mode, intensity: theme.intensity, reduceTransparency: false, focused: true })
  // Both the translucent layer and the opaque one (inactive window, Reduce transparency) count.
  const shown = [alpha, 1].flatMap((a) => stops.map((s) => effectiveRgb(s, mode, a)))
  const ink = inkFor(shown)
  return { background, ink, inkMuted: mutedInk(ink, shown), accent: accentOf(theme.colors[0] as ThemeColor) }
}


/** The default theme for a new space: a pastel of the space's hue (saturation at most 0.5, the
 * `#dadbf1` top left) running to a warm `#efe4e4` at the bottom right, with faint grain (sd about 0.6 levels). */
export const DEFAULT_THEME = { maxSaturation: 0.5, warm: { h: 0, s: 0.3 }, grain: 0.25 } as const

/** The theme of a space with no (or an unreadable) theme row: the default gradient in its named color's hue. */
export function defaultThemeFor(color: SpaceColor): SpaceTheme {
  const rgb = hexToRgb(SPACE_COLOR_HEX[color] ?? SPACE_COLOR_HEX.blue) as Rgb
  const { h, s } = rgbToHsl(rgb)
  const hue = { h: Math.round(h * 10) / 10 % 360, s: Math.round(Math.min(s, DEFAULT_THEME.maxSaturation) * 1000) / 1000 }
  return { colors: [hue, { ...DEFAULT_THEME.warm }], intensity: DEFAULT_INTENSITY, grain: DEFAULT_THEME.grain }
}

/**
 * The handles the theme editor opens with for a space with no theme row: defaultThemeFor's colors at the
 * wheel saturation whose paint comes closest to the muted color the sidebar already shows, so the handles sit where
 * the sidebar's colors are and moving one starts from what the user sees.
 */
export function editorThemeFor(color: SpaceColor, mode: ResolvedMode): SpaceTheme {
  const base = defaultThemeFor(color)
  const colors = base.colors.map((c) => {
    const [r, g, b] = legacyColorRgb(c, mode, base.intensity)
    let best = { s: c.s, d: Infinity }
    for (let i = 0; i <= 100; i++) {
      const [r2, g2, b2] = colorRgb({ h: c.h, s: i / 100 }, mode, base.intensity)
      const d = (r - r2) ** 2 + (g - g2) ** 2 + (b - b2) ** 2
      if (d < best.d) best = { s: i / 100, d }
    }
    return { h: c.h, s: best.s }
  })
  return { ...base, colors }
}

/** `spaces[id].accent`: the theme's first color when a theme is set, the named color's hex otherwise. */
export function spaceAccent(theme: SpaceTheme | null, color: SpaceColor): string {
  const first = theme?.colors[0]
  return first ? accentOf(first) : (SPACE_COLOR_HEX[color] ?? SPACE_COLOR_HEX.blue)
}

export interface ThemePreset { id: string; name: string; theme: SpaceTheme }

const preset = (name: string, colors: [number, number][], grain = 0): ThemePreset => ({
  id: name.toLowerCase(),
  name,
  theme: { colors: colors.map(([h, s]) => ({ h, s })), intensity: DEFAULT_INTENSITY, grain }
})

/** Clu's 12 presets (names are Clu's own). */
export const THEME_PRESETS: readonly ThemePreset[] = [
  preset('Moss', [[96, 0.3], [140, 0.32]]),
  preset('Ember', [[12, 0.82], [36, 0.88]]),
  preset('Tide', [[190, 0.62], [222, 0.58]]),
  preset('Dusk', [[262, 0.45], [316, 0.48], [22, 0.6]]),
  preset('Sand', [[38, 0.45]], 0.25),
  preset('Plum', [[290, 0.4], [330, 0.44]]),
  preset('Slate', [[215, 0.16]]),
  preset('Coral', [[4, 0.74], [26, 0.8]]),
  preset('Fern', [[150, 0.45]], 0.15),
  preset('Ink', [[228, 0.36], [250, 0.3]]),
  preset('Honey', [[44, 0.86], [30, 0.8]]),
  preset('Frost', [[200, 0.36], [180, 0.26], [220, 0.32]])
]
