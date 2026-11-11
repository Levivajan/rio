// Main-window look, second pass: the sidebar's
// label grays, its gradient angle and card grain, the media bar's track buttons, the folder row's hover controls, the
// collapsed-folder popover and the Library hover peek. Shots go to shots/parity/look-more/.
import { mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { expect, harnessTest, state, tab, WINDOW, type Harness } from './fixture'
import { readPng, type Png } from './png'

const test = harnessTest('parity-look-more')
const SHOTS = process.env.CLU_SHOTS_DIR ? join(process.env.CLU_SHOTS_DIR, 'parity/look-more') : resolve('shots/parity/look-more')
const SCALE = 2

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)
const hostCalls = (ui: Harness, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

async function shot(ui: Harness, name: string): Promise<Png> {
  mkdirSync(SHOTS, { recursive: true })
  for (const doc of [ui.frameDoc(), ...ui.page.frames().filter((f) => /\/overlay\//.test(f.url()))]) {
    await expect
      .poll(() => doc.evaluate(() => document.getAnimations().filter((a) => a.playState === 'running' && a.effect?.getTiming().iterations !== Infinity).length))
      .toBe(0)
  }
  return readPng(await ui.page.screenshot({ path: join(SHOTS, `${name}.png`) }))
}

const onPage = (ui: Harness) => ui.page.mouse.move(900, 400)
const onSidebar = (ui: Harness) => ui.page.mouse.move(130, 25)

async function darkMode(ui: Harness) {
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
}

/** Grain spread of a region of the shot (points): the green channel's horizontal neighbor differences over root 2, so
 *  the gradient's slope does not count. */
function grainSd(png: Png, x: number, y: number, w: number, h: number): number {
  const diffs: number[] = []
  const g = (px: number, py: number) => png.data[(py * png.width + px) * png.channels + 1] ?? 0
  for (let py = y * SCALE; py < (y + h) * SCALE; py++) for (let px = x * SCALE; px < (x + w) * SCALE - 1; px++) diffs.push(g(px + 1, py) - g(px, py))
  const mean = diffs.reduce((a, b) => a + b, 0) / diffs.length
  return Math.sqrt(diffs.reduce((a, b) => a + (b - mean) ** 2, 0) / diffs.length / 2)
}

/** The secondary labels' colors: the space header, New Tab, the pill placeholder and Clear. */
async function labelColors(ui: Harness) {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const color = (sel: string) => nav.locator(sel).first().evaluate((el) => getComputedStyle(el).color)
  return {
    header: await color('.clu-space-header-name'),
    newTab: await color('.clu-new-tab'),
    placeholder: await color('.clu-url-host[data-empty="true"]'),
    clear: await color('[data-clear-today]')
  }
}

test('the secondary labels take the design grays in light and dark', async ({ ui }) => {
  // No page, so the pill shows its placeholder.
  await host(ui, ['windows', WINDOW, 'activeTabId'], null)
  await expect(ui.frame.locator('.clu-url-host[data-empty="true"]')).toBeVisible()
  await onSidebar(ui)
  // Header #796363 (black about 46%), New Tab #9b7f7f (31%), placeholder 35%, Clear about 35%.
  await expect.poll(() => labelColors(ui)).toEqual({ header: 'rgba(0, 0, 0, 0.46)', newTab: 'rgba(0, 0, 0, 0.31)', placeholder: 'rgba(0, 0, 0, 0.35)', clear: 'rgba(0, 0, 0, 0.35)' })
  await shot(ui, '01-label-grays-light')
  await darkMode(ui)
  await onSidebar(ui)
  // The bundle's named dark label, tertiary and placeholder whites.
  await expect.poll(() => labelColors(ui)).toEqual({
    header: 'rgba(255, 255, 255, 0.45)',
    newTab: 'rgba(255, 255, 255, 0.33)',
    placeholder: 'rgba(255, 255, 255, 0.3)',
    clear: 'rgba(255, 255, 255, 0.3)'
  })
  await shot(ui, '02-label-grays-dark')
})

test('the window gradient runs at 120 degrees and the empty card carries a third of its grain', async ({ ui }) => {
  await expect(ui.frame.locator('.clu-theme-bg')).toHaveCSS('background-image', /^linear-gradient\(120deg,/)
  await host(ui, ['windows', WINDOW, 'activeTabId'], null)
  const card = ui.frame.locator('[data-surface="page-card"][data-empty="true"]')
  await expect(card).toBeVisible()
    const after = await card.evaluate((el) => ({ opacity: Number(getComputedStyle(el, '::after').opacity), image: getComputedStyle(el, '::after').backgroundImage }))
  expect(after.image).toMatch(/^url\("data:image\/png/)
  expect(after.opacity).toBeGreaterThan(0)
  // The card carries about a third of the window's grain (spread read in the top gutter).
  const spreads = async (name: string) => {
    await onPage(ui)
    const png = await shot(ui, name)
    return { card: grainSd(png, 600, 300, 300, 100), window: grainSd(png, 400, 1, 600, 8) }
  }
  const light = await spreads('03-empty-card-grain-light')
  expect(light.card).toBeGreaterThan(light.window / 5)
  expect(light.card).toBeLessThan(light.window / 2)
  // Dark was never captured: the card shows some grain, less than the window.
  await darkMode(ui)
  const dark = await spreads('04-empty-card-grain-dark')
  expect(dark.card).toBeGreaterThan(0.05)
  expect(dark.card).toBeLessThan(dark.window)
})

test('the media bar has previous and next track buttons that reach the media session', async ({ ui }) => {
  const bar = ui.frame.locator('.clu-media-card')
  await expect(bar).toBeVisible()
  const labels = await bar.locator('button').evaluateAll((els) => els.map((el) => el.getAttribute('aria-label')))
  expect(labels.slice(1)).toEqual(['Previous track', 'Pause', 'Next track', 'Mute tab'])
  // Five glyphs on an even pitch of about 38.5 across the bar.
  const centers = await bar.locator('button').evaluateAll((els) => els.map((el) => { const r = el.getBoundingClientRect(); return r.x + r.width / 2 }))
  const pitches = centers.slice(1).map((c, i) => c - (centers[i] ?? 0))
  for (const p of pitches) expect(Math.abs(p - (pitches[0] ?? 0))).toBeLessThanOrEqual(1)
  const tabId = await bar.getAttribute('data-media-tab-id')
  await bar.getByRole('button', { name: 'Previous track' }).click()
  await bar.getByRole('button', { name: 'Next track' }).click()
  expect(await hostCalls(ui, 'media.previous')).toEqual([{ tabId }])
  expect(await hostCalls(ui, 'media.next')).toEqual([{ tabId }])
  await onPage(ui)
  await shot(ui, '05-media-bar-light')
})

const FOLDER = '00000000-0000-4000-8000-000400000001'
const folderRow = (ui: Harness) => ui.frame.locator(`.clu-folder-row[data-folder-id="${FOLDER}"]`)

test('folder row hover: the glyph stays a plain folder, a left click on it only toggles, a minus collapses it', async ({ ui }) => {
  const row = folderRow(ui)
  await expect(row).toHaveAttribute('data-collapsed', 'false')
  await onPage(ui)
  await expect(row.getByRole('button', { name: 'Collapse folder' })).toHaveCSS('opacity', '0')
  await row.hover()
  // Hovering the glyph does nothing: no dotted menu glyph.
  await expect(row.locator('.clu-folder-glyph circle')).toHaveCount(0)
  await expect(row.getByRole('button', { name: 'Folder options' })).toHaveCount(0)
  const minus = row.getByRole('button', { name: 'Collapse folder' })
  await expect(minus).toHaveCSS('opacity', '1')
  // The minus is centered 18 from the row's right edge, as the close button is.
  const [r, m] = await Promise.all([row.boundingBox(), minus.boundingBox()])
  expect(Math.abs((r?.x ?? 0) + (r?.width ?? 0) - 18 - ((m?.x ?? 0) + (m?.width ?? 0) / 2))).toBeLessThanOrEqual(1)
  await shot(ui, '06-folder-hover-light')
  // A left click on the glyph closes the folder and opens no menu; a second opens it again; no ring after the clicks.
  await row.locator('.clu-folder-glyph').click()
  await expect(row).toHaveAttribute('data-collapsed', 'true')
  await row.locator('.clu-folder-glyph').click()
  await expect(row).toHaveAttribute('data-collapsed', 'false')
  expect(await hostCalls(ui, 'tabs.openMenu')).toEqual([])
  await expect(row).toHaveCSS('outline-style', 'none')
  // The right-click opens the folder menu.
  await row.click({ button: 'right' })
  await expect.poll(() => hostCalls(ui, 'tabs.openMenu')).toEqual([expect.objectContaining({ menu: 'row', nodeId: FOLDER })])
  await ui.page.keyboard.press('Escape')
  await minus.click()
  await expect(row).toHaveAttribute('data-collapsed', 'true')
  // A collapsed folder keeps its plain glyph and has no minus.
  await expect(row.locator('.clu-folder-glyph circle')).toHaveCount(0)
  await expect(row.getByRole('button', { name: 'Collapse folder' })).toHaveCount(0)
})

test('collapsed folder hover: a popover beside the sidebar lists its tabs, holds while the pointer is on it, opens a tab', async ({ ui }) => {
  const now = Date.now()
  await host(ui, ['tabs', tab(12), 'lastActiveAt'], now - 30_000)
  await host(ui, ['tabs', tab(13), 'lastActiveAt'], now - 4 * 86_400_000)
  await host(ui, ['folders', FOLDER, 'collapsed'], true)
  const row = folderRow(ui)
  await expect(row).toHaveAttribute('data-collapsed', 'true')
  await onPage(ui)
  await row.hover()
  const view = ui.page.locator('iframe.passive')
  await expect(view).toBeVisible()
  const peek = ui.page.frameLocator('iframe.passive').locator('.clu-folder-peek')
  await expect(peek.getByRole('searchbox')).toHaveAttribute('placeholder', 'Search Chromium M2…')
  await expect(peek.locator('.clu-folder-peek-text')).toHaveText(['Frame WebUI mocksJust now', 'Mojo bindings in WebUI4 days ago'])
  await expect(peek.getByRole('option').first()).toHaveAttribute('aria-selected', 'true')
  // The card sits 4 right of the sidebar and centers on the folder row.
  const [card, r, nav] = await Promise.all([view.boundingBox(), row.boundingBox(), ui.frame.locator('nav.clu-sidebar').boundingBox()])
  const box = await peek.boundingBox()
  expect(Math.abs((box?.x ?? 0) - ((nav?.x ?? 0) + (nav?.width ?? 0) + 4))).toBeLessThanOrEqual(1)
  expect(Math.abs((box?.y ?? 0) + (box?.height ?? 0) / 2 - ((r?.y ?? 0) + (r?.height ?? 0) / 2))).toBeLessThanOrEqual(1)
  expect(card?.width).toBe(229)
  await shot(ui, '07-folder-popover-light')
  // The pointer crosses to the popover: it holds open past the close delay.
  await ui.page.mouse.move((box?.x ?? 0) + 120, (box?.y ?? 0) + 70, { steps: 4 })
  await expect.poll(() => hostCalls(ui, 'folders.peek')).toContainEqual({ folderId: FOLDER, held: true })
  // The frame's close after leaving the row comes and goes; the held popover stays.
  await expect.poll(() => hostCalls(ui, 'folders.peek')).toContainEqual({ folderId: null })
  await expect(view).toBeVisible()
  // Full-contrast rows, never greyed.
  await expect(peek.locator('.clu-folder-peek-title').first()).toHaveCSS('color', 'rgba(0, 0, 0, 0.88)')
  await expect(peek.locator('.clu-row-favicon, .clu-letter-tile').first()).toHaveCSS('opacity', '1')
  // The field takes typing at once, without a click; arrows move, Escape closes.
  await expect(peek.getByRole('searchbox')).toBeFocused()
  await ui.page.keyboard.press('ArrowDown')
  await expect(peek.getByRole('option').nth(1)).toHaveAttribute('aria-selected', 'true')
  await ui.page.keyboard.type('mojo')
  await expect(peek.getByRole('searchbox')).toHaveValue('mojo')
  await expect(peek.getByRole('option')).toHaveCount(1)
  await peek.getByRole('option').first().click()
  expect(await hostCalls(ui, 'tabs.activate')).toContainEqual({ tabId: tab(13) })
  await expect(view).toBeHidden()
  // Escape closes it from its field.
  await row.hover()
  await expect(view).toBeVisible()
  await expect(peek.getByRole('searchbox')).toBeFocused()
  await ui.page.keyboard.press('Escape')
  await expect(view).toBeHidden()
  // Leaving the row for the page closes it.
  await onPage(ui)
  await row.hover()
  await expect(view).toBeVisible()
  await onPage(ui)
  await expect(view).toBeHidden()
})

test('collapsed folder popover in dark mode', async ({ ui }) => {
  await host(ui, ['folders', FOLDER, 'collapsed'], true)
  await darkMode(ui)
  await onPage(ui)
  await folderRow(ui).hover()
  await expect(ui.page.locator('iframe.passive')).toBeVisible()
  await expect(ui.page.frameLocator('iframe.passive').getByRole('option')).toHaveCount(2)
  await shot(ui, '08-folder-popover-dark')
})

test('library hover peek: the newest files rise above the bottom bar while the tab list fades, a row opens its file', async ({ ui }) => {
  await ui.page.goto('/?downloads=1')
  await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
  const nav = ui.frame.locator('nav.clu-sidebar')
  await onPage(ui)
  await expect(nav.locator('.clu-library-peek')).toHaveCount(0)
  await nav.getByRole('button', { name: 'View Library' }).hover()
  const peek = nav.getByRole('list', { name: 'Recent files' })
  await expect(peek).toBeVisible()
  // The four newest of Rio's downloads, the Downloads folder and Media, oldest on top, the newest by the button;
  // the time since a finished file. The screenshot shows its QuickLook thumbnail, the others their glyph.
  await expect(peek.locator('.clu-library-peek-title')).toHaveText(['invoice-2026-11.pdf', /^Screenshot 2026-11-09 at/, 'clu-sidebar-mocks.zip', 'quarterly-report.pdf'])
  await expect(peek.locator('.clu-library-peek-time').nth(0)).toHaveText(/ago$|^Just now$/)
  await expect(peek.locator('.clu-library-peek-tile').nth(1).locator('img')).toHaveAttribute('src', /^data:image\/svg\+xml,/)
  await expect(peek.locator('.clu-library-peek-tile').nth(0).locator('img')).toHaveCount(0)
  await expect(nav.locator('.clu-space-slider')).not.toHaveCSS('mask-image', 'none')
  await shot(ui, '09-library-peek-light')
  // 40 x 40 tiles at the content's left edge on a 56 pitch; the last ends 6 above the media bar.
  const tiles = await peek.locator('.clu-library-peek-tile').evaluateAll((els) => els.map((el) => el.getBoundingClientRect().toJSON() as DOMRect))
  const media = await nav.locator('.clu-media-card').boundingBox()
  expect(tiles.map((t) => [t.width, t.height])).toEqual(Array(4).fill([40, 40]))
  expect(tiles[0]?.x).toBe(8)
  expect((tiles[1]?.y ?? 0) - (tiles[0]?.y ?? 0)).toBe(56)
  expect(Math.abs((media?.y ?? 0) - ((tiles[3]?.y ?? 0) + 40) - 6)).toBeLessThanOrEqual(0.5)
  // The pointer moves onto the rows and the peek stays; a finished file opens.
  await peek.locator('.clu-library-peek-row').nth(0).hover()
  await peek.locator('.clu-library-peek-row').nth(0).click()
  expect(await hostCalls(ui, 'download.act')).toEqual([{ id: '00000000-0000-4000-8000-000700000003', action: 'open' }])
  await expect(nav.locator('.clu-library-peek')).toHaveCount(0)
  await expect(nav.locator('.clu-space-slider')).toHaveCSS('mask-image', 'none')
  // Hovering again and leaving for the page drops it.
  await nav.getByRole('button', { name: 'View Library' }).hover()
  await expect(peek).toBeVisible()
  await onPage(ui)
  await expect(nav.locator('.clu-library-peek')).toHaveCount(0)
})

test('library hover peek without a media bar, and nothing without downloads', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await nav.getByRole('button', { name: 'View Library' }).hover()
  // The button's tooltip shows after 450 ms, well past the peek's 150 ms delay.
  await expect(nav.locator('.clu-tooltip')).toBeVisible()
  await expect(nav.locator('.clu-library-peek')).toHaveCount(0)
  await ui.page.goto('/?downloads=1')
  await host(ui, ['windows', WINDOW, 'mediaTabId'], null)
  await expect(ui.frame.locator('.clu-media-card')).toHaveCount(0)
  await ui.frame.getByRole('button', { name: 'View Library' }).hover()
  await expect(ui.frame.getByRole('list', { name: 'Recent files' })).toBeVisible()
  await shot(ui, '10-library-peek-no-media-light')
})
