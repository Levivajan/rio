// M6 flows in the frame and overlay WebUIs over the FakeBridge (docs/chromium/m6.md): a split row in the sidebar over
// Chromium's own pane chrome, the drop preview while a sidebar tab is dragged over the page, Separate from the key
// table, and the media card's pause, play and jump. The browser side of the same flows is clu_browsertests
// CluSplit*, CluMedia*, CluPeek* and CluMini*. Screenshots go to $CLU_SHOTS_DIR/chromium-m6-ui.
import { expect, harnessTest, state, tab, WINDOW } from './fixture'

const test = harnessTest('chromium-m6-ui')

test('a split is one sidebar row over Chromium pane chrome; Separate ends it', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const made = await ui.frameDoc().evaluate(([a, b]) => window.clu.split.create({ tabIds: [a, b], dir: 'row' }), [tab(20), tab(21)] as const)
  expect(made.ok).toBe(true)
  const splitId = made.ok ? made.value.splitId : ''
  const row = nav.locator(`[data-split-id="${splitId}"]`)
  await expect(row).toBeVisible()
  await expect(row.locator('[data-split-member]')).toHaveCount(2)
  await expect(row).toHaveAttribute('data-active', 'true')
  // Chromium draws the panes' bars, divider and outline: the frame draws none.
  await expect(ui.frame.locator(`[data-split-view="${splitId}"][data-native="true"]`)).toHaveCount(1)
  await expect(ui.frame.locator('[data-split-pane-bar]')).toHaveCount(0)
  await ui.shot('split-row')

  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'split.separate' }))
  await expect(row).toHaveCount(0)
  await expect(nav.locator(`[data-tab-id="${tab(20)}"]`)).toBeVisible()
  await expect(nav.locator(`[data-tab-id="${tab(21)}"]`)).toBeVisible()
})

// A split holds up to 4 panes. A third tab dropped on a pane edge joins the split; the frame draws each
// hosted pane's bar, and the bar's close button closes that pane while the others stay a split.
test('a third pane joins the split with its own bar, and closing it keeps two panes', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const made = await ui.frameDoc().evaluate(([a, b]) => window.clu.split.create({ tabIds: [a, b], dir: 'row' }), [tab(20), tab(21)] as const)
  expect(made.ok).toBe(true)
  const splitId = made.ok ? made.value.splitId : ''
  const joined = await ui.frameDoc().evaluate(([c, b]) => window.clu.split.dropTab({ tabId: c, targetTabId: b, edge: 'right' }), [tab(23), tab(21)] as const)
  expect(joined.ok).toBe(true)
  const third = joined.ok ? joined.value.splitId : ''
  const row = nav.locator(`[data-split-id="${third}"]`)
  await expect(row.locator('[data-split-member]')).toHaveCount(3)
  await expect(nav.locator(`[data-split-id="${splitId}"]`)).toHaveCount(0)
  const bars = ui.frame.locator('[data-split-pane-bar]')
  await expect(bars).toHaveCount(3)
  const boxes = await Promise.all([0, 1, 2].map((i) => bars.nth(i).boundingBox()))
  // Side by side at even shares.
  expect(Math.abs((boxes[0]?.width ?? 0) - (boxes[2]?.width ?? 0))).toBeLessThanOrEqual(1)
  expect(boxes[1]?.x ?? 0).toBeGreaterThan((boxes[0]?.x ?? 0) + (boxes[0]?.width ?? 0))
  // Each bar is a 32 pt pane toolbar, back, forward and the URL on every pane; the focused pane
  // adds reload, site controls and split.
  expect(boxes[0]?.height).toBe(32)
  for (const id of [tab(20), tab(21), tab(23)]) {
    const bar = ui.frame.locator(`[data-split-pane-bar="${id}"]`)
    await expect(bar.getByRole('button', { name: 'Back' })).toBeVisible()
    await expect(bar.getByRole('button', { name: 'Forward' })).toBeVisible()
    await expect(bar.locator('[data-split-pane-url]')).not.toBeEmpty()
  }
  const focused = ui.frame.locator('[data-split-pane][data-focused="true"] [data-split-pane-bar]')
  await expect(focused).toHaveCount(1)
  await expect(focused.getByRole('button', { name: 'Reload' })).toBeVisible()
  await expect(focused.getByRole('button', { name: 'Separate pane' })).toBeVisible()
  await expect(ui.frame.locator('[data-split-pane][data-focused="false"] [aria-label="Reload"]')).toHaveCount(0)
  // The pane glyphs on a 16 pt box (reload, split and close were 14 and 12), and the host darker than its path.
  for (const name of ['Reload', 'Separate pane', 'Close pane']) {
    const box = await focused.getByRole('button', { name }).locator('svg').boundingBox()
    expect(box?.width, name).toBe(16)
  }
  await expect(ui.frame.locator('[data-split-pane-url] .clu-split-bar-url-rest').first()).toHaveCSS('color', 'rgb(127, 127, 127)')
  await ui.shot('split-three-panes')

  await ui.frame.locator(`[data-split-pane-bar="${tab(23)}"] [aria-label="Close pane"]`).click()
  await expect(bars).toHaveCount(0)
  await expect(nav.locator('[data-split-member]')).toHaveCount(2)
  await expect(ui.frame.locator('[data-split-view][data-native="true"]')).toHaveCount(1)
})

test('dragging a tab over the page previews half of the pane toward the edge', async ({ ui }) => {
  await ui.frameDoc().evaluate((id) => window.clu.tabs.activate({ tabId: id }), tab(20))
  await ui.frameDoc().evaluate((id) => window.clu.split.dragState({ phase: 'start', tabId: id }), tab(21))
  await ui.frameDoc().evaluate(([dragged, target]) => window.clu.split.dragState({ phase: 'hover', tabId: dragged, targetTabId: target, edge: 'right' }), [tab(21), tab(20)] as const)
  const zone = ui.overlay.locator(`[data-split-drop-zone="${tab(20)}"][data-edge="right"]`)
  await expect(zone).toBeVisible()
  const s = await state(ui)
  const page = s.windows[WINDOW]?.shell.layout.pageRect
  const box = await zone.boundingBox()
  expect(Math.round(box?.width ?? 0)).toBe(Math.floor((page?.width ?? 0) / 2))
  await ui.shot('split-drop-preview')

  await ui.frameDoc().evaluate((id) => window.clu.split.dragState({ phase: 'cancel', tabId: id }), tab(21))
  await expect(zone).toHaveCount(0)
  expect((await state(ui)).windows[WINDOW]?.splitDrop).toBeNull()
})

test('the media card pauses and plays a background tab and jumps to it', async ({ ui }) => {
  const card = ui.frame.locator('.clu-media-card')
  await expect(card).toBeVisible()
  const tabId = (await card.getAttribute('data-media-tab-id')) ?? ''
  await expect(card).toHaveAttribute('data-state', 'playing')
  await card.getByRole('button', { name: 'Pause', exact: true }).click()
  await expect(card).toHaveAttribute('data-state', 'paused')
  await ui.shot('media-card-paused')
  await card.getByRole('button', { name: 'Play', exact: true }).click()
  await expect(card).toHaveAttribute('data-state', 'playing')
  await card.locator('.clu-media-title').click()
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.activeTabId).toBe(tabId)
})
