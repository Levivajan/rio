// The UI tints with the active space's MAIN color (the theme editor's first handle), never a gradient's later
// stops. On a green-to-orange space the tooltips, popovers and the empty page take the green; the orange shows only in
// the gradient. Shots in $CLU_SHOTS_DIR/tint-main.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import type { Frame } from '@playwright/test'
import { expect, harnessTest, shotsDir, state, type Harness } from './fixture'

const LABEL = 'tint-main'
const test = harnessTest(LABEL)
const SHOTS = shotsDir(LABEL)
const HOME = '00000000-0000-4000-8000-000300000001'
const GREEN = { h: 100, s: 0.5 }
const ORANGE = { h: 25, s: 0.62 }
const GREEN_TO_ORANGE = { colors: [GREEN, ORANGE], intensity: 1, grain: 0 }
const ORANGE_TO_GREEN = { colors: [ORANGE, GREEN], intensity: 1, grain: 0 }

const rootVar = (f: Frame, name: string) => f.evaluate((n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim(), name)
const stop = (ui: Harness, i: number) => ui.frame.locator('.clu-theme-bg').evaluate((el, n) => getComputedStyle(el).getPropertyValue(`--clu-theme-c${n}`).trim(), i)
/** The background an element of this class paints in this document (a tooltip pill mixes --tint into it). */
const bgOf = (f: Frame, className = 'clu-tooltip') =>
  f.evaluate((c) => {
    const el = document.createElement('div')
    el.className = c
    document.body.append(el)
    const bg = getComputedStyle(el).backgroundColor
    el.remove()
    return bg
  }, className)
const overlayDoc = (ui: Harness): Frame => {
  const f = ui.page.frame({ url: /\/overlay\/index\.html/ })
  if (!f) throw new Error('no overlay frame')
  return f
}
/** A computed color (#rrggbb, or rgb() for a registered property) as channels in [0,255]. */
const rgbOf = (c: string): number[] =>
  c.startsWith('#') ? [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16)) : (c.match(/[\d.]+/g) ?? []).slice(0, 3).map(Number)
const same = (a: string, b: string) => rgbOf(a).join() === rgbOf(b).join()
/** Hue in degrees of a computed color. */
const hue = (c: string) => {
  const [r, g, b] = rgbOf(c).map((v) => v / 255) as [number, number, number]
  const max = Math.max(r, g, b)
  const d = max - Math.min(r, g, b)
  if (d === 0) return 0
  const h = max === r ? ((g - b) / d) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4
  return ((h * 60) % 360 + 360) % 360
}

async function setTheme(ui: Harness, theme: typeof GREEN_TO_ORANGE): Promise<void> {
  await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.setSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme })
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme).toEqual(theme)
}

for (const mode of ['light', 'dark'] as const) {
  test(`a green-to-orange space tints the UI green, and follows a theme change live (${mode})`, async ({ ui }) => {
    if (mode === 'dark') {
      await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
      await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
    }
    await setTheme(ui, GREEN_TO_ORANGE)
    // Both stops settle before they are read: under load the last stop can still be on its way from the old theme.
    await expect.poll(async () => Math.abs(hue(await stop(ui, 0)) - 100)).toBeLessThan(15)
    await expect.poll(async () => Math.abs(hue(await stop(ui, 2)) - 25)).toBeLessThan(15)
    const green = await stop(ui, 0)
    // Frame and overlay both publish the main color as --tint; the tooltip pill and the strong accent are green too.
    await expect.poll(async () => same(await rootVar(ui.frameDoc(), '--tint'), green)).toBe(true)
    const overlay = overlayDoc(ui)
    await expect.poll(async () => same(await rootVar(overlay, '--tint'), green)).toBe(true)
    for (const f of [ui.frameDoc(), overlay]) {
      const [r, g, b] = rgbOf(await bgOf(f)) as [number, number, number]
      expect(g).toBeGreaterThan(r)
      expect(g).toBeGreaterThanOrEqual(b)
    }
    expect(Math.abs(hue(await rootVar(overlay, '--accent-strong')) - 100)).toBeLessThan(15)
    // Site controls' setting circles take the main color.
    expect(Math.abs(hue(await bgOf(overlay, 'clu-site-controls-setting-icon')) - 100)).toBeLessThan(15)
    mkdirSync(SHOTS, { recursive: true })
    await ui.page.screenshot({ path: join(SHOTS, `green-to-orange-${mode}.png`) })

    // Making orange the main color moves every tint to orange at once.
    await setTheme(ui, ORANGE_TO_GREEN)
    await expect.poll(async () => Math.abs(hue(await rootVar(ui.frameDoc(), '--tint')) - 25)).toBeLessThan(15)
    await expect.poll(async () => Math.abs(hue(await rootVar(overlay, '--tint')) - 25)).toBeLessThan(15)
    await expect.poll(async () => Math.abs(hue(await rootVar(ui.frameDoc(), '--accent-strong')) - 25)).toBeLessThan(15)
    await expect.poll(async () => Math.abs(hue(await bgOf(overlay, 'clu-site-controls-setting-icon')) - 25)).toBeLessThan(15)
    await ui.page.screenshot({ path: join(SHOTS, `orange-to-green-${mode}.png`) })
  })
}
