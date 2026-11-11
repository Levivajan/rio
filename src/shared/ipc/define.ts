// Envelope primitives shared by ipc.ts and every per-lane contract module.
import type { z } from 'zod'
import type { InternalPage } from '../url'

export type InternalSurface = `internal:${InternalPage}`
export type Surface = 'ui' | 'overlay' | 'mini-ui' | InternalSurface
export const SURFACES: readonly Surface[] = [
  'ui', 'overlay', 'mini-ui', 'internal:settings', 'internal:history', 'internal:extensions', 'internal:about'
]

export const ERROR_CODES = [
  'not-a-surface', 'subframe', 'wrong-origin', 'not-allowed', 'too-large', 'bad-payload',
  'not-implemented', 'not-found', 'stale', 'rejected'
] as const
export type ErrorCode = (typeof ERROR_CODES)[number]
export interface IpcErrorBody { code: ErrorCode; message?: string; reason?: string }
export type IpcResult<T> = { ok: true; value: T } | { ok: false; error: IpcErrorBody }

export const DEFAULT_ALLOW: readonly Surface[] = ['ui', 'overlay']

export interface CommandDef<I extends z.ZodType = z.ZodType, O = unknown> {
  input: I
  /** Phantom: carries the output type only. */
  output: O
  allow: readonly Surface[]
}

/** Phantom output marker: `output: out<{ tabId: string }>()`. */
export const out = <T = void>(): T => undefined as unknown as T

export function defineCommand<I extends z.ZodType, O = void>(def: { input: I; output?: O; allow?: readonly Surface[] }): CommandDef<I, O> {
  return { input: def.input, output: undefined as unknown as O, allow: def.allow ?? DEFAULT_ALLOW }
}

export const MAX_PAYLOAD_BYTES = 256 * 1024
