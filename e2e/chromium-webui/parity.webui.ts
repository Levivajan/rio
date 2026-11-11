// Main-window look: shoots each sidebar state
// into shots/parity/look-frame/ and pins its sizes and colors, so a change that
// brings a difference back fails here.
import { mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { rgbToHsl } from '../../src/shared/theme/space'
import { expect, harnessTest, state, tab, WINDOW, type Harness } from './fixture'
import { colorAt, readPng } from './png'

const test = harnessTest('parity-look-frame')
const SHOTS = process.env.CLU_SHOTS_DIR ? join(process.env.CLU_SHOTS_DIR, 'parity/look-frame') : resolve('shots/parity/look-frame')
const HOME = '00000000-0000-4000-8000-000300000001'
const PROFILE = '00000000-0000-4000-8000-000200000001'
const SCALE = 2

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)
const hostCalls = (ui: Harness, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

async function shot(ui: Harness, name: string) {
  mkdirSync(SHOTS, { recursive: true })
  // Shoot once the hover fades and the space slide have settled (the loading pulse never ends).
  await expect
    .poll(() => ui.frameDoc().evaluate(() => document.getAnimations().filter((a) => a.playState === 'running' && a.effect?.getTiming().iterations !== Infinity).length))
    .toBe(0)
  return readPng(await ui.page.screenshot({ path: join(SHOTS, `${name}.png`) }))
}

const onPage = (ui: Harness) => ui.page.mouse.move(900, 400)
/** The pointer over the sidebar's empty area under the rows, where nothing is hovered but the sidebar itself. */
const onSidebar = (ui: Harness) => ui.page.mouse.move(130, 25)

async function darkMode(ui: Harness) {
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
}

/** Twelve favorites, 4 rows of 3 at the default width. */
async function twelveFavorites(ui: Harness) {
  const s = await state(ui)
  const seed = s.tabs[tab(1)]
  // An edit made while the harness still settles its first snapshot can be overwritten by it; repeat until it shows.
  await expect(async () => {
    const ids = [1, 2, 3, 4].map(tab)
    for (let i = 5; i <= 12; i++) {
      const id = tab(100 + i)
      await host(ui, ['tabs', id], { ...seed, id, url: `https://site${i}.example.com/`, title: `Site ${i}`, pinnedUrl: `https://site${i}.example.com/` })
      ids.push(id)
    }
    await host(ui, ['favorites', PROFILE], ids)
    await expect(ui.frame.locator('.clu-favorites .clu-favorite')).toHaveCount(12, { timeout: 1000 })
  }).toPass()
}

test('rest: no section labels, a divider before Today, the pill shows the host alone, a dot for a space without an icon', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await onPage(ui)
  // Neither Pinned nor Today is labeled; a divider sets Today apart and the header has no border at rest.
  await expect(nav.getByText('Pinned', { exact: true })).toHaveCount(0)
  await expect(nav.getByText('Today', { exact: true })).toHaveCount(0)
  await expect(nav.locator('.clu-today-divider')).toHaveCount(1)
  await expect(nav.locator('.clu-sidebar[data-scrolled]')).toHaveCount(0)
  // Copy link, site controls and Clear show only while the pointer is over the sidebar (faded out).
  await expect(nav.locator('[data-shell="copy-url"]')).toHaveCSS('opacity', '0')
  await expect(nav.locator('[data-clear-today]')).toBeHidden()
  // Home's icon is a dot, not a letter, dark while active.
  const active = nav.locator('.clu-space-icon[aria-pressed="true"]')
  await expect(active.locator('.clu-space-dot')).toHaveCount(1)
  await expect(nav.locator('.clu-space-letter')).toHaveCount(0)
  // The items center between the Library and + on a 34 pitch: with 4 spaces at 214 the
  // first center is 58.
  const first = await nav.locator('.clu-space-icon[data-index="0"]').boundingBox()
  const spaces = await nav.locator('.clu-space-icon').count()
  expect(Math.abs((first?.x ?? 0) + (first?.width ?? 0) / 2 - (214 / 2 - ((spaces - 1) * 34) / 2))).toBeLessThanOrEqual(1)
  // Back and forward are arrows with a shaft, 15 wide, not 5 wide chevrons.
  const back = await nav.locator('[data-shell="nav-back"] svg path').boundingBox()
  expect(back?.width ?? 0).toBeGreaterThanOrEqual(12)
  // The folder row has no chevron; its glyph is drawn in the space accent.
  const folder = nav.locator('.clu-folder-row').first()
  // Its svgs are the glyph and the hover minus: no chevron and no dotted menu glyph on hover.
  await expect(folder.locator('svg')).toHaveCount(2)
  await expect(folder.locator('.clu-folder-glyph svg:visible')).toHaveCount(1)
  await expect(folder.locator('.clu-folder-glyph')).toHaveCSS('color', 'rgb(59, 130, 246)')
  // Favorites carry no loaded dot; the media bar is a 33 pt white bar with no title.
  await expect(nav.locator('.clu-favorite-dot')).toHaveCount(0)
  // A long title ends in an ellipsis, not a fade.
  await expect(nav.locator('.clu-tab-row .clu-row-title').first()).toHaveCSS('text-overflow', 'ellipsis')
  await expect(nav.locator('.clu-tab-row .clu-row-title').first()).toHaveCSS('mask-image', 'none')
  expect((await nav.locator('.clu-media-card').boundingBox())?.height).toBe(33)
  await expect(nav.locator('.clu-media-card')).not.toContainText('Now playing')

  const png = await shot(ui, '01-rest-light')
  // The window background is the default pastel gradient: low saturation at the top left, warmer at the top right.
  const left = colorAt(png, 4, 3, SCALE) as [number, number, number]
  const right = colorAt(png, 1276, 3, SCALE) as [number, number, number]
  expect(rgbToHsl(left).s).toBeLessThan(0.6)
  expect(rgbToHsl(left).l).toBeGreaterThan(0.84)
  expect(right[0]).toBeGreaterThan(left[0])
})

test('sidebar hover: copy link and site controls show in the pill, Clear shows on the divider and clears Today', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await onSidebar(ui)
  await expect(nav.locator('[data-shell="copy-url"]')).toBeVisible()
  await expect(nav.locator('[data-site-controls-button]')).toBeVisible()
  const clear = nav.locator('[data-clear-today]')
  await expect(clear).toBeVisible()
  // The divider line stops short of Clear.
  const line = await nav.locator('.clu-today-divider').evaluate((el) => getComputedStyle(el, '::before').right)
  expect(parseFloat(line)).toBeGreaterThan(40)
  await shot(ui, '02-sidebar-hover-light')
  await clear.click()
  await expect.poll(() => hostCalls(ui, 'archive.clearToday')).toEqual([{ spaceId: HOME }])
  // With Today empty the divider goes too: New Tab follows the pinned rows.
  await expect(nav.locator('.clu-today-divider')).toHaveCSS('height', '0px')
  await expect(clear).toHaveCount(0)
})

test('sliders button: 28.25 from copy link, its tooltip stays in the sidebar, pressed while open', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await onSidebar(ui)
  // Measured once their 150 ms fade and settle has ended.
  await expect(nav.locator('[data-slot="site-controls"]')).toHaveCSS('scale', '1')
  const copy = await nav.locator('[data-shell="copy-url"]').boundingBox()
  const sliders = nav.locator('[data-site-controls-button]')
  const box = await sliders.boundingBox()
  // Copy link centered at x 158.75, site controls at 187 (pitch 28.25).
  expect(Math.abs((copy?.x ?? 0) + (copy?.width ?? 0) / 2 - 158.75)).toBeLessThanOrEqual(0.5)
  expect(Math.abs((box?.x ?? 0) + (box?.width ?? 0) / 2 - 187)).toBeLessThanOrEqual(0.5)
  await ui.page.mouse.move((box?.x ?? 0) + 4, (box?.y ?? 0) + 12)
  const tip = nav.locator('.clu-tooltip-anchor:has(> [data-site-controls-button]) > .clu-tooltip')
  await expect(tip).toBeVisible()
  const sidebar = await nav.boundingBox()
  const tipBox = await tip.boundingBox()
  expect((tipBox?.x ?? 0) + (tipBox?.width ?? 0)).toBeLessThanOrEqual((sidebar?.x ?? 0) + (sidebar?.width ?? 0))
  await sliders.click()
  await expect(sliders).toHaveAttribute('aria-expanded', 'true')
  const pressed = await sliders.evaluate((el) => {
    const s = getComputedStyle(el, '::before')
    const r = el.getBoundingClientRect()
    return { w: r.width - 2 * parseFloat(s.left), h: r.height - 2 * parseFloat(s.top), r: s.borderRadius, bg: s.backgroundColor }
  })
  expect(pressed).toMatchObject({ w: 28, h: 26, r: '8px' })
  expect(pressed.bg).not.toBe('rgba(0, 0, 0, 0)')
  await shot(ui, 'sliders-pressed')
})

test('a clicked row keeps neither Clear nor its close button once the pointer leaves the sidebar', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const row = nav.locator(`[data-tab-id="${tab(21)}"]`)
  await row.click()
  await onPage(ui)
  // Mouse focus stays in the sidebar, but these show only under the pointer (keyboard focus still shows them).
  await expect(nav.locator('[data-clear-today]')).toBeHidden()
  await expect(row.locator('.clu-row-close')).toHaveCSS('opacity', '0')
})

test('while the command bar opens a new tab, New Tab is the selected row', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await nav.locator('[data-new-tab="true"]').click()
  await expect(ui.overlay.locator('.clu-cmd')).toBeVisible()
  await expect(nav.locator('[data-new-tab="true"]')).toHaveAttribute('data-active', 'true')
  // The current tab steps back to rest, so only one row reads as selected.
  await expect(nav.locator('.clu-tab-row[data-active="true"]')).toHaveCSS('box-shadow', 'none')
  await onPage(ui)
  await shot(ui, '12a-command-new-tab-light')
})

test('the row that opened a Peek shows the peeked page\'s favicon at its right end', async ({ ui }) => {
  const rect = { x: 400, y: 120, width: 600, height: 400 }
  await host(ui, ['windows', WINDOW, 'peek'], { seq: 1, url: 'https://peek.example.com/', host: 'peek.example.com', title: 'Peek', favicon: 'data:image/gif;base64,R0lGODlhAQABAAAAACw=', canGoBack: false, originRect: rect, cardRect: rect })
  const icon = ui.frame.locator('nav.clu-sidebar .clu-tab-row[data-active="true"] [data-peek-icon]')
  await expect(icon).toHaveCount(1)
  // 16 pt at x 180 to 196 at the 214 width.
  const box = await icon.boundingBox()
  expect(Math.abs((box?.x ?? 0) - 180)).toBeLessThanOrEqual(1)
  expect(box?.width).toBe(16)
  await expect(ui.frame.locator('nav.clu-sidebar [data-peek-icon]')).toHaveCount(1)
  await host(ui, ['windows', WINDOW, 'peek'], null)
  await expect(icon).toHaveCount(0)
})

test('an http page shows its host only, no security glyph', async ({ ui }) => {
  await host(ui, ['tabs', tab(21), 'url'], 'http://plain.example.com/page')
  await expect(ui.frame.locator('[data-shell="url-host"]')).toHaveText('plain.example.com')
  await expect(ui.frame.locator('[data-shell="url-pill"]')).toHaveAttribute('data-security', 'insecure')
  await expect(ui.frame.locator('[data-shell="url-pill"] svg')).toHaveCount(2)
  await expect(ui.frame.locator('[data-shell="url-pill"]')).not.toContainText('Not secure')
})

test('row hover and space header hover', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await nav.locator(`[data-tab-id="${tab(20)}"]`).hover()
  await shot(ui, '03-row-hover-light')
  const header = nav.locator('.clu-space-header')
  await header.hover()
  // On hover the header fills like a row and shows the pinned chevron and the three-dot button.
  await expect(header.locator('.clu-space-header-toggle')).toBeVisible()
  await expect(header.locator('.clu-space-header-more')).toBeVisible()
  await shot(ui, '04-space-header-hover-light')
  // The three-dot button takes its own rounded hover over the row's.
  const more = header.locator('.clu-space-header-more')
  await expect(more).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)')
  await more.hover()
  await expect(more).toHaveCSS('background-color', 'rgba(0, 0, 0, 0.07)')
  await expect(more).toHaveCSS('border-radius', '4px')
  await shot(ui, '04b-space-dots-hover-light')
  // "> name": the chevron sits left of the name (x 13 and 23.5 of the sidebar), and the whole row toggles.
  const side = await nav.boundingBox()
  await expect.poll(async () => ((await header.locator('.clu-space-header-name').boundingBox())?.x ?? 0) - (side?.x ?? 0)).toBe(23.5)
  await header.locator('.clu-space-header-name').click()
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.pinnedCollapsed).toBe(true)
  await expect(nav.locator(`[data-tab-id="${tab(10)}"]`)).toHaveCount(0)
  await onPage(ui)
  // Collapsed, the chevron stays, pointing right, its left end at x 13.
  await expect(header.locator('.clu-space-header-toggle')).toBeVisible()
  const chevron = await header.locator('.clu-space-header-toggle svg path').boundingBox()
  expect(Math.round(((chevron?.x ?? 0) - (side?.x ?? 0)) * 2) / 2).toBe(13)
  await shot(ui, '05-pinned-collapsed-light')
  await header.click({ position: { x: 120, y: 10 } })
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.pinnedCollapsed).toBe(false)
})

test('a space with an icon shows it in the header; a folder collapses to the closed glyph', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await host(ui, ['spaces', HOME, 'icon'], { kind: 'emoji', value: '🏠' })
  await expect(nav.locator('.clu-space-header-emoji')).toHaveText('🏠')
  await nav.locator('.clu-folder-row').first().click()
  await expect(nav.locator('.clu-folder-glyph[data-open="false"]')).toHaveCount(1)
  await onPage(ui)
  await shot(ui, '06-header-icon-folder-closed-light')
})

test('loading: the row shows the accent square, never a spinner, and the pill pulses above the card', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await ui.frameDoc().evaluate((id) => window.clu.tabs.activate({ tabId: id }), tab(23))
  await expect(ui.frame.locator('[data-shell="card-loading"]')).toBeVisible()
  await expect(nav.locator(`[data-tab-id="${tab(23)}"] [data-loading-icon]`)).toHaveCount(1)
  await expect(nav.locator('[data-spinner]')).toHaveCount(0)
  await onPage(ui)
  // Stop reads as light as the disabled arrows beside it.
  await expect(nav.locator('[data-shell="nav-stop"]')).toHaveCSS('color', /^rgba\(0, 0, 0, 0\.22\d*\)$/)
  await shot(ui, '07-loading-light')
})

test('twelve favorites, the tooltip and the row menu', async ({ ui }) => {
  await twelveFavorites(ui)
  await onPage(ui)
  await shot(ui, '08-favorites-12-light')
  await ui.frame.locator('[data-shell="sidebar-toggle"]').hover()
  await expect(ui.frame.locator('.clu-tooltip')).toBeVisible()
  await shot(ui, '09-toolbar-tooltip-light')
  await ui.frame.locator(`[data-tab-id="${tab(20)}"]`).click({ button: 'right' })
  await expect(ui.overlay.getByRole('menu').first()).toBeVisible()
  await shot(ui, '10-row-menu-light')
})

test('the empty page is the bare tinted card', async ({ ui }) => {
  await host(ui, ['windows', WINDOW, 'activeTabId'], null)
  await expect(ui.frame.locator('[data-surface="page-card"]')).toHaveText('')
  // The pill shows a 12 pt magnifier at x 20 and "Search..." below 228.
  const pill = ui.frame.locator('[data-shell="url-host"]')
  await expect(pill).toHaveText('Search...')
  const glyph = await pill.locator('.clu-url-search-icon').boundingBox()
  expect(Math.abs((glyph?.x ?? 0) - 20)).toBeLessThanOrEqual(1)
  expect(glyph?.width).toBe(12)
  // On the text's line, centered in the pill, not on a line of its own above "Search...".
  const box = await pill.boundingBox()
  expect(Math.abs((glyph?.y ?? 0) + 6 - ((box?.y ?? 0) + (box?.height ?? 0) / 2))).toBeLessThanOrEqual(2)
  await onPage(ui)
  await shot(ui, '11-empty-page-light')
})

test('the default theme: an indigo space paints the lavender gradient', async ({ ui }) => {
  await host(ui, ['spaces', HOME, 'color'], 'indigo')
  await onPage(ui)
  const png = await shot(ui, '12-default-lavender-light')
  // Window top left #dadbf1 for a new space with the default theme.
  const tl = colorAt(png, 4, 3, SCALE) as [number, number, number]
  const want = [0xda, 0xdb, 0xf1]
  expect(Math.max(...tl.map((v, i) => Math.abs(v - (want[i] ?? 0))))).toBeLessThanOrEqual(8)
})

test('dark: the sidebar is a muted gray of the hue, not a saturated navy', async ({ ui }) => {
  await darkMode(ui)
  await onPage(ui)
  const png = await shot(ui, '13-rest-dark')
  const tint = colorAt(png, 130, 3, SCALE) as [number, number, number]
  expect(rgbToHsl(tint).s).toBeLessThanOrEqual(0.2)
  expect(rgbToHsl(tint).l).toBeLessThan(0.22)
  await onSidebar(ui)
  await shot(ui, '14-sidebar-hover-dark')
  await ui.frame.locator(`[data-tab-id="${tab(20)}"]`).hover()
  await shot(ui, '15-row-hover-dark')
})
