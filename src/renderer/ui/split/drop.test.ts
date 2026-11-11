import { describe, expect, it } from 'vitest'
import type { SplitView } from '../../../shared/split'
import { EDGE_BAND_MAX, dropTargets, edgeBand, hitEdge, hitSwap, hitTabDrop, sameHit } from './drop'

const pane = { x: 100, y: 50, width: 400, height: 800 }

describe('edgeBand', () => {
  it('is 25% of the side, capped at 160 DIP, and 0 for junk sizes', () => {
    expect(edgeBand(400)).toBe(100)
    expect(edgeBand(640)).toBe(160)
    expect(edgeBand(1600)).toBe(EDGE_BAND_MAX)
    expect(edgeBand(0)).toBe(0)
    expect(edgeBand(-5)).toBe(0)
    expect(edgeBand(Number.NaN)).toBe(0)
  })
})

describe('hitEdge', () => {
  it('finds each band at 25% of a narrow pane and nothing in the center', () => {
    // Bands: 100 DIP left and right, 160 (capped from 200) top and bottom.
    expect(hitEdge(pane, 199, 450)).toBe('left')
    expect(hitEdge(pane, 200, 450)).toBeNull()
    expect(hitEdge(pane, 401, 450)).toBe('right')
    expect(hitEdge(pane, 400, 450)).toBeNull()
    expect(hitEdge(pane, 300, 209)).toBe('top')
    expect(hitEdge(pane, 300, 210)).toBeNull()
    expect(hitEdge(pane, 300, 691)).toBe('bottom')
    expect(hitEdge(pane, 300, 450)).toBeNull()
  })

  it('caps the band at 160 DIP on a wide pane', () => {
    const wide = { x: 0, y: 0, width: 2000, height: 1000 }
    // 25% would be 500 DIP; the cap leaves the band at 160.
    expect(hitEdge(wide, 2000 - 159, 500)).toBe('right')
    expect(hitEdge(wide, 2000 - 161, 500)).toBeNull()
    expect(hitEdge(wide, 2000 - 400, 500)).toBeNull()
    expect(hitEdge(wide, 159, 500)).toBe('left')
    expect(hitEdge(wide, 161, 500)).toBeNull()
    expect(hitEdge(wide, 1000, 1000 - 150)).toBe('bottom')
  })

  it('gives a corner to the edge the point is relatively nearer to', () => {
    // 10 of a 100 DIP left band (0.1) against 40 of a 160 DIP top band (0.25).
    expect(hitEdge(pane, 110, 90)).toBe('left')
    expect(hitEdge(pane, 180, 60)).toBe('top')
  })

  it('returns null outside the rect, for junk points and for an empty rect', () => {
    expect(hitEdge(pane, 99, 450)).toBeNull()
    expect(hitEdge(pane, 500, 450)).toBeNull()
    expect(hitEdge(pane, Number.NaN, 450)).toBeNull()
    expect(hitEdge(pane, Infinity, 450)).toBeNull()
    expect(hitEdge({ x: 0, y: 0, width: 0, height: 0 }, 0, 0)).toBeNull()
  })
})

describe('drop targets', () => {
  const view: SplitView = {
    splitId: 's',
    panes: [
      { tabId: 'a', bar: { x: 0, y: 0, width: 496, height: 24 }, rect: { x: 0, y: 24, width: 496, height: 776 } },
      { tabId: 'b', bar: { x: 504, y: 0, width: 496, height: 24 }, rect: { x: 504, y: 24, width: 496, height: 776 } }
    ],
    dividers: []
  }
  const page = { x: 0, y: 0, width: 1000, height: 800 }

  it('uses each pane card (bar and page) in a split, or the active tab at the page rect', () => {
    expect(dropTargets(view, page, 'a')).toEqual([
      { tabId: 'a', rect: { x: 0, y: 0, width: 496, height: 800 } },
      { tabId: 'b', rect: { x: 504, y: 0, width: 496, height: 800 } }
    ])
    expect(dropTargets(null, page, 'a')).toEqual([{ tabId: 'a', rect: page }])
    expect(dropTargets(null, page, null)).toEqual([])
    expect(dropTargets(null, undefined, 'a')).toEqual([])
  })

  it('hits a band of the pane under the pointer, never the dragged tab, and nothing in the gap or center', () => {
    const targets = dropTargets(view, page, 'a')
    expect(hitTabDrop(targets, 990, 400, 'c')).toEqual({ targetTabId: 'b', edge: 'right' })
    expect(hitTabDrop(targets, 700, 790, 'c')).toEqual({ targetTabId: 'b', edge: 'bottom' })
    expect(hitTabDrop(targets, 750, 400, 'c')).toBeNull()
    expect(hitTabDrop(targets, 500, 400, 'c')).toBeNull()
    expect(hitTabDrop(targets, 990, 400, 'b')).toBeNull()
    expect(hitTabDrop(targets, -10, 400, 'c')).toBeNull()
  })

  it('swaps onto any point of another pane', () => {
    const targets = dropTargets(view, page, 'a')
    expect(hitSwap(targets, 200, 400, 'b')).toEqual({ targetTabId: 'a', edge: null })
    expect(hitSwap(targets, 700, 400, 'b')).toBeNull()
    expect(hitSwap(targets, 500, 400, 'b')).toBeNull()
  })

  it('compares hits by target and edge', () => {
    expect(sameHit(null, null)).toBe(true)
    expect(sameHit({ targetTabId: 'a', edge: 'left' }, { targetTabId: 'a', edge: 'left' })).toBe(true)
    expect(sameHit({ targetTabId: 'a', edge: 'left' }, { targetTabId: 'a', edge: 'right' })).toBe(false)
    expect(sameHit({ targetTabId: 'a', edge: null }, null)).toBe(false)
  })
})
