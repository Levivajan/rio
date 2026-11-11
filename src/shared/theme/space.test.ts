// Space theme model (theme-t2): HSL math, themeToCss, ink contrast on every preset, the validator and defaults.
import { channelDelta } from '../look/derive'
import { describe, expect, it } from 'vitest'
import { SPACE_COLOR_HEX } from '../spaces'
import { SPACE_COLORS } from '../types'
import { translucencyAlpha } from './appearance'
import { parseSpaceTheme } from './schema'
import {
  GLYPH_CONTRAST, THEME_PRESETS, contrast, defaultThemeFor, editorThemeFor, effectiveRgb, glyphOnTint, hexToRgb, hslToRgb, inkFor, mixRgb, rgbToHex,
  COLOR_SHARE, colorRgb, deepenAboveDefault, legacyColorRgb, oklabToRgb, rgbToHsl, rgbToOklab, spaceAccent, themeStops, themeToCss, type Rgb
} from './space'
import { swatchColor } from './picker'
import type { SpaceTheme } from './types'

const theme = (colors: [number, number][], intensity = 0.5, grain = 0): SpaceTheme => ({ colors: colors.map(([h, s]) => ({ h, s })), intensity, grain })

describe('HSL helpers', () => {
  it('converts the primaries, grays and wraps the hue', () => {
    expect(hslToRgb(0, 1, 0.5)).toEqual([255, 0, 0])
    expect(hslToRgb(120, 1, 0.5)).toEqual([0, 255, 0])
    expect(hslToRgb(240, 1, 0.5)).toEqual([0, 0, 255])
    expect(hslToRgb(0, 0, 0.9)).toEqual([230, 230, 230])
    expect(hslToRgb(360, 1, 0.5)).toEqual(hslToRgb(0, 1, 0.5))
    expect(hslToRgb(-120, 1, 0.5)).toEqual(hslToRgb(240, 1, 0.5))
  })

  it('round-trips through rgbToHsl and hex', () => {
    for (const hex of Object.values(SPACE_COLOR_HEX)) {
      const rgb = hexToRgb(hex) as Rgb
      const { h, s, l } = rgbToHsl(rgb)
      expect(h).toBeGreaterThanOrEqual(0)
      expect(h).toBeLessThan(360)
      expect(rgbToHex(hslToRgb(h, s, l))).toBe(hex)
    }
    expect(rgbToHsl([128, 128, 128])).toMatchObject({ h: 0, s: 0 })
    expect(hexToRgb('#12')).toBeNull()
    expect(hexToRgb('red')).toBeNull()
    expect(rgbToHex([300, -4, 15.6])).toBe('#ff0010')
  })

  it('mixes in sRGB and clamps t', () => {
    expect(mixRgb([0, 0, 0], [200, 100, 50], 0.5)).toEqual([100, 50, 25])
    expect(mixRgb([0, 0, 0], [200, 100, 50], 2)).toEqual([200, 100, 50])
  })
})

describe('themeToCss', () => {
  // Handle colors on the wheel and the sidebar each gives (Auto, dark macOS, one color).
  const STUDY: [number, number, string][] = [
    [0, 0, '#423f40'],
    [0, 0.5, '#ce3e3f'], // the edge sample is at the handle's clamp, 147.5 of 150
    [0, 0.983, '#e1b5b6'],
    [270, 0.5, '#863dcd'],
    [180, 0.5, '#3ecdcc'],
    [90, 0.5, '#84cc3d']
  ]

  it('paints a color: its wheel color mixed into gray, lighter toward the edge', () => {
    for (const [h, s, want] of STUDY) {
      const got = themeStops(theme([[h, s]]), 'dark')[0]
      expect(channelDelta(got, hexToRgb(want) as Rgb), `${h} ${s}`).toBeLessThanOrEqual(3)
    }
    // Light mode lifts the same color toward white.
    const dark = themeStops(theme([[0, 0.5]]), 'dark')[0]
    const light = themeStops(theme([[0, 0.5]]), 'light')[0]
    expect(light[1]).toBeGreaterThan(dark[1])
    expect(themeToCss(theme([[210, 0.5]]), 'light').accent).toBe(rgbToHex(hslToRgb(210, 0.5, 0.68)))
  })

  it('paints 2 and 3 colors as a 120 degree gradient, the design angle', () => {
    const two = theme([[0, 0.6], [200, 0.6]])
    const [a, , b] = themeStops(two, 'light').map(rgbToHex)
    expect(themeToCss(two, 'light').background).toBe(`linear-gradient(120deg, ${a}, ${b})`)
    const three = theme([[0, 0.6], [100, 0.6], [200, 0.6]])
    expect(themeToCss(three, 'dark').background).toBe(`linear-gradient(120deg, ${themeStops(three, 'dark').map(rgbToHex).join(', ')})`)
    expect(themeToCss(three, 'dark').accent).toBe(rgbToHex(hslToRgb(0, 0.58, 0.68)))
  })

  it('scales the color with intensity: low is a light pastel, high nearly the wheel color', () => {
    const at = (i: number) => themeStops(theme([[0, 0.5]], i), 'dark')[0]
    expect(channelDelta(at(0), [171, 171, 171])).toBeGreaterThan(20)
    expect(at(1)[0] - at(1)[1]).toBeGreaterThan(at(0.5)[0] - at(0.5)[1])
    expect(at(0.5)[0] - at(0.5)[1]).toBeGreaterThan(at(0)[0] - at(0)[1])
    expect(at(Number.NaN)).toEqual(at(0.5))
  })

  it('expands every theme to three stops', () => {
    const one = themeStops(theme([[10, 0.5]]), 'light')
    expect(one[1]).toEqual(one[0])
    expect(one[2]).toEqual(one[0])
    const [a, mid, b] = themeStops(theme([[0, 1], [240, 1]]), 'light')
    expect(mid).toEqual(mixRgb(a, b, 0.5))
  })

  it('keeps the ink readable on all 12 presets in light and dark (mid tones reach 2.5 at least)', () => {
    expect(THEME_PRESETS.map((p) => p.name)).toEqual(['Moss', 'Ember', 'Tide', 'Dusk', 'Sand', 'Plum', 'Slate', 'Coral', 'Fern', 'Ink', 'Honey', 'Frost'])
    for (const p of THEME_PRESETS) {
      expect(parseSpaceTheme(p.theme)).toEqual(p.theme)
      for (const mode of ['light', 'dark'] as const) {
        const css = themeToCss(p.theme, mode)
        for (const stop of themeStops(p.theme, mode)) {
          expect(contrast(hexToRgb(css.ink) as Rgb, stop), `${p.name} ${mode} ink`).toBeGreaterThanOrEqual(2.5)
        }
      }
    }
  })

  it('keeps the muted default look for spaces with no theme row', () => {
    const t = defaultThemeFor('blue')
    expect(themeStops(t, 'light', true)).not.toEqual(themeStops(t, 'light'))
    expect(themeToCss(t, 'dark', true).background).toContain('linear-gradient(120deg')
    void effectiveRgb
    void translucencyAlpha
  })

  it('inkFor picks the text color with more contrast', () => {
    expect(inkFor([250, 250, 250])).toBe('#1d1d1f')
    expect(inkFor([10, 10, 30])).toBe('#f2f2f7')
    expect(inkFor([[250, 250, 250], [240, 240, 240]])).toBe('#1d1d1f')
  })
})

describe('intensity slider (the minimum is a light pastel that keeps its color)', () => {
  const lch = (c: Rgb) => {
    const [L, a, b] = rgbToOklab(c)
    return { L, C: Math.hypot(a, b) }
  }
  const dist = (x: Rgb, y: Rgb) => Math.hypot(...rgbToOklab(x).map((v, k) => v - (rgbToOklab(y)[k] as number)))

  it('converts sRGB to OKLab and back', () => {
    expect(rgbToOklab([255, 255, 255])[0]).toBeCloseTo(1, 4)
    expect(rgbToOklab([0, 0, 0])[0]).toBeCloseTo(0, 6)
    for (const c of [[242, 182, 184], [229, 162, 189], [10, 200, 30], [255, 0, 0]] as Rgb[]) expect(oklabToRgb(rgbToOklab(c))).toEqual(c)
    // Out of gamut clips instead of returning junk.
    expect(oklabToRgb([1.2, 0.4, 0.4]).every((v) => v >= 0 && v <= 255)).toBe(true)
  })

  it('lands the pink swatch at the minimum near the design\'s light mode (#f2b6b8)', () => {
    const pink = swatchColor('#e59eba')
    const min = colorRgb(pink, 'light', 0)
    const mid = colorRgb(pink, 'light', 0.5)
    expect(dist(min, [242, 182, 184])).toBeLessThan(0.035)
    expect(lch(min).L - lch(mid).L).toBeCloseTo(0.054, 2)
    expect(lch(min).C / lch(mid).C).toBeGreaterThan(0.85)
    // The middle is unchanged (the 0.64 share).
    expect(COLOR_SHARE).toBe(0.64)
    expect(rgbToHex(mid)).toBe('#e5a2bd')
  })

  it('follows the design in dark: lightness up 0.073 and 0.38 of the chroma at the minimum, linear to the middle', () => {
    const pink = swatchColor('#e59eba')
    const mid = lch(colorRgb(pink, 'dark', 0.5))
    for (const [i, lift, keep] of [[0, 0.073, 0.38], [0.25, 0.0365, 0.69]] as const) {
      const at = lch(colorRgb(pink, 'dark', i))
      expect(at.L - mid.L, String(i)).toBeCloseTo(lift, 2)
      expect(at.C / mid.C, String(i)).toBeCloseTo(keep, 1)
    }
  })

  it('follows the design at the top: lightness down 0.050 and chroma 1.84 times the middle\'s, linear from the middle', () => {
    // The design's sidebar for one theme: #959ccf at the middle, #7d87e4 at the maximum.
    const mid: Rgb = [0x95, 0x9c, 0xcf]
    const max = deepenAboveDefault(mid, 1)
    expect(dist(max, [0x7d, 0x87, 0xe4])).toBeLessThan(0.01)
    expect(lch(max).L - lch(mid).L).toBeCloseTo(-0.05, 2)
    expect(lch(max).C / lch(mid).C).toBeCloseTo(1.84, 1)
    const at75 = deepenAboveDefault(mid, 0.75)
    expect(lch(at75).L - lch(mid).L).toBeCloseTo(-0.025, 2)
    expect(lch(at75).C / lch(mid).C).toBeCloseTo(1.42, 1)
    expect(deepenAboveDefault(mid, 0.5)).toEqual(mid)
    for (const mode of ['light', 'dark'] as const) {
      const pink = swatchColor('#e59eba')
      expect(lch(colorRgb(pink, mode, 1)).L, mode).toBeLessThan(lch(colorRgb(pink, mode, 0.5)).L - 0.03)
    }
  })

  it('cuts chroma the screen can\'t show instead of clipping a channel, so a vivid color keeps its hue at the top', () => {
    const hue = (c: Rgb) => {
      const [, a, b] = rgbToOklab(c)
      return (Math.atan2(b, a) * 180) / Math.PI
    }
    for (const c of [[0xce, 0x3e, 0x3f], [0x3e, 0xcd, 0xcc], [0x86, 0x3d, 0xcd], [0x42, 0x3f, 0x40]] as Rgb[]) {
      const top = deepenAboveDefault(c, 1)
      expect(top.every((v) => v >= 0 && v <= 255), rgbToHex(c)).toBe(true)
      if (lch(c).C > 0.02) expect(Math.abs(hue(top) - hue(c)), rgbToHex(c)).toBeLessThan(2)
      expect(lch(top).C, rgbToHex(c)).toBeGreaterThanOrEqual(lch(c).C - 1e-3)
    }
    expect(deepenAboveDefault([0, 0, 0], 1)).toEqual([0, 0, 0])
    expect(deepenAboveDefault([255, 255, 255], 1).every((v) => v < 255)).toBe(true)
  })

  it('gives the values clu_theme_math_unittest.cc expects for a theme with no row (the C++ mirror)', () => {
    expect(legacyColorRgb({ h: 10, s: 0.5 }, 'light', 0)).toEqual([255, 239, 236])
    expect(legacyColorRgb({ h: 10, s: 0.5 }, 'light', 0.25)).toEqual([250, 230, 226])
    expect(legacyColorRgb({ h: 10, s: 0.5 }, 'dark', 0)).toEqual([64, 59, 58])
    expect(legacyColorRgb({ h: 10, s: 0.5 }, 'dark', 0.25)).toEqual([57, 49, 48])
    expect(legacyColorRgb({ h: 10, s: 0.5 }, 'light', 0.5)).toEqual(hslToRgb(10, 0.5, 0.9))
  })

  it('gets lighter and never gray toward the minimum, in both modes, for edited and default themes', () => {
    for (const mode of ['light', 'dark'] as const) {
      for (const paint of [(i: number) => themeStops(theme([[340, 0.6]], i), mode)[0],
        (i: number) => legacyColorRgb({ h: 340, s: 0.5 }, mode, i)]) {
        for (let n = 0; n < 10; n++) {
          const i = n / 20
          expect(lch(paint(i)).L, `${mode} ${i}`).toBeGreaterThan(lch(paint(i + 0.05)).L - 1e-3)
          expect(lch(paint(i)).C, `${mode} ${i}`).toBeGreaterThan(0.3 * lch(paint(0.5)).C)
        }
        expect(lch(paint(1)).C, mode).toBeGreaterThan(lch(paint(0.5)).C)
        expect(paint(Number.NaN)).toEqual(paint(0.5))
      }
    }
  })
})

describe('validator', () => {
  const ok = theme([[0, 0], [359.9, 1]], 1, 0)
  it('accepts 1 to 3 colors with values in range', () => {
    expect(parseSpaceTheme(ok)).toEqual(ok)
    expect(parseSpaceTheme(theme([[1, 0.1], [2, 0.2], [3, 0.3]], 0, 1))).not.toBeNull()
  })

  it.each([
    ['0 colors', { colors: [], intensity: 0.5, grain: 0 }],
    ['4 colors', theme([[1, 0.1], [2, 0.2], [3, 0.3], [4, 0.4]])],
    ['h 360', theme([[360, 0.5]])],
    ['h below 0', theme([[-1, 0.5]])],
    ['s above 1', theme([[10, 1.01]])],
    ['s below 0', theme([[10, -0.1]])],
    ['intensity above 1', theme([[10, 0.5]], 1.5)],
    ['intensity below 0', theme([[10, 0.5]], -0.1)],
    ['grain above 1', theme([[10, 0.5]], 0.5, 2)],
    ['grain below 0', theme([[10, 0.5]], 0.5, -1)],
    ['NaN', theme([[Number.NaN, 0.5]])],
    ['Infinity', theme([[10, 0.5]], Number.POSITIVE_INFINITY)],
    ['extra keys', { ...theme([[10, 0.5]]), extra: 1 }],
    ['extra color keys', { colors: [{ h: 1, s: 0.5, l: 0.5 }], intensity: 0.5, grain: 0 }],
    ['strings', { colors: [{ h: '10', s: 0.5 }], intensity: 0.5, grain: 0 }],
    ['null', null],
    ['an array', [1, 2]]
  ])('rejects %s', (_label, value) => {
    expect(parseSpaceTheme(value)).toBeNull()
  })
})

describe('defaults and accent', () => {
  // The editor's handles for a space with no theme sit where the wheel paints closest to the muted sidebar.
  it('editorThemeFor keeps the default hues and picks the wheel saturation whose paint is nearest the legacy sidebar', () => {
    for (const mode of ['light', 'dark'] as const) {
      for (const color of SPACE_COLORS) {
        const base = defaultThemeFor(color)
        const t = editorThemeFor(color, mode)
        expect(parseSpaceTheme(t)).toEqual(t)
        expect(t.colors.map((c) => c.h)).toEqual(base.colors.map((c) => c.h))
        const want = themeStops(base, mode, true)
        const got = themeStops(t, mode)
        const dist = (a: Rgb, b: Rgb) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2])
        // No other saturation paints nearer the sidebar's first color.
        const c0 = t.colors[0] as { h: number; s: number }
        for (const s of [0, 0.25, 0.5, 0.75, 1]) {
          expect(dist(got[0], want[0])).toBeLessThanOrEqual(dist(themeStops({ ...t, colors: [{ h: c0.h, s }, ...t.colors.slice(1)] }, mode)[0], want[0]) + 1e-9)
        }
      }
    }
  })

  it('derives the design default gradient from every named color: a pastel of its hue to a warm corner, faint grain', () => {
    for (const color of SPACE_COLORS) {
      const t = defaultThemeFor(color)
      expect(parseSpaceTheme(t)).toEqual(t)
      expect(t).toMatchObject({ intensity: 0.5, grain: 0.25 })
      expect(t.colors).toHaveLength(2)
      const { h, s } = rgbToHsl(hexToRgb(SPACE_COLOR_HEX[color]) as Rgb)
      expect(t.colors[0]?.h).toBeCloseTo(h, 0)
      expect(t.colors[0]?.s).toBeCloseTo(Math.min(s, 0.5), 2)
      expect(t.colors[1]).toEqual({ h: 0, s: 0.3 })
    }
    // Indigo is the lavender default: its stops land within a few levels of #dadbf1 and #efe4e4.
    const [a, , b] = themeStops(defaultThemeFor('indigo'), 'light', true)
    expect(channelDelta(a, [0xda, 0xdb, 0xf1])).toBeLessThanOrEqual(3)
    expect(channelDelta(b, [0xef, 0xe4, 0xe4])).toBeLessThanOrEqual(6)
  })

  it('keeps dark tints a muted gray of the hue, not a saturated navy', () => {
    const [stop] = themeStops(defaultThemeFor('blue'), 'dark', true)
    const { s, l } = rgbToHsl(stop)
    expect(s).toBeLessThanOrEqual(0.2)
    expect(l).toBeCloseTo(0.17, 1)
  })

  it('takes the accent from the first theme color, else the named color', () => {
    const t = theme([[150, 0.45], [20, 0.9]])
    expect(spaceAccent(t, 'red')).toBe(rgbToHex(hslToRgb(150, 0.45, 0.68)))
    // A full-saturation mint pick is the sidebar's soft mint, never neon #00ff52.
    expect(spaceAccent(theme([[139.3, 1]]), 'red')).toBe(rgbToHex(hslToRgb(139.3, 0.58, 0.68)))
    expect(spaceAccent(theme([[139.3, 1]]), 'red')).not.toBe('#00ff52')
    expect(spaceAccent(null, 'red')).toBe(SPACE_COLOR_HEX.red)
  })
})

describe('glyphOnTint (the folder glyph stays visible in a colored space)', () => {
  it('keeps an accent that already stands out', () => {
    expect(glyphOnTint([59, 130, 246], [39, 42, 49])).toEqual([59, 130, 246])
  })
  it('lightens an accent close to a dark sidebar until it stands out', () => {
    const tint = hexToRgb('#cf3e3e') as Rgb
    const glyph = glyphOnTint(hexToRgb('#e30000') as Rgb, tint)
    expect(contrast(glyph, tint)).toBeGreaterThanOrEqual(GLYPH_CONTRAST)
    expect(glyph[0]).toBeGreaterThan(glyph[1])
  })
  it('darkens an accent close to a light sidebar', () => {
    const tint = hexToRgb('#e1b5b6') as Rgb
    const glyph = glyphOnTint(hexToRgb('#e8a0a2') as Rgb, tint)
    expect(contrast(glyph, tint)).toBeGreaterThanOrEqual(GLYPH_CONTRAST)
  })
})
