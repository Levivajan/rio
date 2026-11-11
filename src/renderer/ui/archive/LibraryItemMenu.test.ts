import { describe, expect, it } from 'vitest'
import { itemMenuTop } from './LibraryItemMenu'

describe('itemMenuTop', () => {
  it('opens below the pointer when the menu fits above the window bottom', () => {
    expect(itemMenuTop(100, 200, 800)).toBe(100)
    expect(itemMenuTop(596, 200, 800)).toBe(596)
  })

  it('opens upward from the pointer near the bottom, as the pop-up rows above the bottom bar are', () => {
    expect(itemMenuTop(682, 200, 800)).toBe(482)
    expect(itemMenuTop(790, 236, 760)).toBe(520)
  })

  it('stays inside a window shorter than the menu', () => {
    expect(itemMenuTop(150, 236, 200)).toBe(4)
  })
})
