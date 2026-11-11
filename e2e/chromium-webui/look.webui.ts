// M11 look suite on the WebUI harness (docs/chromium/m11.md section 4): every design.md section 5 state the frame and
// overlay WebUIs draw, measured with chromium/clu/test/data/look/measure.js and judged against the target values
// (src/shared/look/targets.ts) by the shared comparator. Pixels come from the screenshot of the state, which goes to
// $CLU_SHOTS_DIR/chromium-m11 with a JSON report beside it. clu_browsertests CluLook* run the same probes in Chromium.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import type { Frame } from '@playwright/test'
import { compareLook, formatLookReport, probesFor, type LookValue } from '../../src/shared/look/compare'
import { LOOK_STATES, type LookState } from '../../src/shared/look/targets'
import type { TabState } from '../../src/shared/state'
import { expect, harnessTest, shotsDir, state, tab, WINDOW, type Harness } from './fixture'
import { colorAt, readPng } from './png'

const test = harnessTest('chromium-m11')
const SHOTS = shotsDir('chromium-m11')
const MEASURE = readFileSync(resolve('chromium/clu/test/data/look/measure.js'), 'utf8')
const PROFILE = '00000000-0000-4000-8000-000200000001'
const REST_ROW = '.clu-tab-tree .clu-tab-row[data-active="false"]'
const SCALE = 2

type Raw = Record<string, number | { at: [number, number] } | null>

const overlayDoc = (ui: Harness): Frame => {
  const f = ui.page.frame({ url: /\/overlay\/index\.html\?layer=top/ })
  if (!f) throw new Error('overlay WebUI missing')
  return f
}
const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

/** Favorites 1, 2, 3 and 4 of the seed, plus copies up to `n`. */
async function favorites(ui: Harness, n: number): Promise<void> {
  const s = await state(ui)
  const seed = s.tabs[tab(1)] as TabState
  // An edit made while the harness still settles its first snapshot can be overwritten by it; repeat until it shows.
  await expect(async () => {
    const ids = [1, 2, 3, 4].map(tab)
    for (let i = 5; i <= n; i++) {
      const id = tab(100 + i)
      await host(ui, ['tabs', id], { ...seed, id, url: `https://site${i}.example.com/`, title: `Site ${i}`, pinnedUrl: `https://site${i}.example.com/` })
      ids.push(id)
    }
    await host(ui, ['favorites', PROFILE], ids.slice(0, n))
    await expect(ui.frame.locator('.clu-favorites .clu-favorite')).toHaveCount(n, { timeout: 1000 })
  }).toPass()
}

async function openCommandBar(ui: Harness, text?: string): Promise<void> {
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = ui.overlay.getByRole('combobox', { name: 'Search or enter URL' })
  await expect(input).toBeFocused()
  if (text) {
    await input.pressSequentially(text)
    await expect(ui.overlay.locator('.clu-cmd-row').nth(1)).toBeVisible()
  }
}

const rest = (ui: Harness) => ui.page.mouse.move(900, 400)

const SETUP: Partial<Record<LookState, (ui: Harness) => Promise<void>>> = {
  'page-card': rest,
  'page-card-dark': async (ui) => {
    await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
    await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
  },
  'empty-page': async (ui) => {
    await host(ui, ['windows', WINDOW, 'activeTabId'], null)
    await expect(ui.frame.locator('[data-surface="page-card"]')).toHaveAttribute('data-empty', 'true')
    // The empty page is the bare tinted card: no text, no illustration.
    await expect(ui.frame.locator('[data-surface="page-card"]')).toHaveText('')
  },
  'rows-rest': rest,
  'row-hover': (ui) => ui.frame.locator(REST_ROW).first().hover(),
  'row-active': rest,
  'row-pressed': async (ui) => {
    await ui.frame.locator(REST_ROW).first().hover()
    await ui.page.mouse.down()
  },
  'row-focus': (ui) => ui.frame.locator(REST_ROW).first().focus(),
  'row-loading': async (ui) => {
    await ui.frameDoc().evaluate((id) => window.clu.tabs.activate({ tabId: id }), tab(23))
    await expect(ui.frame.locator('[data-shell="card-loading"]')).toBeVisible()
  },
  'row-audible': async (ui) => {
    await rest(ui)
    // The speaker, and the title's shift with it, come 0.8 s after a tab starts playing.
    await expect(ui.frame.locator('.clu-tab-tree .clu-tab-row[data-audible="true"][data-muted="false"] .clu-row-audio').first()).toBeVisible()
  },
  'row-muted': rest,
  'row-long-title': async (ui) => {
    await host(ui, ['tabs', tab(20), 'title'], `A very long title ${'x'.repeat(1980)}`)
    await rest(ui)
  },
  'favorites-1': (ui) => favorites(ui, 1),
  'favorites-6': async (ui) => {
    await favorites(ui, 6)
    await rest(ui)
  },
  'favorites-12': (ui) => favorites(ui, 12),
  'favorite-hover': async (ui) => {
    await favorites(ui, 6)
    await ui.frame.locator('.clu-favorites .clu-favorite-tile').first().hover()
  },
  folders: rest,
  'toolbar-hover': (ui) => ui.frame.locator('[data-shell="sidebar-toggle"]').hover(),
  tooltip: async (ui) => {
    await ui.frame.locator('[data-shell="sidebar-toggle"]').hover()
    await expect(ui.frame.locator('.clu-tooltip')).toBeVisible()
  },
  'command-empty': (ui) => openCommandBar(ui),
  'command-typing': (ui) => openCommandBar(ui, 'e'),
  'command-selected': (ui) => openCommandBar(ui, 'e'),
  'menu-root': async (ui) => {
    await ui.frame.locator(REST_ROW).first().click({ button: 'right' })
    await expect(ui.overlay.getByRole('menu').first()).toBeVisible()
  }
}

async function measure(ui: Harness, st: LookState) {
  const readings: Record<string, LookValue> = {}
  const pixels: [string, [number, number]][] = []
  for (const doc of ['frame', 'overlay'] as const) {
    const probes = probesFor(st, doc)
    if (!probes.length) continue
    const out = (await (doc === 'frame' ? ui.frameDoc() : overlayDoc(ui)).evaluate(`${MEASURE}\n;globalThis.cluLookMeasure(${JSON.stringify(probes)})`)) as Raw
    for (const [id, v] of Object.entries(out)) {
      if (v !== null && typeof v === 'object') pixels.push([id, v.at])
      else readings[id] = v
    }
  }
  mkdirSync(SHOTS, { recursive: true })
  const png = readPng(await ui.page.screenshot({ path: join(SHOTS, `${st}.png`) }))
  for (const [id, [x, y]] of pixels) readings[id] = colorAt(png, x, y, SCALE)
  const w = (await state(ui)).windows[WINDOW]
  const sidebarWidth = w?.shell.layout.sidebarRect.width ?? 260
  // The tint: the sidebar's margin above the top row, which nothing covers.
  const tint = colorAt(png, sidebarWidth / 2, 3, SCALE) ?? [255, 255, 255]
  const ctx = { tint, panel: [248, 249, 249] as [number, number, number], sidebarWidth, windowHeight: 800 }
  const checks = compareLook(readings, ctx, [st])
  writeFileSync(join(SHOTS, `${st}.json`), `${JSON.stringify({ state: st, context: ctx, readings, checks }, null, 2)}\n`)
  return checks
}

for (const st of LOOK_STATES.filter((s) => s !== 'native')) {
  test(`look: ${st} matches the look targets`, async ({ ui }) => {
    await SETUP[st]?.(ui)
    const checks = await measure(ui, st)
    expect(checks.length, `targets for ${st}`).toBeGreaterThan(0)
    expect(checks.filter((c) => !c.pass), formatLookReport(checks)).toEqual([])
    if (st === 'row-pressed') await ui.page.mouse.up()
  })
}

test('look: hover and press timings come from the motion tokens', async ({ ui }) => {
  const row = ui.frame.locator(REST_ROW).first()
  // At rest a row's background leaves over --t-hover-out and its press settles over --t-press.
  const durations = await row.evaluate((el) => getComputedStyle(el).transitionDuration)
  expect(durations.split(',').map((d) => Math.round(parseFloat(d) * 1000))).toEqual([33, 75])
  // The command bar opens in one frame: no animation on the panel.
  await openCommandBar(ui)
  await expect(ui.overlay.locator('.clu-cmd')).toHaveCSS('animation-name', 'none')
})
