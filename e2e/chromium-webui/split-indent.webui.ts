// A split row inside a folder takes the folder's indent, 16 per level, as a tab row does: in a
// folder and in a nested folder, active and under a
// collapsed folder. The drop line beside it and a lifted row's ghost keep the same indent. The browser side is
// clu_browsertests CluMultitaskBrowserTest.CluSplitInsideAFolderTakesTheFolderIndent.
import { expect, harnessTest, state, tab, WINDOW, type Harness } from './fixture'

const test = harnessTest('split-indent')
const NESTED = '00000000-0000-4000-8000-0004000000f9'

const edit = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

async function settle(ui: Harness) {
  await expect
    .poll(() => ui.frameDoc().evaluate(() => document.getAnimations().filter((a) => a.playState === 'running' && a.effect?.getTiming().iterations !== Infinity).length))
    .toBe(0)
}

/** The seeded folder "Chromium M2" (tabs 12 and 13) and, inside it, a folder holding tabs 10 and 11. */
async function nest(ui: Harness) {
  const s = await state(ui)
  const spaceId = s.windows[WINDOW]?.activeSpaceId ?? ''
  const outer = Object.values(s.folders).find((f) => f.name === 'Chromium M2')
  if (!outer) throw new Error('no seeded folder')
  await edit(ui, ['folders', NESTED], { id: NESTED, spaceId, name: 'Nested', icon: null, collapsed: false, children: [] })
  await edit(ui, ['folders', outer.id, 'children'], [...outer.children, { kind: 'folder', id: NESTED }])
  for (const [i, n] of [10, 11].entries()) {
    const moved = await ui.frameDoc().evaluate(([id, target]) => window.clu.tabs.move({ nodeId: id, target }), [tab(n), { section: 'pinned' as const, spaceId, parentId: NESTED, index: i }] as const)
    expect(moved.ok).toBe(true)
  }
  return { outer: outer.id, spaceId }
}

async function split(ui: Harness, a: number, b: number) {
  const made = await ui.frameDoc().evaluate(([x, y]) => window.clu.split.create({ tabIds: [x, y], dir: 'row' }), [tab(a), tab(b)] as const)
  if (!made.ok) throw new Error('split.create failed')
  return made.value.splitId
}

const x = async (ui: Harness, selector: string) => {
  const box = await ui.frame.locator(selector).first().boundingBox()
  if (!box) throw new Error(`${selector} not laid out`)
  return box
}

test('a split row in a folder and in a nested folder takes the folder indent, open, collapsed and active', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const { outer } = await nest(ui)
  const atTop = await split(ui, 20, 21)
  const inFolder = await split(ui, 12, 13)
  const inNested = await split(ui, 10, 11)
  await settle(ui)
  const top = await x(ui, `[data-split-id="${atTop}"]`)
  const topIcon = await x(ui, `[data-split-id="${atTop}"] [data-split-member] :is(img, .clu-letter-tile)`)
  await expect(nav.locator(`[data-split-id="${atTop}"]`)).toHaveAttribute('aria-level', '1')
  const folder = await x(ui, `[data-folder-id="${outer}"]`)
  const nested = await x(ui, `[data-folder-id="${NESTED}"]`)
  expect(nested.x).toBe(folder.x + 16)
  for (const [id, level] of [[inFolder, 2], [inNested, 3]] as const) {
    const row = nav.locator(`[data-split-id="${id}"]`)
    await expect(row).toHaveAttribute('aria-level', String(level))
    const box = await x(ui, `[data-split-id="${id}"]`)
    // The box moves in by 16 per level and keeps the top-level row's right edge.
    expect(box.x).toBe(top.x + 16 * (level - 1))
    expect(box.x + box.width).toBe(top.x + top.width)
  }
  // The inactive split in the folder draws its first favicon 16 in from the inactive top-level split's (x 34 against 18).
  expect((await x(ui, `[data-split-id="${inFolder}"] [data-split-member] :is(img, .clu-letter-tile)`)).x).toBe(topIcon.x + 16)
  // The nested split is active; under its collapsed folder it stays, whole, at the folder's child indent.
  await expect(nav.locator(`[data-split-id="${inNested}"]`)).toHaveAttribute('data-active', 'true')
  await ui.shot('split-in-folders')
  await nav.locator(`[data-folder-id="${NESTED}"]`).click()
  await settle(ui)
  const held = nav.locator(`[data-split-id="${inNested}"]`)
  await expect(held).toHaveAttribute('aria-level', '3')
  await expect(held.locator('[data-split-member]')).toHaveCount(2)
  expect((await x(ui, `[data-split-id="${inNested}"]`)).x).toBe(top.x + 32)
  await ui.shot('split-nested-collapsed')
  await nav.locator(`[data-folder-id="${outer}"]`).click()
  await settle(ui)
  await expect(held).toHaveAttribute('aria-level', '2')
  await expect(held.locator('[data-split-member]')).toHaveCount(2)
  expect((await x(ui, `[data-split-id="${inNested}"]`)).x).toBe(top.x + 16)
  await expect(nav.locator(`[data-split-id="${inFolder}"]`)).toHaveCount(0)
  await ui.shot('split-outer-collapsed')
})

test('the drop line beside a split in a nested folder sits at its indent, and a lifted folder row keeps its place', async ({ ui }) => {
  await nest(ui)
  const inNested = await split(ui, 10, 11)
  await settle(ui)
  // A lifted child row: the ghost starts exactly over the row, the same 16 in.
  const child = await x(ui, `.clu-tab-row[data-tab-id="${tab(12)}"]`)
  await ui.page.mouse.move(child.x + 40, child.y + child.height / 2)
  await ui.page.mouse.down()
  await ui.page.mouse.move(child.x + 40, child.y + child.height / 2 + 6, { steps: 3 })
  // (within the pressed row's slight press scale; before the fix the copy sat 16 to the right)
  const ghost = await x(ui, '.clu-drag-ghost-row > *')
  expect(Math.abs(ghost.x - child.x)).toBeLessThan(1)
  expect(Math.abs(ghost.y - (child.y + 6))).toBeLessThan(1)
  await ui.page.keyboard.press('Escape')
  await ui.page.mouse.up()
  await settle(ui)

  // A Today row dragged into the lower part of the nested split row: the ring centers 11.5 into that row.
  const from = await x(ui, `.clu-tab-row[data-tab-id="${tab(22)}"]`)
  const to = await x(ui, `[data-split-id="${inNested}"]`)
  await ui.page.mouse.move(from.x + 20, from.y + from.height / 2)
  await ui.page.mouse.down()
  const endY = to.y + to.height - 4
  for (let i = 1; i <= 10; i++) await ui.page.mouse.move(to.x + 20, from.y + from.height / 2 + ((endY - (from.y + from.height / 2)) * i) / 10)
  const line = ui.frame.locator('.clu-drop-line')
  await expect(line).toBeVisible()
  await expect(line).toHaveAttribute('data-drop-line', '3')
  const lb = await x(ui, '.clu-drop-line')
  expect(lb.x).toBe(to.x + 11.5 - 3.5)
  expect(lb.x + lb.width).toBe(to.x + to.width - 3)
  await ui.shot('split-nested-drop-line')
  await ui.page.keyboard.press('Escape')
  await ui.page.mouse.up()
})
