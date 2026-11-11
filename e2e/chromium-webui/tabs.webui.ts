// M3 flows in the frame WebUI over the FakeBridge (docs/chromium/m3.md): the archive panel with a literal search and
// a restore, Clear with Undo, a drag from Today to Pinned through the real computeDrop, and an accessibility audit of
// the archive panel. The browser side of the same flows is clu_browsertests CluTabs*. Screenshots go to
// $CLU_SHOTS_DIR/chromium-m3-ui.
import { readFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import type { Frame } from '@playwright/test'
import { expect, harnessTest, state, tab, WINDOW } from './fixture'

const test = harnessTest('chromium-m3-ui')
const HOME = '00000000-0000-4000-8000-000300000001'

type HostCall = { name: string; payload: unknown; result: { ok: boolean } }
const hostCalls = (ui: { page: import('@playwright/test').Page }, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: HostCall[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

test('archive panel: lists archived tabs by day, searches % literally, restores to the top of Today', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await nav.getByRole('button', { name: 'View Library' }).click()
  // The Library button opens on Downloads; the archive is a rail section.
  await ui.frame.getByRole('tab', { name: 'Archived Tabs' }).click()
  const panel = ui.frame.locator('section.clu-archive')
  await expect(panel).toBeVisible()
  await expect(panel.locator('.clu-archive-row')).toHaveCount(5)
  // No buttons on a row; right-click offers Restore Tab and Delete from Archive.
  await expect(panel.locator('.clu-archive-row').first().locator('button')).toHaveCount(1)
  await panel.locator('.clu-archive-row').first().click({ button: 'right' })
  await expect(ui.frame.getByRole('menuitem')).toHaveText(['Restore Tab', 'Delete from Archive'])
  await ui.shot('archive-row-menu')
  await ui.page.keyboard.press('Escape')
  await expect(ui.frame.getByRole('menu')).toHaveCount(0)
  await ui.shot('archive-panel')

  await panel.getByRole('searchbox', { name: 'Search Archive' }).or(panel.getByLabel('Search Archive')).first().fill('100%')
  await expect(panel.locator('.clu-archive-row')).toHaveCount(1)
  await expect(panel.locator('.clu-archive-row')).toContainText('100% done')
  await ui.shot('archive-search')

  // A click on the row restores it (02-tabs F7), as its Restore icon does.
  await panel.locator('.clu-archive-row .clu-archive-open').first().click()
  await expect.poll(async () => {
    const s = await state(ui)
    const first = s.spaces[HOME]?.today[0] ?? ''
    return s.tabs[first]?.url
  }).toBe('https://deals.example.com/')
  await expect(panel.locator('.clu-archive-row')).toHaveCount(0)
  const s = await state(ui)
  expect(s.windows[WINDOW]?.activeTabId).toBe(s.spaces[HOME]?.today[0])

  await nav.getByRole('button', { name: 'Back to tabs' }).click()
  await expect(panel).toHaveCount(0)
})

test('Clear takes the Today rows off the list with an Undo toast; Undo puts them back', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await expect(nav.locator(`[data-tab-id="${tab(20)}"]`)).toBeVisible()
  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'archive.clearToday' }))
  await expect(nav.locator(`[data-tab-id="${tab(20)}"]`)).toHaveCount(0)
  await ui.shot('today-cleared')
  const toast = (await state(ui)).windows[WINDOW]?.toasts.at(-1)
  expect(toast?.text).toBe('Cleared 5 tabs')
  expect(toast?.action?.commandId).toBe('archive.undoClear')

  await ui.frameDoc().evaluate((id) => window.clu.commands.run({ id }), toast?.action?.commandId ?? '')
  await expect(nav.locator(`[data-tab-id="${tab(20)}"]`)).toBeVisible()
  await expect(nav.locator(`[data-tab-id="${tab(24)}"]`)).toBeVisible()
})

test('dragging a Today row onto Pinned pins it through tabs.move', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const row = nav.locator(`[data-tab-id="${tab(22)}"]`)
  const target = nav.locator(`[data-tab-id="${tab(10)}"]`)
  const from = await row.boundingBox()
  const to = await target.boundingBox()
  if (!from || !to) throw new Error('rows not laid out')
  await ui.page.mouse.move(from.x + from.width / 2, from.y + from.height / 2)
  await ui.page.mouse.down()
  // Past the 4 px activation distance, then into the top third of the first pinned row.
  for (let i = 1; i <= 10; i++) {
    await ui.page.mouse.move(from.x + from.width / 2, from.y + from.height / 2 + ((to.y + 4 - (from.y + from.height / 2)) * i) / 10)
  }
  await ui.page.mouse.up()
  await expect.poll(async () => (await state(ui)).tabs[tab(22)]?.section).toBe('pinned')
  const moves = (await hostCalls(ui, 'tabs.move')) as { nodeId: string; target: { section: string; index: number } }[]
  // A drop asks for the cross-profile confirm and marks itself a drop, which ticks the trackpad.
  expect(moves.at(-1)).toMatchObject({ nodeId: tab(22), target: { section: 'pinned', index: 0 }, ask: true, drop: true })
  await expect(row).toBeVisible()
  // The rows settle by FLIP; shoot once no finite animation runs (the loading spinner never ends).
  await expect
    .poll(() => ui.frameDoc().evaluate(() => document.getAnimations().filter((a) => a.playState === 'running' && a.effect?.getTiming().iterations !== Infinity).length))
    .toBe(0)
  await ui.shot('dragged-to-pinned')
})

test.describe('accessibility', () => {
  test.use({ bypassCSP: true })
  const axeSource = readFileSync(createRequire(import.meta.url).resolve('axe-core/axe.min.js'), 'utf8')
  type AxeViolation = { id: string; impact: string | null; help: string }

  async function audit(doc: Frame, include: string): Promise<AxeViolation[]> {
    await doc.addScriptTag({ content: axeSource })
    return doc.evaluate(async (sel) => {
      const axe = (window as unknown as { axe: { run(ctx: unknown, opts: unknown): Promise<{ violations: AxeViolation[] }> } }).axe
      const r = await axe.run({ include: [sel] }, { runOnly: ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa'] })
      return r.violations.map((v) => ({ id: v.id, impact: v.impact, help: v.help }))
    }, include)
  }

  test('the archive panel has no serious or critical WCAG 2.1 AA violations', async ({ ui }) => {
    await ui.frame.locator('nav.clu-sidebar').getByRole('button', { name: 'View Library' }).click()
    await ui.frame.getByRole('tab', { name: 'Archived Tabs' }).click()
    await expect(ui.frame.locator('section.clu-archive .clu-archive-row').first()).toBeVisible()
    const violations = await audit(ui.frameDoc(), 'section.clu-archive')
    expect(violations.filter((v) => v.impact === 'serious' || v.impact === 'critical')).toEqual([])
  })
})
