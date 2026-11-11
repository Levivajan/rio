// Color rules of the look (M11): how a fill, glyph or surface follows the space tint. Every sidebar fill and glyph
// is black (white in dark mode) at an alpha over the tint, so the target colors on two
// different tints solve to one alpha each (tokens.ts LOOK.color). The text ink is the exception: a near-black pulled
// toward the tint's hue.
import { hslToRgb, luminance, rgbToHex, rgbToHsl, type Rgb } from '../theme/space'
import type { ResolvedMode } from '../theme/appearance'
import type { LookColor } from './tokens'

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v))

/** `over` composited onto an opaque `base` (source-over). */
export function blend(over: LookColor, base: Rgb): Rgb {
  const a = clamp(over.a, 0, 1)
  return [base[0] * (1 - a) + over.r * a, base[1] * (1 - a) + over.g * a, base[2] * (1 - a) + over.b * a]
}

/** `share` of the tint over white: the empty page (0.33) and the tooltip fill (0.35). */
export function tintOverWhite(tint: Rgb, share: number): Rgb {
  const s = clamp(share, 0, 1)
  return [255 + (tint[0] - 255) * s, 255 + (tint[1] - 255) * s, 255 + (tint[2] - 255) * s]
}

/**
 * Primary text on a tint: a near-black in the tint's hue (`#32040a` on a pink tint and `#0a0d4b` on
 * the lavender one). Saturation doubles up to 0.85 and lightness sits at 0.12; a gray tint gives a neutral near-black.
 */
export function inkForTint(tint: Rgb): string {
  const { h, s } = rgbToHsl(tint)
  return rgbToHex(hslToRgb(h, Math.min(0.85, s * 1.9), 0.12))
}

/** Relative luminance above which a tint takes the light chrome in dark mode (#e1b5b6 is 0.52, #ce3e3f 0.17). */
export const LIGHT_TINT = 0.4
/** Relative luminance below which a tint takes the dark chrome in light mode: where white and black text contrast
 * equally (4.6:1), so a dark color never gets near-black text. */
export const DARK_TINT = 0.179

/**
 * The chrome set (tokens.css light or dark) for the sidebar on `tint`. The chrome follows the color, not
 * macOS: a pastel turns the whole sidebar light with dark glyphs in dark mode, a dark color keeps it dark
 * with light text in light mode. Between the two thresholds the mode decides.
 */
export function chromeForTint(tint: Rgb, mode: ResolvedMode): ResolvedMode {
  const l = luminance(tint)
  if (mode === 'dark') return l > LIGHT_TINT ? 'light' : 'dark'
  return l < DARK_TINT ? 'dark' : 'light'
}

/** Largest per-channel difference, the color distance the comparator and the browser tests use. */
export function channelDelta(a: Rgb, b: Rgb): number {
  return Math.max(Math.abs(a[0] - b[0]), Math.abs(a[1] - b[1]), Math.abs(a[2] - b[2]))
}

export const roundRgb = (c: Rgb): Rgb => [Math.round(c[0]), Math.round(c[1]), Math.round(c[2])]
