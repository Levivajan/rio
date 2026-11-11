import { afterEach, describe, expect, it, vi } from 'vitest'
import { parseSpaceTheme } from './schema'
import { THEME_PRESETS, hexToRgb, hslToRgb, rgbToHsl } from './space'
import {
  CANVAS, DIAL, HANDLE_LIMIT, WHEEL_LIGHTNESS, PICKER_GAP, PICKER_MARGIN, PICKER_SIZE, SLIDER, SWATCHES, SWATCH_PAGES, WHEEL_RADIUS,
  addColor, canvasToColor, colorToCanvas, dialAngle, dialValue, handleFill, harmony, moveColor, nudge, placePicker,
  removeColor, sameTheme, sliderValue, sliderX, snap, step, swatchColor, swatchLabel, swatchMatches, thumbSize, throttle, waveAmplitude, wavePath
} from './picker'
import type { SpaceTheme } from './types'

const R = WHEEL_RADIUS
const theme = (n: number): SpaceTheme => ({ colors: [{ h: 10, s: 0.5 }, { h: 100, s: 0.6 }, { h: 200, s: 0.7 }].slice(0, n), intensity: 0.5, grain: 0 })

describe('the measured layout', () => {
  it('keeps the popover, canvas, swatches, slider and dial where the design draws them', () => {
    expect(PICKER_SIZE).toEqual({ width: 356, height: 508 })
    expect(CANVAS.x + CANVAS.size + CANVAS.x).toBe(PICKER_SIZE.width)
    expect(SWATCHES.firstX + 8 * SWATCHES.pitch).toBe(296.5)
    expect(SLIDER.x + SLIDER.width).toBe(242.5)
    expect(DIAL.dots * 18).toBe(360)
  })
})

describe('color wheel drag math', () => {
  it('maps the angle to hue (0 at 3 o\'clock, clockwise on screen) and the distance to depth', () => {
    expect(canvasToColor({ x: 120, y: 0 })).toEqual({ h: 0, s: 0.8 })
    expect(canvasToColor({ x: 0, y: 120 })).toEqual({ h: 90, s: 0.8 })
    expect(canvasToColor({ x: -R / 2, y: 0 })).toEqual({ h: 180, s: 0.5 })
    expect(canvasToColor({ x: 0, y: -R / 4 })).toEqual({ h: 270, s: 0.25 })
    expect(canvasToColor({ x: 0, y: 0 })).toEqual({ h: 0, s: 0 })
  })
  it('colors a handle on the wheel: lightness grows with the distance at full saturation', () => {
    // Handle samples: center, east at half radius and at the edge, north at half, 25 degrees at r 107.
    const near = (got: string, want: string) => {
      const a = hexToRgb(got) as [number, number, number]
      const b = hexToRgb(want) as [number, number, number]
      return Math.max(...a.map((v, i) => Math.abs(v - (b[i] as number))))
    }
    expect(near(handleFill(canvasToColor({ x: 0.001, y: 0 })), '#050001')).toBeLessThanOrEqual(5)
    expect(near(handleFill(canvasToColor({ x: 75, y: 0 })), '#df0001')).toBeLessThanOrEqual(5)
    expect(near(handleFill(canvasToColor({ x: 150, y: 0 })), '#ffbaba')).toBeLessThanOrEqual(5)
    expect(near(handleFill(canvasToColor({ x: 0, y: -75 })), '#6f00df')).toBeLessThanOrEqual(5)
    expect(near(handleFill(canvasToColor({ x: 107 * Math.cos(0.436), y: 107 * Math.sin(0.436) })), '#ff8e3f')).toBeLessThanOrEqual(8)
  })
  it('keeps a handle inside the square 22.5 pt in from the canvas edge; past the wheel the depth stays 1', () => {
    const edge = Math.round((HANDLE_LIMIT / R) * 1000) / 1000
    expect(canvasToColor({ x: 4 * R, y: 0 })).toEqual({ h: 0, s: edge })
    expect(canvasToColor({ x: R, y: -0.0001 })).toEqual({ h: 359.9, s: edge })
    expect(canvasToColor({ x: 400, y: 400 })).toEqual({ h: 45, s: 1 })
    expect(colorToCanvas({ h: 0, s: 1 })).toEqual({ x: HANDLE_LIMIT, y: 0 })
    expect(colorToCanvas({ h: 45, s: 1 }).x).toBeLessThanOrEqual(HANDLE_LIMIT)
    expect(canvasToColor({ x: Number.NaN, y: Number.POSITIVE_INFINITY }).s).toBeLessThanOrEqual(1)
  })
  it('gives colors the validator accepts everywhere', () => {
    for (const x of [-400, -R, -1, 0, 0.5, R, 900]) {
      for (const y of [-400, -R, 0, 0.0001, R, 1e9]) {
        expect(parseSpaceTheme({ colors: [canvasToColor({ x, y })], intensity: 0.5, grain: 0 })).not.toBeNull()
      }
    }
  })
  it('round-trips a color to its handle position', () => {
    for (const c of [{ h: 210, s: 0.4 }, { h: 0, s: 0.98 }, { h: 45.5, s: 0.123 }, { h: 300, s: 0.75 }]) {
      const back = canvasToColor(colorToCanvas(c))
      expect(back.s).toBeCloseTo(c.s, 2)
      expect(Math.abs(back.h - c.h) < 0.2 || Math.abs(back.h - c.h) > 359.7).toBe(true)
    }
  })
  it('paints a handle near black at the center and a pastel at the edge', () => {
    expect(handleFill({ h: 0, s: 0 })).toBe('#050000')
    expect(handleFill({ h: 0, s: HANDLE_LIMIT / R })).toBe('#ffbaba')
    expect(handleFill({ h: 10, s: 9 })).toBe(handleFill({ h: 10, s: 1 }))
  })
  it('nudges by 2 degrees of hue (wrapping) and 0.02 depth (clamped)', () => {
    expect(nudge({ h: 10, s: 0.5 }, 'ArrowRight')).toEqual({ h: 12, s: 0.5 })
    expect(nudge({ h: 1, s: 0.5 }, 'ArrowLeft')).toEqual({ h: 359, s: 0.5 })
    expect(nudge({ h: 359, s: 0.5 }, 'ArrowRight')).toEqual({ h: 1, s: 0.5 })
    expect(nudge({ h: 10, s: 0.5 }, 'ArrowUp')).toEqual({ h: 10, s: 0.52 })
    expect(nudge({ h: 10, s: 0.99 }, 'ArrowUp')).toEqual({ h: 10, s: 1 })
    expect(nudge({ h: 10, s: 0.01 }, 'ArrowDown')).toEqual({ h: 10, s: 0 })
    expect(nudge({ h: 10, s: 0.5 }, 'ArrowDown', 10)).toEqual({ h: 10, s: 0.3 })
  })
})

describe('preset swatches', () => {
  it('has the design\'s 5 pages of 9, the first one the page the design opens on', () => {
    expect(SWATCH_PAGES).toHaveLength(5)
    for (const p of SWATCH_PAGES) expect(p.colors).toHaveLength(9)
    expect(SWATCH_PAGES[0]?.colors[1]).toBe('#e59eba')
  })
  it('turns every swatch into a valid color on the wheel whose handle shows its hue and lightness', () => {
    for (const p of SWATCH_PAGES) {
      for (const hex of p.colors) {
        const c = swatchColor(hex)
        expect(parseSpaceTheme({ colors: [c], intensity: 0.5, grain: 0 })).not.toBeNull()
        const want = rgbToHsl(hexToRgb(hex) as [number, number, number])
        if (want.s >= 0.08 && want.l <= WHEEL_LIGHTNESS.center + WHEEL_LIGHTNESS.span) expect(Math.abs(WHEEL_LIGHTNESS.center + WHEEL_LIGHTNESS.span * c.s - want.l)).toBeLessThan(0.01)
      }
    }
    expect(swatchColor('#808080')).toEqual({ h: 0, s: 0 })
    expect(swatchColor('junk')).toEqual({ h: 0, s: 0 })
    const pink = swatchColor('#f29bbb')
    expect(Math.abs(pink.h - rgbToHsl(hexToRgb('#f29bbb') as [number, number, number]).h)).toBeLessThan(0.1)
    expect(hslToRgb(pink.h, 1, WHEEL_LIGHTNESS.center + WHEEL_LIGHTNESS.span * pink.s)[0]).toBeGreaterThan(200)
  })
})

describe('the wavy intensity slider', () => {
  it('runs the thumb center over the wave, 5.5 to 218.5 pt; the design\'s 0.628 sits at 139 pt', () => {
    expect(sliderX(0)).toBe(5.5)
    expect(sliderX(1)).toBe(218.5)
    expect(Math.round(sliderX(0.6279))).toBe(139)
    expect(sliderValue(5.5)).toBe(0)
    expect(sliderValue(218.5)).toBe(1)
    expect(sliderValue(-40)).toBe(0)
    expect(sliderValue(400)).toBe(1)
    expect(sliderValue(112)).toBe(0.5)
    expect(sliderValue(Number.NaN)).toBe(0)
  })
  it('draws a sine between the travel ends whose height grows with the value: flat at 0, 7.75 pt at 0.628', () => {
    const ys = (d: string) => [...d.matchAll(/[ML][\d.]+ ([\d.-]+)/g)].map((m) => Number(m[1]))
    const d = wavePath(waveAmplitude(0.628))
    expect(d.startsWith('M5.5 8')).toBe(true)
    expect(Math.max(...ys(d))).toBeCloseTo(8 + 7.75, 1)
    expect(Math.min(...ys(d))).toBeCloseTo(8 - 7.75, 1)
    expect(new Set(ys(wavePath(waveAmplitude(0))))).toEqual(new Set([8]))
    expect(waveAmplitude(1)).toBe(SLIDER.maxAmplitude)
    expect(waveAmplitude(Number.NaN)).toBe(0)
  })
  it('grows the thumb from 13 x 48 through 19.5 x 51.5 to 21.5 x 59', () => {
    expect(thumbSize(0)).toEqual({ width: 13, height: 48 })
    expect(thumbSize(0.5)).toEqual({ width: 19.5, height: 51.5 })
    expect(thumbSize(1)).toEqual({ width: 21.5, height: 59 })
    expect(thumbSize(0.75)).toEqual({ width: 20.5, height: 55.25 })
    expect(thumbSize(-3)).toEqual({ width: 13, height: 48 })
  })
})

describe('intensity and grain steps', () => {
  it('step in 0.05 and clamp to [0,1]', () => {
    expect(step(0.5, 1)).toBe(0.55)
    expect(step(0.5, -1)).toBe(0.45)
    expect(step(0.95, 1)).toBe(1)
    expect(step(1, 1)).toBe(1)
    expect(step(0, -1)).toBe(0)
    expect(step(0.1, 1, 10)).toBe(0.6)
  })
  it('snap off-grid values to the 0.05 grid first', () => {
    expect(snap(0.333)).toBe(0.35)
    expect(snap(0.024)).toBe(0)
    expect(snap(-3)).toBe(0)
    expect(snap(Number.NaN)).toBe(0)
    expect(step(0.333, 1)).toBe(0.4)
    let v = 0
    for (let i = 0; i < 20; i++) v = step(v, 1)
    expect(v).toBe(1)
  })
  it('the grain dial turns clockwise from the top, continuously, its scale leaving 15 degrees each side of the top', () => {
    expect(dialAngle(0)).toBe(15)
    expect(dialAngle(1)).toBe(345)
    expect(dialAngle(0.5)).toBe(180)
    expect(dialValue(0, 10)).toBe(0.5)
    expect(dialValue(10, 0)).toBe(0.227)
    const at = (deg: number) => dialValue(Math.sin((deg * Math.PI) / 180), -Math.cos((deg * Math.PI) / 180))
    // Continuous: one degree moves it, no 18 degree dots.
    expect(at(100)).not.toBe(at(101))
    expect(at(dialAngle(0.3))).toBe(0.3)
    expect(at(338)).toBeCloseTo(0.979, 3)
    expect(dialValue(0, 0, 0.4)).toBe(0.4)
    expect(dialValue(Number.NaN, 1, 0.4)).toBe(0.4)
  })
  it('wraps past the end of the scale straight back to the least grain', () => {
    const at = (deg: number) => dialValue(Math.sin((deg * Math.PI) / 180), -Math.cos((deg * Math.PI) / 180))
    expect(at(356)).toBe(0)
    expect(at(14)).toBe(0)
    expect(at(344)).toBeGreaterThan(0.99)
  })
})

describe('colors', () => {
  it('adds the second color opposite the main one and the third pair at -150 and +150, same distance', () => {
    expect(addColor(theme(1)).colors).toEqual([{ h: 10, s: 0.5 }, { h: 190, s: 0.5 }])
    // A red main gives blue (210) and green (150).
    expect(addColor(addColor({ ...theme(1), colors: [{ h: 0, s: 0.5 }] })).colors).toEqual([{ h: 0, s: 0.5 }, { h: 210, s: 0.5 }, { h: 150, s: 0.5 }])
    expect(addColor(theme(3))).toEqual(theme(3))
    // Minus drops the last dot; the others stay where they are.
    expect(removeColor(theme(3)).colors).toEqual(theme(3).colors.slice(0, 2))
    expect(removeColor(theme(1))).toEqual(theme(1))
    expect(harmony({ h: 350, s: 0.2 }, 9)).toHaveLength(3)
  })
  it('dragging a dot moves only that dot, whichever dot it is', () => {
    const three = { ...theme(1), colors: harmony({ h: 0, s: 0.5 }, 3) }
    // The main dot: the others neither rotate nor change distance.
    expect(moveColor(three, 0, { h: 270, s: 0.7 }).colors).toEqual([{ h: 270, s: 0.7 }, { h: 210, s: 0.5 }, { h: 150, s: 0.5 }])
    // A secondary dot: it does not become the main one and nothing re-places around it.
    expect(moveColor(three, 2, { h: 40, s: 0.9 }).colors).toEqual([{ h: 0, s: 0.5 }, { h: 210, s: 0.5 }, { h: 40, s: 0.9 }])
    // Two dots dragged one after the other on top of each other: any mix can be made.
    const mixed = moveColor(moveColor(three, 1, { h: 20, s: 0.3 }), 2, { h: 25, s: 0.8 })
    expect(mixed.colors).toEqual([{ h: 0, s: 0.5 }, { h: 20, s: 0.3 }, { h: 25, s: 0.8 }])
    // Junk indexes leave the theme as it is.
    expect(moveColor(three, 3, { h: 1, s: 1 })).toBe(three)
    expect(moveColor(three, -1, { h: 1, s: 1 })).toBe(three)
    expect(moveColor(three, 0.5, { h: 1, s: 1 })).toBe(three)
  })
  it('+ keeps the starting spots while nobody moved a dot, and never moves a dot that was dragged', () => {
    const one = { ...theme(1), colors: [{ h: 0, s: 0.5 }] }
    // Untouched: 180, then -150 and +150.
    expect(addColor(one).colors).toEqual([{ h: 0, s: 0.5 }, { h: 180, s: 0.5 }])
    expect(addColor(addColor(one)).colors).toEqual(harmony({ h: 0, s: 0.5 }, 3))
    // The second dot was dragged: it stays, and the third takes the +150 spot from the main one.
    const moved = moveColor(addColor(one), 1, { h: 45, s: 0.2 })
    expect(addColor(moved).colors).toEqual([{ h: 0, s: 0.5 }, { h: 45, s: 0.2 }, { h: 150, s: 0.5 }])
    // The main dot was dragged after +: the second stays where + put it.
    const mainMoved = moveColor(addColor(one), 0, { h: 90, s: 0.6 })
    expect(addColor(mainMoved).colors).toEqual([{ h: 90, s: 0.6 }, { h: 180, s: 0.5 }, { h: 240, s: 0.6 }])
    // No color yet: + starts from the default main color.
    expect(addColor({ ...theme(1), colors: [] }).colors).toHaveLength(1)
  })
  it('compares themes by value', () => {
    expect(sameTheme(theme(2), theme(2))).toBe(true)
    expect(sameTheme(theme(2), theme(3))).toBe(false)
    expect(sameTheme(theme(2), { ...theme(2), grain: 0.05 })).toBe(false)
    expect(THEME_PRESETS.filter((p) => sameTheme(p.theme, THEME_PRESETS[1]?.theme as SpaceTheme))).toHaveLength(1)
  })
})

describe('placement beside the sidebar', () => {
  const vp = { width: 1280, height: 800 }
  it('opens 13 pt right of the sidebar (the popover\'s own 13 pt rim) with its top at the anchor\'s top', () => {
    expect(placePicker({ x: 40, y: 60, width: 100, height: 24 }, vp, 240)).toEqual({ x: 240 + PICKER_GAP, y: 60 })
    expect(placePicker({ x: 120, y: 200, width: 0, height: 0 }, vp, 300)).toEqual({ x: 313, y: 200 })
  })
  it('opens at the window margin with the sidebar hidden, and stays inside the window', () => {
    expect(placePicker({ x: 40, y: 60, width: 0, height: 0 }, vp, 0)).toEqual({ x: PICKER_MARGIN, y: 60 })
    expect(placePicker({ x: 40, y: 700, width: 100, height: 24 }, vp, 240)).toEqual({ x: 253, y: vp.height - PICKER_SIZE.height - PICKER_MARGIN })
    expect(placePicker({ x: 0, y: 0, width: 0, height: 0 }, vp, 1200)).toEqual({ x: vp.width - PICKER_SIZE.width - PICKER_MARGIN, y: PICKER_MARGIN })
    expect(placePicker({ x: 10, y: 10, width: 0, height: 0 }, { width: 200, height: 200 }, 100)).toEqual({ x: PICKER_MARGIN, y: PICKER_MARGIN })
    expect(placePicker({ x: 1e12, y: -1e12, width: -5, height: 1e12 }, vp, Number.NaN)).toEqual({ x: PICKER_MARGIN, y: PICKER_MARGIN })
  })
})

describe('preview throttle', () => {
  afterEach(() => vi.useRealTimers())
  it('sends the first value at once, then the latest value at most once per window', () => {
    vi.useFakeTimers()
    const sent: number[] = []
    const t = throttle((v: number) => sent.push(v), 33, () => Date.now())
    t.call(1)
    t.call(2)
    t.call(3)
    expect(sent).toEqual([1])
    vi.advanceTimersByTime(33)
    expect(sent).toEqual([1, 3])
    t.call(4)
    t.flush()
    expect(sent).toEqual([1, 3, 4])
    t.call(5)
    t.cancel()
    vi.advanceTimersByTime(100)
    expect(sent).toEqual([1, 3, 4])
  })
})

describe('swatch names and selection', () => {
  it('names the pages with the design sets, Brand running over the first two', () => {
    expect(SWATCH_PAGES.map((p) => p.name)).toEqual(['Brand', 'Brand', 'Pastel', 'Drab', 'Greyscale'])
    expect(swatchLabel(0, 0)).toBe('Brand 1')
    expect(swatchLabel(1, 0)).toBe('Brand 10')
    expect(swatchLabel(2, 8)).toBe('Pastel 9')
    expect(swatchLabel(4, 3)).toBe('Greyscale 4')
  })

  it('keeps a swatch marked after the browser rounds its color, and only that swatch', () => {
    const pink = SWATCH_PAGES[0]?.colors[1] as string
    const c = swatchColor(pink)
    expect(swatchMatches(pink, c)).toBe(true)
    expect(swatchMatches(pink, { h: Math.round(c.h * 10) / 10 + 0.1, s: Math.round(c.s * 1000) / 1000 + 0.001 })).toBe(true)
    expect(swatchMatches(pink, { h: c.h + 2, s: c.s })).toBe(false)
    const page = SWATCH_PAGES[0]?.colors ?? []
    expect(page.filter((hex) => swatchMatches(hex, c))).toEqual([pink])
  })

  it('wraps the hue at 0 when matching and reads junk as no match', () => {
    expect(swatchMatches('#e1676f', { ...swatchColor('#e1676f'), h: (swatchColor('#e1676f').h + 359.9) % 360 })).toBe(true)
    expect(swatchMatches('nope', { h: 120, s: 0.5 })).toBe(false)
  })
})
