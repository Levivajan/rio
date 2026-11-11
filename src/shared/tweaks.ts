// Tweaks types and the TweakControls schema (seeded by the scaffold).
import { finite, str, z } from './schema'

export const TWEAK_FONTS = ['system-ui', 'serif', 'sans-serif', 'monospace', 'Georgia', 'Helvetica Neue', 'Menlo'] as const

export const isTweakFont = (v: unknown): v is (typeof TWEAK_FONTS)[number] => typeof v === 'string' && (TWEAK_FONTS as readonly string[]).includes(v)

/** Slider bounds of each numeric control (PLAN 4, TweakControls). */
export const TWEAK_RANGES = {
  hue: { min: -180, max: 180 },
  contrast: { min: 0.5, max: 2 },
  brightness: { min: 0.5, max: 2 },
  saturation: { min: 0, max: 2 },
  fontScale: { min: 0.75, max: 1.5 }
} as const
type RangeKey = keyof typeof TWEAK_RANGES

const rangeOf = (k: RangeKey) => finite().pipe(z.number().min(TWEAK_RANGES[k].min).max(TWEAK_RANGES[k].max))
export const tweakControls = () =>
  z
    .object({
      hue: rangeOf('hue'),
      invert: z.boolean(),
      contrast: rangeOf('contrast'),
      brightness: rangeOf('brightness'),
      saturation: rangeOf('saturation'),
      fontFamily: str(64).nullable().refine((v) => v === null || isTweakFont(v), 'unknown font'),
      fontScale: rangeOf('fontScale')
    })
    .strict()
export type TweakControls = z.infer<ReturnType<typeof tweakControls>>

/** The page as the site made it: no filter, the site's own font at its own size. */
export const DEFAULT_TWEAK_CONTROLS: Readonly<TweakControls> = Object.freeze({ hue: 0, invert: false, contrast: 1, brightness: 1, saturation: 1, fontFamily: null, fontScale: 1 })

const clampTo = (k: RangeKey, v: unknown): number =>
  typeof v === 'number' && Number.isFinite(v) ? Math.min(TWEAK_RANGES[k].max, Math.max(TWEAK_RANGES[k].min, v)) : DEFAULT_TWEAK_CONTROLS[k]

/** Repairs anything into controls: numbers clamp into range, NaN and junk fields take the default, unknown fonts read as none. */
export function clampControls(raw: unknown): TweakControls {
  const r = raw && typeof raw === 'object' && !Array.isArray(raw) ? (raw as Record<string, unknown>) : {}
  return {
    hue: clampTo('hue', r.hue),
    invert: r.invert === true,
    contrast: clampTo('contrast', r.contrast),
    brightness: clampTo('brightness', r.brightness),
    saturation: clampTo('saturation', r.saturation),
    fontFamily: isTweakFont(r.fontFamily) ? r.fontFamily : null,
    fontScale: clampTo('fontScale', r.fontScale)
  }
}

export const isDefaultControls = (c: TweakControls): boolean =>
  (Object.keys(DEFAULT_TWEAK_CONTROLS) as (keyof TweakControls)[]).every((k) => c[k] === DEFAULT_TWEAK_CONTROLS[k])

/** One hidden element (or set of related elements): `selector { display: none !important }`. */
export interface Zap { selector: string; label: string; related: boolean; createdAt: number }

export interface SiteTweak { site: string; enabled: boolean; css: string; js: string; controls: TweakControls | null; zaps: Zap[]; updatedAt: number }

/** One site in the settings list (tweaks.list). */
export interface SiteTweakRow { site: string; enabled: boolean; hasCss: boolean; hasJs: boolean; hasControls: boolean; zapCount: number; updatedAt: number }

/** What a tweak holds, in words: 'CSS', 'JS', 'Style controls', '<n> hidden'. */
export function tweakParts(t: { hasCss: boolean; hasJs: boolean; hasControls: boolean; zapCount: number }): string[] {
  return [t.hasCss && 'CSS', t.hasJs && 'JS', t.hasControls && 'Style controls', t.zapCount > 0 && `${t.zapCount} hidden`].filter((p): p is string => !!p)
}
