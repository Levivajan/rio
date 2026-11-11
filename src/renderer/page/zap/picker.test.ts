// @vitest-environment jsdom
// The injected picker source (tweaks-t2): self-contained, closed host, ignores forged input, stops cleanly.
import { afterEach, describe, expect, it } from 'vitest'
import { pickScript, STOP_SCRIPT, updateScript, type PickResult } from './picker'

// The page sees the picker only as source text, so run it that way.
const run = (code: string): unknown => (0, eval)(code)
const settled = async (p: Promise<PickResult>) => Promise.race([p, new Promise((r) => setTimeout(() => r('pending'), 20))])

afterEach(() => {
  run(STOP_SCRIPT)
  document.body.innerHTML = ''
})

describe('zap picker', () => {
  it('installs one closed host, shows the count and ignores page-dispatched clicks and keys', async () => {
    document.body.innerHTML = '<div id="promo">Promo</div>'
    let clicks = 0
    document.addEventListener('click', () => clicks++)
    const pick = run(pickScript({ count: 1, message: null })) as Promise<PickResult>
    expect(pick).toBeInstanceOf(Promise)
    const hosts = Array.from(document.documentElement.children).filter((e) => e !== document.head && e !== document.body)
    expect(hosts).toHaveLength(1)
    expect((hosts[0] as HTMLElement).shadowRoot).toBeNull()
    expect(document.documentElement.textContent).not.toContain('hidden')

    document.getElementById('promo')?.dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: 1, clientY: 1 }))
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))
    expect(await settled(pick)).toBe('pending')
    // Forged events are not swallowed either: the page sees its own click.
    expect(clicks).toBe(1)

    // Running the script again reuses the host.
    run(updateScript({ count: 2, message: 'Hid 3 related elements' }))
    run(pickScript({ count: 2, message: null }))
    expect(Array.from(document.documentElement.children).filter((e) => e !== document.head && e !== document.body)).toHaveLength(1)
  })

  it('resolves the pending pick as stopped and removes the host on stop', async () => {
    const pick = run(pickScript({ count: 0, message: null })) as Promise<PickResult>
    run(STOP_SCRIPT)
    expect(await pick).toEqual({ kind: 'stopped' })
    expect(Array.from(document.documentElement.children).filter((e) => e !== document.head && e !== document.body)).toHaveLength(0)
    expect((globalThis as { __cluZap?: unknown }).__cluZap).toBeUndefined()
  })

  it('puts a removed host back', async () => {
    run(pickScript({ count: 0, message: null }))
    const host = Array.from(document.documentElement.children).find((e) => e !== document.head && e !== document.body) as Element
    host.remove()
    await new Promise((r) => setTimeout(r, 0))
    expect(host.isConnected).toBe(true)
  })
})
