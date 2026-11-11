import { describe, expect, it } from 'vitest'
import { z } from 'zod'
import { contract, SURFACES } from './ipc'

const UUID = '22222222-2222-4222-8222-222222222222'

describe('IPC contract', () => {
  it('gives every command a strict schema and an allow list', () => {
    for (const [name, def] of Object.entries(contract)) {
      expect(def.allow.length, name).toBeGreaterThan(0)
      for (const s of def.allow) expect(SURFACES, name).toContain(s)
      // A strict object (or union of strict objects) rejects an unknown key.
      const extra = def.input.safeParse({ __extra: 1 })
      expect(extra.success, `${name} accepted an extra key`).toBe(false)
    }
  })
  it('names are area.verb', () => {
    for (const name of Object.keys(contract)) expect(name).toMatch(/^[a-z][a-zA-Z]*\.[a-z][a-zA-Z0-9]*$/)
  })
  const reject = (name: keyof typeof contract, payload: unknown) => expect(contract[name].input.safeParse(payload).success).toBe(false)
  it('rejects NaN, over-length strings and non-UUID ids per area', () => {
    reject('shell.setSidebarWidth', { width: Number.NaN })
    reject('split.resize', { splitId: UUID, path: [], sizes: [0.5, Number.NaN] })
    reject('tabs.create', { url: 'x'.repeat(33 * 1024) })
    reject('tabs.activate', { tabId: 'not-a-uuid' })
    reject('spaces.switch', { spaceId: 'nope' })
    reject('find.update', { text: 'x'.repeat(1001) })
    reject('history.query', { text: 'x'.repeat(201) })
    reject('settings.set', { key: 'appearance.mode', scope: 'not-global', value: 'dark' })
    reject('command.execute', { sessionId: UUID, result: { kind: 'navigate', url: 1 }, disposition: 'default', input: '' })
    reject('tweaks.previewControls', { tabId: UUID, controls: { hue: 999, invert: false, contrast: 1, brightness: 1, saturation: 1, fontFamily: null, fontScale: 1 } })
    reject('perf.mark', { name: 'main:entry', at: 1 })
  })
  it('accepts valid payloads', () => {
    expect(contract['tabs.create'].input.safeParse({ url: 'https://a.test' }).success).toBe(true)
    expect(contract['spaces.switch'].input.safeParse({ index: 3 }).success).toBe(true)
    expect(contract['state.get'].input.safeParse({}).success).toBe(true)
    expect(z.object({}).strict().safeParse({}).success).toBe(true)
  })
})
