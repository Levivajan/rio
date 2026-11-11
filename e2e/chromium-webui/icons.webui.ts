// The App icon picker and the new mark in the ui:fake harness (docs/chromium/icons.md): the settings window's Icon tab
// shows the seven icons with Flat warm picked, a click or an arrow key saves appearance.appIcon, and onboarding's welcome
// shows the Flat warm icon. Screenshots go to $CLU_SHOTS_DIR/chromium-icons-ui.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import type { Page } from '@playwright/test'
import { expect, harnessTest, shotsDir } from './fixture'

const test = harnessTest('chromium-icons-ui')
const shot = (name: string) => {
  mkdirSync(shotsDir('chromium-icons-ui'), { recursive: true })
  return join(shotsDir('chromium-icons-ui'), `${name}.png`)
}

const storedIcon = (page: Page) =>
  page.evaluate(async () => {
    const r = await window.clu.state.get()
    if (!r.ok) throw new Error('state.get failed')
    return r.value.settings.global['appearance.appIcon'] ?? null
  })

test('settings: the Icon tab picks one of seven icons and saves it', async ({ ui }) => {
  await ui.page.goto('/settings/#icon')
  const group = ui.page.getByRole('radiogroup', { name: 'App icon' })
  await expect(group).toBeVisible()
  const radios = group.getByRole('radio')
  await expect(radios).toHaveCount(7)
  await expect(group.locator('.clu-sw-icon-name > span:first-child')).toHaveText(['Flat warm', 'Sunset', 'Ocean', 'Flat blue', 'Rio sunset', 'Warm', 'Dusk'])
  await expect(group.locator('label[data-app-icon="flatWarm"]')).toContainText('Current icon')
  await expect(group.getByRole('radio', { name: 'Flat warm', exact: true })).toBeChecked()
  // Every thumbnail loads under the WebUI CSP.
  await expect.poll(() => group.locator('img').evaluateAll((imgs) => imgs.filter((i) => (i as HTMLImageElement).naturalWidth > 0).length)).toBe(7)
  await group.screenshot({ path: shot('settings-app-icon') })

  await group.locator('label[data-app-icon="ocean"]').click()
  await expect(group.getByRole('radio', { name: 'Ocean', exact: true })).toBeChecked()
  await expect.poll(() => storedIcon(ui.page)).toBe('ocean')

  await ui.page.keyboard.press('ArrowRight')
  await expect(group.getByRole('radio', { name: 'Flat blue', exact: true })).toBeChecked()
  await expect(group.getByRole('radio', { name: 'Flat blue', exact: true })).toBeFocused()
  await expect.poll(() => storedIcon(ui.page)).toBe('flatBlue')
  await group.screenshot({ path: shot('settings-app-icon-flat-blue') })
})

test('onboarding: the welcome step shows the Flat warm icon', async ({ ui }) => {
  await ui.page.goto('/?onboarding=1')
  const onboarding = ui.page.frameLocator('#frame').locator('.clu-ob')
  await expect(onboarding).toHaveAttribute('data-step', 'welcome')
  const icon = onboarding.locator('img.clu-ob-icon')
  await expect.poll(() => icon.evaluate((i) => (i as HTMLImageElement).naturalWidth)).toBeGreaterThan(0)
  await onboarding.locator('.clu-ob-card').screenshot({ path: shot('onboarding-welcome-icon') })
})
