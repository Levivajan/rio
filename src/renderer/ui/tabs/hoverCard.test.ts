import { afterEach, describe, expect, it, vi } from 'vitest'
import { stubClu } from '../../../test-utils/stubClu'
import { HOVER_HOLD_MS, hoverEnter, hoverLeave, hoverPointerLeave } from './hoverCard'

const TAB = '00000000-0000-4000-8000-00000000000a'

function setup() {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: '00000000-0000-4000-8000-000000000001' }
  const el = document.createElement('div')
  document.body.append(el)
  const hovers = () => stub.calls.filter((c) => c.name === 'tabs.hover').map((c) => c.payload as { tabId: string | null; anchor?: unknown })
  return { el, hovers }
}

afterEach(() => {
  hoverLeave()
  vi.useRealTimers()
  document.body.replaceChildren()
})

// The card that holds can release its own hold (the pointer crossed it) while the tile's leave is still pending.
describe('hoverEnter back on a tile within the hold', () => {
  it('asks again for a holding card, which may have faded meanwhile', () => {
    vi.useFakeTimers()
    const { el, hovers } = setup()
    const at = { x: 120, y: 80 }
    hoverEnter(TAB, el, at, 230, true)
    vi.advanceTimersByTime(230)
    expect(hovers()).toEqual([{ tabId: TAB, anchor: { x: 120, y: 80, width: 0, height: 0 } }])
    hoverPointerLeave()
    hoverEnter(TAB, el, at, 230, true)
    expect(hovers()).toEqual([
      { tabId: TAB, anchor: { x: 120, y: 80, width: 0, height: 0 } },
      { tabId: TAB, anchor: { x: 120, y: 80, width: 0, height: 0 } }
    ])
    // The pending close was cancelled.
    vi.advanceTimersByTime(HOVER_HOLD_MS + 230)
    expect(hovers().map((h) => h.tabId)).toEqual([TAB, TAB])
  })

  it('sends nothing again for a card that does not hold', () => {
    vi.useFakeTimers()
    const { el, hovers } = setup()
    hoverEnter(TAB, el, { x: 10, y: 10 }, 230, false)
    vi.advanceTimersByTime(230)
    hoverPointerLeave()
    expect(hovers().map((h) => h.tabId)).toEqual([TAB, null])
    hoverEnter(TAB, el, { x: 10, y: 10 }, 230, false)
    vi.advanceTimersByTime(230)
    expect(hovers().map((h) => h.tabId)).toEqual([TAB, null, TAB])
  })
})
