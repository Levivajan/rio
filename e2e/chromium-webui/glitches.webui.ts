// Placement glitches, checked at three sidebar widths: the favorites'
// unread dot on the favicon's top right corner, the pill's magnifier whole at its inset, and the toast with its
// shadow inside its view. Shots go to $CLU_SHOTS_DIR/glitches.
import { mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { expect, harnessTest, state, tab, WINDOW, type Harness } from './fixture'

const test = harnessTest('glitches')
const SHOTS = process.env.CLU_SHOTS_DIR ? join(process.env.CLU_SHOTS_DIR, 'glitches') : resolve('test-results/webui-shots', 'glitches')
const WIDTHS = [164, 228, 400] as const

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

async function setWidth(ui: Harness, width: number) {
  await ui.frameDoc().evaluate((w) => window.clu.shell.setSidebarWidth({ width: w, commit: true }), width)
  await expect(ui.frame.locator('nav.clu-sidebar')).toHaveCSS('width', `${width}px`)
}

async function shot(ui: Harness, name: string) {
  mkdirSync(SHOTS, { recursive: true })
  await ui.page.screenshot({ path: join(SHOTS, `${name}.png`) })
}

async function rect(ui: Harness, selector: string) {
  const r = await ui.frame.locator(selector).first().boundingBox()
  if (!r) throw new Error(`no box for ${selector}`)
  return r
}

test('the unread dot sits on the favicon\'s top right corner at every width, hovered or not', async ({ ui }) => {
  await host(ui, ['tabs', tab(1), 'title'], '(3) Inbox')
  const dot = '.clu-favorite-unread'
  await expect(ui.frame.locator(dot)).toHaveCount(1)
  const tile = ui.frame.locator('.clu-favorite').filter({ has: ui.frame.locator(dot) })
  const iconSel = '.clu-favorite:has(.clu-favorite-unread) .clu-favorite-icon > :not(.clu-favorite-unread)'
  for (const w of WIDTHS) {
    await setWidth(ui, w)
    for (const hovered of [false, true]) {
      if (hovered) await tile.hover()
      const icon = await rect(ui, iconSel)
      const d = await rect(ui, dot)
      expect(icon.width, `favicon at ${w}`).toBe(16)
      // Centered half a point inside the corner: 3 of its 5 points over the icon, 2 past it, at every width.
      expect(d.x + d.width / 2 - (icon.x + icon.width), `dot x at ${w}`).toBeCloseTo(-0.5, 1)
      expect(d.y + d.height / 2 - icon.y, `dot y at ${w}`).toBeCloseTo(0.5, 1)
    }
    await shot(ui, `b49-unread-${w}`)
    await ui.page.mouse.move(900, 400)
  }
})

test('the empty pill\'s magnifier is whole, 12 pt at window x 20, at every width', async ({ ui }) => {
  await host(ui, ['windows', WINDOW, 'activeTabId'], null)
  const hostBox = ui.frame.locator('[data-shell="url-host"]')
  for (const w of WIDTHS) {
    await setWidth(ui, w)
    await expect(hostBox).toHaveAttribute('data-empty', 'true')
    const glyph = await rect(ui, '.clu-url-search-icon')
    const clip = await rect(ui, '[data-shell="url-host"]')
    expect(Math.abs(glyph.x - 20), `magnifier x at ${w}`).toBeLessThanOrEqual(0.5)
    expect(glyph.width).toBe(12)
    // The host clips its content (overflow hidden), so the glyph must start inside it.
    expect(glyph.x, `magnifier inside the host at ${w}`).toBeGreaterThanOrEqual(clip.x)
    // The placeholder text keeps x 42.5.
    const textX = await hostBox.evaluate((el) => {
      const range = document.createRange()
      const text = [...el.childNodes].find((n) => n.nodeType === Node.TEXT_NODE)
      if (!text) return -1
      range.selectNodeContents(text)
      return range.getBoundingClientRect().x
    })
    expect(Math.abs(textX - 42.5), `placeholder x at ${w}`).toBeLessThanOrEqual(1)
    await shot(ui, `b50-pill-${w}`)
  }
})

test('the toast and its shadow sit inside the view the browser gives it, 8 in from each edge', async ({ ui }) => {
  const s = await state(ui)
  const surfaces = s.windows[WINDOW]?.overlay.surfaces ?? []
  await host(ui, ['windows', WINDOW, 'toasts'], [{ id: 't-b51', text: 'Copied Current URL', action: { label: 'Share', commandId: 'siteControls.share' }, until: Date.now() + 600_000 }])
  // The default card's spot (ToastRect: 952, 10 at a 1280 window): the harness window is the viewport.
  const vw = await ui.page.evaluate(() => window.innerWidth)
  await host(ui, ['windows', WINDOW, 'overlay', 'surfaces'], [...surfaces, { instanceId: 'toast-b51', surface: 'toast', layer: 'passive', modality: 'passive', focus: false, openedFrom: null, slot: 0, rect: { x: vw - 328, y: 10, width: 320, height: 60 }, props: {} }])
  const view = ui.page.frameLocator('iframe.passive[data-slot="0"]')
  const card = view.locator('.clu-toast')
  await expect(card).toBeVisible()
  await expect.poll(() => card.evaluate((el) => el.getAnimations().filter((a) => a.playState === 'running').length)).toBe(0)
  const inView = await card.evaluate((el) => {
    const r = el.getBoundingClientRect()
    return { top: r.top, right: window.innerWidth - r.right, bottom: window.innerHeight - r.bottom, shadow: getComputedStyle(el).boxShadow }
  })
  expect(inView.top).toBe(8)
  expect(inView.right).toBe(8)
  expect(inView.bottom).toBe(8)
  // "0 2px 6px": 4 above, 8 below, 6 at the sides, all inside the 8 point margin.
  expect(inView.shadow).toMatch(/0px 2px 6px/)
  for (const w of WIDTHS) {
    await setWidth(ui, w)
    await shot(ui, `b51-toast-${w}`)
  }
})
