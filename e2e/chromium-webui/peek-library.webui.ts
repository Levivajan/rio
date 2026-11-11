// the ui:fake harness: with the sidebar hidden, the floating sidebar shows the bottom-left pop-up of new
// files on hover and opens the Library from its Library button, as the docked one does, and holds itself open while the
// Library or a pop-up row's Rename is open. The retract timing is SidebarPeek.test.tsx's, on fake timers; the browser's
// half (the widget widening in place, the hold and the keyboard) is CluSidebarPeekOpensTheLibraryAndStaysHeld.
import { expect, harnessTest, state, WINDOW, type Harness } from './fixture'

const test = harnessTest('chromium-f-peek-library')

type Host = { cluFakeHost: { edit(path: string[], value: unknown): void; calls: { name: string; payload: unknown }[] } }
const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as Host).cluFakeHost.edit(p as string[], v), [path, value] as const)
const hostCalls = (ui: Harness, name: string) =>
  ui.page.evaluate((n) => (window as unknown as Host).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

/** Hides the sidebar and opens the floating one as the browser publishes it, at the layout's sidebar width. */
async function float(ui: Harness) {
  await ui.frameDoc().evaluate(() => window.clu.shell.toggleSidebar({}))
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
  await placePeek(ui)
  const doc = ui.page.frameLocator('iframe.passive[data-slot="3"]')
  await expect(doc.locator('[data-shell="sidebar-peek"]')).toBeVisible()
  return doc
}

/** The browser sizes the peek's widget to the sidebar rect plus the bleed, and resizes it in place when that changes. */
async function placePeek(ui: Harness) {
  const w = (await state(ui)).windows[WINDOW]
  const rect = w?.shell.layout.sidebarRect ?? { width: 214, height: 800 }
  const others = (w?.overlay.surfaces ?? []).filter((s) => s.instanceId !== 'peek-b70')
  await host(ui, ['windows', WINDOW, 'overlay', 'surfaces'], [
    ...others,
    { instanceId: 'peek-b70', surface: 'sidebar-peek', layer: 'passive', modality: 'passive', slot: 3, focus: false, openedFrom: null, rect: { x: 0, y: 0, width: rect.width + 16, height: rect.height }, props: {} }
  ])
}

test('the floating sidebar shows the pop-up of recent files on hover and opens the Library, held open while it is', async ({ ui }) => {
  await ui.page.goto('/?downloads=1')
  await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
  const doc = await float(ui)
  const peek = doc.locator('[data-shell="sidebar-peek"]')
  const button = peek.getByRole('button', { name: 'View Library' })

  // Resting on the Library button lists the newest files right on the floating sidebar.
  await button.hover()
  const recent = peek.getByRole('list', { name: 'Recent files' })
  await expect(recent).toBeVisible()
  await expect(recent.locator('.clu-library-peek-title')).toHaveCount(4)
  await ui.shot('peek-recent-files')

  // A new screenshot rises at the bottom left of the floating sidebar too (the pointer stays on it, off the button).
  await peek.locator('nav.clu-sidebar').hover({ position: { x: 100, y: 140 } })
  await expect(recent).toHaveCount(0)
  await host(ui, ['screenshotArrivals'], [{ name: 'Screenshot 2026-11-09 at 18.58.12.png', kind: 'image', at: Date.now() }])
  await expect(peek.getByRole('list', { name: 'Just added' }).getByRole('button', { name: /^Screenshot 2026-11-09 at 18\.58\.12\.png/ })).toBeVisible()
  await ui.shot('peek-just-added')

  // The Library button opens the Library in the floating sidebar, which holds itself open while it shows.
  await button.click()
  await expect.poll(() => hostCalls(ui, 'archive.open')).toHaveLength(1)
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.sidebarRect.width).toBeGreaterThanOrEqual(417)
  await placePeek(ui)
  await expect(peek.locator('.clu-sidebar-library')).toBeVisible()
  await expect(peek).toHaveAttribute('data-held', 'true')
  await expect.poll(() => hostCalls(ui, 'shell.holdSidebarPeek')).toEqual([{ held: true }])
  await ui.shot('peek-library')

  // Closed from its back arrow, the Library releases the hold.
  await peek.getByRole('button', { name: 'Back to tabs' }).click()
  await expect(peek).toHaveAttribute('data-held', 'false')
  await expect.poll(() => hostCalls(ui, 'shell.holdSidebarPeek')).toEqual([{ held: true }, { held: false }])
  // The panel narrowing from the Library's width and handing the keyboard back sends a leave with the pointer
  // still on it. The floating sidebar keeps its tabs in place past the grace and the slide; main retracts it once the
  // pointer is really off.
  await peek.evaluate((el) => el.dispatchEvent(new PointerEvent('pointerout', { bubbles: true, relatedTarget: null })))
  await peek.evaluate(() => new Promise((resolve) => setTimeout(resolve, 165 + 150 + 300)))
  await expect(peek).toHaveAttribute('data-leaving', 'false')
  await expect(peek.locator('.clu-sidebar-library')).toHaveCount(0)
  await expect(peek.locator('nav.clu-sidebar')).toBeVisible()
  expect(await hostCalls(ui, 'shell.setSidebarPeek')).toEqual([])
  await ui.shot('peek-back-pointer-stays')

  // Escape closes the Library that holds it, so a held peek always has a way out. The pointer is
  // still on it, so the floating sidebar stays.
  await button.click()
  await placePeek(ui)
  await expect(peek).toHaveAttribute('data-held', 'true')
  await ui.page.keyboard.press('Escape')
  await expect.poll(() => hostCalls(ui, 'archive.close')).toHaveLength(2)
  await expect(peek).toHaveAttribute('data-held', 'false')
  expect(await hostCalls(ui, 'shell.setSidebarPeek')).toEqual([])
})
