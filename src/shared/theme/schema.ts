// Space theme schemas. Kept out of space.ts, which the renderers import: zod probes eval with
// new Function(""), and the renderers' Trusted Types CSP reports that as a violation.
import { finite, z } from '../schema'
import type { SpaceTheme } from './types'

const unit = () => finite().pipe(z.number().min(0).max(1))
export const themeColorSchema = () => z.object({ h: finite().pipe(z.number().min(0).lt(360)), s: unit() }).strict()
/** 1 to 3 colors, h in [0,360), s, intensity and grain in [0,1]; NaN, Infinity and extra keys are rejected. */
export const spaceThemeSchema = () => z.object({ colors: z.array(themeColorSchema()).min(1).max(3), intensity: unit(), grain: unit() }).strict()

const schema = spaceThemeSchema()
export function parseSpaceTheme(value: unknown): SpaceTheme | null {
  const r = schema.safeParse(value)
  return r.success ? r.data : null
}
