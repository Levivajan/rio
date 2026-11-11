import { describe, expect, it } from 'vitest'
import { computeLayout, DEFAULT_CHROME, effectiveSidebarWidth, storedSidebarWidth } from './layout'
import { LOOK } from '../look/tokens'

const size = { width: 1280, height: 800 }

describe('computeLayout', () => {
  it('puts the card right of a 260 px sidebar, inset 10 top, right and bottom', () => {
    const l = computeLayout(size, { ...DEFAULT_CHROME, sidebarWidth: 260 })
    expect(l.pageRect).toEqual({ x: 260, y: 10, width: 1010, height: 780 })
    expect(l.sidebarRect).toEqual({ x: 0, y: 0, width: 260, height: 800 })
    expect(l.hotZone).toBeNull()
    expect(l.radius).toBe(6.5)
    expect(l.trafficLights).toBe(true)
    expect(l.trafficSpacer).toBe(66)
  })

  it('widens the docked sidebar to the Library while it is open, keeping 360 px of page; hidden, the floating sidebar takes that width', () => {
    const chrome = { ...DEFAULT_CHROME, libraryOpen: true }
    expect(computeLayout({ width: 1280, height: 800 }, chrome)).toMatchObject({ effectiveWidth: 417, pageRect: { x: 417, y: 10, width: 853, height: 780 } })
    expect(computeLayout({ width: 700, height: 800 }, chrome).effectiveWidth).toBe(340)
    expect(computeLayout({ width: 1280, height: 800 }, { ...chrome, sidebarHidden: true })).toMatchObject({ sidebarRect: { width: 417 }, pageRect: { x: 10, width: 1260 } })
    expect(computeLayout({ width: 1280, height: 800 }, { ...chrome, sidebarWidth: 420 }).effectiveWidth).toBe(420)
    // Focus mode floats the sidebar too: the Library keeps its width there and the page keeps its gutter.
    expect(computeLayout({ width: 1280, height: 800 }, { ...chrome, focusMode: true })).toMatchObject({ sidebarRect: { width: 417 }, pageRect: { x: 4, y: 0, width: 1276, height: 800 } })
  })
  it('widens the Library to the width its Spaces view asks for, never under 417 nor past the page room', () => {
    const chrome = { ...DEFAULT_CHROME, libraryOpen: true }
    expect(computeLayout({ width: 1600, height: 800 }, { ...chrome, libraryWidth: 860 }).effectiveWidth).toBe(860)
    expect(computeLayout({ width: 1000, height: 800 }, { ...chrome, libraryWidth: 860 }).effectiveWidth).toBe(640)
    expect(computeLayout({ width: 1600, height: 800 }, { ...chrome, libraryWidth: 100 }).effectiveWidth).toBe(417)
    expect(computeLayout({ width: 1600, height: 800 }, { ...chrome, libraryWidth: Number.NaN }).effectiveWidth).toBe(417)
    expect(computeLayout({ width: 1600, height: 800 }, { ...DEFAULT_CHROME, libraryWidth: 860 }).effectiveWidth).toBe(214)
  })
  it('insets the card 10 px on all sides with a 10 px hot zone when the sidebar is hidden', () => {
    const l = computeLayout(size, { ...DEFAULT_CHROME, sidebarHidden: true })
    expect(l.pageRect).toEqual({ x: 10, y: 10, width: 1260, height: 780 })
    expect(l.hotZone).toEqual({ x: 0, y: 0, width: 6, height: 800 })
    expect(l.sidebarRect).toEqual({ x: 0, y: 0, width: 214, height: 800 })
    expect(l.radius).toBe(6.5)
    expect(l.trafficLights).toBe(false)
  })

  it('runs the card edge to edge with a 4 px gutter and no radius in focus mode', () => {
    const l = computeLayout(size, { ...DEFAULT_CHROME, focusMode: true })
    expect(l.pageRect).toEqual({ x: 4, y: 0, width: 1276, height: 800 })
    expect(l.radius).toBe(0)
    expect(l.hotZone).toEqual({ x: 0, y: 0, width: 4, height: 800 })
    expect(l.trafficLights).toBe(false)
    // Focus mode wins over a visible sidebar.
    expect(computeLayout(size, { ...DEFAULT_CHROME, focusMode: true, sidebarHidden: false }).pageRect.x).toBe(4)
  })

  it('moves the card under the 38 px toolbar band in toolbar mode, sidebar shown or hidden; focus mode drops it', () => {
    const toolbar = { ...DEFAULT_CHROME, toolbar: true }
    expect(computeLayout(size, toolbar).pageRect).toEqual({ x: 214, y: 48, width: 1056, height: 742 })
    expect(computeLayout(size, { ...toolbar, sidebarHidden: true }).pageRect).toEqual({ x: 10, y: 48, width: 1260, height: 742 })
    expect(computeLayout(size, { ...toolbar, focusMode: true }).pageRect).toEqual({ x: 4, y: 0, width: 1276, height: 800 })
    expect(computeLayout({ width: 400, height: 30 }, toolbar).pageRect.height).toBe(0)
  })

  it('gives a page in Developer Mode the same band, and focus mode drops it', () => {
    const dev = { ...DEFAULT_CHROME, devStrip: true }
    expect(computeLayout(size, dev).pageRect).toEqual({ x: 214, y: 48, width: 1056, height: 742 })
    expect(computeLayout(size, { ...dev, toolbar: true }).pageRect.y).toBe(48)
    expect(computeLayout(size, { ...dev, focusMode: true }).pageRect.y).toBe(0)
  })

  it('clamps the effective width on a narrow window and leaves the stored width alone', () => {
    const chrome = { ...DEFAULT_CHROME, sidebarWidth: 400 }
    const narrow = computeLayout({ width: 700, height: 600 }, chrome)
    expect(narrow.effectiveWidth).toBe(315)
    expect(narrow.sidebarRect.width).toBe(315)
    expect(narrow.pageRect).toEqual({ x: 315, y: 10, width: 375, height: 580 })
    expect(chrome.sidebarWidth).toBe(400)
    // The same stored width comes back in full once the window is wide again.
    expect(computeLayout(size, chrome).effectiveWidth).toBe(400)
    // The minimum window (560) still leaves the sidebar 200 px beside 360 px of page.
    expect(computeLayout({ width: 560, height: 400 }, chrome).effectiveWidth).toBe(200)
  })

  it('falls back to 214 for a junk stored width and clamps to 164..500', () => {
    expect(computeLayout(size, { ...DEFAULT_CHROME, sidebarWidth: Number.NaN }).pageRect.x).toBe(214)
    expect(storedSidebarWidth(Number.POSITIVE_INFINITY)).toBe(214)
    expect(storedSidebarWidth(50)).toBe(164)
    expect(storedSidebarWidth(9000)).toBe(500)
    expect(storedSidebarWidth(321.6)).toBe(322)
    expect(effectiveSidebarWidth(260, Number.NaN)).toBe(164)
  })

  it('keeps the 66 px traffic-light spacer in full screen, where macOS reveals the lights at the top', () => {
    expect(computeLayout(size, DEFAULT_CHROME).trafficSpacer).toBe(66)
    const full = computeLayout(size, { ...DEFAULT_CHROME, fullscreen: true })
    expect(full.trafficSpacer).toBe(66)
    // The sidebar's 8 px inset, the spacer and the toggle's 3 px nudge start the toggle at 77, after the zoom light's
    // right edge at 19.75 + 2 * 23 + 6 = 71.75.
    const { firstCenterX, pitch, diameter } = LOOK.window.trafficLights
    expect(8 + full.trafficSpacer + 3).toBeGreaterThan(firstCenterX + 2 * pitch + diameter / 2)
    for (const chrome of [{ ...DEFAULT_CHROME, fullscreen: true, sidebarHidden: true }, { ...DEFAULT_CHROME, fullscreen: true, focusMode: true }]) {
      expect(computeLayout(size, chrome).trafficSpacer).toBe(66)
    }
    expect(full.pageRect).toEqual({ x: 214, y: 10, width: 1056, height: 780 })
  })

  it('never returns negative or fractional rects for junk sizes', () => {
    for (const s of [{ width: Number.NaN, height: -5 }, { width: 3, height: 3 }, { width: 100.7, height: Number.POSITIVE_INFINITY }]) {
      for (const chrome of [DEFAULT_CHROME, { ...DEFAULT_CHROME, sidebarHidden: true }, { ...DEFAULT_CHROME, focusMode: true }]) {
        const l = computeLayout(s, chrome)
        for (const r of [l.pageRect, l.sidebarRect, l.hotZone].filter((x) => x !== null)) {
          for (const v of Object.values(r)) {
            expect(Number.isInteger(v)).toBe(true)
            expect(v).toBeGreaterThanOrEqual(0)
          }
        }
      }
    }
  })
})
