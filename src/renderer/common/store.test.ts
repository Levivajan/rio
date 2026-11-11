import { describe, expect, it, vi } from 'vitest'
import { defaultState } from '../../shared/state'
import { createStateMirror } from './store'

const ID = '66666666-6666-4666-8666-666666666666'

describe('state mirror', () => {
  it('applies a snapshot, then patches in order', () => {
    const m = createStateMirror(async () => null)
    m.receive({ kind: 'snapshot', rev: 3, state: { ...defaultState(), rev: 3 } })
    m.receive({ kind: 'patch', rev: 4, ops: [{ op: 'set', path: ['historyRevision'], value: 7 }] })
    expect(m.get().historyRevision).toBe(7)
    expect(m.get().rev).toBe(4)
  })
  it('refetches with state.get on a rev gap', async () => {
    const fresh = { ...defaultState(), rev: 10, archiveRevision: 5 }
    const refetch = vi.fn(async () => fresh)
    const m = createStateMirror(refetch)
    m.receive({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1 } })
    m.receive({ kind: 'patch', rev: 5, ops: [{ op: 'set', path: ['tabs', ID], value: {} }] })
    await vi.waitFor(() => expect(m.get().archiveRevision).toBe(5))
    expect(refetch).toHaveBeenCalledTimes(1)
    expect(m.get().tabs[ID]).toBeUndefined()
  })
})
