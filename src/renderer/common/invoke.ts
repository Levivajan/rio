// Typed call helper: `not-implemented` becomes a disabled result, never a crash.
import type { IpcResult } from '../../shared/ipc'

export type CallResult<T> = { ok: true; value: T } | { ok: false; disabled: boolean; code: string }

export async function invoke<T>(call: Promise<IpcResult<T>>): Promise<CallResult<T>> {
  try {
    const r = await call
    if (r.ok) return r
    return { ok: false, disabled: r.error.code === 'not-implemented', code: r.error.code }
  } catch {
    return { ok: false, disabled: false, code: 'rejected' }
  }
}
