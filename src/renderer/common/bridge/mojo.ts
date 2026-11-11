// MojoBridge (Chromium build): the CluBridge over the generated Mojo WebUI bindings.
// Commands go to `<Area>Handler.<verb>` with the payload encoded by the generated wire table; the state channel is
// StateSource.Subscribe plus StateObserver.OnState, one JSON StateMessage per call. The browser decides windowId,
// layer and slot (SurfaceInfo); the page never does.
import type { CluBridge } from '../../../shared/bridge'
import type { CommandName } from '../../../shared/ipc'
import { ERROR_CODES, type ErrorCode, type IpcResult } from '../../../shared/ipc/define'
import { encodeWire, WireError } from '../../../shared/mojoWire'
import type { StateMessage } from '../../../shared/state'
import type { MojoBindings, MojoCallResult, MojoRemote } from './mojo-bindings'
import { CONTRACT_VERSION, MOJO_WIRE } from './mojo-wire.gen'

const LAYERS = ['top', 'under-peek', 'passive'] as const
// The passive pool: toasts, the switcher, the hover card and the hover peek (CluShell::kPassiveSlots).
const PASSIVE_SLOTS = 4
/** Messages kept until the store subscribes; past this the store resyncs through state.get on the first patch. */
const MAX_BUFFERED = 1000

const fail = (code: ErrorCode, message?: string): IpcResult<never> => ({ ok: false, error: { code, ...(message ? { message } : {}) } })

export function decodeResult(r: MojoCallResult | null | undefined): IpcResult<unknown> {
  if (r?.error) {
    const code = ERROR_CODES[r.error.code] ?? 'rejected'
    return { ok: false, error: { code, ...(r.error.message ? { message: r.error.message } : {}), ...(r.error.reason ? { reason: r.error.reason } : {}) } }
  }
  if (typeof r?.valueJson !== 'string') return fail('rejected', 'empty result')
  try {
    return { ok: true, value: JSON.parse(r.valueJson) as unknown }
  } catch {
    return fail('rejected', 'result is not JSON')
  }
}

export function parseStateMessage(json: string): StateMessage | null {
  let msg: unknown
  try {
    msg = JSON.parse(json)
  } catch {
    return null
  }
  if (typeof msg !== 'object' || msg === null) return null
  const m = msg as { kind?: unknown; rev?: unknown; state?: unknown; ops?: unknown }
  if (typeof m.rev !== 'number' || !Number.isSafeInteger(m.rev)) return null
  if (m.kind === 'snapshot' && typeof m.state === 'object' && m.state !== null) return msg as StateMessage
  if (m.kind === 'patch' && Array.isArray(m.ops)) return msg as StateMessage
  return null
}

export class ContractMismatchError extends Error {}

/** Connects to the browser, then answers with a bridge whose windowId, layer and slot come from SurfaceInfo. */
export async function createMojoBridge(bindings: MojoBindings): Promise<CluBridge> {
  const listeners = new Set<(msg: StateMessage) => void>()
  let buffered: StateMessage[] | null = []
  const deliver = (json: string) => {
    const msg = parseStateMessage(json)
    if (!msg) return
    if (buffered) {
      buffered.push(msg)
      if (buffered.length > MAX_BUFFERED) buffered = buffered.filter((m) => m.kind === 'snapshot').slice(-1)
      return
    }
    for (const cb of listeners) cb(msg)
  }
  const { info } = await bindings.subscribeState(deliver)
  if (info.contractVersion !== CONTRACT_VERSION) {
    throw new ContractMismatchError(`browser contract ${info.contractVersion}, WebUI contract ${CONTRACT_VERSION}: rebuild one of them`)
  }

  const remotes = new Map<string, MojoRemote>()
  const remote = (file: string, iface: string) => {
    let r = remotes.get(iface)
    if (!r) {
      r = bindings.getRemote(file, iface)
      remotes.set(iface, r)
    }
    return r
  }

  async function call(name: CommandName, payload: unknown): Promise<IpcResult<unknown>> {
    const entry = MOJO_WIRE[name]
    let args: unknown[]
    try {
      if (entry.input) args = [encodeWire(entry.input, payload ?? {})]
      else if (payload === undefined || (typeof payload === 'object' && payload !== null && !Array.isArray(payload) && Object.keys(payload).length === 0)) args = []
      else throw new WireError('expected an empty payload')
    } catch (e) {
      return fail('bad-payload', e instanceof Error ? e.message : undefined)
    }
    try {
      const r = remote(entry.lane, entry.iface)
      const method = r[entry.method]
      if (!method) return fail('not-implemented')
      // The generated remotes are classes whose methods use `this`, so the call keeps its receiver.
      const { result } = await method.call(r, ...args)
      return decodeResult(result)
    } catch {
      // A closed pipe or a binding that threw: the UI treats it like any failed call.
      return fail('rejected', 'mojo call failed')
    }
  }

  const areas: Record<string, Record<string, (payload?: unknown) => Promise<IpcResult<unknown>>>> = {}
  for (const name of Object.keys(MOJO_WIRE) as CommandName[]) {
    const dot = name.indexOf('.')
    ;(areas[name.slice(0, dot)] ??= {})[name.slice(dot + 1)] = (payload?: unknown) => call(name, payload)
  }
  const slot = info.slot !== null && Number.isInteger(info.slot) && info.slot >= 0 && info.slot < PASSIVE_SLOTS ? info.slot : null
  const bridge = {
    ...areas,
    onState(cb: (msg: StateMessage) => void) {
      listeners.add(cb)
      if (buffered) {
        const pending = buffered
        buffered = null
        for (const msg of pending) cb(msg)
      }
      return () => {
        listeners.delete(cb)
      }
    },
    windowId: info.windowId,
    layer: info.layer === null ? null : (LAYERS[info.layer] ?? null),
    slot
  }
  return bridge as unknown as CluBridge
}
