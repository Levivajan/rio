// M12 in the frame and overlay WebUIs over the FakeBridge (docs/chromium/m12.md): the sidebar's update card
// for a ready update, nothing without one, and Check for Updates… from the command bar with its toast. The browser
// side is clu_browsertests CluUpdate*; the real Sparkle update is scripts/chromium/update-smoke.sh. Screenshots go to
// $CLU_SHOTS_DIR/chromium-m12-ui.
import { expect, harnessTest, state, WINDOW } from './fixture'

const test = harnessTest('chromium-m12-ui')

type HostCall = { name: string; result: { ok: boolean; value?: unknown } }
const hostCalls = (ui: { page: import('@playwright/test').Page }, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: HostCall[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.result), name)

test('a build without an update shows no update card', async ({ ui }) => {
  await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
  await expect(ui.frame.locator('[data-update-relaunch]')).toHaveCount(0)
  expect((await state(ui)).app.update.status).toBe('unavailable')
})

test('a ready update shows the update card above the footer, and Restart installs it', async ({ ui }) => {
  await ui.page.goto('/?update=ready')
  const card = ui.frame.getByRole('region', { name: 'Update' })
  await expect(card).toContainText('Update ready')
  await expect(card).toContainText('Rio 158.0.8127.19 installs when you quit.')
  const button = card.getByRole('button', { name: 'Restart to update' })
  await expect(button).toBeVisible()
  // It sits between the tab list and the footer, inside the sidebar, and never over the footer's buttons.
  const [box, footer, sidebar] = await Promise.all([
    card.boundingBox(),
    ui.frame.locator('footer.clu-footer').boundingBox(),
    ui.frame.locator('nav.clu-sidebar').boundingBox()
  ])
  expect(box && footer && box.y + box.height <= footer.y + 1).toBe(true)
  expect(box && sidebar && box.x >= sidebar.x && box.x + box.width <= sidebar.x + sidebar.width).toBe(true)
  await ui.shot('update-card')
  await button.click()
  await expect.poll(() => hostCalls(ui, 'platform.installUpdate')).toEqual([{ ok: true, value: { status: 'installing' } }])
  await expect(button).toBeDisabled()
})

// The close button hides the card for this version (it still installs on quit), from the keyboard too.
test('the update card closes from the keyboard and stays closed for that version', async ({ ui }) => {
  await ui.page.goto('/?update=ready')
  const card = ui.frame.getByRole('region', { name: 'Update' })
  const close = card.getByRole('button', { name: 'Close' })
  await close.focus()
  await expect(close).toBeFocused()
  await ui.page.keyboard.press('Tab')
  await expect(card.getByRole('button', { name: 'Restart to update' })).toBeFocused()
  await ui.page.keyboard.press('Shift+Tab')
  await ui.shot('update-card-close-focus')
  await ui.page.keyboard.press('Enter')
  await expect(card).toHaveCount(0)
  await expect.poll(() => hostCalls(ui, 'platform.dismissUpdate')).toEqual([{ ok: true, value: { status: 'dismissed' } }])
  expect((await state(ui)).app.update).toMatchObject({ status: 'ready', version: '158.0.8127.19', dismissed: true })
  await expect(ui.frame.locator('footer.clu-footer')).toBeVisible()
  expect(await hostCalls(ui, 'platform.installUpdate')).toEqual([])
})

test('Check for Updates… from the command bar toasts the result', async ({ ui }) => {
  await ui.page.goto('/?update=idle')
  await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
  const bar = ui.overlay.locator('[data-surface="command"]')
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = bar.getByRole('combobox')
  await expect(input).toBeFocused()
  // Tab on the empty input lists actions only, so the first row is the palette command.
  await input.press('Tab')
  await input.pressSequentially('check for updates')
  await expect(bar.getByRole('option').first()).toContainText('Check for Updates…')
  await ui.shot('check-for-updates-row')
  await input.press('Enter')
  await expect(bar).toHaveCount(0)
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.toasts.map((t) => t.text)).toContain('Rio is up to date')
  expect((await state(ui)).app.update.status).toBe('up-to-date')
})

// macOS asks for the keychain once after each update of a self-signed build, and the sidebar never offers to stop it.
test('the sidebar shows no keychain card, even after an update', async ({ ui }) => {
  await ui.page.goto('/?update=ready&keychain=ask')
  await expect(ui.frame.locator('[data-update-relaunch]')).toBeVisible()
  await expect(ui.frame.getByText('Stop the keychain prompt after each update?')).toHaveCount(0)
  await expect(ui.frame.getByRole('region', { name: 'Keychain' })).toHaveCount(0)
  expect((await state(ui)).app).not.toHaveProperty('keychainTrust')
})
