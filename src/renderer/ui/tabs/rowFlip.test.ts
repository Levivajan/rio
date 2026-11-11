import { describe, expect, it } from 'vitest'
import { FOLDER_COLLAPSE, FOLDER_EXPAND, flipTiming, PINNED_COLLAPSE, PINNED_EXPAND, ROW_MOVE } from './rowFlip'

const shape = (folders: Record<string, boolean>, pinnedCollapsed = false) => ({ folders, pinnedCollapsed })

describe('row motion timings', () => {
  it('moves rows in 110 ms when nothing opened or closed (pin, unpin, close, reorder)', () => {
    expect(flipTiming(shape({ f: false }), shape({ f: false }))).toBe(ROW_MOVE)
    expect(ROW_MOVE.ms).toBe(110)
  })

  it('takes 190 ms to expand a folder and 225 ms to collapse one', () => {
    expect(flipTiming(shape({ f: true }), shape({ f: false }))).toBe(FOLDER_EXPAND)
    expect(flipTiming(shape({ f: false, g: true }), shape({ f: true, g: false }))).toBe(FOLDER_COLLAPSE)
    expect([FOLDER_EXPAND.ms, FOLDER_COLLAPSE.ms]).toEqual([190, 225])
  })

  it('takes 95 ms to collapse the pinned section and 190 ms to expand it', () => {
    expect(flipTiming(shape({}, false), shape({}, true))).toBe(PINNED_COLLAPSE)
    expect(flipTiming(shape({}, true), shape({}, false))).toBe(PINNED_EXPAND)
    expect([PINNED_COLLAPSE.ms, PINNED_EXPAND.ms]).toEqual([95, 190])
  })

  it('ignores a folder that just appeared or went away', () => {
    expect(flipTiming(shape({}), shape({ f: true }))).toBe(ROW_MOVE)
    expect(flipTiming(shape({ f: true }), shape({}))).toBe(ROW_MOVE)
  })

  it('every easing starts at 0 and ends at 1', () => {
    for (const t of [ROW_MOVE, FOLDER_EXPAND, FOLDER_COLLAPSE, PINNED_COLLAPSE, PINNED_EXPAND]) {
      expect(t.easing.startsWith('linear(0,')).toBe(true)
      expect(t.easing.endsWith(', 1)')).toBe(true)
    }
  })
})
