// Menus and the command bar follow the space's light or dark look, not macOS. On a pastel
// green-to-purple space in dark mode (a pastel space) the right-click menu and the command bar are light,
// and the selected row is a muted green, never a neon (18, 237, 54). Shots in $CLU_SHOTS_DIR/space-look.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import { expect, harnessTest, shotsDir, state, type Harness } from './fixture'

const LABEL = 'space-look'
const test = harnessTest(LABEL)
const SHOTS = shotsDir(LABEL)
const HOME = '00000000-0000-4000-8000-000300000001'
const PASTEL = { colors: [{ h: 120, s: 1 }, { h: 280, s: 1 }], intensity: 1, grain: 0 }
const REST_ROW = '.clu-tab-tree .clu-tab-row[data-active="false"]'

/** A computed color as channels in [0, 255]; color-mix() computes to color(srgb r g b) in [0, 1]. */
const rgbOf = (c: string): number[] => {
  const v = (c.match(/[\d.]+/g) ?? []).slice(0, 3).map(Number)
  return c.startsWith('color(srgb') ? v.map((x) => x * 255) : v
}
const lum = (c: string) => {
  const [r, g, b] = rgbOf(c) as [number, number, number]
  return (r + g + b) / 3
}
const sat = (c: string) => {
  const [r, g, b] = rgbOf(c) as [number, number, number]
  return Math.max(r, g, b) - Math.min(r, g, b)
}

async function pastelInDark(ui: Harness): Promise<void> {
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.setSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme: PASTEL })
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme).toEqual(PASTEL)
  await expect.poll(() => ui.frameDoc().evaluate(() => document.documentElement.dataset.theme)).toBe('light')
}

const overlayTheme = (ui: Harness) => ui.overlay.locator(':root').evaluate((el) => (el as HTMLElement).dataset.theme)

test('a pastel space in dark mode gets a light right-click menu', async ({ ui }) => {
  await pastelInDark(ui)
  await ui.frame.locator(REST_ROW).first().click({ button: 'right' })
  const menu = ui.overlay.getByRole('menu').first()
  await expect(menu).toBeVisible()
  await expect.poll(() => overlayTheme(ui)).toBe('light')
  expect(lum(await menu.evaluate((el) => getComputedStyle(el).backgroundColor))).toBeGreaterThan(200)
  mkdirSync(SHOTS, { recursive: true })
  await ui.page.screenshot({ path: join(SHOTS, 'menu-pastel-dark.png') })
})

test('a pastel space in dark mode gets a light command bar with a muted selected row', async ({ ui }) => {
  await pastelInDark(ui)
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  await expect(ui.overlay.getByRole('combobox')).toBeFocused()
  await expect.poll(() => overlayTheme(ui)).toBe('light')
  expect(lum(await ui.overlay.locator('.clu-cmd').evaluate((el) => getComputedStyle(el).backgroundColor))).toBeGreaterThan(230)
  const selected = ui.overlay.locator('.clu-cmd-row[aria-selected="true"]').first()
  await expect(selected).toBeVisible()
  const bg = await selected.evaluate((el) => getComputedStyle(el).backgroundColor)
  const [r, g, b] = rgbOf(bg) as [number, number, number]
  // Green, at 68% lightness and at most 58% saturation: far from (18, 237, 54).
  expect(g).toBeGreaterThan(r)
  expect(g).toBeGreaterThan(b)
  expect(sat(bg)).toBeLessThan(110)
  expect(lum(bg)).toBeGreaterThan(140)
  mkdirSync(SHOTS, { recursive: true })
  await ui.page.screenshot({ path: join(SHOTS, 'command-pastel-dark.png') })
})
