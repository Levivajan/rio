import { describe, expect, it } from 'vitest'
import { dragSizes } from './drag'

describe('dragSizes', () => {
  it('turns 100 DIP of travel into share traded between the two neighbors', () => {
    const next = dragSizes([0.5, 0.5], 0, 100, 1000)
    expect(next[0]).toBeCloseTo(0.6)
    expect(next[1]).toBeCloseTo(0.4)
    expect(next.reduce((x, y) => x + y, 0)).toBeCloseTo(1)
  })

  it('leaves other children alone and keeps every share inside (0, 1)', () => {
    const next = dragSizes([0.2, 0.3, 0.5], 1, 10_000, 1000)
    expect(next[0]).toBe(0.2)
    expect(next[1]).toBeGreaterThan(0)
    expect(next[2]).toBeGreaterThan(0)
    expect((next[1] ?? 0) + (next[2] ?? 0)).toBeCloseTo(0.8)
  })

  it('ignores junk input', () => {
    expect(dragSizes([0.5, 0.5], 3, 10, 1000)).toEqual([0.5, 0.5])
    expect(dragSizes([0.5, 0.5], 0, Number.NaN, 1000)).toEqual([0.5, 0.5])
    expect(dragSizes([0.5, 0.5], 0, 10, 0)).toEqual([0.5, 0.5])
  })
})
