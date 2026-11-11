// Tab rows and folders: the inline rename field, Tidy on the
// Today divider, folder rows, Today's order and spacing, and row states. Shots go to $CLU_SHOTS_DIR/rows-folders.
import { expect, harnessTest, type Harness } from './fixture'

const test = harnessTest('rows-folders')
const FOLDER = '00000000-0000-4000-8000-000400000001'
const folderRow = (ui: Harness) => ui.frame.locator(`.clu-folder-row[data-folder-id="${FOLDER}"]`)
const box = async (l: { boundingBox(): Promise<{ x: number; y: number; width: number; height: number } | null> }) => {
  const b = await l.boundingBox()
  if (!b) throw new Error('not laid out')
  return b
}

test('a folder renames in place, no box, still semibold', async ({ ui }) => {
  const row = folderRow(ui)
  await row.locator('.clu-row-title').dblclick()
  const field = row.getByRole('textbox', { name: 'Folder name' })
  await expect(field).toBeFocused()
  await expect(field).toHaveCSS('border-top-width', '0px')
  await expect(field).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)')
  await expect(field).toHaveCSS('font-weight', '600')
  await ui.shot('g28-folder-rename')
  await field.press('Escape')
})

test('Tidy shows the broom and its label, a hairline, then Clear without its arrow', async ({ ui }) => {
  for (const url of ['https://github.com/clu/pull/7', 'https://weather.test/week', 'https://github.com/clu/pull/9', 'https://news.test/world', 'https://a.test/', 'https://b.test/', 'https://c.test/']) {
    await ui.frameDoc().evaluate((u) => window.clu.tabs.create({ url: u, activate: false }), url)
  }
  const tidy = ui.frame.locator('[data-tidy-today]')
  await expect(tidy).toHaveCount(1)
  await ui.frame.locator('nav.clu-sidebar').hover()
  await expect(tidy).toBeVisible()
  await expect(tidy).toHaveText('Tidy')
  const clear = ui.frame.locator('[data-clear-today]')
  await expect(clear.locator('svg')).toBeHidden()
  const [t, c] = await Promise.all([box(tidy), box(clear)])
  // Side by side, never overlapping, with the hairline between them.
  expect(t.x + t.width).toBeLessThanOrEqual(c.x - 6)
  await ui.shot('g30-tidy-clear')
})

test('the tab list scrolls under a clear track', async ({ ui }) => {
  const list = ui.frame.locator('.clu-tab-list')
  await expect(list).toHaveCSS('scrollbar-width', 'thin')
  await expect(list).toHaveCSS('scrollbar-color', 'rgba(0, 0, 0, 0) rgba(0, 0, 0, 0)')
})

test('a child row fills from 16 in, the closed glyph is filled, an open folder shows its name in an accent pill', async ({ ui }) => {
  const row = folderRow(ui)
  const child = ui.frame.locator('.clu-tab-row[aria-level="2"]').first()
  const [f, c] = await Promise.all([box(row), box(child)])
  expect(Math.round(c.x - f.x)).toBe(16)
  await child.hover()
  await ui.shot('g31-child-hover')
  await row.locator('.clu-row-title').hover()
  const pill = row.getByRole('tooltip')
  await expect(pill).toHaveText('Chromium M2')
  const accent = await ui.frameDoc().evaluate(() => getComputedStyle(document.querySelector('.clu-folder-glyph') as Element).color)
  await expect(pill).toHaveCSS('background-color', accent)
  await ui.shot('g38-folder-pill')
  await row.getByRole('button', { name: 'Collapse folder' }).click()
  await expect(row).toHaveAttribute('data-collapsed', 'true')
  await ui.page.mouse.move(900, 400)
  const fill = await row.locator('.clu-folder-glyph svg > path').first().evaluate((p) => getComputedStyle(p).fill)
  expect(fill).not.toBe('none')
  await ui.shot('g36-closed-glyph')
})

test('Today divider spacing, New Tab on the pitch with Today empty, the space name at x 16 at rest', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await ui.page.mouse.move(900, 400)
  const lastPinned = ui.frame.locator('.clu-tab-row[aria-level="2"]').last()
  const newTab = ui.frame.locator('[data-new-tab]')
  const divider = ui.frame.locator('.clu-today-divider')
  const [p, n, d] = await Promise.all([box(lastPinned), box(newTab), box(divider)])
  expect(n.y - (p.y + p.height)).toBe(30.5)
  const lineTop = await divider.evaluate((el) => parseFloat(getComputedStyle(el, '::before').top))
  expect(d.y + lineTop - (p.y + p.height)).toBe(14.5)
  const [side, name] = await Promise.all([box(nav), box(ui.frame.locator('.clu-space-header-name'))])
  expect(name.x - side.x).toBe(16)
  await ui.shot('g40-today-divider')
  await nav.hover()
  await ui.frame.locator('[data-clear-today]').click()
  await expect(divider).toHaveCSS('height', '0px')
  await ui.page.mouse.move(900, 400)
  const [p2, n2] = await Promise.all([box(lastPinned), box(newTab)])
  await expect.poll(async () => (await box(newTab)).y - (p2.y + p2.height)).toBe(5)
  expect(n2.height).toBe(36)
  await ui.shot('g41-today-empty')
})

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

test('the navigated-away button and title, a multi-selection in the selected card, no new tab on a double-click', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const pinned = nav.locator('.clu-tab-row[data-tab-id]').first()
  const id = await pinned.getAttribute('data-tab-id')
  if (!id) throw new Error('no pinned row')
  await host(ui, ['tabs', id, 'offPinned'], true)
  await expect(pinned).toHaveAttribute('data-off-pinned', 'true')
  const [side, button, title] = await Promise.all([box(nav), box(pinned.locator('.clu-row-reset')), box(pinned.locator('.clu-row-title'))])
  expect([button.width, button.height]).toEqual([38, 38])
  expect(button.x - side.x).toBe(7)
  expect(Math.abs(title.x - side.x - 46.5)).toBeLessThanOrEqual(1)
  await pinned.locator('.clu-row-reset').hover()
  await ui.shot('g47-pinned-navigated')

  // Cmd+click two Today rows: both take the selected row's card.
  const rows = nav.locator('.clu-tab-row[data-tab-id]')
  const count = await rows.count()
  await rows.nth(count - 1).click({ modifiers: ['Meta'] })
  await rows.nth(count - 2).click({ modifiers: ['Meta'] })
  const selected = nav.locator(".clu-tab-row[data-selected='true']:not([data-active='true'])").first()
  await expect(selected).toBeVisible()
  const active = await nav.locator(".clu-tab-row[data-active='true']").first().evaluate((el) => getComputedStyle(el).backgroundColor)
  await expect(selected).not.toHaveCSS('box-shadow', 'none')
  expect(await selected.evaluate((el) => getComputedStyle(el).backgroundColor)).toBe(active)
  await ui.shot('g52-multi-select')
  await ui.page.keyboard.press('Escape')

  // A double-click on the empty list space opens nothing.
  const list = await box(ui.frame.locator('.clu-tab-list'))
  const last = await box(rows.nth(count - 1))
  if (list.y + list.height - (last.y + last.height) > 12) {
    await ui.page.mouse.dblclick(list.x + 60, last.y + last.height + 8)
    await expect(ui.overlay.locator('[data-surface="command"]')).toHaveCount(0)
  }
})

test('the speaker slides the title in, close is there with the hover, Clear fades Today at once', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const rows = nav.locator('.clu-new-tab ~ .clu-tab-row[data-tab-id]')
  const row = rows.first()
  // The close itself shows at once (no opacity or width transition); only its own hover fill fades, over 85 ms.
  await expect(row.locator('.clu-row-close')).toHaveCSS('transition-property', 'background-color')
  await expect(row.locator('.clu-row-close')).toHaveCSS('transition-duration', '0.085s')
  const id = await row.getAttribute('data-tab-id')
  if (!id) throw new Error('no Today row')
  await host(ui, ['tabs', id, 'audible'], true)
  const speaker = row.locator('.clu-tooltip-anchor:has(> .clu-row-audio)')
  await expect(speaker).toBeVisible()
  await expect(speaker).toHaveCSS('animation-name', 'clu-audio-in')
  await expect(speaker).toHaveCSS('animation-duration', '0.15s')
  await nav.hover()
  // In one task: the click starts the rows' fade and hides their titles; Clear is asked at once, so New Tab moves in
  // the click's first frame.
  const fading = await ui.frameDoc().evaluate(() => {
    ;(document.querySelector('[data-clear-today]') as HTMLElement).click()
    const first = document.querySelector('.clu-new-tab ~ .clu-tab-row') as HTMLElement
    return { property: getComputedStyle(first).transitionProperty, title: getComputedStyle(first.querySelector('.clu-row-title') as HTMLElement).visibility }
  })
  expect(fading).toEqual({ property: 'opacity', title: 'hidden' })
  await expect(rows).toHaveCount(0)
  await expect(ui.frame.locator('.clu-tab-tree')).not.toHaveAttribute('data-clearing', '')
})

test('a collapsing folder slides its children up under its row, clipped at its bottom edge', async ({ ui }) => {
  const row = folderRow(ui)
  const children = ui.frame.locator('.clu-tab-row[aria-level="2"]')
  const count = await children.count()
  expect(count).toBeGreaterThan(0)
  await row.hover()
  await row.getByRole('button', { name: 'Collapse folder' }).click()
  const clip = ui.frame.locator('.clu-row-collapse-clip')
  await expect(clip).toHaveCount(1)
  await expect(clip.locator('.clu-tab-row')).toHaveCount(count)
  await expect(clip).toHaveAttribute('aria-hidden', 'true')
  const [r, c] = await Promise.all([box(row), box(clip)])
  expect(Math.round(c.y)).toBe(Math.round(r.y + r.height))
  await ui.shot('g37-collapse-mid')
  await expect(children).toHaveCount(0)
  await expect(clip).toHaveCount(0)
})

test('a title tidied right after the pin morphs in letter by letter, with the rename hint under it', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const row = nav.locator('.clu-new-tab ~ .clu-tab-row[data-tab-id]').first()
  const id = await row.getAttribute('data-tab-id')
  if (!id) throw new Error('no Today row')
  const space = await ui.frameDoc().evaluate(async () => {
    const r = await window.clu.state.get()
    if (!r.ok) throw new Error('state')
    return Object.values(r.value.windows)[0]?.activeSpaceId ?? ''
  })
  await ui.frameDoc().evaluate(([tabId, spaceId]) => window.clu.tabs.move({ nodeId: tabId as string, target: { section: 'pinned', spaceId: spaceId as string, parentId: null, index: 0 } }), [id, space])
  const pinned = nav.locator(`.clu-tab-row[data-tab-id="${id}"]`)
  await expect(pinned).toBeVisible()
  await ui.frameDoc().evaluate((tabId) => window.clu.tabs.rename({ nodeId: tabId, title: 'Testing Domain' }), id)
  await expect(pinned.locator('[data-morph]')).toHaveAttribute('aria-label', 'Testing Domain')
  await expect(pinned.locator('.clu-morph-letter')).toHaveCount('Testing Domain'.length)
  await ui.shot('g34-morph')
  await expect(pinned.locator('.clu-row-subtitle')).toHaveText('Double-click to rename')
  // After the morph the plain title is back.
  await expect(pinned.locator('[data-morph]')).toHaveCount(0)
  await expect(pinned.locator('.clu-row-title')).toHaveText('Testing Domain')
})

// Tabs ▸ Reveal Tab in Sidebar scrolls the active row back into view.
test('Reveal Tab in Sidebar scrolls the active row into view', async ({ ui }) => {
  let first = ''
  for (let i = 0; i < 24; i++) {
    const r = await ui.frameDoc().evaluate((u) => window.clu.tabs.create({ url: u, activate: false }), `https://reveal.test/${i}`)
    if (i === 0 && r.ok) first = r.value.tabId
  }
  expect(first).not.toBe('')
  await ui.frameDoc().evaluate((tabId) => window.clu.tabs.activate({ tabId }), first)
  const row = ui.frame.locator(`.clu-tab-row[data-tab-id="${first}"]`)
  await expect(row).toHaveAttribute('data-active', 'true')
  const list = ui.frame.locator('.clu-tab-list')
  await list.evaluate((el) => (el.scrollTop = 0))
  await expect(row).not.toBeInViewport()
  await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'tabs.reveal' }))
  await expect(row).toBeInViewport()
  await ui.shot('g100-reveal')
})
