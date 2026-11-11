// Symbol space icons in the ui:fake harness: the editor's Symbols tab picks a glyph, and the space header, the
// bottom bar, Library Spaces and the command bar's Switch to row draw it. Screenshots go to
// $CLU_SHOTS_DIR/g-space-symbols.
import { expect, harnessTest, state } from './fixture'

const test = harnessTest('g-space-symbols')

const HOME = '00000000-0000-4000-8000-000300000001'

test('the Symbols tab sets a symbol icon that the header, the bottom bar and Library draw', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  // With the letter icon, the bar draws a dot and the header no glyph.
  await expect(nav.locator('.clu-space-icon[data-index="0"] .clu-space-dot')).toHaveCount(1)
  await expect(nav.locator('.clu-space-header[data-icon="none"]')).toHaveCount(1)
  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'spaces.editSpace' }))
  const editor = ui.overlay.getByRole('dialog', { name: 'Edit Space' })
  await expect(editor).toBeVisible()
  await expect(editor.getByRole('tab', { name: 'Emoji' })).toHaveAttribute('aria-selected', 'true')
  await editor.getByRole('tab', { name: 'Symbols' }).click()
  await expect(editor.getByRole('group', { name: 'Symbols' }).getByRole('button')).toHaveCount(40)
  await editor.getByRole('textbox', { name: 'Search symbols' }).fill('pla')
  await expect(editor.getByRole('group', { name: 'Symbols' }).getByRole('button')).toHaveCount(2)
  await editor.getByRole('button', { name: 'Planet' }).click()
  await expect(editor.getByRole('button', { name: 'Planet' })).toHaveAttribute('aria-pressed', 'true')
  await ui.shot('01-picker-symbols')
  await editor.getByLabel('Name').press('Enter')
  await expect(editor).toHaveCount(0)
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.icon).toEqual({ kind: 'symbol', value: 'planet' })

  await expect(nav.locator('.clu-space-header[data-icon="symbol"] svg[data-symbol="planet"]')).toBeVisible()
  await expect(nav.locator('.clu-space-icon[data-index="0"] svg[data-symbol="planet"]')).toBeVisible()
  await expect(nav.locator('.clu-space-icon[data-index="0"] .clu-space-dot')).toHaveCount(0)
  await ui.page.mouse.move(900, 400)
  await ui.shot('02-header-and-bar')

  // Reopened, the editor starts on Symbols with Planet marked.
  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'spaces.editSpace' }))
  await expect(editor.getByRole('tab', { name: 'Symbols' })).toHaveAttribute('aria-selected', 'true')
  await expect(editor.getByRole('button', { name: 'Planet' })).toHaveAttribute('aria-pressed', 'true')
  await ui.page.keyboard.press('Escape')
  await expect(editor).toHaveCount(0)

  await nav.getByRole('button', { name: 'View Library' }).click()
  await ui.frame.getByRole('tab', { name: 'Spaces' }).click()
  await expect(ui.frame.locator(`.clu-library-spaces .clu-space-card[data-space-id="${HOME}"] svg[data-symbol="planet"]`)).toBeVisible()
  await ui.shot('03-library-spaces')
})

test('the Create a Space panel picks a symbol with the same picker, draws it in the well and Create Space saves it', async ({ ui }) => {
  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'spaces.newSpace' }))
  const panel = ui.overlay.getByRole('dialog', { name: 'Create a Space' })
  await expect(panel).toBeVisible()
  await panel.getByLabel('Space name').fill('Orbit')
  const well = panel.getByRole('button', { name: 'Space icon' })
  await well.click()
  await expect(panel.getByRole('tab', { name: 'Emoji' })).toHaveAttribute('aria-selected', 'true')
  await panel.getByRole('tab', { name: 'Symbols' }).click()
  await expect(panel.getByRole('group', { name: 'Symbols' }).getByRole('button')).toHaveCount(40)
  // The grid fits the sidebar: every symbol button is at least 24 px wide and inside the panel.
  const box = await panel.boundingBox()
  for (const b of await panel.getByRole('group', { name: 'Symbols' }).getByRole('button').evaluateAll((els) => els.map((e) => e.getBoundingClientRect().toJSON() as DOMRect))) {
    expect(b.width).toBeGreaterThanOrEqual(24)
    expect(b.right).toBeLessThanOrEqual((box?.x ?? 0) + (box?.width ?? 0))
  }
  await panel.getByRole('button', { name: 'Planet' }).click()
  await expect(well.locator('svg[data-symbol="planet"]')).toBeVisible()
  await ui.shot('04-create-panel-symbols')
  await panel.getByRole('button', { name: 'Create Space' }).click()
  await expect(panel).toHaveCount(0)
  const s = await state(ui)
  const orbit = Object.values(s.spaces).find((sp) => sp?.name === 'Orbit')
  expect(orbit?.icon).toEqual({ kind: 'symbol', value: 'planet' })
  await expect(ui.frame.locator('nav.clu-sidebar .clu-space-header[data-icon="symbol"] svg[data-symbol="planet"]')).toBeVisible()
})

test("the command bar's Switch to row draws the space's symbol, then its emoji", async ({ ui }) => {
  const WORK = '00000000-0000-4000-8000-000300000002'
  await ui.frameDoc().evaluate((id) => window.clu.spaces.update({ spaceId: id, icon: { kind: 'symbol', value: 'rocket' } }), WORK)
  await expect.poll(async () => (await state(ui)).spaces[WORK]?.icon).toEqual({ kind: 'symbol', value: 'rocket' })
  const bar = ui.overlay.locator('[data-surface="command"]')
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = bar.getByRole('combobox')
  await expect(input).toBeFocused()
  await input.pressSequentially('switch to work')
  const row = bar.getByRole('option').filter({ hasText: 'Switch to Work' })
  await expect(row.locator('.clu-cmd-icon [data-icon="symbol"] svg[data-symbol="rocket"]')).toBeVisible()
  await ui.shot('05-cmdbar-switch-symbol')
  await input.press('Escape')
  await expect(bar).toHaveCount(0)

  await ui.frameDoc().evaluate((id) => window.clu.spaces.update({ spaceId: id, icon: { kind: 'emoji', value: '📚' } }), WORK)
  await expect.poll(async () => (await state(ui)).spaces[WORK]?.icon).toEqual({ kind: 'emoji', value: '📚' })
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  await input.pressSequentially('switch to work')
  await expect(row.locator('.clu-cmd-icon [data-icon="emoji"]')).toHaveText('📚')
  await ui.shot('06-cmdbar-switch-emoji')
})
