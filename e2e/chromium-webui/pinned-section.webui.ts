// In the frame WebUI: the space name row toggles the pinned section, and a loaded Google Calendar pinned row shows its next event under its title. Shots go to
// $CLU_SHOTS_DIR/pinned-section.
import { expect, harnessTest, state, WINDOW, type Harness } from './fixture'

const test = harnessTest('pinned-section')
const HOME = '00000000-0000-4000-8000-000300000001'

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

const pinnedTabs = async (ui: Harness) => {
  const s = await state(ui)
  return (s.spaces[HOME]?.pinned ?? []).filter((n) => n.kind === 'tab').map((n) => n.id)
}

test('open shows the icon and the dimmed name; closed shows > and only the selected pinned row', async ({ ui }) => {
  const header = ui.frame.locator(`[data-space-header="${HOME}"]`)
  const pinned = await pinnedTabs(ui)
  expect(pinned.length).toBeGreaterThan(1)
  const selected = pinned[0] as string
  await ui.frameDoc().evaluate((id) => window.clu.tabs.activate({ tabId: id }), selected)
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.activeTabId).toBe(selected)
  for (const id of pinned) await expect(ui.frame.locator(`[data-tab-id="${id}"]`)).toBeVisible()
  // Open, at rest: the name is the dimmed label color and the chevron is hidden.
  await ui.page.mouse.move(600, 500)
  await expect(header).toHaveAttribute('data-collapsed', 'false')
  await expect(header.locator('.clu-space-header-name')).toHaveCSS('color', /rgba\(\d+, \d+, \d+, 0\.\d+\)/)
  await expect(header.locator('.clu-space-header-toggle')).toBeHidden()

  await header.locator('.clu-space-header-name').click()
  await expect(header).toHaveAttribute('data-collapsed', 'true')
  expect((await state(ui)).spaces[HOME]?.pinnedCollapsed).toBe(true)
  await ui.page.mouse.move(600, 500)
  await expect(header.locator('.clu-space-header-toggle')).toBeVisible()
  await expect(header.locator('.clu-space-header-toggle')).toHaveAttribute('aria-label', 'Show pinned tabs')
  // Every pinned row hides except the selected one.
  await expect(ui.frame.locator(`[data-tab-id="${selected}"]`)).toBeVisible()
  for (const id of pinned.slice(1)) await expect(ui.frame.locator(`[data-tab-id="${id}"]`)).toHaveCount(0)
  await ui.shot('b48-closed')

  // One click opens it again.
  await header.locator('.clu-space-header-name').click()
  await expect(header).toHaveAttribute('data-collapsed', 'false')
  for (const id of pinned) await expect(ui.frame.locator(`[data-tab-id="${id}"]`)).toBeVisible()
  await ui.page.mouse.move(600, 500)
  await ui.shot('b48-open')
})

test('a Calendar pinned row shows its next event under the title, nothing without one', async ({ ui }) => {
  const id = (await pinnedTabs(ui))[0] as string
  await host(ui, ['tabs', id, 'url'], 'https://calendar.google.com/calendar/u/0/r/day')
  await host(ui, ['tabs', id, 'subtitle'], 'Event at 4:30 PM')
  const row = ui.frame.locator(`[data-tab-id="${id}"]`)
  await expect(row.locator('.clu-row-title-stack[data-event="true"] .clu-row-subtitle')).toHaveText('Event at 4:30 PM')
  await expect(row.locator('.clu-row-subtitle')).toHaveCSS('opacity', '1')
  await ui.shot('b48-calendar-subtitle')
  await host(ui, ['tabs', id, 'subtitle'], null)
  await expect(row.locator('.clu-row-subtitle')).toHaveCount(0)
})
