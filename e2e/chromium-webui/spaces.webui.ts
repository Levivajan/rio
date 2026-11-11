// M4 spaces in the ui:fake harness (docs/chromium/m4.md): the space keys with no wrap, the swipe over the sidebar only,
// and the space editor opened from its table command. The FakeBridge answers as CluSpacesService does. Screenshots
// go to $CLU_SHOTS_DIR/chromium-m4-ui.
import { expect, harnessTest, state, WINDOW } from './fixture'

const test = harnessTest('chromium-m4-ui')

const HOME = '00000000-0000-4000-8000-000300000001'
const WORK = '00000000-0000-4000-8000-000300000002'
const READING = '00000000-0000-4000-8000-000300000003'

test('keys: Ctrl+N picks a space, Cmd+Option+arrows step through them and stop at either end', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Control+3')
  await expect(nav).toHaveAttribute('data-active-space', READING)
  await ui.page.keyboard.press('Meta+Alt+ArrowLeft')
  await expect(nav).toHaveAttribute('data-active-space', WORK)
  await ui.page.keyboard.press('Control+1')
  await expect(nav).toHaveAttribute('data-active-space', HOME)
  // No wrap: Left from the first space does nothing, so Right then lands on the second (a wrap would end on Home).
  await ui.page.keyboard.press('Meta+Alt+ArrowLeft')
  await ui.page.keyboard.press('Meta+Alt+ArrowRight')
  await expect(nav).toHaveAttribute('data-active-space', WORK)
  await expect(nav.locator('.clu-space-slider')).toHaveAttribute('data-state', 'idle')
  // Ctrl+9 with three spaces does nothing: Ctrl+1 afterwards is the only switch.
  await ui.page.keyboard.press('Control+9')
  await ui.page.keyboard.press('Control+1')
  await expect(nav).toHaveAttribute('data-active-space', HOME)
})

test('swipe: a horizontal wheel over the sidebar moves one space; over the page card it does nothing', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const sidebar = await nav.boundingBox()
  const card = await ui.frame.locator('[data-surface="page-card"]').boundingBox()
  if (!sidebar || !card) throw new Error('sidebar or page card missing')
  // Over the page card first: those wheels belong to the page and never reach the swipe.
  await ui.page.mouse.move(card.x + card.width / 2, card.y + card.height / 2)
  for (let i = 0; i < 10; i++) await ui.page.mouse.wheel(60, 0)
  // Then over the sidebar: one switch to the next space, so the window lands on Work and not on Reading.
  await ui.page.mouse.move(sidebar.x + sidebar.width / 2, sidebar.y + sidebar.height / 2)
  for (let i = 0; i < 10; i++) await ui.page.mouse.wheel(60, 0)
  await expect(nav).toHaveAttribute('data-active-space', WORK)
  await expect(nav.locator('.clu-space-slider')).toHaveAttribute('data-state', 'idle')
  const s = await state(ui)
  expect(s.windows[WINDOW]?.swipe).toBeNull()
  expect(s.windows[WINDOW]?.activeSpaceId).toBe(WORK)
  await ui.shot('swiped-to-work')
})

test('swipe: fingers moving right on the first space slide the Library in, moving left slide it back out', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await expect(nav).toHaveAttribute('data-active-space', HOME)
  const sidebar = await nav.boundingBox()
  if (!sidebar) throw new Error('sidebar missing')
  await ui.page.mouse.move(sidebar.x + sidebar.width / 2, sidebar.y + sidebar.height / 2)
  for (let i = 0; i < 10; i++) await ui.page.mouse.wheel(-60, 0)
  const library = ui.frame.locator('nav.clu-sidebar-library')
  await expect(library).toBeVisible()
  expect((await state(ui)).archivePanel[WINDOW]).toBe(true)
  await ui.shot('swiped-into-library')
  // The open Library's node, and whether the exit slide draws that same node (a fresh Library mounted in
  // the frame it closed, a one-frame swap).
  await ui.frameDoc().evaluate(() => {
    const w = window as unknown as { cluLib: Element | null; cluExitSame: boolean | null }
    w.cluLib = document.querySelector('nav.clu-sidebar-library .clu-library')
    w.cluExitSame = null
    const watch = new MutationObserver(() => {
      const exit = document.querySelector('.clu-library-exit')
      if (!exit || w.cluExitSame !== null) return
      w.cluExitSame = w.cluLib !== null && exit.contains(w.cluLib)
      watch.disconnect()
    })
    watch.observe(document.body, { childList: true, subtree: true, attributes: true, attributeFilter: ['class'] })
  })
  // Fingers moving left slide it back out to the first space, which stays the active one.
  for (let i = 0; i < 10; i++) await ui.page.mouse.wheel(60, 0)
  await expect(library).toHaveCount(0)
  expect(await ui.frameDoc().evaluate(() => (window as unknown as { cluExitSame: boolean | null }).cluExitSame)).toBe(true)
  await expect(nav).toHaveAttribute('data-active-space', HOME)
  const s = await state(ui)
  expect(s.archivePanel[WINDOW]).not.toBe(true)
  expect(s.windows[WINDOW]?.swipe).toBeNull()
  await ui.shot('swiped-out-of-library')
})

test('swipe: with the full-screen bar revealed, the Library sliding out keeps the bar offset it had open', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await ui.page.evaluate((w) => {
    const host = (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost
    host.edit(['windows', w, 'fullscreen'], true)
    host.edit(['windows', w, 'shell', 'titlebarReveal'], 52)
  }, WINDOW)
  const sidebar = await nav.boundingBox()
  if (!sidebar) throw new Error('sidebar missing')
  await ui.page.mouse.move(sidebar.x + sidebar.width / 2, sidebar.y + sidebar.height / 2)
  for (let i = 0; i < 10; i++) await ui.page.mouse.wheel(-60, 0)
  const library = ui.frame.locator('nav.clu-sidebar-library')
  await expect(library).toBeVisible()
  await expect.poll(() => library.evaluate((el) => getComputedStyle(el).paddingTop)).toBe('52px')
  await expect.poll(async () => (await state(ui)).archivePanel[WINDOW]).toBe(true)
  await ui.shot('library-open-under-bar')
  // The exit copy's padding, read the moment it mounts.
  await ui.frameDoc().evaluate(() => {
    const w = window as unknown as { cluExitPad: string | null }
    w.cluExitPad = null
    const watch = new MutationObserver(() => {
      const exit = document.querySelector('.clu-library-exit')
      if (!exit || w.cluExitPad !== null) return
      w.cluExitPad = getComputedStyle(exit).paddingTop
      watch.disconnect()
    })
    watch.observe(document.body, { childList: true, subtree: true, attributes: true, attributeFilter: ['class'] })
  })
  for (let i = 0; i < 10; i++) await ui.page.mouse.wheel(60, 0)
  await expect(library).toHaveCount(0)
  expect(await ui.frameDoc().evaluate(() => (window as unknown as { cluExitPad: string | null }).cluExitPad)).toBe('52px')
  await ui.shot('library-exit-under-bar')
})

test('editor: New Space opens the Create a Space panel over the sidebar; naming it and Create Space makes the space and shows it', async ({ ui }) => {
  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'spaces.newSpace' }))
  const panel = ui.overlay.getByRole('dialog', { name: 'Create a Space' })
  await expect(panel).toBeVisible()
  // It covers the sidebar between the top row and the bottom bar; the page is not dimmed.
  const box = await panel.boundingBox()
  const sidebar = await ui.frame.locator('nav.clu-sidebar').boundingBox()
  expect(box && sidebar).toBeTruthy()
  expect(Math.abs((box?.x ?? 0) - (sidebar?.x ?? 0))).toBeLessThanOrEqual(1)
  expect(Math.abs((box?.width ?? 0) - (sidebar?.width ?? 0))).toBeLessThanOrEqual(1)
  expect(box?.y).toBe(44)
  await expect(ui.overlay.locator('.clu-overlay-root')).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)')
  // Unnamed, Create Space dim, focus in the name field.
  const name = panel.getByLabel('Space name')
  await expect(name).toHaveValue('')
  await expect(name).toBeFocused()
  const create = panel.getByRole('button', { name: 'Create Space' })
  await expect(create).toBeDisabled()
  await expect(panel.getByRole('button', { name: 'Choose a Theme' })).toBeVisible()
  await expect(panel.getByLabel('Profile', { exact: true })).toBeVisible()
  // The bottom bar: no Library button and no "+", a dot marks where the new space goes.
  const footer = ui.frame.locator('nav.clu-sidebar footer')
  await expect(footer.getByRole('button', { name: 'View Library' })).toHaveCount(0)
  await expect(footer.locator('.clu-space-new')).toHaveCount(0)
  await expect(footer.locator('.clu-space-placeholder')).toHaveCount(1)
  await expect(footer.locator('.clu-space-icon[aria-pressed="true"]')).toHaveCount(0)
  // Under the panel the sidebar is bare: no URL pill, favorites, tab list or cards show through.
  await expect(ui.frame.locator('nav.clu-sidebar .clu-sidebar-creating')).toHaveCount(1)
  await expect(ui.frame.locator('nav.clu-sidebar .clu-space-slider')).toHaveCount(0)
  await expect(ui.frame.locator('nav.clu-sidebar .clu-url-pill, nav.clu-sidebar .clu-media-card')).toHaveCount(0)
  await expect(panel).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)')
  await ui.shot('create-space-empty')
  await name.fill('Garden')
  await expect(create).toBeEnabled()
  await panel.getByRole('button', { name: 'Choose a Theme' }).click()
  await panel.getByRole('button', { name: 'Teal' }).click()
  await ui.shot('space-editor')
  await name.press('Enter')
  await expect(panel).toHaveCount(0)
  const s = await state(ui)
  expect(s.spaceOrder).toHaveLength(4)
  // Right after the space the window showed.
  const garden = s.spaceOrder[1] ?? ''
  expect(s.spaces[garden]?.name).toBe('Garden')
  expect(s.spaces[garden]?.color).toBe('teal')
  await expect(ui.frame.locator('nav.clu-sidebar')).toHaveAttribute('data-active-space', garden)
  await expect(footer.getByRole('button', { name: 'View Library' })).toHaveCount(1)
  await expect(footer.locator('.clu-space-placeholder')).toHaveCount(0)
  await expect(ui.frame.locator('nav.clu-sidebar .clu-sidebar-creating')).toHaveCount(0)
})

test('editor: Esc closes the Create a Space panel without making a space and brings the bottom bar back', async ({ ui }) => {
  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'spaces.newSpace' }))
  const panel = ui.overlay.getByRole('dialog', { name: 'Create a Space' })
  await expect(panel).toBeVisible()
  await panel.getByLabel('Space name').fill('Nope')
  await panel.getByLabel('Space name').press('Escape')
  await expect(panel).toHaveCount(0)
  expect((await state(ui)).spaceOrder).toHaveLength(3)
  await expect(ui.frame.locator('nav.clu-sidebar footer .clu-space-new')).toHaveCount(1)
})

test('switcher: five spaces at the default width keep their icons, no overflow button; the tooltip has the profile chip and ⌃N keycaps', async ({ ui }) => {
  for (const name of ['Four', 'Five']) await ui.frameDoc().evaluate((n) => window.clu.spaces.create({ name: n, icon: { kind: 'emoji', value: '🌍' } }), name)
  await expect.poll(async () => (await state(ui)).spaceOrder.length).toBe(5)
  const icons = ui.frame.locator('.clu-space-icon')
  await expect(icons).toHaveCount(5)
  for (const kind of await icons.evaluateAll((els) => els.map((e) => (e as HTMLElement).dataset.kind))) expect(kind).toBe('icon')
  await expect(ui.frame.locator('.clu-space-more')).toHaveCount(0)
  await icons.first().hover()
  const tip = ui.frame.getByRole('tooltip')
  await expect(tip.locator('.clu-tooltip-chip')).toHaveText('Personal')
  await expect(tip.locator('kbd')).toHaveText(['⌃', '1'])
  await ui.shot('switcher-five')
})

test('library button: "View Library" with the ⇧⌘L keycaps, the filled glyph', async ({ ui }) => {
  const button = ui.frame.locator('nav.clu-sidebar footer').getByRole('button', { name: 'View Library' })
  await ui.page.mouse.move(400, 300)
  await button.hover()
  const tip = ui.frame.getByRole('tooltip').filter({ hasText: 'View Library' })
  await expect(tip.locator('kbd')).toHaveText(['⇧', '⌘', 'L'])
  // It hangs above the pointer, inside the window (parity-sidebar.md).
  await expect(tip).toHaveAttribute('data-above', 'true')
  await expect(button.locator('svg path[fill-rule="evenodd"]')).toHaveCount(1)
  await ui.shot('library-button')
})

test('library button: rests as a closed box, opens on hover and closes again', async ({ ui }) => {
  const button = ui.frame.locator('nav.clu-sidebar footer').getByRole('button', { name: 'View Library' })
  const lid = button.locator('.clu-library-lid')
  const sparkles = button.locator('.clu-library-sparkles')
  await ui.page.mouse.move(400, 300)
  await expect(lid).toHaveCSS('transform', 'none')
  await expect(sparkles).toHaveCSS('opacity', '0')
  await ui.shot('library-button-closed')
  await button.hover()
  await expect(lid).not.toHaveCSS('transform', 'none')
  await expect(sparkles).toHaveCSS('opacity', '1')
  await ui.shot('library-button-open')
  await ui.page.mouse.move(400, 300)
  await expect(lid).toHaveCSS('transform', 'none')
  await expect(sparkles).toHaveCSS('opacity', '0')
})

test('space icon: a right-click opens the space menu, not a separate icon menu', async ({ ui }) => {
  const icons = ui.frame.locator('nav.clu-sidebar footer .clu-space-icon')
  const other = icons.nth(1)
  const spaceId = (await other.getAttribute('data-space-id')) ?? ''
  const b = await other.boundingBox()
  if (!b) throw new Error('no space icon')
  const at = { x: Math.round(b.x + b.width / 2), y: Math.round(b.y + b.height / 2) }
  await ui.page.mouse.click(at.x, at.y, { button: 'right' })
  const calls = () => ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === 'tabs.openMenu' || c.name === 'spaces.openIconMenu').map((c) => c.payload))
  await expect.poll(calls).toEqual([{ menu: 'space', nodeId: spaceId, anchor: at, light: true }])
  const menu = ui.overlay.locator('[data-surface="menu"]')
  for (const name of ['Change Space Icon…', 'Rename Space…', 'Edit Theme Color…', 'Set Profile']) await expect(menu.getByRole('menuitem', { name })).toBeVisible()
  await ui.shot('space-icon-menu')
})

test('bottom bar: no divider line above it, even while rows run on under it', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const footer = nav.locator('footer.clu-footer')
  const border = () => footer.evaluate((el) => [getComputedStyle(el, '::before').content, getComputedStyle(el).borderTopWidth])
  await expect(nav).not.toHaveAttribute('data-rows-below', '')
  expect(await border()).toEqual(['none', '0px'])
  await ui.page.setViewportSize({ width: 1280, height: 420 })
  await expect(nav).toHaveAttribute('data-rows-below', '')
  expect(await border()).toEqual(['none', '0px'])
  await ui.shot('bottom-bar-border')
  // Scrolled to the end, the last row sits above the bar and the border goes.
  await nav.locator('.clu-tab-list').evaluate((el) => el.scrollTo({ top: el.scrollHeight }))
  await expect(nav).not.toHaveAttribute('data-rows-below', '')
})

test('space switch: the favorites and the name row slide with the tabs', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  // Slow the slide down so it can be caught mid-way.
  await ui.frameDoc().evaluate(() => document.documentElement.style.setProperty('--t-slow', '3000ms'))
  await nav.locator('footer .clu-space-icon').nth(1).click()
  const leaving = nav.locator('.clu-space-panel[data-outgoing="true"]')
  await expect(leaving.locator('.clu-favorites')).toHaveCount(1)
  await expect(leaving.locator('.clu-space-header')).toHaveCount(1)
  await expect(nav.locator('.clu-space-panel:not([data-outgoing]) .clu-space-header')).toHaveCount(1)
  await ui.shot('space-slide-mid')
  // Nothing outside the slider is left to stay put.
  await expect(nav.locator(':scope > .clu-favorites, :scope > .clu-space-header')).toHaveCount(0)
})

test('"+" popover: a themed space tints the panel and its selected row', async ({ ui }) => {
  const home = (await state(ui)).windows[WINDOW]?.activeSpaceId ?? ''
  await ui.page.evaluate(([id]) => {
    const host = (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost
    host.edit(['spaces', id, 'theme'], { colors: [{ h: 340, s: 0.8 }], intensity: 0.5, grain: 0 })
    host.edit(['spaces', id, 'accent'], '#e6195f')
  }, [home] as const)
  await ui.frame.locator('.clu-space-new').click()
  const pop = ui.overlay.locator('[data-surface="menu-popover"] .clu-menu-popover')
  await expect(pop).toBeVisible()
  // 0 to 255 channels, whether the computed value is rgb() or color(srgb ...).
  const rgb = (el: import('@playwright/test').Locator) =>
    el.evaluate((e) => {
      const bg = getComputedStyle(e).backgroundColor
      const n = (bg.match(/[\d.]+/g) ?? []).map(Number)
      return bg.startsWith('color(') ? n.slice(0, 3).map((v) => Math.round(v * 255)) : n.slice(0, 3)
    })
  await expect.poll(async () => { const [r = 0, g = 0, b = 0] = await rgb(pop); return r > g + 5 && r > b }).toBe(true)
  await pop.locator('.clu-menu-popover-row').first().hover()
  await expect.poll(async () => { const [r = 0, g = 0] = await rgb(pop.locator('.clu-menu-popover-row').first()); return r > g + 40 }).toBe(true)
  await ui.shot('plus-popover-pink')
})

test('new space: the 🙌 card shows in the sidebar for a second, then the empty space', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  // Switching to a space that already existed shows no card.
  await nav.locator('footer .clu-space-icon').nth(1).click()
  await expect(nav.locator('.clu-new-space-card')).toHaveCount(0)
  const made = await ui.frameDoc().evaluate(() => window.clu.spaces.create({ name: 'Garden' }))
  expect(made.ok).toBe(true)
  const card = nav.getByRole('status', { name: 'Space created' })
  await expect(card).toBeVisible()
  await expect(card).toHaveText('🙌')
  await expect(nav.locator('.clu-url-pill, .clu-favorites')).toHaveCount(0)
  await ui.shot('new-space-card')
  await expect(card).toHaveCount(0)
  await expect(nav.locator('.clu-space-header')).toContainText('Garden')
})
