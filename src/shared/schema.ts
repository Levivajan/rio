// zod re-export plus the payload helpers every contract module uses.
import { z } from 'zod'

export { z }

const UUID_V4 = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i

export const MAX_URL = 32 * 1024

/** UUID v4 id. */
export const id = () => z.string().regex(UUID_V4, 'expected a UUID v4')
/** String with a hard length cap. */
export const str = (max: number) => z.string().max(max)
/** Integer in [min, max]. */
export const int = (min: number, max: number) => z.number().int().min(min).max(max)
/** Finite number (rejects NaN and Infinity). */
export const finite = () => z.number().refine(Number.isFinite, 'expected a finite number')
export const rect = () => z.object({ x: finite(), y: finite(), width: finite(), height: finite() }).strict()
export const point = () => z.object({ x: finite(), y: finite() }).strict()
/** URL string at most 32 KiB. The URL policy is checked again in main. */
export const url = () => z.string().max(MAX_URL)
export const empty = () => z.object({}).strict()

export type JsonSchema = z.ZodType<import('./types').Json>
export const json: () => JsonSchema = () =>
  z.lazy(() => z.union([z.null(), z.boolean(), finite(), z.string().max(MAX_URL), z.array(json()).max(1000), z.record(z.string().max(256), json())])) as JsonSchema

export function isUuid(value: unknown): value is string {
  return typeof value === 'string' && UUID_V4.test(value)
}
