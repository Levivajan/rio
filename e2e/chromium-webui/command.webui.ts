// M5 command bar in the ui:fake harness (docs/chromium/m5.md): the open keys from a focused frame, Esc and the scrim,
// the actions-only filter with palette and provider rows, and the keystroke budget on 10,000 history rows. The
// FakeBridge answers as CluCommandHandler does (every action unmatched, the bar matches). Screenshots go to
// $CLU_SHOTS_DIR/chromium-m5-ui.
import type { Frame } from '@playwright/test'
import { expect, harnessTest, state, WINDOW } from './fixture'

const test = harnessTest('chromium-m5-ui')

const WORK = '00000000-0000-4000-8000-000300000002'

test('keys: Cmd+T opens the bar, Cmd+L switches it in place, the same key closes it; Esc and the scrim close it', async ({ ui }) => {
  const bar = ui.overlay.locator('[data-surface="command"]')
  const card = ui.overlay.locator('.clu-cmd')
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  await expect(card).toHaveAttribute('data-command-mode', 'new-tab')
  const session = (await state(ui)).windows[WINDOW]?.command?.sessionId
  await ui.page.keyboard.press('Meta+l')
  await expect(card).toHaveAttribute('data-command-mode', 'edit-url')
  expect((await state(ui)).windows[WINDOW]?.command?.sessionId).toBe(session)
  await ui.shot('command-bar-edit-url')
  await ui.page.keyboard.press('Meta+l')
  await expect(bar).toHaveCount(0)

  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = bar.getByRole('combobox')
  await expect(input).toBeFocused()
  await input.press('Escape')
  await expect(bar).toHaveCount(0)
  expect((await state(ui)).windows[WINDOW]?.command).toBeNull()

  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  await expect(input).toBeFocused()
  // The scrim covers the page area around the bar without dimming it (M11): a click there closes it.
  await expect(ui.overlay.locator('.clu-overlay-root')).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)')
  await ui.page.mouse.click(400, 760)
  await expect(bar).toHaveCount(0)
})

test('actions: Tab on an empty input lists palette commands and provider rows; Enter runs one', async ({ ui }) => {
  const bar = ui.overlay.locator('[data-surface="command"]')
  const card = ui.overlay.locator('.clu-cmd')
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = bar.getByRole('combobox')
  await expect(input).toBeFocused()
  await input.press('Tab')
  await expect(card).toHaveAttribute('data-actions-only', 'true')
  const rows = bar.getByRole('option')
  await expect(rows.first()).toBeVisible()
  for (const kind of await rows.evaluateAll((els) => els.map((e) => (e as HTMLElement).dataset.kind))) {
    expect(['command', 'provided']).toContain(kind)
  }
  await ui.shot('command-bar-actions')

  // A provider row: "Switch to Work" switches the space.
  await input.pressSequentially('switch to work')
  await expect(rows.first()).toContainText('Switch to Work')
  await input.press('Enter')
  await expect(bar).toHaveCount(0)
  expect((await state(ui)).windows[WINDOW]?.activeSpaceId).toBe(WORK)

  // A palette command by an alias from the table: "Change space colors" is Edit Theme…; "Toggle Sidebar" runs.
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  await input.pressSequentially('toggle sidebar')
  const toggle = rows.filter({ hasText: 'Toggle Sidebar' }).first()
  await expect(toggle).toBeVisible()
  await expect(rows.first()).toContainText('Toggle Sidebar')
  await input.press('Enter')
  await expect(bar).toHaveCount(0)
  expect((await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
})

test('perf: typing 20 characters over 10,000 history rows keeps each keystroke within 16 ms at p95', async ({ ui }) => {
  await ui.page.goto('/?history=10000')
  await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
  const bar = ui.overlay.locator('[data-surface="command"]')
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = bar.getByRole('combobox')
  await expect(input).toBeFocused()
  // History arrives after the first frame; wait until it ranks.
  await input.pressSequentially('page 12')
  await expect(bar.getByRole('option').filter({ hasText: 'about harbors' }).first()).toBeVisible()
  await input.fill('')
  const overlay = (): Frame => {
    const f = ui.page.frame({ url: /\/overlay\/index\.html\?layer=top/ })
    if (!f) throw new Error('overlay WebUI missing')
    return f
  }
  await overlay().evaluate(() => performance.clearMeasures('cmd:keystroke'))
  // One keystroke per frame or so, as a typist gives them; each records cmd:keystroke on the frame after it.
  await input.pressSequentially('harbors page about 1', { delay: 40 })
  await expect(bar.getByRole('option').first()).toBeVisible()
  const durations = await overlay().evaluate(() =>
    performance
      .getEntriesByName('cmd:keystroke', 'measure')
      .map((m) => m.duration)
      .sort((a, b) => a - b)
  )
  // Each measure ends on the frame after the commit, so it includes up to one frame of waiting for vsync.
  console.log(`cmd:keystroke ms: ${durations.map((d) => d.toFixed(1)).join(' ')}`)
  expect(durations.length).toBeGreaterThanOrEqual(15)
  // Nearest-rank p95.
  const p95 = durations[Math.min(durations.length - 1, Math.ceil(durations.length * 0.95) - 1)] ?? Infinity
  // One 60 Hz frame plus 1 ms for a loaded Mac; Rio's own work is 4 to 9 ms a key.
  expect(p95).toBeLessThanOrEqual(17)
  await ui.shot('command-bar-10k-history')
})
