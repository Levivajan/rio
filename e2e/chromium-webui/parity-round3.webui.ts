// Fixes in the frame and overlay WebUIs (docs/chromium/fixes.md): the space name
// row's right-click menu and its Live Folders ▸, a drop above a pinned split row, the theme editor of a space
// with no theme, the selected favorite's ring and the row speaker's place and delay. Shots go to $CLU_SHOTS_DIR/fix-round3.
import { expect, harnessTest, state, tab, WINDOW, type Harness } from './fixture'

const test = harnessTest('fix-round3')
const HOME = '00000000-0000-4000-8000-000300000001'

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)
const hostCalls = (ui: Harness, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

test('a right-click on the space name row opens the space menu at the pointer, with Live Folders ▸', async ({ ui }) => {
  const header = ui.frame.locator(`[data-space-header="${HOME}"]`)
  const box = await header.boundingBox()
  if (!box) throw new Error('no space header')
  const at = { x: Math.round(box.x + 60), y: Math.round(box.y + box.height / 2) }
  await ui.page.mouse.click(at.x, at.y, { button: 'right' })
  await expect.poll(() => hostCalls(ui, 'tabs.openMenu')).toEqual([{ menu: 'space', nodeId: HOME, anchor: { x: at.x, y: at.y }, light: true }])
  // A right-click neither collapses the pinned rows nor starts a rename.
  expect((await state(ui)).spaces[HOME]?.pinnedCollapsed).toBe(false)
  await expect(header.locator('input')).toHaveCount(0)

  const menu = ui.overlay.locator('[data-surface="menu"]')
  await expect(menu.getByRole('menuitem', { name: 'Live Folders' })).toBeVisible()
  await expect(menu.getByRole('menuitem', { name: /Export “.*” into Folder/ })).toBeVisible()
  await menu.getByRole('menuitem', { name: 'Live Folders' }).hover()
  await expect(menu.getByRole('menuitem', { name: 'GitHub Releases…' })).toBeVisible()
  await ui.shot('space-menu-live-folders')
  await menu.getByRole('menuitem', { name: 'RSS or Atom Feed…' }).click()

  const prompt = ui.overlay.locator('.clu-center[data-surface="folders.live"]')
  await expect(prompt).toBeVisible()
  await expect(prompt).toHaveAttribute('data-kind', 'feed')
  const field = prompt.getByLabel('RSS or Atom feed address')
  // Junk keeps the dialog open with the reason.
  await field.fill('not a feed')
  await prompt.getByRole('button', { name: 'Create' }).click()
  await expect(prompt.getByRole('alert')).toHaveText('Enter a feed address, like example.com/feed.xml')
  await field.fill('news.example.com/feed.xml')
  await ui.shot('live-folder-prompt')
  await prompt.getByRole('button', { name: 'Create' }).click()
  await expect(prompt).toBeHidden()
  const made = (await hostCalls(ui, 'folders.createLive')).at(-1)
  expect(made).toEqual({ spaceId: HOME, kind: 'feed', source: 'news.example.com/feed.xml' })
  const s = await state(ui)
  const folder = s.spaces[HOME]?.pinned.at(-1)
  expect(folder?.kind).toBe('folder')
  await expect(ui.frame.locator(`[data-folder-id="${folder?.id}"]`)).toContainText('news.example.com')
})

test('a Today row dropped above a pinned split row shows the line and pins before the split', async ({ ui }) => {
  await ui.frameDoc().evaluate(([a, b]) => window.clu.split.create({ tabIds: [a, b], dir: 'row' }), [tab(10), tab(11)] as const)
  const split = ui.frame.locator('[data-split-id]').first()
  await expect(split).toBeVisible()
  // The rows below slide up into the place the split freed (FLIP); measure once they settle.
  await expect
    .poll(() => ui.frameDoc().evaluate(() => document.getAnimations().filter((a) => a.playState === 'running' && a.effect?.getTiming().iterations !== Infinity).length))
    .toBe(0)
  const row = ui.frame.locator(`[data-tab-id="${tab(22)}"]`)
  const from = await row.boundingBox()
  const to = await split.boundingBox()
  if (!from || !to) throw new Error('rows not laid out')
  await ui.page.mouse.move(from.x + from.width / 2, from.y + from.height / 2)
  await ui.page.mouse.down()
  for (let i = 1; i <= 10; i++) {
    await ui.page.mouse.move(from.x + from.width / 2, from.y + from.height / 2 + ((to.y + 5 - (from.y + from.height / 2)) * i) / 10)
  }
  // The drop line sits on the split row's top edge.
  const line = ui.frame.locator('.clu-drop-line, [data-drop-indicator="line"]').first()
  await expect(line).toBeVisible()
  const lineBox = await line.boundingBox()
  expect(Math.abs((lineBox?.y ?? 0) + (lineBox?.height ?? 0) / 2 - to.y)).toBeLessThanOrEqual(3)
  await ui.shot('drop-above-split')
  await ui.page.mouse.up()
  await expect.poll(() => hostCalls(ui, 'tabs.move')).toEqual([expect.objectContaining({ nodeId: tab(22), target: expect.objectContaining({ section: 'pinned', index: 0 }) })])
  await expect.poll(async () => (await state(ui)).tabs[tab(22)]?.section).toBe('pinned')
})

test('a space with no theme opens the editor with the handles of the theme its sidebar shows; a tap moves the main one', async ({ ui }) => {
  await host(ui, ['spaces', HOME, 'theme'], null)
  await ui.frameDoc().evaluate((spaceId) => window.clu.theme.openPicker({ spaceId, anchor: { x: 12, y: 96, width: 236, height: 28 } }), HOME)
  const picker = ui.overlay.locator('.clu-theme-picker')
  await expect(picker).toBeVisible()
  await expect(picker).toHaveAttribute('data-colors', '2')
  await expect(picker.locator('.clu-tp-handle')).toHaveCount(2)
  await expect(picker.getByText('Tap to pick a color for this space')).toHaveCount(0)
  await expect(picker.getByRole('button', { name: 'Remove color' })).toBeEnabled()
  await expect(picker.getByRole('button', { name: 'Add color' })).toBeEnabled()
  // Nothing is written by opening it, so the sidebar keeps its color until a handle moves.
  expect(await hostCalls(ui, 'theme.setSpaceTheme')).toEqual([])
  expect(await hostCalls(ui, 'theme.previewSpaceTheme')).toEqual([])
  await ui.shot('theme-no-theme-handles')

  const canvas = await picker.getByTestId('theme-field').boundingBox()
  if (!canvas) throw new Error('no canvas')
  await ui.page.mouse.click(canvas.x + canvas.width / 2 + 80, canvas.y + canvas.height / 2)
  await expect(picker.locator('.clu-tp-handle')).toHaveCount(2)
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.colors.length).toBe(2)
  await ui.shot('theme-no-theme-tap')
})

// The selected favorite's ring is the space's accent, whatever color the site icon has.
test('the selected favorite carries a ring in the space accent, not the icon color', async ({ ui }) => {
  const fav = ui.frame.locator('.clu-favorite').first()
  const id = await fav.getAttribute('data-tab-id')
  if (!id) throw new Error('no favorite')
  await host(ui, ['tabs', id, 'iconColor'], '#494949')
  await host(ui, ['spaces', HOME, 'accent'], '#e8762b')
  await host(ui, ['windows', WINDOW, 'activeTabId'], id)
  await expect(fav).toHaveAttribute('data-active', 'true')
  await expect
    .poll(() => fav.locator('.clu-favorite-tile').evaluate((el) => getComputedStyle(el).boxShadow))
    .toContain('rgb(232, 118, 43) 0px 0px 0px 2px inset')
  expect(await fav.locator('.clu-favorite-tile').evaluate((el) => getComputedStyle(el).boxShadow)).not.toContain('rgb(73, 73, 73)')
  await ui.shot('favorite-ring-accent')
})

test('a playing row shows its speaker 0.8 s after the sound starts, its box 3 left of the slot', async ({ ui }) => {
  const row = ui.frame.locator(`[data-tab-id="${tab(21)}"]`)
  const speaker = row.locator('.clu-row-audio')
  await expect(speaker).toHaveCount(0)
  const started = Date.now()
  await host(ui, ['tabs', tab(21), 'audible'], true)
  await expect(row).toHaveAttribute('data-audible', 'true')
  await expect(speaker).toHaveCount(0)
  await expect(speaker).toBeVisible()
  expect(Date.now() - started).toBeGreaterThanOrEqual(750)
  const box = await speaker.boundingBox()
  // Hover box x 42 to 65, title at 70.5.
  expect(box?.width).toBe(23)
  // The title slides there over 150 ms.
  await expect.poll(async () => Math.abs(((await row.locator('.clu-row-title').boundingBox())?.x ?? 0) - ((box?.x ?? 0) + 23) - 5.5)).toBeLessThanOrEqual(0.5)
  // Stopping hides it at once; a muted tab shows the crossed speaker at once.
  await host(ui, ['tabs', tab(21), 'audible'], false)
  await expect(speaker).toHaveCount(0)
  await host(ui, ['tabs', tab(21), 'muted'], true)
  await expect(row.locator('.clu-row-audio[data-audio="muted"]')).toBeVisible()
  expect((await state(ui)).windows[WINDOW]).toBeTruthy()
})
