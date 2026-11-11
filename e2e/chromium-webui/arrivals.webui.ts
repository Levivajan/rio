// In the ui:fake harness: a screenshot landing (state.screenshotArrivals, from the browser's folder watcher)
// and a download finishing each show a row at the bottom left of the sidebar right above the bottom bar: the file's
// thumbnail, the name, "N seconds ago", newest on top, the Library button on its tile; the x
// dismisses one and the rest drop away on their own. Right-click on a row opens that file's Library menu.
import { expect, harnessTest, state, WINDOW } from './fixture'

const test = harnessTest('chromium-f-recent')

type Host = { cluFakeHost: { edit(path: string[], value: unknown): void; state(): { downloads: { state: string; endedAt: number | null }[] } } }

test('a screenshot and a finished download list right above the bottom bar, newest on top, with thumbnails', async ({ ui }) => {
  await ui.page.goto('/?downloads=1')
  const sidebar = ui.frame.locator('nav.clu-sidebar')
  await expect(sidebar).toBeVisible()
  // What was there before (the seed's finished download) never shows.
  await expect(ui.frame.getByRole('list', { name: 'Just added' })).toHaveCount(0)

  await ui.page.evaluate(() =>
    (window as unknown as Host).cluFakeHost.edit(['screenshotArrivals'], [{ name: 'Screenshot 2026-11-09 at 18.58.12.png', kind: 'image', at: Date.now() }])
  )
  const list = ui.frame.getByRole('list', { name: 'Just added' })
  const shotRow = list.getByRole('button', { name: /^Screenshot 2026-11-09 at 18\.58\.12\.png/ })
  await expect(shotRow).toBeVisible()
  // The screenshot's own thumbnail (the fake's QuickLook stand-in), and how many seconds ago it landed.
  await expect(shotRow.locator('.clu-library-peek-tile img')).toBeVisible()
  await expect(shotRow).toContainText(/\d+ seconds? ago/)
  // The Library button sits on its tinted tile while the list shows.
  await expect.poll(() => ui.frame.locator('.clu-archive-button').evaluate((el) => getComputedStyle(el).backgroundColor)).not.toBe('rgba(0, 0, 0, 0)')

  // The running download finishes.
  await ui.page.evaluate(() => {
    const host = (window as unknown as Host).cluFakeHost
    const rows = host.state().downloads.map((d) => (d.state === 'progressing' ? { ...d, state: 'completed', receivedBytes: 4_200_000, bytesPerSecond: 0, etaMs: null, endedAt: Date.now() + 2000 } : d))
    host.edit(['downloads'], rows)
  })
  await expect(list.getByRole('listitem')).toHaveCount(2)
  // Newest on top.
  await expect(list.getByRole('listitem').first()).toContainText('quarterly-report.pdf')
  await expect(list.getByRole('listitem').last()).toContainText('Screenshot 2026-11-09 at 18.58.12.png')

  // Let the rise finish before measuring.
  await expect.poll(() => list.getByRole('listitem').last().evaluate((el) => getComputedStyle(el).opacity)).toBe('1')
  await ui.shot('arrivals-stack')
  // Bottom left, the lowest thumbnail right above the bottom bar (rows sit where the Library peek's do), inside the
  // sidebar.
  const card = await list.getByRole('listitem').last().locator('.clu-library-peek-tile').boundingBox()
  // Above the media bar when one plays, as the Library peek.
  const media = ui.frame.locator('.clu-media-card')
  const footer = await ((await media.count()) > 0 ? media.first() : ui.frame.locator('.clu-footer')).boundingBox()
  const nav = await sidebar.boundingBox()
  expect(card && footer && nav).toBeTruthy()
  if (card && footer && nav) {
    expect(card.y + card.height).toBeLessThanOrEqual(footer.y)
    expect(footer.y - (card.y + card.height)).toBeLessThan(16)
    expect(card.x).toBeGreaterThanOrEqual(nav.x)
    expect(card.x + card.width).toBeLessThanOrEqual(nav.x + nav.width)
  }

  // Right-click on a row opens the file's Library menu where the pointer is, inside the window, long after the
  // row rose (a filled rise animation made it the menu's containing block, so the menu opened
  // off the window). Copy toasts "Copied File".
  await shotRow.click({ button: 'right', position: { x: 60, y: 20 } })
  const menu = ui.frame.getByRole('menu')
  await expect(menu.getByRole('menuitem')).toHaveText(['Open', 'Show in Finder', 'Copy', 'Copy Path', 'Share…', 'Rename…', 'Move to Trash'])
  const row = await shotRow.boundingBox()
  const box = await menu.boundingBox()
  expect(row && box && nav).toBeTruthy()
  if (row && box && nav) {
    // The rows sit at the window's bottom, so the menu opens upward from the pointer, inside the sidebar's width.
    expect(Math.abs(box.y + box.height - (row.y + 20))).toBeLessThan(2)
    expect(box.y).toBeGreaterThanOrEqual(0)
    expect(box.x).toBeGreaterThanOrEqual(nav.x)
    expect(box.x + box.width).toBeLessThanOrEqual(nav.x + nav.width)
  }
  await expect(menu.getByRole('menuitem', { name: 'Show in Finder' })).toBeInViewport({ ratio: 1 })
  await ui.shot('arrivals-menu')
  await menu.getByRole('menuitem', { name: 'Copy', exact: true }).click()
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.toasts.map((t) => t.text)).toEqual(['Copied File'])
  await expect(ui.frame.getByRole('menu')).toHaveCount(0)

  await list.getByRole('listitem').last().hover()
  await list.getByRole('button', { name: 'Dismiss Screenshot 2026-11-09 at 18.58.12.png' }).click()
  await expect(list.getByRole('listitem')).toHaveCount(1)
  // The pointer leaves the list, so the other is no longer held.
  await ui.page.mouse.move(600, 20)
  // The other goes on its own after the show time.
  await expect(ui.frame.getByRole('list', { name: 'Just added' })).toHaveCount(0, { timeout: 14_000 })
})
