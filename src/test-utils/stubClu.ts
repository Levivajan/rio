// A stub window.clu built from the typed contract (seeded by the scaffold).
// Every method resolves `not-implemented` unless a test overrides it, so components never reach a real preload.
import type { CluBridge } from '../shared/bridge'
import { COMMAND_NAMES } from '../shared/ipc'
import { defaultState, type StateMessage } from '../shared/state'

export type Calls = { name: string; payload: unknown }[]

export function stubClu(overrides: Record<string, (payload: unknown) => unknown> = {}): { clu: CluBridge; calls: Calls; push(msg: StateMessage): void } {
  const calls: Calls = []
  const listeners = new Set<(msg: StateMessage) => void>()
  const areas: Record<string, Record<string, (p?: unknown) => Promise<unknown>>> = {}
  for (const name of COMMAND_NAMES) {
    const [area = '', verb = ''] = [name.slice(0, name.indexOf('.')), name.slice(name.indexOf('.') + 1)]
    ;(areas[area] ??= {})[verb] = async (payload?: unknown) => {
      calls.push({ name, payload })
      const fn = overrides[name]
      if (fn) return { ok: true, value: fn(payload) ?? null }
      if (name === 'state.get') return { ok: true, value: defaultState() }
      return { ok: false, error: { code: 'not-implemented' } }
    }
  }
  const clu = {
    ...areas,
    onState(cb: (msg: StateMessage) => void) {
      listeners.add(cb)
      return () => listeners.delete(cb)
    },
    windowId: '00000000-0000-4000-8000-000000000001',
    layer: null,
    slot: null
  } as unknown as CluBridge
  return { clu, calls, push: (msg) => listeners.forEach((cb) => cb(msg)) }
}
