// Sidebar look: shoots the
// sidebar at five widths in light and dark into shots/parity/sidebar/ and pins every breakpoint and size,
// so a change that brings a difference back fails here.
import type { Locator } from '@playwright/test'
import { mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { expect, harnessTest, state, tab, WINDOW, type Harness } from './fixture'

const test = harnessTest('parity-sidebar')
const SHOTS = process.env.CLU_SHOTS_DIR ? join(process.env.CLU_SHOTS_DIR, 'parity/sidebar') : resolve('shots/parity/sidebar')
const PROFILE = '00000000-0000-4000-8000-000200000001'
/** The sweep widths, with 214 for the light pass. */
const WIDTHS = [164, 212, 214, 296, 500] as const

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)
const hostCalls = (ui: Harness, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

async function settle(ui: Harness) {
  await expect
    .poll(() => ui.frameDoc().evaluate(() => document.getAnimations().filter((a) => a.playState === 'running' && a.effect?.getTiming().iterations !== Infinity).length))
    .toBe(0)
}

async function shot(ui: Harness, name: string) {
  mkdirSync(SHOTS, { recursive: true })
  await settle(ui)
  await ui.page.screenshot({ path: join(SHOTS, `${name}.png`) })
}

async function setMode(ui: Harness, mode: 'light' | 'dark') {
  await ui.frameDoc().evaluate((m) => window.clu.theme.setMode({ mode: m }), mode)
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe(mode)
}

async function setWidth(ui: Harness, width: number) {
  await ui.frameDoc().evaluate((w) => window.clu.shell.setSidebarWidth({ width: w, commit: true }), width)
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.sidebarRect.width).toBe(width)
  // The sidebar has drawn the new width (the state arrives before React paints it).
  await expect(ui.frame.locator('nav.clu-sidebar')).toHaveCSS('width', `${width}px`)
  await settle(ui)
}

/** Twelve favorites. */
async function twelveFavorites(ui: Harness) {
  const s = await state(ui)
  const seed = s.tabs[tab(1)]
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

const onPage = (ui: Harness) => ui.page.mouse.move(900, 400)
const box = async (ui: Harness, selector: string) => {
  const b = await ui.frame.locator(selector).first().boundingBox()
  if (!b) throw new Error(`no box for ${selector}`)
  return b
}

for (const mode of ['light', 'dark'] as const) {
  test(`widths (${mode}): the sidebar at 164, 212, 214, 296 and 500`, async ({ ui }) => {
    await setMode(ui, mode)
    await twelveFavorites(ui)
    for (const w of WIDTHS) {
      await setWidth(ui, w)
      await onPage(ui)
      await shot(ui, `w${w}-${mode}`)
      await ui.page.mouse.move(Math.round(w / 2), 600)
      await shot(ui, `w${w}-hover-${mode}`)
    }
  })
}

test('the top row fits inside the sidebar at every width, with the design\'s controls and boxes', async ({ ui }) => {
  test.setTimeout(60_000)
  const nav = ui.frame.locator('nav.clu-sidebar')
  for (const w of [164, 168, 190, 211, 212, 213, 216, 228, 300, 500]) {
    await setWidth(ui, w)
    const buttons = nav.locator('.clu-top-row .clu-icon-button')
    const n = await buttons.count()
    for (let i = 0; i < n; i++) {
      const b = await buttons.nth(i).boundingBox()
      // Nothing runs under the page card, and the last control's right edge is 6 from it.
      expect((b?.x ?? 0) + (b?.width ?? 0), `button ${i} at ${w}`).toBeLessThanOrEqual(w - 6 + 0.5)
    }
    const back = await box(ui, '[data-shell="nav-back"]')
    const forward = await box(ui, '[data-shell="nav-forward"]')
    if (w <= 211) {
      // Compact: back, forward and "..." 28 boxes at w - 90, w - 62, w - 34; no toggle, no reload.
      await expect(nav.locator('[data-shell="sidebar-toggle"]')).toHaveCount(0)
      await expect(nav.locator('[data-shell="nav-reload"]')).toHaveCount(0)
      const more = await box(ui, '[data-shell="more"]')
      expect([back.x, forward.x, more.x, back.width, more.height]).toEqual([w - 90, w - 62, w - 34, 28, 28])
    } else {
      // Full: toggle at min(77, w - 140), back w - 106, forward w - 72, reload w - 38, all 32.
      await expect(nav.locator('[data-shell="more"]')).toHaveCount(0)
      const toggle = await box(ui, '[data-shell="sidebar-toggle"]')
      const reload = await box(ui, '[data-shell="nav-reload"]')
      expect([toggle.x, back.x, forward.x, reload.x, reload.width]).toEqual([Math.min(77, w - 140), w - 106, w - 72, w - 38, 32])
      // The buttons sit at y 7.5.
      for (const b of [toggle, back, forward, reload]) expect(Math.abs(b.y - 7.5), `y ${b.y} at ${w}`).toBeLessThanOrEqual(0.5)
    }
  }
})

/** An element's background as 0 to 255 channels, whether the computed value is rgb() or color(srgb ...). */
async function rgbOf(el: Locator): Promise<number[]> {
  const bg = await el.evaluate((e) => getComputedStyle(e).backgroundColor)
  const n = (bg.match(/[\d.]+/g) ?? []).map(Number)
  return bg.startsWith('color(') ? n.slice(0, 3).map((v) => Math.round(v * 255)) : n.slice(0, 3)
}

test('"..." opens the design\'s popover (200 wide, 40 pt rows, icons and keys), turns into ×, and Toggle Sidebar hides the sidebar', async ({ ui }) => {
  await setWidth(ui, 190)
  await ui.frame.locator('[data-shell="more"]').click()
  const pop = ui.overlay.locator('[data-surface="menu-popover"] .clu-menu-popover')
  await expect(pop.locator('.clu-menu-popover-label')).toHaveText(['Reload', 'Toggle Sidebar'])
  await expect(pop.locator('.clu-menu-popover-keys')).toHaveText(['⌘ R', '⌘ S'])
  await expect(pop.locator('.clu-menu-popover-icon svg')).toHaveCount(2)
  await expect(pop).toHaveCSS('width', '200px')
  await expect(pop).toHaveCSS('height', '96px')
  await expect(pop.locator('.clu-menu-popover-row').first()).toHaveCSS('height', '40px')
  await expect(pop.locator('.clu-menu-popover-icon').first()).toHaveCSS('width', '21px')
  // The default space has no theme, so the panel is gray, not the blue accent's navy.
  expect(await rgbOf(pop)).toEqual([21, 21, 22])
  // No light ring around the panel, and its top 16.5 under the button.
  await expect(pop).toHaveCSS('box-shadow', 'rgba(0, 0, 0, 0.45) 0px 8px 24px 0px')
  const more = await box(ui, '[data-shell="more"]')
  // Floored: the top row's 0.25 lift leaves the gap at 17.5 while the panel stays put.
  await expect.poll(async () => Math.floor(((await pop.boundingBox())?.y ?? 0) - (more.y + more.height))).toBe(17)
  // The button shows × while its popover is open.
  await expect(ui.frame.locator('[data-shell="more"]')).toHaveAttribute('aria-label', 'Close')
  await pop.getByText('Toggle Sidebar').hover()
  await shot(ui, 'more-popover-dark')
  await pop.getByText('Toggle Sidebar').click()
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
})

test('"+" opens the design\'s popover above it with New Space, New Folder, New Split ⌃⇧= and New Tab ⌘T, and turns into ×', async ({ ui }) => {
  await ui.frame.locator('.clu-space-new').click()
  const pop = ui.overlay.locator('[data-surface="menu-popover"] .clu-menu-popover')
  await expect(pop.locator('.clu-menu-popover-label')).toHaveText(['New Space', 'New Folder', 'New Split', 'New Tab'])
  await expect(pop.locator('.clu-menu-popover-keys')).toHaveText(['⌃ ⇧ =', '⌘ T'])
  await expect(pop).toHaveCSS('width', '210px')
  // Once its 120 ms grow has ended.
  await expect.poll(async () => Math.round((await pop.boundingBox())?.width ?? 0)).toBe(210)
  await expect(pop.locator('.clu-menu-popover-row').first()).toHaveCSS('height', '40px')
  expect(await rgbOf(pop)).toEqual([21, 21, 22])
  const panel = await pop.boundingBox()
  const plus = await box(ui, '.clu-space-new')
  // Above the button: the panel ends over it.
  expect((panel?.y ?? 0) + (panel?.height ?? 0)).toBeLessThan(plus.y)
  await expect(ui.frame.locator('.clu-space-new')).toHaveAttribute('aria-label', 'Close')
  // A bare × on the sidebar while open, no hover box under the pointer that opened it.
  await expect(ui.frame.locator('.clu-space-new')).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)')
  await shot(ui, 'plus-popover-dark')
  await ui.page.keyboard.press('Escape')
  await expect(ui.overlay.locator('[data-surface="menu-popover"]')).toHaveCount(0)
  await expect(ui.frame.locator('.clu-space-new')).toHaveAttribute('aria-label', 'New')
})

test('the pill reads "Search..." below 228 and "Search or Enter URL..." from 228', async ({ ui }) => {
  await host(ui, ['windows', WINDOW, 'activeTabId'], null)
  const pill = ui.frame.locator('[data-shell="url-host"]')
  await setWidth(ui, 227)
  await expect(pill).toHaveText('Search...')
  await setWidth(ui, 228)
  await expect(pill).toHaveText('Search or Enter URL...')
  // The pill keeps its box at every width: 8 in from both edges, 36.5 tall.
  for (const w of [164, 500]) {
    await setWidth(ui, w)
    const b = await box(ui, '[data-shell="url-pill"]')
    expect(Math.abs(b.x - 7.5)).toBeLessThanOrEqual(0.5)
    expect(Math.abs(b.x + b.width - (w - 7.5))).toBeLessThanOrEqual(0.5)
    expect(b.height).toBe(36.5)
  }
})

test('favorites take 2, 3 and 4 columns at the design\'s breakpoints, tiles 47 tall, favicons 16', async ({ ui }) => {
  await twelveFavorites(ui)
  const grid = ui.frame.locator('.clu-favorites')
  const measured: [number, number][] = [[164, 2], [172, 2], [173, 3], [213, 3], [227, 3], [228, 4], [296, 4], [500, 4]]
  for (const [w, n] of measured) {
    await setWidth(ui, w)
    await expect(grid).toHaveAttribute('data-columns', String(n))
    const tile = await box(ui, '.clu-favorite-tile')
    // Tile width (w - 16 - 8 (n - 1)) / n, 47 tall, first tile at x 8.
    expect(Math.abs(tile.width - (w - 16 - 8 * (n - 1)) / n), `tile width at ${w}`).toBeLessThanOrEqual(0.5)
    expect(tile.height).toBe(47)
    expect(tile.x).toBe(8)
  }
  const icon = await box(ui, '.clu-favorite-tile img, .clu-favorite-tile .clu-letter-tile')
  expect(icon.width).toBe(16)
})

test('the bottom bar keeps the Library at 8 and + at w - 40, the space items centered between them', async ({ ui }) => {
  for (const w of [164, 213, 300, 500]) {
    await setWidth(ui, w)
    const library = await box(ui, '.clu-footer .clu-icon-button')
    const plus = await box(ui, '.clu-space-new')
    expect([library.x, library.width, plus.x, plus.width]).toEqual([8, 32, w - 40, 32])
    const icons = ui.frame.locator('.clu-space-icon')
    const first = await icons.first().boundingBox()
    const last = await icons.last().boundingBox()
    const mid = ((first?.x ?? 0) + (last?.x ?? 0) + (last?.width ?? 0)) / 2
    expect(Math.abs(mid - w / 2), `centered at ${w}`).toBeLessThanOrEqual(1)
    if (w >= 300) {
      // Room enough: 36 wide on a 34 pitch.
      expect(first?.width).toBe(36)
      if ((await icons.count()) > 1) expect(((await icons.nth(1).boundingBox())?.x ?? 0) - (first?.x ?? 0)).toBe(34)
    }
  }
})

test('the sidebar resizes 164 to 500 under the pointer, ignoring the first point, with the edge bar', async ({ ui }) => {
  const handle = ui.frame.locator('[data-shell="resize-handle"]')
  const hb = await handle.boundingBox()
  // The band runs up the whole bar, from 4.5 below the card's top.
  expect([hb?.x, hb?.width, hb?.y]).toEqual([210, 8, 14.5])
  const y = 400
  await ui.page.mouse.move(213, y)
  // The bar shows about 250 ms after the pointer rests on the band.
  const bar = ui.frame.locator('[data-shell="resize-bar"]')
  await expect(bar).toHaveAttribute('data-shown', 'true')
  await expect(bar).toHaveCSS('opacity', '1')
  const bb = await bar.boundingBox()
  expect([bb?.x, bb?.width, bb?.y]).toEqual([208, 4, 14.5])
  // Drawn in the space's accent.
  const accent = await ui.frameDoc().evaluate(() => {
    const probe = document.createElement('i')
    probe.style.color = getComputedStyle(document.documentElement).getPropertyValue('--accent')
    document.body.append(probe)
    const c = getComputedStyle(probe).color
    probe.remove()
    return c
  })
  await expect(bar).toHaveCSS('background-color', accent)
  await shot(ui, 'edge-bar-light')
  await ui.page.mouse.down()
  await ui.page.mouse.move(214, y)
  await ui.page.mouse.move(260, y, { steps: 4 })
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.sidebarRect.width).toBe(261)
  await ui.page.mouse.move(900, y, { steps: 6 })
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.sidebarRect.width).toBe(500)
  await ui.page.mouse.move(20, y, { steps: 6 })
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.sidebarRect.width).toBe(164)
  await ui.page.mouse.up()
  expect((await hostCalls(ui, 'shell.setSidebarWidth')).at(-1)).toEqual({ width: 20 - 213 + 214, commit: true })
})

test('Today\'s divider is 20.5 tall with "↓ Clear" fading in while the pointer is over the sidebar', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await onPage(ui)
  await expect(nav.locator('.clu-today-divider')).toHaveCSS('height', '20.5px')
  const clear = nav.locator('[data-clear-today]')
  await expect(clear).toHaveCSS('opacity', '0')
  await ui.page.mouse.move(100, 600)
  await expect(clear).toHaveCSS('opacity', '1')
  await expect(clear.locator('svg')).toHaveCount(1)
  await expect(clear).toHaveText('Clear')
  await shot(ui, 'today-clear-light')
})

test('the row menus are the design\'s, in its order', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const menu = ui.overlay.locator('[data-surface="menu"]')
  const pinned = nav.locator('.clu-tab-row[data-tab-id]').first()
  await pinned.click({ button: 'right' })
  await expect(menu.locator('.clu-menu-item').first()).toHaveText('Copy Link⇧⌘C')
  await expect(menu.locator('.clu-menu-item')).toHaveText(['Copy Link⇧⌘C', 'Share', 'Change Icon…', 'Rename…', 'Mute', 'Customize Page…', 'Edit Pinned Page', 'Add Split View', 'Duplicate', 'Move to', 'Remove Pin'])
  await shot(ui, 'menu-pinned-light')
})

test('no dots on pinned rows or tiles, and no shield in the pill', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await twelveFavorites(ui)
  await ui.page.mouse.move(100, 600)
  await expect(nav.locator('.clu-favorite-dot, .clu-row-dot, [data-dot]')).toHaveCount(0)
  await expect(nav.locator('[data-shell="url-pill"] [data-security-icon], [data-shell="url-pill"] .clu-shield')).toHaveCount(0)
  // The pill's only glyphs while hovered are copy link and site controls: 26 square, centered 47.25 and 19 from the
  // pill's right edge. The host text is SF 12.5 at weight 500.
  await expect(nav.locator('[data-shell="url-pill"] .clu-icon-button:visible')).toHaveCount(2)
  // Measured once their 150 ms fade and settle has ended.
  await expect(nav.locator('[data-shell="copy-url"]')).toHaveCSS('scale', '1')
  const pill = await box(ui, '[data-shell="url-pill"]')
  const copy = await box(ui, '[data-shell="copy-url"]')
  const site = await box(ui, '[data-site-controls-button]')
  expect([copy.width, copy.height, site.width, site.height]).toEqual([26, 26, 26, 26])
  expect(pill.x + pill.width - (copy.x + copy.width / 2)).toBeCloseTo(47.25, 1)
  expect(pill.x + pill.width - (site.x + site.width / 2)).toBeCloseTo(19, 1)
  await expect(nav.locator('[data-shell="url-host"]')).toHaveCSS('font-weight', '500')
})

// The band's 5 pt limit, the dwell and the fast-pointer rule are pinned in EdgeHotZone.test.tsx and peek.test.ts.
test('the hidden sidebar reveals once the pointer rests within 5 of the edge', async ({ ui }) => {
  await ui.frameDoc().evaluate(() => window.clu.shell.toggleSidebar())
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
  await expect(ui.frame.locator('[data-shell="hot-zone"]')).toHaveCount(1)
  await ui.page.mouse.move(3, 400)
  await expect.poll(() => hostCalls(ui, 'shell.setSidebarPeek')).toEqual([{ open: true }])
})

test('a pointer flung left reveals the hidden sidebar from x 9, before it lands in the zone; a held button does not', async ({ ui }) => {
  await ui.frameDoc().evaluate(() => window.clu.shell.toggleSidebar())
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
  await expect(ui.frame.locator('[data-shell="hot-zone"]')).toHaveCount(1)
  // Dragged to the edge with the button held: nothing, however long it rests there.
  await ui.page.mouse.move(3, 10)
  await ui.page.mouse.down()
  await ui.page.mouse.move(2, 400)
  // A dozen frames, several dwells long.
  await ui.frameDoc().evaluate(() => new Promise((done) => {
    let n = 0
    const tick = () => (++n >= 12 ? done(null) : requestAnimationFrame(tick))
    requestAnimationFrame(tick)
  }))
  expect(await hostCalls(ui, 'shell.setSidebarPeek')).toEqual([])
  await ui.page.mouse.move(300, 400)
  await ui.page.mouse.up()
  // One sample at x 300, the next at x 9: outside the zone, but moving into it.
  await ui.page.mouse.move(9, 400)
  await expect.poll(() => hostCalls(ui, 'shell.setSidebarPeek')).toEqual([{ open: true }])
})

test('dragging a Today row over Pinned draws the design\'s ring and line in the gap; over a folder, the band', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const row = nav.locator(`[data-tab-id="${tab(22)}"]`)
  const target = nav.locator('.clu-tab-row[data-tab-id]').first()
  const from = await row.boundingBox()
  const to = await target.boundingBox()
  if (!from || !to) throw new Error('rows not laid out')
  await ui.page.mouse.move(from.x + from.width / 2, from.y + from.height / 2)
  await ui.page.mouse.down()
  // Into the lower third of the first pinned row: the line sits in the gap under it.
  for (let i = 1; i <= 10; i++) await ui.page.mouse.move(from.x + from.width / 2, from.y + from.height / 2 + ((to.y + to.height - 4 - (from.y + from.height / 2)) * i) / 10)
  const line = ui.frame.locator('.clu-drop-line')
  await expect(line).toBeVisible()
  const lb = await line.boundingBox()
  const at = (await target.boundingBox()) ?? to
  // The ring centers 11.5 into the row (x 19.5 at the 8 inset) and in the 5 pt gap; the line ends 3 short of the row.
  expect(lb?.x).toBe(at.x + 11.5 - 3.5)
  expect(lb?.height).toBe(7)
  expect((lb?.y ?? 0) + 3.5).toBe(at.y + at.height + 2.5)
  expect((lb?.x ?? 0) + (lb?.width ?? 0)).toBe(at.x + at.width - 3)
  // The dragged row keeps the selected style at 0.8 opacity.
  await expect(ui.frame.locator('.clu-drag-ghost')).toHaveCSS('opacity', '0.8')
  await shot(ui, 'drag-line-light')
  // Over the middle of a folder row: the band behind it, no line.
  const folder = nav.locator('.clu-folder-row').first()
  const fb = await folder.boundingBox()
  if (!fb) throw new Error('no folder')
  await ui.page.mouse.move(fb.x + fb.width / 2, fb.y + fb.height / 2, { steps: 6 })
  await expect(folder).toHaveAttribute('data-drop-into', 'true')
  await expect(line).toHaveCount(0)
  await shot(ui, 'drag-folder-light')
  await ui.page.keyboard.press('Escape')
  await ui.page.mouse.up()
  await expect(folder).not.toHaveAttribute('data-drop-into', 'true')
})

test('a folder expands in 190 ms and collapses in 225 ms, the rows below sliding by its children', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const folder = nav.locator('.clu-folder-row').first()
  const durations = () =>
    nav.locator('.clu-new-tab').evaluate((el) => el.getAnimations().map((a) => (a.effect?.getTiming().duration as number) ?? 0))
  await folder.click()
  await expect.poll(durations).toEqual([225])
  await settle(ui)
  await folder.click()
  await expect.poll(durations).toEqual([190])
})

test('a pinned tab that navigated away shows its favicon as a button, "/", and "Back to Pinned URL" on hover', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const pinned = nav.locator('.clu-tab-row[data-tab-id]').first()
  const id = await pinned.getAttribute('data-tab-id')
  if (!id) throw new Error('no pinned row')
  await host(ui, ['tabs', id, 'offPinned'], true)
  await expect(pinned).toHaveAttribute('data-off-pinned', 'true')
  await expect(pinned.locator('.clu-row-slash')).toHaveText('/')
  const subtitle = pinned.locator('.clu-row-subtitle')
  await expect(subtitle).toHaveCSS('opacity', '0')
  await pinned.locator('.clu-row-reset').hover()
  await expect(subtitle).toHaveCSS('opacity', '1')
  await expect(subtitle).toHaveText('Back to Pinned URL')
  await shot(ui, 'pinned-navigated-light')
  await pinned.locator('.clu-row-reset').click()
  expect(await hostCalls(ui, 'tabs.resetToPinned')).toEqual([{ tabId: id }])
})

// Each row state, shot in light and dark.
for (const mode of ['light', 'dark'] as const) {
  test(`row states (${mode}): hover, press, loading and audio rows, a split row and the hidden sidebar's reveal`, async ({ ui }) => {
    test.setTimeout(60_000)
    await setMode(ui, mode)
    await setWidth(ui, 213)
    const nav = ui.frame.locator('nav.clu-sidebar')
    // Hover: the row's fill in one frame and its close control (x on Today, - on a pinned row).
    const today = nav.locator(`[data-tab-id="${tab(22)}"]`)
    await today.hover()
    await expect(today.locator('.clu-row-close')).toHaveAttribute('data-close', 'close')
    await shot(ui, `b9-hover-today-${mode}`)
    const pinned = nav.locator('.clu-tab-row[data-tab-id]').first()
    await pinned.hover()
    await expect(pinned.locator('.clu-row-close')).toHaveAttribute('data-close', 'unload')
    await shot(ui, `b9-hover-pinned-${mode}`)
    // Press: selected on mouse-down, shrunk to 0.985 while held.
    const box = await today.boundingBox()
    if (!box) throw new Error('no row')
    await ui.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2)
    await ui.page.mouse.down()
    await expect(today).toHaveCSS('transform', /matrix\(0\.985/)
    await shot(ui, `b9-pressed-${mode}`)
    await ui.page.mouse.up()
    // Loading and audio rows are in the seed; a split row joins two Today rows.
    await expect(nav.locator('.clu-tab-row[data-loading="true"]')).not.toHaveCount(0)
    await expect(nav.locator('.clu-tab-row[data-audible="true"] .clu-row-audio')).not.toHaveCount(0)
    await ui.frameDoc().evaluate(([a, b]) => window.clu.split.create({ tabIds: [a, b], dir: 'row' }), [tab(20), tab(21)] as const)
    await expect(nav.locator('[data-split-id]')).toHaveCount(1)
    await onPage(ui)
    await shot(ui, `b9-rows-${mode}`)
    // The hidden sidebar and its reveal.
    await ui.frameDoc().evaluate(() => window.clu.shell.toggleSidebar())
    await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
    await ui.page.mouse.move(3, 400)
    await expect.poll(() => hostCalls(ui, 'shell.setSidebarPeek')).toEqual([{ open: true }])
    await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.peeking).toBe(true)
    await shot(ui, `b9-reveal-${mode}`)
  })
}

// "−" unloads a pinned row, which stays where it is and at once
// shows "×"; the "×" then takes the row off the sidebar.
test('unloading a pinned row turns its "−" into "×", which removes the row', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const pinned = nav.locator('.clu-tab-row[data-tab-id]').first()
  const id = await pinned.getAttribute('data-tab-id')
  if (!id) throw new Error('no pinned row')
  await pinned.click()
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.activeTabId).toBe(id)
  await pinned.hover()
  await expect(pinned.locator('.clu-row-close')).toHaveAttribute('data-close', 'unload')
  await pinned.locator('.clu-row-close').click()
  await expect.poll(async () => (await state(ui)).tabs[id]?.lifecycle).toBe('unloaded')
  await expect(pinned.locator('.clu-row-close')).toHaveAttribute('data-close', 'remove')
  await expect(pinned.locator('.clu-row-close')).toHaveAttribute('aria-label', 'Remove from sidebar')
  expect((await state(ui)).windows[WINDOW]?.activeTabId).toBeNull()
  await shot(ui, 'b44-after-unload')
  await pinned.locator('.clu-row-close').click()
  expect(await hostCalls(ui, 'tabs.delete')).toEqual([{ nodeId: id }])
  await expect(nav.locator(`[data-tab-id="${id}"]`)).toHaveCount(0)
})

test('nothing in the sidebar falls past the window bottom, and the bottom bar keeps its bottom padding', async ({ ui }) => {
  await twelveFavorites(ui)
  for (const w of [164, 300]) {
    await setWidth(ui, w)
    for (const h of [400, 500, 600, 800, 1000]) {
      await ui.page.setViewportSize({ width: 1280, height: h })
      await settle(ui)
      const library = await box(ui, '.clu-footer .clu-icon-button')
      // The bar: its 32 box centered 25 up from the bottom edge.
      expect(Math.abs(library.y + library.height - (h - 9)), `library bottom at ${w}x${h}`).toBeLessThanOrEqual(0.5)
      const past = await ui.frameDoc().evaluate(() =>
        [...document.querySelectorAll('nav.clu-sidebar *')]
          .filter((e) => e.checkVisibility() && e.getBoundingClientRect().height > 0)
          .map((e) => {
            // Where it is drawn: its box cut by every clipping ancestor (a scrolled list may run past, its viewport may not).
            let bottom = e.getBoundingClientRect().bottom
            for (let a = e.parentElement; a; a = a.parentElement)
              if (getComputedStyle(a).overflowY !== 'visible') bottom = Math.min(bottom, a.getBoundingClientRect().bottom)
            return { e, bottom }
          })
          .filter(({ bottom }) => bottom > innerHeight + 0.5)
          .map(({ e, bottom }) => `${e.tagName}.${e.className} ${bottom}`),
      )
      expect(past, `drawn past the bottom at ${w}x${h}`).toEqual([])
    }
  }
  // The shot's peach shape: the Library button's tooltip (the tint at 35% over white) hanging below the pointer, cut
  // off by the window's edge. It hangs above instead, with its tight corner toward the pointer; the space items' are
  // pinned above the bar.
  for (const target of ['.clu-footer .clu-icon-button', '.clu-space-icon >> nth=-1', '.clu-space-new']) {
    const b = await box(ui, target)
    await ui.page.mouse.move(b.x + b.width / 2, b.y + b.height / 2)
    const tip = ui.frame.locator('.clu-footer [role="tooltip"]')
    await expect(tip).toBeVisible()
    if (target === '.clu-footer .clu-icon-button') await expect(tip).toHaveAttribute('data-above', 'true')
    const t = await tip.boundingBox()
    expect(t && t.y + t.height, target).toBeLessThanOrEqual(b.y + b.height / 2 - 9)
    expect(t && t.x + t.width, target).toBeLessThanOrEqual(1280)
    await shot(ui, `b28-tooltip-${target.replace(/\W+/g, '-')}`)
    await ui.page.mouse.move(640, 300)
    await expect(tip).toHaveCount(0)
  }
})

test('⇧⌘D puts the URL bar across the top of the page; its right-click asks for the toolbar menu', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await ui.page.mouse.click(600, 300)
  await ui.page.keyboard.press('Meta+Shift+D')
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.toolbar).toBe(true)
  // The card moves under the 38 pt band; the pill leaves the sidebar for the band, 30 tall on the top row's line.
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.pageRect.y).toBe(48)
  await expect(nav.locator('[data-shell="url-pill"]')).toHaveCount(0)
  const card = await box(ui, '[data-surface="page-card"]')
  const field = await box(ui, '[data-shell="toolbar"] [data-shell="url-pill"]')
  expect([field.x, field.width, field.height, field.y + field.height / 2]).toEqual([card.x, card.width, 30, 23.5])
  await shot(ui, 'toolbar-light')
  await ui.frame.locator('[data-shell="toolbar"] [data-shell="url-host"]').click({ button: 'right' })
  expect((await hostCalls(ui, 'shell.openToolbarMenu')).length).toBe(1)
  await ui.page.keyboard.press('Meta+Shift+D')
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.toolbar).toBe(false)
  await expect(ui.frame.locator('[data-shell="toolbar"]')).toHaveCount(0)
  await expect(nav.locator('[data-shell="url-pill"]')).toHaveCount(1)
})

test('a row that is not selected lifts from a press on its icon or title, even as the press blurs the frame', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  for (const part of ['.clu-row-reset', '.clu-row-title']) {
    const row = nav.locator('.clu-tab-row[data-tab-id][data-active="false"]').first()
    const id = await row.getAttribute('data-tab-id')
    if (!id) throw new Error('no unselected row')
    if (part === '.clu-row-reset') {
      await host(ui, ['tabs', id, 'offPinned'], true)
      await expect(row).toHaveAttribute('data-off-pinned', 'true')
    }
    const box = await nav.locator(`[data-tab-id="${id}"] ${part}`).first().boundingBox()
    if (!box) throw new Error('no press target')
    const x = box.x + box.width / 2
    const y = box.y + box.height / 2
    await ui.page.mouse.move(x, y)
    await ui.page.mouse.down()
    // The press selects the row at once; in the app the page then takes focus and the frame blurs.
    await expect(nav.locator(`[data-tab-id="${id}"][role="treeitem"]`)).toHaveAttribute('data-active', 'true')
    await ui.frameDoc().evaluate(() => window.dispatchEvent(new Event('blur')))
    await ui.page.mouse.move(x, y + 12, { steps: 4 })
    await expect(ui.frame.locator('.clu-drag-ghost')).toBeVisible()
    // A drop, not a click: the icon does not reset the row.
    await ui.page.mouse.up()
    await expect(ui.frame.locator('.clu-drag-ghost')).toHaveCount(0)
  }
  expect(await hostCalls(ui, 'tabs.resetToPinned')).toEqual([])
})
