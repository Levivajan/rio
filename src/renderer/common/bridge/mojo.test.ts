import { describe, expect, it } from 'vitest'
import type { MojoBindings, MojoCallResult, MojoRemote, MojoSurfaceInfo } from './mojo-bindings'
import { ContractMismatchError, createMojoBridge, decodeResult, parseStateMessage } from './mojo'
import { CONTRACT_VERSION } from './mojo-wire.gen'

const TAB = '00000000-0000-4000-8000-000000000001'
const SPACE = '00000000-0000-4000-8000-0000000000a1'

/** Bindings shaped like Chromium's generated mojom-webui.js: one-key unions, numeric enums, `{ result }` replies. */
function fakeBindings(opts: { info?: Partial<MojoSurfaceInfo>; reply?: (iface: string, method: string, input: unknown) => MojoCallResult; early?: string[] } = {}) {
  const sent: { file: string; iface: string; method: string; args: unknown[] }[] = []
  let push: (json: string) => void = () => {}
  const bindings: MojoBindings = {
    getRemote: (file, iface) =>
      new Proxy(
        {},
        {
          get: (_t, method: string) => async (...args: unknown[]) => {
            sent.push({ file, iface, method, args })
            return { result: opts.reply?.(iface, method, args[0]) ?? { valueJson: 'null' } }
          }
        }
      ),
    async subscribeState(onMessage) {
      push = onMessage
      for (const m of opts.early ?? []) onMessage(m)
      return { info: { windowId: 'w1', layer: null, slot: null, contractVersion: CONTRACT_VERSION, ...opts.info } }
    }
  }
  return { bindings, sent, push: (json: string) => push(json) }
}

describe('MojoBridge commands', () => {
  it('sends each command to its area interface with the payload in mojom shape', async () => {
    const f = fakeBindings()
    const clu = await createMojoBridge(f.bindings)
    await clu.tabs.move({ nodeId: TAB, target: { section: 'today', spaceId: SPACE, index: 0 } })
    await clu.theme.setMode({ mode: 'light' })
    await clu.state.get()
    expect(f.sent).toEqual([
      { file: 'tabs', iface: 'TabsHandler', method: 'move', args: [{ nodeId: TAB, target: { today: { section: 'today', spaceId: SPACE, index: 0 } }, confirmed: null, ask: null, drop: null }] },
      { file: 'theme', iface: 'ThemeHandler', method: 'setMode', args: [{ mode: 1 }] },
      { file: 'core', iface: 'StateHandler', method: 'get', args: [] }
    ])
  })

  it('answers bad-payload without a Mojo call when the payload cannot have the mojom shape', async () => {
    const f = fakeBindings()
    const clu = await createMojoBridge(f.bindings)
    const bad = clu.theme.setMode as unknown as (p: unknown) => Promise<unknown>
    expect(await bad({ mode: 'sepia' })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    expect(await (clu.state.get as unknown as (p: unknown) => Promise<unknown>)({ junk: 1 })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    expect(f.sent).toEqual([])
  })

  it('decodes values, error codes and junk replies', async () => {
    const f = fakeBindings({
      reply: (_i, method) =>
        method === 'create'
          ? { valueJson: '{"tabId":"t"}' }
          : method === 'close'
            ? { error: { code: 9, message: null, reason: 'last-space' } }
            : { valueJson: '{not json' }
    })
    const clu = await createMojoBridge(f.bindings)
    expect(await clu.tabs.create({ url: 'https://example.com/' })).toEqual({ ok: true, value: { tabId: 't' } })
    expect(await clu.tabs.close({})).toEqual({ ok: false, error: { code: 'rejected', reason: 'last-space' } })
    expect(await clu.tabs.duplicate({})).toMatchObject({ ok: false, error: { code: 'rejected' } })
    expect(decodeResult({ error: { code: 6, message: 'x', reason: null } })).toEqual({ ok: false, error: { code: 'not-implemented', message: 'x' } })
    expect(decodeResult({ error: { code: 99, message: null, reason: null } })).toMatchObject({ ok: false, error: { code: 'rejected' } })
    expect(decodeResult(null)).toMatchObject({ ok: false })
  })

  it('calls each method on its remote, as the generated remote classes need', async () => {
    const f = fakeBindings()
    class TabsHandlerRemote {
      sent: unknown[] = []
      async activate(input: unknown) {
        this.sent.push(input)
        return { result: { valueJson: 'null' } }
      }
    }
    const remote = new TabsHandlerRemote()
    f.bindings.getRemote = () => remote as unknown as MojoRemote
    const clu = await createMojoBridge(f.bindings)
    expect(await clu.tabs.activate({ tabId: TAB })).toEqual({ ok: true, value: null })
    expect(remote.sent).toEqual([{ tabId: TAB }])
  })

  it('turns a closed pipe into a rejected result, never a throw', async () => {
    const f = fakeBindings()
    f.bindings.getRemote = () => ({ activate: async () => Promise.reject(new Error('pipe closed')) })
    const clu = await createMojoBridge(f.bindings)
    expect(await clu.tabs.activate({ tabId: TAB })).toMatchObject({ ok: false, error: { code: 'rejected' } })
  })
})

describe('MojoBridge state and surface', () => {
  it('replays messages that arrived before the store subscribed, then streams, dropping junk', async () => {
    const snapshot = JSON.stringify({ kind: 'snapshot', rev: 3, state: { rev: 3 } })
    const f = fakeBindings({ early: [snapshot, '{oops', JSON.stringify({ kind: 'patch', rev: 4, ops: [] })] })
    const clu = await createMojoBridge(f.bindings)
    const got: unknown[] = []
    const off = clu.onState((m) => got.push(m.rev))
    f.push(JSON.stringify({ kind: 'patch', rev: 5, ops: [] }))
    f.push(JSON.stringify({ kind: 'patch', rev: 'six', ops: [] }))
    off()
    f.push(JSON.stringify({ kind: 'patch', rev: 7, ops: [] }))
    expect(got).toEqual([3, 4, 5])
  })

  it('takes windowId, layer and slot from SurfaceInfo, and refuses another contract version', async () => {
    const overlay = await createMojoBridge(fakeBindings({ info: { layer: 2, slot: 1 } }).bindings)
    expect([overlay.windowId, overlay.layer, overlay.slot]).toEqual(['w1', 'passive', 1])
    const peek = await createMojoBridge(fakeBindings({ info: { layer: 2, slot: 3 } }).bindings)
    expect(peek.slot).toBe(3)
    const junk = await createMojoBridge(fakeBindings({ info: { layer: 7, slot: 9 } }).bindings)
    expect([junk.layer, junk.slot]).toEqual([null, null])
    for (const slot of [4, -1, 1.5]) {
      expect((await createMojoBridge(fakeBindings({ info: { layer: 2, slot } }).bindings)).slot).toBeNull()
    }
    await expect(createMojoBridge(fakeBindings({ info: { contractVersion: 'old' } }).bindings)).rejects.toThrow(ContractMismatchError)
  })

  it('parses only well-formed state messages', () => {
    expect(parseStateMessage('{"kind":"patch","rev":1,"ops":[]}')).toEqual({ kind: 'patch', rev: 1, ops: [] })
    for (const bad of ['', 'null', '[]', '{"kind":"patch","rev":1}', '{"kind":"snapshot","rev":1.5,"state":{}}', '{"kind":"x","rev":1}']) expect(parseStateMessage(bad)).toBeNull()
  })
})
