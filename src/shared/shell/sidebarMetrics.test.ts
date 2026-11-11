import { describe, expect, it } from 'vitest'
import { favoriteColumns, pillPlaceholder, spacePitch, topRowMode } from './sidebarMetrics'

describe('sidebar breakpoints', () => {
  it('folds the toggle and Reload into "..." up to 211 and shows them from 212', () => {
    expect(topRowMode(164)).toBe('compact')
    expect(topRowMode(211)).toBe('compact')
    expect(topRowMode(212)).toBe('full')
    expect(topRowMode(500)).toBe('full')
    expect(topRowMode(Number.NaN)).toBe('full')
  })

  it('reads the short placeholder below 228 and the long one from 228', () => {
    expect(pillPlaceholder(227)).toBe('Search...')
    expect(pillPlaceholder(228)).toBe('Search or Enter URL...')
  })

  it('takes the measured favorites columns at every measured width', () => {
    const measured: [number, number][] = [[164, 2], [172, 2], [173, 3], [213, 3], [227, 3], [228, 4], [296, 4], [500, 4]]
    for (const [w, n] of measured) expect(favoriteColumns(w)).toBe(n)
    expect(favoriteColumns(-5)).toBe(1)
    expect(favoriteColumns(Number.POSITIVE_INFINITY)).toBe(3)
  })

  it('keeps the 34 pitch while the spaces fit and shares the room evenly below it', () => {
    expect(spacePitch(300, 5)).toBe(34)
    expect(spacePitch(264, 5)).toBe(34)
    expect(spacePitch(213, 4)).toBeCloseTo(32.75, 2)
    expect(spacePitch(164, 5)).toBeCloseTo(16.4, 1)
    expect(spacePitch(214, 0)).toBe(34)
  })
})
