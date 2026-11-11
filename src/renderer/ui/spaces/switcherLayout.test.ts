import { describe, expect, it } from 'vitest'
import { DOT_SLOT, ICON_SLOT, switcherLayout } from './switcherLayout'

const kinds = (l: ReturnType<typeof switcherLayout>) => l.items.map((i) => `${i.index}${i.kind === 'icon' ? 'I' : '.'}`).join(' ')

describe('switcherLayout', () => {
  it('keeps every icon for 5 spaces at the default width', () => {
    // A 213 wide sidebar leaves about 131 between the Library and +.
    expect(kinds(switcherLayout(5, 2, 131))).toBe('0I 1I 2I 3I 4I')
    // The narrowest sidebar (164) still shows 5 icons at the minimum boxes.
    expect(kinds(switcherLayout(5, 0, 82))).toBe('0I 1I 2I 3I 4I')
  })
  it('collapses inactive spaces to dots only once the minimum boxes do not fit', () => {
    expect(kinds(switcherLayout(10, 4, ICON_SLOT + 9 * DOT_SLOT))).toBe('0. 1. 2. 3. 4I 5. 6. 7. 8. 9.')
  })
  it('never adds an overflow button and keeps the active space with its neighbors', () => {
    const width = ICON_SLOT + 4 * DOT_SLOT
    expect(kinds(switcherLayout(25, 12, width))).toBe('10. 11. 12I 13. 14.')
    expect(kinds(switcherLayout(25, 0, width))).toBe('0I 1. 2. 3. 4.')
    expect(kinds(switcherLayout(25, 24, width))).toBe('20. 21. 22. 23. 24I')
    expect(switcherLayout(25, 12, width)).not.toHaveProperty('overflow')
  })
  it('handles junk input without throwing', () => {
    expect(switcherLayout(0, 0, 200)).toEqual({ items: [] })
    expect(kinds(switcherLayout(3, -1, 200))).toBe('0I 1I 2I')
    expect(kinds(switcherLayout(3, 7, Number.NaN))).toBe('0I')
    expect(switcherLayout(Number.POSITIVE_INFINITY, 0, 100).items).toEqual([])
  })
})
