import { describe, expect, it } from 'vitest'
import { GRAIN_SD, TILE_SD, grainAlpha } from './grain'

describe('grainAlpha', () => {
  it('never draws zero grain: the least is the design sd 0.83, the most about 4.76 on a mid sidebar', () => {
    expect(grainAlpha(0) * TILE_SD).toBeCloseTo(0.83, 2)
    expect(grainAlpha(0.94) * TILE_SD).toBeCloseTo(4.76, 0)
    expect(grainAlpha(1) * TILE_SD).toBeCloseTo(GRAIN_SD.min + GRAIN_SD.span, 2)
  })

  it('tracks the grain curve between the ends within about 0.2 levels', () => {
    // sd 1.54 at 90 degrees, 2.64 at 184, 3.80 at 270, read as values on the 15 to 345 degree scale.
    for (const [deg, sd] of [[90, 1.54], [184, 2.64], [270, 3.8]] as const) {
      expect(Math.abs(grainAlpha((deg - 15) / 330) * TILE_SD - sd)).toBeLessThan(0.25)
    }
  })

  it('grows with grain and reads junk as the least', () => {
    expect(grainAlpha(0.5)).toBeGreaterThan(grainAlpha(0.4))
    expect(grainAlpha(Number.NaN)).toBe(grainAlpha(0))
    expect(grainAlpha(7)).toBe(grainAlpha(1))
  })
})
