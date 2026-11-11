// Hovering a pinned row on a preview site (Google Calendar here, a fake page in the FakeBridge, never a real
// account) opens the 239 x 256 live card beside the row, centered on it: the frame the browser lays the live compact
// page in, never a page snapshot. Any other site keeps the title pill.
import { expect, harnessTest } from './fixture'

const test = harnessTest('previews')

const host = (ui: { page: import('@playwright/test').Page }, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

test('a pinned Calendar row shows its live preview card beside the row', async ({ ui }) => {
  const pinned = ui.frame.locator('nav.clu-sidebar .clu-tab-row[data-tab-id]').first()
  const id = await pinned.getAttribute('data-tab-id')
  if (!id) throw new Error('no pinned row')
  await host(ui, ['tabs', id, 'url'], 'https://calendar.google.com/calendar/u/0/r/day')
  await host(ui, ['tabs', id, 'lifecycle'], 'live')
  await pinned.hover()
  const card = ui.page.frameLocator('iframe.passive[data-slot="2"]').locator('.clu-live-card')
  await expect(card).toBeVisible()
  await expect(card).toHaveAttribute('data-site', 'Google Calendar')
  await expect(card.locator('img')).toHaveCount(0)
  // The card grows from 0.76 over 100 ms; measure it once it has.
  await expect.poll(async () => Math.round((await card.boundingBox())?.width ?? 0)).toBe(239)
  const row = await pinned.boundingBox()
  const box = await card.boundingBox()
  if (!row || !box) throw new Error('no boxes')
  expect(Math.round(box.height)).toBe(256)
  expect(Math.abs(box.x - (row.x + row.width) - 12.5)).toBeLessThanOrEqual(1)
  await expect.poll(async () => ((await card.boundingBox())?.y ?? 0) + 128 - (row.y + row.height / 2), { timeout: 2000 }).toBeLessThan(1.5)
  await ui.shot('pinned-calendar-preview')
  await ui.page.mouse.move(600, 500)
  await expect(card).toHaveCount(0)
})

// An asleep row, as every pinned row is after a relaunch, gets the same live card as a loaded one,
// centered on the row the same way.
test('an asleep pinned Calendar row opens the live card centered on the row', async ({ ui }) => {
  const pinned = ui.frame.locator('nav.clu-sidebar .clu-tab-row[data-tab-id]').first()
  const id = await pinned.getAttribute('data-tab-id')
  if (!id) throw new Error('no pinned row')
  await host(ui, ['tabs', id, 'url'], 'https://calendar.google.com/')
  await host(ui, ['tabs', id, 'lifecycle'], 'unloaded')
  await pinned.hover()
  const card = ui.page.frameLocator('iframe.passive[data-slot="2"]').locator('.clu-live-card')
  await expect(card).toBeVisible()
  await expect(card).toHaveAttribute('data-site', 'Google Calendar')
  await expect.poll(async () => Math.round((await card.boundingBox())?.width ?? 0)).toBe(239)
  const row = await pinned.boundingBox()
  if (!row) throw new Error('no row')
  expect(Math.round((await card.boundingBox())?.height ?? 0)).toBe(256)
  await expect.poll(async () => Math.abs(((await card.boundingBox())?.y ?? 0) + 128 - (row.y + row.height / 2)), { timeout: 2000 }).toBeLessThan(1.5)
  await ui.shot('pinned-calendar-asleep-preview')
})

test('a pinned row on any other site keeps the title pill, never a page preview', async ({ ui }) => {
  const pinned = ui.frame.locator('nav.clu-sidebar .clu-tab-row[data-tab-id]').first()
  const id = await pinned.getAttribute('data-tab-id')
  if (!id) throw new Error('no pinned row')
  await host(ui, ['tabs', id, 'url'], 'https://github.com/example/repo')
  await host(ui, ['tabs', id, 'lifecycle'], 'live')
  await pinned.hover()
  await expect(ui.page.frameLocator('iframe.passive[data-slot="2"]').locator('.clu-hover-card')).toBeVisible()
  await expect(ui.page.frameLocator('iframe.passive[data-slot="2"]').locator('.clu-live-card')).toHaveCount(0)
})

// A Gmail favorite's card floats on top of the sidebar, hung from the tile's lower right corner 3.5 over it (left 65,
// top 133.5 under the first tile), 239 wide. It is the mail card, never a live page: with the title's count (the fake
// has no feed) it shows the count; a title with none gets the plain card. Moving to another tile closes it.
test('a Gmail favorite opens the mail card on top of the sidebar under the tile', async ({ ui }) => {
  const tiles = ui.frame.locator('.clu-favorites .clu-favorite[data-tab-id]')
  const id = await tiles.first().getAttribute('data-tab-id')
  if (!id) throw new Error('no favorite')
  await host(ui, ['tabs', id, 'url'], 'https://mail.google.com/mail/u/1/#inbox')
  await host(ui, ['tabs', id, 'title'], 'Inbox (3) - test@mail.test - Mail')
  await host(ui, ['tabs', id, 'lifecycle'], 'live')
  await tiles.first().hover()
  const passive = ui.page.frameLocator('iframe.passive[data-slot="2"]')
  const card = passive.locator('.clu-mail-card')
  await expect(card).toBeVisible()
  await expect(passive.locator('.clu-live-card')).toHaveCount(0)
  await expect(card).toHaveText('3 unread emails')
  await expect.poll(async () => Math.round((await card.boundingBox())?.width ?? 0)).toBe(239)
  const sidebar = await ui.frame.locator('nav.clu-sidebar').boundingBox()
  const tile = await tiles.first().locator('.clu-favorite-tile').boundingBox()
  const box = await card.boundingBox()
  if (!sidebar || !tile || !box) throw new Error('no boxes')
  expect(Math.abs(box.x - (tile.x + tile.width - 3.5))).toBeLessThanOrEqual(1.5)
  expect(Math.round(box.height)).toBe(64)
  await expect.poll(async () => Math.abs(((await card.boundingBox())?.y ?? 0) - (tile.y + tile.height - 3.5)), { timeout: 2000 }).toBeLessThan(1.5)
  // It starts inside the sidebar, over the tiles.
  expect(box.x).toBeLessThan(sidebar.x + sidebar.width)
  await ui.shot('favorite-gmail-count-card')
  if ((await tiles.count()) > 1) {
    await tiles.nth(1).hover()
    await expect(card).toHaveCount(0)
  }
  await ui.page.mouse.move(600, 500)
  await expect(card).toHaveCount(0)
  // No count in the title and no feed answer (a message view, a label, a page still loading): the plain card, never
  // a made-up "No unread emails". Only the feed's zero shows the no-mail card.
  await host(ui, ['tabs', id, 'title'], 'Re: invoice - test@mail.test - Gmail')
  await tiles.first().hover()
  await expect(passive.locator('.clu-hover-card')).toBeVisible()
  await expect(passive.locator('.clu-mail-card')).toHaveCount(0)
  await expect(passive.locator('.clu-mail-exhale')).toHaveCount(0)
  await ui.shot('favorite-gmail-no-feed-card')
})

// Gmail's card is usable. The pointer crosses from the tile onto the card and it stays;
// a message row highlights under the pointer; a click opens that message in the tile's own tab and closes the card.
// The FakeBridge's account 9 answers from a made-up feed (never anyone's mail).
test('the pointer reaches Gmail\'s card, a message highlights and opens in the tile\'s own tab', async ({ ui }) => {
  const tiles = ui.frame.locator('.clu-favorites .clu-favorite[data-tab-id]')
  const id = await tiles.first().getAttribute('data-tab-id')
  if (!id) throw new Error('no favorite')
  await host(ui, ['tabs', id, 'url'], 'https://mail.google.com/mail/u/9/#inbox')
  await host(ui, ['tabs', id, 'title'], 'Inbox (2) - fake@mail.test - Mail')
  await host(ui, ['tabs', id, 'lifecycle'], 'unloaded')
  await tiles.first().hover()
  const view = ui.page.locator('iframe.passive[data-slot="2"]')
  const passive = ui.page.frameLocator('iframe.passive[data-slot="2"]')
  const card = passive.locator('.clu-mail-list')
  await expect(card).toBeVisible()
  await expect(passive.locator('.clu-mail-row')).toHaveCount(2)
  await expect.poll(async () => Math.round((await card.boundingBox())?.width ?? 0)).toBe(239)
  // Out of the tile and onto the card, in steps through the corner they share: it stays open.
  const row = passive.locator('.clu-mail-row').nth(1)
  const box = await row.boundingBox()
  if (!box) throw new Error('no row')
  await ui.page.mouse.move(box.x + box.width / 2, box.y + box.height / 2, { steps: 8 })
  await expect(card).toBeVisible()
  await expect.poll(() => row.evaluate((el) => getComputedStyle(el).backgroundColor)).not.toBe('rgba(0, 0, 0, 0)')
  await expect(passive.locator('.clu-mail-row').first()).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)')
  await ui.shot('favorite-gmail-card-row-hover')
  await row.click()
  await expect(view).toHaveCount(0)
  const tab = await ui.page.evaluate((t) => (window as unknown as { cluFakeHost: { state(): { tabs: Record<string, { url: string }> } } }).cluFakeHost.state().tabs[t]?.url, id)
  expect(tab).toBe('https://mail.google.com/mail/u/9/#inbox/fake2')
  await expect(tiles.first()).toHaveAttribute('data-active', 'true')
  // Leaving the card closes it 75 ms on.
  await tiles.first().hover()
  await expect(card).toBeVisible()
  const cardBox = await card.boundingBox()
  if (!cardBox) throw new Error('no card')
  await ui.page.mouse.move(cardBox.x + 40, cardBox.y + cardBox.height / 2, { steps: 6 })
  await ui.page.mouse.move(700, 500, { steps: 4 })
  await expect(card).toHaveCount(0)
})
