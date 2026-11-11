// M7 flows in the frame and overlay WebUIs over the FakeBridge (docs/chromium/m7.md): the action row with pinned
// extensions inside Site controls (the sidebar has none) and its menu (pin, unpin, open), Site controls with the blocked count and the switch, the zap banner,
// and an accessibility audit of the extensions menu. The browser side is clu_browsertests CluExtensions*,
// CluBlocker* and CluTweaks*. Screenshots go to $CLU_SHOTS_DIR/chromium-m7-ui.
import { readFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import type { Frame, Page } from '@playwright/test'
import { expect, harnessTest, state, WINDOW } from './fixture'

const test = harnessTest('chromium-m7-ui')
const NOTES = 'a'.repeat(32)
const PANEL = 'c'.repeat(32)

type HostCall = { name: string; payload: unknown }
const hostCalls = (page: Page, name: string) =>
  page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: HostCall[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

// Popovers fade in over 120 ms; a screenshot waits for the fade to end.
const settled = (locator: import('@playwright/test').Locator) =>
  locator.evaluate((el) => Promise.all(el.getAnimations().map((a) => a.finished)).then(() => true))

// The harness with three extensions in the window's profile (fake-site.ts).
async function withExtensions(page: Page, frame: import('@playwright/test').FrameLocator) {
  await page.goto('/?extensions=1')
  await expect(frame.locator('[data-site-controls-button]')).toBeAttached()
}

// The action row lives in Site controls, opened from the pill's sliders button.
async function openActionRow(ui: { page: Page; frame: import('@playwright/test').FrameLocator; overlay: import('@playwright/test').FrameLocator }) {
  await ui.page.mouse.move(130, 25)
  await ui.frame.locator('[data-site-controls-button]').click()
  const row = ui.overlay.locator('.clu-site-controls [data-action-row]')
  await expect(row).toBeVisible()
  return row
}

// The extensions menu opens from "…" ▸ Manage Extensions….
async function openExtensionsMenu(ui: { overlay: import('@playwright/test').FrameLocator }) {
  const popover = ui.overlay.locator('.clu-site-controls')
  await popover.getByRole('button', { name: 'More site controls' }).click()
  await popover.getByRole('menuitem', { name: 'Manage Extensions…' }).click()
}

// "…" ▸ All Site Settings… opens Rio's own controls under the panel.
async function openAllSettings(popover: import('@playwright/test').Locator) {
  await popover.getByRole('button', { name: 'More site controls' }).click()
  await popover.getByRole('menuitem', { name: 'All Site Settings…' }).click()
}

test('the action row shows pinned actions; the menu lists, pins and opens every extension', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await expect(ui.frame.locator('[data-action-row]')).toHaveCount(0)
  let row = await openActionRow(ui)
  // The panel shows every extension with an action, pinned or not.
  await expect(row.locator('[data-action]')).toHaveCount(3)
  await expect(row.locator(`[data-action="${NOTES}"]`)).toHaveAttribute('aria-label', 'Notes, 3')
  await ui.shot('action-row')

  await row.locator(`[data-action="${NOTES}"]`).click()
  const ran = (await hostCalls(ui.page, 'extensions.runAction')) as { id: string; anchor: { width: number } }[]
  expect(ran.at(-1)?.id).toBe(NOTES)
  expect(ran.at(-1)?.anchor.width).toBeGreaterThan(0)

  if (!(await row.isVisible())) await openActionRow(ui)
  await openExtensionsMenu(ui)
  const menu = ui.overlay.locator('.clu-extensions-menu')
  await expect(menu).toBeVisible()
  await expect(menu.locator('[data-extension]')).toHaveCount(3)
  await settled(menu)
  await ui.shot('extensions-menu')

  await menu.getByRole('button', { name: 'Pin Research panel' }).click()
  await expect(menu.getByRole('button', { name: 'Unpin Research panel' })).toHaveAttribute('aria-pressed', 'true')
  await ui.shot('extensions-menu-pinned')

  // Esc closes the menu; the new pin shows in the row.
  await ui.page.keyboard.press('Escape')
  await expect(menu).toHaveCount(0)
  row = await openActionRow(ui)
  await expect(row.locator(`[data-action="${PANEL}"]`)).toBeVisible()

  await openExtensionsMenu(ui)
  await menu.getByText('Research panel').click()
  await expect(menu).toHaveCount(0)
  expect(((await hostCalls(ui.page, 'extensions.runAction')) as { id: string }[]).at(-1)?.id).toBe(PANEL)
})

test('Site controls shows the blocked count and turns blocking off for the site', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  const button = ui.frame.locator('[data-site-controls-button]')
  await expect(button.locator('[data-blocked-count]')).toHaveText('12')
  // The pill's buttons show while the pointer is over the sidebar.
  await ui.page.mouse.move(130, 25)
  await button.click()
  const popover = ui.overlay.locator('.clu-site-controls')
  await expect(popover).toBeVisible()
  await openAllSettings(popover)
  await expect(popover.locator('[data-blocking-status]')).toHaveText('12 blocked on this page')
  await settled(popover)
  await ui.shot('site-controls')
  await popover.getByRole('switch', { name: 'Block ads and trackers' }).click()
  await expect(popover.locator('[data-blocking-status]')).toHaveText(/^Off for /)
  await expect(button.locator('[data-blocked-count]')).toHaveCount(0)
  await ui.shot('site-controls-off')
  await ui.page.keyboard.press('Escape')
  await expect(popover).toHaveCount(0)
})

test('the Developer Mode row turns on the strip above the card, whose tools run on the page', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await ui.page.mouse.move(130, 25)
  await ui.frame.locator('[data-site-controls-button]').click()
  const popover = ui.overlay.locator('.clu-site-controls')
  const row = popover.locator('[data-dev-mode]')
  await expect(row).toHaveText('Developer ModeOff')
  await row.click()
  await expect(row).toHaveText('Developer ModeOn')
  await settled(popover)
  await ui.shot('dev-mode-row')
  await ui.page.keyboard.press('Escape')
  await expect(popover).toHaveCount(0)
  const strip = ui.frame.locator('[data-shell="dev-strip"]')
  await expect(strip).toBeVisible()
  const before = await state(ui)
  await expect(strip.locator('[data-dev-url]')).toHaveText(before.tabs[before.windows[WINDOW]?.activeTabId ?? '']?.url ?? '')
  // The card sits under the 38 px band.
  expect(before.windows[WINDOW]?.shell.layout.pageRect.y).toBe(48)
  await ui.shot('dev-strip')
  await strip.getByRole('button', { name: 'Console' }).click()
  await strip.getByRole('button', { name: 'Inspect element' }).click()
  expect(await hostCalls(ui.page, 'siteControls.devTool')).toEqual([{ tool: 'console' }, { tool: 'inspect' }])
  await ui.page.mouse.move(130, 25)
  await ui.frame.locator('[data-site-controls-button]').click()
  await popover.locator('[data-dev-mode]').click()
  await expect(strip).toHaveCount(0)
  expect((await state(ui)).windows[WINDOW]?.shell.layout.pageRect.y).toBe(10)
})

test('Site controls lists the site\'s permissions, and a Block sticks when it opens again', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  const button = ui.frame.locator('[data-site-controls-button]')
  await ui.page.mouse.move(130, 25)
  await button.click()
  const popover = ui.overlay.locator('.clu-site-controls')
  const permissions = popover.locator('.clu-site-permissions')
  await openAllSettings(popover)
  await expect(permissions).toBeVisible()
  for (const p of ['camera', 'microphone', 'location', 'notifications']) {
    await expect(permissions.locator(`[data-permission="${p}"]`)).toHaveAttribute('data-decision', 'ask')
  }
  await permissions.getByLabel('Camera').selectOption('block')
  await expect(permissions.locator('[data-permission="camera"]')).toHaveAttribute('data-decision', 'block')
  await expect(permissions.getByRole('button', { name: 'Reload to apply' })).toBeVisible()
  await settled(popover)
  await ui.shot('site-controls-permissions')
  await ui.page.keyboard.press('Escape')
  await expect(popover).toHaveCount(0)
  await ui.page.mouse.move(130, 25)
  await button.click()
  await openAllSettings(popover)
  await expect(permissions.locator('[data-permission="camera"]')).toHaveAttribute('data-decision', 'block')
})

test('every control fits the panel and the Site tweaks switch turns tweaks on', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await openActionRow(ui)
  const popover = ui.overlay.locator('.clu-site-controls')
  await openAllSettings(popover)
  const more = popover.locator('[data-more]')
  await expect(more.getByRole('button', { name: 'Zap elements' })).toBeVisible()
  await settled(popover)
  const fit = await popover.evaluate((el) => {
    const panel = el.getBoundingClientRect()
    const scroller = el.querySelector('[data-more]') as HTMLElement
    const out = [...el.querySelectorAll('[data-more] button, [data-more] select, [data-more] [role="switch"]')]
      .map((c) => ({ c, r: c.getBoundingClientRect() }))
      .filter(({ r }) => r.width > 0 && (r.left < panel.left || r.right > panel.right - 8))
      .map(({ c }) => c.textContent || c.getAttribute('aria-label'))
    return { sideways: scroller.scrollWidth - scroller.clientWidth, out }
  })
  expect(fit).toEqual({ sideways: 0, out: [] })
  // All Site Settings takes the panel's place, never a second panel over it, and the panel is opaque: the overlay
  // widget has nothing behind it to blur, so the page must not show through.
  await expect(popover.getByRole('toolbar', { name: 'Page actions' })).toHaveCount(0)
  await expect(popover.getByRole('button', { name: 'More site controls' })).toHaveCount(0)
  const see = await popover.evaluate((el) => {
    const c = getComputedStyle(el).backgroundColor
    // color-mix computes to color(srgb r g b / a) or rgba(r, g, b, a) when it keeps any transparency.
    return { c, alpha: /\/\s*(0?\.\d+|0)\s*\)$/.test(c) || /^rgba\(.*,\s*(0?\.\d+|0)\)$/.test(c) }
  })
  expect(see.alpha, see.c).toBe(false)
  await ui.shot('site-controls-all-settings')

  const sw = more.getByRole('switch', { name: 'Site tweaks on' })
  await expect(sw).toHaveAttribute('aria-checked', 'false')
  await sw.click()
  await expect(sw).toHaveAttribute('aria-checked', 'true')
  await expect(popover.locator('.clu-tweak-editor')).toHaveCount(0)
  expect(await hostCalls(ui.page, 'tweaks.openEditor')).toEqual([])
  await sw.click()
  await expect(sw).toHaveAttribute('aria-checked', 'false')
})

test('All Site Settings stays inside a short window and scrolls to its zoom row', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await ui.page.setViewportSize({ width: 1000, height: 560 })
  await openActionRow(ui)
  const popover = ui.overlay.locator('.clu-site-controls')
  await openAllSettings(popover)
  const more = popover.locator('[data-more]')
  await expect(more.getByRole('button', { name: 'Reset', exact: true })).toBeAttached()
  await settled(popover)
  await more.evaluate((el) => el.scrollTo({ top: el.scrollHeight }))
  const fit = await popover.evaluate((el) => {
    const panel = el.getBoundingClientRect()
    const scroller = el.querySelector('[data-more]') as HTMLElement
    const reset = [...scroller.querySelectorAll('button')].find((b) => b.textContent?.trim() === 'Reset')?.getBoundingClientRect()
    // Every child of the panel ends inside it, and the panel inside the window.
    const spill = [...el.children].filter((c) => c.getBoundingClientRect().bottom > panel.bottom + 0.5).map((c) => c.className)
    return { panelIn: panel.bottom <= innerHeight, spill, scrolls: scroller.scrollHeight > scroller.clientHeight, resetIn: !!reset && reset.bottom <= scroller.getBoundingClientRect().bottom + 0.5 }
  })
  expect(fit).toEqual({ panelIn: true, spill: [], scrolls: true, resetIn: true })
  await ui.shot('site-controls-all-settings-short')
  await more.getByRole('button', { name: 'Zoom in' }).click()
  await more.getByRole('button', { name: 'Reset', exact: true }).click()
  expect((await hostCalls(ui.page, 'zoom.step')).map((c) => (c as { dir: number }).dir)).toEqual([1, 0])
})

test('"…" holds the site menu without its page enhancement items, and Clear Cache clears the site', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await openActionRow(ui)
  const popover = ui.overlay.locator('.clu-site-controls')
  await popover.getByRole('button', { name: 'More site controls' }).click()
  const menu = popover.getByRole('menu', { name: 'More site controls' })
  await expect(menu.getByRole('menuitem')).toHaveText(['Clear Cache', 'Clear Cookies', 'Manage Extensions…', 'Add Extension…', 'All Site Settings…'])
  await expect(menu.getByRole('separator')).toHaveCount(2)
  await settled(menu)
  await ui.shot('site-controls-more-menu')
  await menu.getByRole('menuitem', { name: 'Clear Cache' }).click()
  await expect(popover).toHaveCount(0)
  expect(await hostCalls(ui.page, 'commands.run')).toContainEqual({ id: 'page.clearCacheAndRefresh' })
})

test('the Capture text tile closes the panel and starts the capture', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await openActionRow(ui)
  const popover = ui.overlay.locator('.clu-site-controls')
  await popover.getByRole('toolbar', { name: 'Page actions' }).getByRole('button', { name: 'Capture text' }).click()
  await expect(popover).toHaveCount(0)
  expect(await hostCalls(ui.page, 'siteControls.captureText')).toHaveLength(1)
})

test('the Share tile opens the share picker at the tile and closes the panel', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await openActionRow(ui)
  const popover = ui.overlay.locator('.clu-site-controls')
  const top = popover.getByRole('toolbar', { name: 'Page actions' })
  await expect(top.getByRole('button')).toHaveCount(4)
  await settled(popover)
  await ui.shot('site-controls-tiles')
  const tile = top.getByRole('button', { name: 'Share' })
  const box = await tile.boundingBox()
  await tile.click()
  await expect(popover).toHaveCount(0)
  const [call] = (await hostCalls(ui.page, 'siteControls.share')) as { anchor: { x: number; y: number; width: number; height: number } }[]
  expect(call?.anchor.width).toBe(48)
  expect(call?.anchor.height).toBe(32)
  expect(Math.abs((call?.anchor.y ?? 0) - (box?.y ?? -99))).toBeLessThan(2)
  expect(await hostCalls(ui.page, 'commands.run')).not.toContainEqual({ id: 'edit.copyUrl' })
})

test('the panel has the design chrome: 21 pt corners, a light shadow, a 27 x 10 arrow, 48 pt tiles 8 apart', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  const row = await openActionRow(ui)
  const popover = ui.overlay.locator('.clu-site-controls')
  await settled(popover)
  const look = await popover.evaluate((el) => {
    const s = getComputedStyle(el)
    const arrow = el.ownerDocument.defaultView?.getComputedStyle(el, '::before')
    return { radius: s.borderRadius, shadow: s.boxShadow, arrowW: arrow?.width, arrowT: arrow?.transform }
  })
  expect(look.radius).toBe('21px')
  expect(look.shadow).not.toContain('24px')
  expect(look.arrowW).toBe('19px')
  const tiles = await row.locator('.clu-site-controls-tile').evaluateAll((els) => els.map((e) => e.getBoundingClientRect()).map((r) => ({ x: r.x, w: r.width, h: r.height })))
  expect(tiles[0]).toMatchObject({ w: 48, h: 32 })
  expect((tiles[1]?.x ?? 0) - (tiles[0]?.x ?? 0)).toBe(56)
  const icon = row.locator('img').first()
  if (await icon.count()) expect(await icon.evaluate((i) => i.getBoundingClientRect().width)).toBe(14)
  await ui.shot('site-controls-chrome')
})

test('a click outside fades the panel out before it closes', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  await openActionRow(ui)
  const popover = ui.overlay.locator('.clu-site-controls')
  await settled(popover)
  await ui.overlay.locator('.clu-overlay-root').click({ position: { x: 4, y: 400 } })
  await expect(popover).toHaveAttribute('data-closing', 'true')
  await expect(popover).toHaveCount(0)
})

test('zap mode shows its banner in the sidebar', async ({ ui }) => {
  await withExtensions(ui.page, ui.frame)
  const s = await state(ui)
  const tabId = s.windows[WINDOW]?.activeTabId ?? ''
  await ui.frameDoc().evaluate((id) => window.clu.tweaks.startZap({ tabId: id }), tabId)
  await expect(ui.frame.locator('[data-zap-banner]')).toBeVisible()
  await ui.shot('zap-banner')
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

  test('the action row and the extensions menu have no WCAG 2.1 AA violations', async ({ ui }) => {
    await withExtensions(ui.page, ui.frame)
    await openActionRow(ui)
    const overlayDoc = ui.page.frame({ url: /\/overlay\/index\.html\?layer=top/ })
    if (!overlayDoc) throw new Error('overlay WebUI missing')
    expect(await audit(overlayDoc, '.clu-site-controls [data-action-row]')).toEqual([])
    await openExtensionsMenu(ui)
    await expect(ui.overlay.locator('.clu-extensions-menu')).toBeVisible()
    await settled(ui.overlay.locator('.clu-extensions-menu'))
    expect(await audit(overlayDoc, '.clu-extensions-menu')).toEqual([])
  })
})
