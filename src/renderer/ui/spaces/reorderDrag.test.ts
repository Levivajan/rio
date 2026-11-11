import { describe, expect, it } from 'vitest'
import { dropGap, reorderTarget } from './reorderDrag'

const row = [0, 1, 2, 3].map((index) => ({ index, left: index * 28, right: index * 28 + 28 }))

describe('switcher drag reorder', () => {
  it('maps the pointer to the gap before the first icon whose center is to its right', () => {
    expect(dropGap(-50, row)).toBe(0)
    expect(dropGap(13, row)).toBe(0)
    expect(dropGap(15, row)).toBe(1)
    expect(dropGap(60, row)).toBe(2)
    expect(dropGap(500, row)).toBe(4)
  })
  it('uses the real indexes of the shown icons when the row is windowed', () => {
    const windowed = [{ index: 7, left: 0, right: 14 }, { index: 8, left: 14, right: 42 }, { index: 9, left: 42, right: 56 }]
    expect(dropGap(1, windowed)).toBe(7)
    expect(dropGap(100, windowed)).toBe(10)
  })
  it('ignores junk input', () => {
    expect(dropGap(Number.NaN, row)).toBeNull()
    expect(dropGap(10, [])).toBeNull()
    expect(reorderTarget(-1, 2)).toBeNull()
    expect(reorderTarget(1, null)).toBeNull()
  })
  it('turns a gap into a reorder index, and the gaps on either side of the icon into no move', () => {
    // Icon 3 (index 2) before icon 1.
    expect(reorderTarget(2, 0)).toBe(0)
    expect(reorderTarget(0, 4)).toBe(3)
    expect(reorderTarget(0, 2)).toBe(1)
    expect(reorderTarget(1, 1)).toBeNull()
    expect(reorderTarget(1, 2)).toBeNull()
  })
})
