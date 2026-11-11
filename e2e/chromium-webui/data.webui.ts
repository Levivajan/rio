// M8 in the ui:fake harness (docs/chromium/m8.md): the settings page as the internal:settings surface with every
// section and both imports, the theme picker's preview and revert, the clear browsing data dialog, and first-run
// onboarding with Skip. The FakeBridge answers as the M8 handlers do. Screenshots go to $CLU_SHOTS_DIR/chromium-m8-ui.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import type { Page } from '@playwright/test'
import { expect, harnessTest, shotsDir, state, WINDOW } from './fixture'

const test = harnessTest('chromium-m8-ui')
const shot = (name: string) => {
  mkdirSync(shotsDir('chromium-m8-ui'), { recursive: true })
  return join(shotsDir('chromium-m8-ui'), `${name}.png`)
}

const HOME = '00000000-0000-4000-8000-000300000001'
const PROFILE = '00000000-0000-4000-8000-000200000001'

// The overlay WebUI has subscribed and rendered once (its ui.ready), so a surface opened now renders in it.
const overlayReady = (page: Page) =>
  expect
    .poll(() => page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { surface: string; name: string }[] } }).cluFakeHost.calls.some((c) => c.surface === 'overlay' && c.name === 'ui.ready')))
    .toBe(true)

// Import from Google Chrome loads the settings at #import-chrome, which opens on the Chrome import itself.
test('settings: #import-chrome opens on the Chrome import: profile, what to bring, Import, the result', async ({ ui }) => {
  await ui.page.goto('/settings/#import-chrome')
  const section = ui.page.getByRole('region', { name: 'Import from Google Chrome' })
  await expect(section.getByRole('heading', { name: 'Import from Google Chrome' })).toBeInViewport()
  const chrome = section.getByRole('group', { name: 'Google Chrome' })
  await expect(chrome.getByRole('combobox', { name: 'Google Chrome profile' })).toBeVisible()
  await expect(chrome.getByRole('checkbox', { name: 'Bookmarks' })).toBeVisible()
  await expect(chrome.getByRole('checkbox', { name: 'History (last 90 days)' })).toBeVisible()
  // The saved passwords and sign-ins come along by default.
  await expect(chrome.getByRole('checkbox', { name: 'Saved passwords' })).toBeChecked()
  await expect(chrome.getByRole('checkbox', { name: /^Sign-ins \(cookies, site storage, permissions\)/ })).toBeChecked()
  await expect(chrome.getByRole('checkbox', { name: 'Saved addresses' })).toBeChecked()
  await expect(chrome.getByRole('checkbox', { name: 'Default search engine' })).toBeChecked()
  await expect(section.getByRole('group', { name: 'Import passwords from a CSV file' })).toBeVisible()
  await ui.page.screenshot({ path: shot('settings-import-chrome') })
  await chrome.getByRole('button', { name: 'Import' }).click()
  await expect(chrome.getByRole('status')).toHaveText('Imported 13 bookmarks, 4 history entries, 3 saved passwords, 2 cookies, 1 site permission and 1 address (2 skipped). Site storage for 2 sites finishes on the next launch of Rio.')
  await ui.page.screenshot({ path: shot('settings-import-chrome-result') })
})

test('settings: the window’s Advanced tab runs the Chrome and sidebar-file imports and reports what they did', async ({ ui }) => {
  await ui.page.goto('/settings/#advanced')
  await expect(ui.page.getByRole('tab', { name: 'Advanced' })).toHaveAttribute('aria-selected', 'true')
  const chrome = ui.page.getByRole('group', { name: 'Google Chrome' })
  await chrome.getByRole('button', { name: 'Import' }).click()
  await expect(chrome.getByRole('status')).toHaveText('Imported 13 bookmarks, 4 history entries, 3 saved passwords, 2 cookies, 1 site permission and 1 address (2 skipped). Site storage for 2 sites finishes on the next launch of Rio.')
  // A second run adds nothing.
  await chrome.getByRole('button', { name: 'Import' }).click()
  await expect(chrome.getByRole('status')).toHaveText('Imported 0 bookmarks and 0 history entries (2 skipped)')
  const sidebar = ui.page.getByRole('group', { name: /sidebar file/i })
  // Saved passwords and sign-ins (cookies) come along by default.
  await expect(sidebar.getByRole('checkbox', { name: 'Saved passwords' })).toBeChecked()
  await expect(sidebar.getByRole('checkbox', { name: /Sign-ins \(cookies, site storage, permissions\)/ })).toBeChecked()
  await expect(sidebar.getByRole('checkbox', { name: 'Saved addresses' })).toBeChecked()
  await sidebar.getByRole('button', { name: 'Bring everything over' }).click()
  await expect(sidebar.getByRole('status')).toHaveText(
    'Imported 2 profiles, 4 spaces, 10 pinned tabs, 3 Today tabs, 3 favorites, 3 history entries, 3 saved passwords, 2 cookies, 1 site permission and 1 address (8 skipped) Site storage for 2 sites finishes on the next launch of Rio. 1 extension to reinstall below.'
  )
  // A second run asks first, and Replace brings everything over again in place of the first import.
  await sidebar.getByRole('button', { name: 'Bring everything over' }).click()
  const ask = sidebar.getByRole('alertdialog', { name: 'Replace the previous import?' })
  await expect(ask).toContainText('The 2 profiles it made')
  // Cancel and Replace show without scrolling the pane.
  await expect(ask.getByRole('button', { name: 'Cancel' })).toBeInViewport({ ratio: 1 })
  await expect(ask.getByRole('button', { name: 'Replace' })).toBeInViewport({ ratio: 1 })
  await ui.page.screenshot({ path: shot('settings-import-replace') })
  await ask.getByRole('button', { name: 'Replace' }).click()
  await expect(ask).toHaveCount(0)
  await expect(sidebar.getByRole('status')).toHaveText(
    'Imported 2 profiles, 4 spaces, 10 pinned tabs, 3 Today tabs, 3 favorites, 3 history entries, 3 saved passwords, 2 cookies, 1 site permission and 1 address (8 skipped) Site storage for 2 sites finishes on the next launch of Rio. 1 extension to reinstall below.'
  )
  // Reinstall asks the browser for the store's install prompt, and the row turns to Installed.
  const extensions = sidebar.getByRole('list', { name: 'Extensions to reinstall' })
  await extensions.getByRole('button', { name: 'Reinstall' }).click()
  await expect(extensions.getByText('Installed')).toBeVisible()
  await ui.page.screenshot({ path: shot('settings-import-reinstalled') })
  const reinstalls = await ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { name: string; payload: { id?: string } }[] } }).cluFakeHost.calls.filter((c) => c.name === 'extensions.installFromStore').map((c) => c.payload.id))
  expect(reinstalls).toEqual(['a'.repeat(32)])
  const replaced = await ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { name: string; payload: { replace?: boolean } }[] } }).cluFakeHost.calls.filter((c) => c.name === 'import.run' && c.payload.replace === true).length)
  expect(replaced).toBe(1)
  // Passwords: Chromium's own CSV import, one labelled entry.
  const passwords = ui.page.getByRole('group', { name: 'Import passwords from a CSV file' })
  await passwords.getByRole('button', { name: 'Import' }).click()
  await expect
    .poll(() => ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { name: string; result: { ok: boolean } }[] } }).cluFakeHost.calls.some((c) => c.name === 'import.openPasswords' && c.result.ok)))
    .toBe(true)
  await expect(passwords.getByRole('status')).toHaveCount(0)
  await ui.page.screenshot({ path: shot('settings-import'), fullPage: true })
})

test('settings: credit cards live in Chromium’s settings, which the Profiles tab opens', async ({ ui }) => {
  await ui.page.goto('/settings/#profiles')
  await ui.page.getByRole('list', { name: 'Your data and settings' }).getByRole('button', { name: 'Credit Cards' }).click()
  await expect
    .poll(() => ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === 'settings.openBrowser').map((c) => c.payload)))
    .toEqual([{ page: 'payments' }])
})

test('theme: the picker previews a theme, Esc puts the committed one back, and a commit sets the accent', async ({ ui }) => {
  await overlayReady(ui.page)
  const before = (await state(ui)).spaces[HOME]
  expect(before?.theme).toBeNull()
  await ui.frameDoc().evaluate((spaceId) => window.clu.theme.openPicker({ spaceId, anchor: { x: 12, y: 44, width: 236, height: 28 } }), HOME)
  const picker = ui.overlay.getByRole('dialog')
  await expect(picker).toBeVisible()
  await ui.shot('theme-picker')
  const ember = { colors: [{ h: 12, s: 0.82 }, { h: 36, s: 0.88 }], intensity: 0.5, grain: 0 }
  await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.previewSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme: ember })
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme).toEqual(ember)
  await ui.page.keyboard.press('Escape')
  await expect(picker).toBeHidden()
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme ?? null).toBeNull()
  await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.setSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme: ember })
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.accent).not.toBe(before?.accent)
  await ui.shot('theme-ember')
})

test('clear data: the dialog clears the chosen types, archive rows included, and says so', async ({ ui }) => {
  await overlayReady(ui.page)
  await ui.frameDoc().evaluate((profileId) => window.clu.browsingData.open({ profileId }), PROFILE)
  const dialog = ui.overlay.getByRole('dialog', { name: 'Clear browsing data' })
  await expect(dialog).toBeVisible()
  await expect(dialog.getByRole('checkbox', { name: 'Cookies and site data' })).toBeChecked()
  await dialog.getByRole('checkbox', { name: 'Archived tabs' }).check()
  await ui.shot('clear-data')
  await dialog.getByRole('button', { name: 'Clear data' }).click()
  await expect(dialog).toBeHidden()
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.toasts.map((t) => t.text)).toContain('Browsing data cleared')
})

test('onboarding: the Import step opens Chromium’s password import from a CSV file and ends setup', async ({ ui }) => {
  // ?arc=0: no other browser's sidebar file, so there is no switch screen and the Import step is the one ask.
  await ui.page.goto('/?onboarding=1&arc=0')
  const onboarding = ui.page.frameLocator('#frame').locator('.clu-ob')
  await expect(onboarding).toHaveAttribute('data-step', 'welcome')
  await expect(onboarding.getByRole('list', { name: 'Step 1 of 6' })).toBeVisible()
  await onboarding.getByRole('button', { name: 'Get started' }).click()
  // The look step is the whole theme editor: all 5 pages of its swatches, no named presets of Rio's own.
  const swatches = onboarding.locator('.clu-tp-swatch')
  await expect(swatches).toHaveCount(45)
  await onboarding.getByRole('button', { name: 'Brand 2' }).click()
  await expect(onboarding.getByRole('button', { name: 'Brand 2' })).toHaveAttribute('aria-pressed', 'true')
  await onboarding.getByRole('button', { name: 'Continue' }).click()
  await onboarding.getByRole('button', { name: 'Continue' }).click()
  await expect(onboarding).toHaveAttribute('data-step', 'default-browser')
  await onboarding.getByRole('button', { name: 'Skip', exact: true }).first().click()
  await expect(onboarding).toHaveAttribute('data-step', 'import')
  await ui.page.screenshot({ path: shot('onboarding-import') })
  await onboarding.getByRole('button', { name: 'Import passwords from a CSV file' }).click()
  await expect(onboarding).toBeHidden()
  const calls = await ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { surface: string; name: string; result: { ok: boolean } }[] } }).cluFakeHost.calls)
  expect(calls.some((c) => c.surface === 'ui' && c.name === 'import.openPasswords' && c.result.ok)).toBe(true)
  expect((await state(ui)).app.onboarding.show).toBe(false)
})

test('onboarding: after the switch screen the look step themes the window’s space by mouse, keyboard and its editor, and nothing asks to import again', async ({ ui }) => {
  await ui.page.goto('/?onboarding=1')
  const onboarding = ui.page.frameLocator('#frame').locator('.clu-ob')
  await onboarding.getByRole('button', { name: 'Not now' }).click()
  await expect(onboarding.getByRole('list', { name: 'Step 1 of 5' })).toBeVisible()
  await onboarding.getByRole('button', { name: 'Get started' }).click()
  const spaceId = (await state(ui)).windows[WINDOW]?.activeSpaceId
  if (!spaceId) throw new Error('no space')
  const themeOf = async () => (await state(ui)).spaces[spaceId]?.theme ?? null
  const before = await onboarding.evaluate((el) => getComputedStyle(el).backgroundImage + getComputedStyle(el).backgroundColor)
  // Mouse: a swatch commits to the window's space and the takeover repaints.
  await onboarding.getByRole('button', { name: 'Brand 2' }).click()
  await expect(onboarding.getByRole('button', { name: 'Brand 2' })).toHaveAttribute('aria-pressed', 'true')
  await expect.poll(async () => onboarding.evaluate((el) => getComputedStyle(el).backgroundImage + getComputedStyle(el).backgroundColor)).not.toBe(before)
  const byMouse = await themeOf()
  // Keyboard: Tab to the next swatch and press Space.
  await onboarding.getByRole('button', { name: 'Brand 2' }).focus()
  await ui.page.keyboard.press('Tab')
  await ui.page.keyboard.press('Space')
  await expect.poll(themeOf).not.toEqual(byMouse)
  // The regular editor's color field is right in the card, on the same space.
  const picker = onboarding.locator('.clu-theme-picker')
  await expect(picker).toBeVisible()
  await ui.page.screenshot({ path: shot('onboarding-look-editor') })
  const field = await picker.getByTestId('theme-field').boundingBox()
  if (!field) throw new Error('no field')
  const byKey = await themeOf()
  await ui.page.mouse.click(field.x + field.width / 2 - 70, field.y + field.height / 2 + 40)
  await expect.poll(themeOf).not.toEqual(byKey)
  const picked = await themeOf()
  await onboarding.getByRole('button', { name: 'Continue' }).click()
  await expect(onboarding).toHaveAttribute('data-step', 'moves')
  expect(await themeOf()).toEqual(picked)
  await onboarding.getByRole('button', { name: 'Continue' }).click()
  await onboarding.getByRole('button', { name: 'Skip', exact: true }).first().click()
  await expect(onboarding).toHaveAttribute('data-step', 'done')
  await expect(onboarding.getByRole('list', { name: 'Step 5 of 5' })).toBeVisible()
  await onboarding.getByRole('button', { name: 'Back' }).click()
  await expect(onboarding).toHaveAttribute('data-step', 'default-browser')
  expect(await themeOf()).toEqual(picked)
})

test('onboarding at its smallest window (560 x 400): Pick a look is the whole theme editor inside the card, and each dot drags on its own', async ({ ui }) => {
  await ui.page.setViewportSize({ width: 560, height: 400 })
  await ui.page.goto('/?onboarding=1&arc=0')
  const onboarding = ui.page.frameLocator('#frame').locator('.clu-ob')
  await onboarding.getByRole('button', { name: 'Get started' }).click()
  await expect(onboarding).toHaveAttribute('data-step', 'look')
  const spaceId = (await state(ui)).windows[WINDOW]?.activeSpaceId
  if (!spaceId) throw new Error('no space')
  const themeOf = async () => (await state(ui)).spaces[spaceId]?.theme ?? null
  const card = await onboarding.locator('.clu-ob-card').boundingBox()
  const editor = onboarding.locator('.clu-theme-picker')
  const box = await editor.boundingBox()
  if (!card || !box) throw new Error('no card')
  // Nothing of the editor is cropped: its whole box sits inside the card, which sits inside the window.
  expect(box.x).toBeGreaterThanOrEqual(card.x)
  expect(box.y).toBeGreaterThanOrEqual(card.y)
  expect(box.x + box.width).toBeLessThanOrEqual(card.x + card.width + 0.5)
  expect(box.y + box.height).toBeLessThanOrEqual(card.y + card.height + 0.5)
  expect(card.y + card.height).toBeLessThanOrEqual(400)
  expect(box.height / box.width).toBeCloseTo(508 / 356, 1)
  for (const name of ['Add color', 'Remove color', 'Previous presets', 'Next presets', 'Continue', 'Back']) await expect(onboarding.getByRole('button', { name })).toBeInViewport({ ratio: 1 })
  for (const name of ['Intensity', 'Grain']) await expect(onboarding.getByRole('slider', { name })).toBeInViewport({ ratio: 1 })
  await expect(onboarding.getByRole('radio', { name: 'Dark' })).toBeInViewport({ ratio: 1 })
  // The swatch pages turn.
  await onboarding.getByRole('button', { name: 'Next presets' }).click()
  await expect(editor.locator('.clu-tp-presets')).toHaveAttribute('data-page', '1')
  await onboarding.getByRole('button', { name: 'Previous presets' }).click()
  // A swatch, then + twice: three dots at the starting spots.
  await editor.locator('.clu-tp-swatch').nth(3).click()
  await onboarding.getByRole('button', { name: 'Add color' }).click()
  await onboarding.getByRole('button', { name: 'Add color' }).click()
  await expect(editor.locator('.clu-tp-handle')).toHaveCount(3)
  await expect.poll(async () => (await themeOf())?.colors.length).toBe(3)
  const before = (await themeOf())?.colors ?? []
  await ui.page.screenshot({ path: shot('onboarding-look-560x400') })
  // Drag the third dot to the canvas center; the main and the second stay exactly where they were.
  const third = await editor.locator('.clu-tp-handle[data-index="2"]').boundingBox()
  const field = await editor.getByTestId('theme-field').boundingBox()
  if (!third || !field) throw new Error('no handle')
  await ui.page.mouse.move(third.x + third.width / 2, third.y + third.height / 2)
  await ui.page.mouse.down()
  await ui.page.mouse.move(field.x + field.width / 2 + 10, field.y + field.height / 2 - 20, { steps: 6 })
  await ui.page.mouse.up()
  await expect.poll(async () => (await themeOf())?.colors[2]).not.toEqual(before[2])
  const after = (await themeOf())?.colors ?? []
  expect(after.slice(0, 2)).toEqual(before.slice(0, 2))
  // The zoomed canvas maps the pointer back to canvas points: 10 east, 20 north of the center at zoom z.
  expect(after[2]?.h).toBeGreaterThan(270)
  await ui.page.screenshot({ path: shot('onboarding-look-560x400-dragged') })
})

test('onboarding: a fresh install shows it once, and Skip setup leaves it for good', async ({ ui }) => {
  await ui.page.goto('/?onboarding=1')
  const onboarding = ui.page.frameLocator('#frame').locator('.clu-ob')
  await expect(onboarding).toBeVisible()
  await expect(onboarding).toHaveAttribute('data-step', 'welcome')
  await ui.page.screenshot({ path: shot('onboarding-welcome') })
  await onboarding.getByRole('button', { name: 'Skip setup' }).click()
  await expect(onboarding).toBeHidden()
  const s = await state(ui)
  expect(s.app.onboarding.show).toBe(false)
  expect(typeof s.settings.global['onboarding.completedAt']).toBe('string')
})

test('onboarding: with another browser on the Mac, setup opens on the switch screen, which brings everything over', async ({ ui }) => {
  await ui.page.goto('/?onboarding=1')
  const onboarding = ui.page.frameLocator('#frame').locator('.clu-ob')
  await expect(onboarding.getByRole('heading', { name: 'Bring everything over' })).toBeVisible()
  const boxes = onboarding.locator('.clu-switch-item input')
  await expect(boxes).toHaveCount(12)
  await onboarding.getByRole('checkbox', { name: 'History' }).uncheck()
  await ui.page.screenshot({ path: shot('onboarding-switch') })
  // The frame behind the takeover follows it in the document, so while it shows the frame and its sidebar
  // have no drag areas (the Mac took a press on the card's buttons as a window drag); the card's buttons are no-drag.
  const frameDoc = ui.page.frameLocator('#frame')
  const region = (l: ReturnType<typeof frameDoc.locator>) => l.first().evaluate((el) => getComputedStyle(el).getPropertyValue('-webkit-app-region'))
  expect(await region(frameDoc.locator('.clu-frame'))).toBe('none')
  expect(await region(frameDoc.locator('.clu-frame .clu-tab-list'))).toBe('none')
  expect(await region(onboarding.getByRole('button', { name: 'Bring everything over' }))).toBe('no-drag')
  expect(await region(onboarding.getByRole('button', { name: 'Not now' }))).toBe('no-drag')
  await onboarding.getByRole('button', { name: 'Bring everything over' }).click()
  await expect(onboarding.locator('.clu-switch-summary [role=status]')).toContainText('Imported')
  await ui.page.screenshot({ path: shot('onboarding-switch-summary') })
  const calls = await ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { surface: string; name: string; payload: unknown }[] } }).cluFakeHost.calls)
  const run = calls.find((c) => c.surface === 'ui' && c.name === 'import.run')
  expect(run?.payload).toMatchObject({ source: 'arc', history: false, bookmarks: true, passwords: true })
  await onboarding.getByRole('button', { name: 'Continue' }).click()
  await expect(onboarding.getByRole('heading', { name: 'Welcome to Rio' })).toBeVisible()
})
