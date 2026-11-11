// Frame theme layer (theme-t2): the active space's stops, the swipe interpolation and the grain tile.
import { describe, expect, it } from 'vitest'
import { colorRgb, defaultThemeFor, hexToRgb, mixRgb, rgbToHex, rgbToHsl, themeStops, type Rgb } from '../../../shared/theme/space'
import type { SpaceTheme } from '../../../shared/theme/types'
import { GRAIN_TILE, grainPixels, seededRandom } from './grain'
import { frameLayer, mainTint, selectionTint, sidebarTint, strongAccent, type LayerInput, type LayerSpace } from './layer'

const ember: SpaceTheme = { colors: [{ h: 12, s: 0.82 }, { h: 36, s: 0.88 }], intensity: 1, grain: 0.4 }
const a: LayerSpace = { theme: null, color: 'blue' }
const b: LayerSpace = { theme: ember, color: 'red' }
const base: LayerInput = { active: a, swipe: null, resolved: 'light', reduceTransparency: false, focused: true }
const hex = (stops: Rgb[]) => stops.map(rgbToHex)

describe('frameLayer', () => {
  it('shows the active space theme, or the default from its color without one', () => {
    expect(frameLayer(base).stops).toEqual(hex(themeStops(defaultThemeFor('blue'), 'light', true)))
    const dark = frameLayer({ ...base, active: b, resolved: 'dark' })
    expect(dark.stops).toEqual(hex(themeStops(ember, 'dark')))
    // Opaque in every state (M11): the tint never shows the desktop.
    expect(dark).toMatchObject({ alpha: 1, grain: 0.4 })
    expect(frameLayer({ ...base, focused: false }).alpha).toBe(1)
    expect(frameLayer({ ...base, reduceTransparency: true }).alpha).toBe(1)
    expect(frameLayer({ ...base, active: undefined }).stops).toHaveLength(3)
  })

  it('interpolates stops, alpha and grain by the swipe fraction', () => {
    const from = themeStops(defaultThemeFor('blue'), 'light', true)
    const to = themeStops(ember, 'light')
    const half = frameLayer({ ...base, swipe: { target: b, fraction: 0.5 } })
    expect(half.stops).toEqual(from.map((c, i) => rgbToHex(mixRgb(c, to[i] as Rgb, 0.5))))
    expect(half.grain).toBe(0.325)
    expect(half.alpha).toBe(1)
    // The direction's sign does not matter; past the end clamps at the target.
    expect(frameLayer({ ...base, swipe: { target: b, fraction: -0.5 } })).toEqual(half)
    expect(frameLayer({ ...base, swipe: { target: b, fraction: 3 } }).stops).toEqual(hex(to))
    expect(frameLayer({ ...base, swipe: { target: b, fraction: 0 } })).toEqual(frameLayer(base))
  })

  it('ignores a swipe with no target or a junk fraction', () => {
    expect(frameLayer({ ...base, swipe: { target: undefined, fraction: 0.5 } })).toEqual(frameLayer(base))
    expect(frameLayer({ ...base, swipe: { target: b, fraction: Number.NaN } })).toEqual(frameLayer(base))
    const mid = frameLayer({ ...base, swipe: { target: b, fraction: 0.5 } }).stops[1] as string
    expect(hexToRgb(mid)).not.toBeNull()
  })
})

describe('mainTint', () => {
  // A space with a green main color, orange second color. The UI tints with the green, never the orange or the midpoint.
  const green = { h: 100, s: 0.5 }
  const orange = { h: 25, s: 0.62 }
  const violet = { h: 270, s: 0.5 }
  const themed = (colors: SpaceTheme['colors']): LayerInput => ({ ...base, active: { theme: { colors, intensity: 1, grain: 0 }, color: 'green' } })

  it('is the main (first) color for one, two and three color themes, in both modes', () => {
    for (const resolved of ['light', 'dark'] as const) {
      const main = rgbToHex(colorRgb(green, resolved, 1))
      expect(mainTint(frameLayer({ ...themed([green]), resolved }))).toBe(main)
      const two = frameLayer({ ...themed([green, orange]), resolved })
      expect(mainTint(two)).toBe(main)
      expect(mainTint(two)).not.toBe(two.stops[1])
      expect(mainTint(two)).not.toBe(rgbToHex(colorRgb(orange, resolved, 1)))
      expect(mainTint(frameLayer({ ...themed([green, orange, violet]), resolved }))).toBe(main)
    }
  })

  it('follows a space switch: the tint is the target main color once the swipe ends', () => {
    const to: LayerSpace = { theme: { colors: [orange, green], intensity: 1, grain: 0 }, color: 'orange' }
    const done = frameLayer({ ...themed([green, orange]), swipe: { target: to, fraction: 1 } })
    expect(mainTint(done)).toBe(rgbToHex(colorRgb(orange, 'light', 1)))
  })
})

describe('strongAccent', () => {
  it("keeps the main color's hue at the default theme's lightness, and rejects junk", () => {
    // The default theme: accent #9095fb, strong #2028f2 (light) and #6a70ff (dark).
    const near = (a: string | null, b: string) => {
      const x = hexToRgb(a ?? '') as Rgb
      const y = hexToRgb(b) as Rgb
      return Math.max(...x.map((v, i) => Math.abs(v - (y[i] as number))))
    }
    expect(near(strongAccent('#9095fb', true), '#2028f2')).toBeLessThanOrEqual(12)
    expect(near(strongAccent('#9095fb', false), '#6a70ff')).toBeLessThanOrEqual(12)
    const greenStrong = strongAccent('#5fae3c', true) as string
    expect(Math.abs(rgbToHsl(hexToRgb(greenStrong) as Rgb).h - rgbToHsl([0x5f, 0xae, 0x3c]).h)).toBeLessThan(2)
    expect(strongAccent('orange', true)).toBeNull()
    expect(strongAccent('', false)).toBeNull()
  })
})

describe('grain', () => {
  it('draws the same opaque gray tile every time', () => {
    const one = grainPixels()
    expect(one).toHaveLength(GRAIN_TILE * GRAIN_TILE * 4)
    expect(grainPixels()).toEqual(one)
    for (let i = 0; i < 64; i += 4) {
      expect(one[i]).toBe(one[i + 1])
      expect(one[i + 3]).toBe(255)
    }
    const r = seededRandom(1)
    const values = Array.from({ length: 1000 }, r)
    expect(Math.min(...values)).toBeGreaterThanOrEqual(0)
    expect(Math.max(...values)).toBeLessThan(1)
    expect(new Set(values).size).toBeGreaterThan(990)
  })
})

describe('selectionTint', () => {
  const off = (got: string | null, want: Rgb) => Math.max(...(hexToRgb(got ?? '') as Rgb).map((c, i) => Math.abs(c - (want[i] as number))))
  it('keeps the selected row tone on a light and a dark space and mutes a neon accent', () => {
    // A light pink space selects in #dd808d and a dark red one in #9b3a45.
    expect(off(selectionTint('#dd808d', true), [0xdd, 0x80, 0x8d])).toBeLessThanOrEqual(3)
    expect(off(selectionTint('#9b3a45', false), [0x9b, 0x3a, 0x45])).toBeLessThanOrEqual(1)
    // A neon green (18, 237, 54) comes out a muted green.
    const neon = rgbToHsl(hexToRgb(selectionTint('#12ed36', true) ?? '') as Rgb)
    expect(neon.s).toBeLessThanOrEqual(0.59)
    expect(neon.l).toBeCloseTo(0.68, 2)
    expect(selectionTint('nope', true)).toBeNull()
  })
})

describe('sidebarTint', () => {
  it('is the middle stop, or the corners\' midpoint with three colors', () => {
    expect(sidebarTint(['#000000', '#808080', '#ffffff'], 2)).toBe('#808080')
    expect(sidebarTint(['#000000', '#123456', '#ffffff'], 3)).toBe(rgbToHex(mixRgb([0, 0, 0], [255, 255, 255], 0.5)))
  })
})
