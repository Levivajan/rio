// Renderer state mirror: applies the snapshot and patches from clu:state; a rev gap refetches.
import { applyPatch, defaultState, type CluState, type StateMessage } from '../../shared/state'
import { clu } from './clu'

type Listener = () => void

export interface StateMirror {
  get(): CluState
  hasSnapshot(): boolean
  subscribe(fn: Listener): () => void
  /** Feeds one message from the channel (exposed for unit tests). */
  receive(msg: StateMessage): void
}

export function createStateMirror(refetch: () => Promise<CluState | null>): StateMirror {
  let state: CluState = defaultState()
  let rev = -1
  let has = false
  let refetching = false
  const listeners = new Set<Listener>()
  const notify = () => {
    for (const fn of listeners) fn()
  }

  function resync(): void {
    if (refetching) return
    refetching = true
    void refetch()
      .then((s) => {
        if (s && typeof s.rev === 'number' && s.rev >= rev) {
          state = s
          rev = s.rev
          has = true
          notify()
        }
      })
      .finally(() => {
        refetching = false
      })
  }

  return {
    get: () => state,
    hasSnapshot: () => has,
    subscribe(fn) {
      listeners.add(fn)
      return () => listeners.delete(fn)
    },
    receive(msg) {
      if (msg.kind === 'snapshot') {
        if (msg.rev < rev) return
        state = msg.state
        rev = msg.rev
        has = true
        return notify()
      }
      if (!has) return resync()
      if (msg.rev <= rev) return
      if (msg.rev !== rev + 1) return resync()
      state = applyPatch(state, msg.ops)
      state = { ...state, rev: msg.rev }
      rev = msg.rev
      notify()
    }
  }
}

let mirror: StateMirror | null = null

/** The process-wide mirror, subscribed to window.clu on first use. */
export function stateMirror(): StateMirror {
  if (mirror) return mirror
  const m = createStateMirror(async () => {
    const r = await clu().state.get()
    return r.ok ? r.value : null
  })
  mirror = m
  clu().onState((msg) => m.receive(msg))
  return m
}

/** Test seam. */
export function setStateMirrorForTest(m: StateMirror | null): void {
  mirror = m
}
