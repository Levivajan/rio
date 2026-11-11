// Proves the renderer project (quality Q1): jsdom, jest-dom matchers, and a window.clu stub built from the contract.
import { act, render, screen } from '@testing-library/react'
import { useEffect, useState } from 'react'
import { describe, expect, it } from 'vitest'
import { COMMAND_NAMES } from '../shared/ipc'
import { defaultState, type StateMessage } from '../shared/state'
import { stubClu } from './stubClu'

function Rev() {
  const [rev, setRev] = useState<number | null>(null)
  useEffect(() => {
    void window.clu.state.get().then((r) => {
      if (r.ok) setRev(r.value.rev)
    })
    return window.clu.onState((msg: StateMessage) => setRev(msg.rev))
  }, [])
  return <output aria-label="revision">{rev ?? 'none'}</output>
}

describe('renderer project', () => {
  it('runs in jsdom and renders against the stubbed window.clu', async () => {
    render(<Rev />)
    expect(await screen.findByRole('status', { name: 'revision' })).toHaveTextContent(String(defaultState().rev))
  })

  it('a component sees state pushed through the stub', async () => {
    const stub = stubClu()
    window.clu = stub.clu
    render(<Rev />)
    const output = await screen.findByRole('status', { name: 'revision' })
    await act(async () => {
      await Promise.resolve()
      stub.push({ kind: 'snapshot', rev: 7, state: { ...defaultState(), rev: 7 } })
    })
    expect(output).toHaveTextContent('7')
  })
})

describe('stubClu', () => {
  const method = (clu: unknown, name: string) => {
    const dot = name.indexOf('.')
    return (clu as Record<string, Record<string, unknown>>)[name.slice(0, dot)]?.[name.slice(dot + 1)]
  }

  it('has a method for every command in the contract', () => {
    const { clu } = stubClu()
    expect(COMMAND_NAMES.length).toBeGreaterThan(0)
    for (const name of COMMAND_NAMES) expect(method(clu, name), name).toBeTypeOf('function')
  })

  it('answers not-implemented, records the call, and honors overrides', async () => {
    const name = COMMAND_NAMES.find((n) => n !== 'state.get') ?? ''
    const plain = stubClu()
    const call = method(plain.clu, name) as (p: unknown) => Promise<unknown>
    await expect(call({ a: 1 })).resolves.toEqual({ ok: false, error: { code: 'not-implemented' } })
    expect(plain.calls).toEqual([{ name, payload: { a: 1 } }])

    const stubbed = stubClu({ [name]: () => 'value' })
    await expect((method(stubbed.clu, name) as (p: unknown) => Promise<unknown>)(undefined)).resolves.toEqual({ ok: true, value: 'value' })
  })
})
