// The UI polish pass (docs/chromium/ui-polish.md): every surface Rio draws itself, shot in light and in dark at
// 1280 x 800, 2x, into $CLU_SHOTS_DIR/ui-polish/<phase> (CLU_POLISH_PHASE, default "after"), so a before and an after
// set can be compared pair by pair.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import type { FrameLocator, Locator } from '@playwright/test'
import { expect, harnessTest, shotsDir, state, tab, WINDOW, type Harness } from './fixture'

const PHASE = process.env.CLU_POLISH_PHASE === 'before' ? 'before' : 'after'
const LABEL = `ui-polish/${PHASE}`
const test = harnessTest(LABEL)
const SHOTS = shotsDir(LABEL)
const PROFILE = '00000000-0000-4000-8000-000200000001'
const HOME = '00000000-0000-4000-8000-000300000001'
const REST_ROW = '.clu-tab-tree .clu-tab-row[data-active="false"]'
type Mode = 'light' | 'dark'

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

async function shot(ui: Harness, name: string, mode: Mode, opts: { fullPage?: boolean } = {}): Promise<void> {
  mkdirSync(SHOTS, { recursive: true })
  // Popovers and dialogs fade in; the shot waits for every animation in every document to end.
  for (const f of ui.page.frames()) {
    await f.evaluate(() => Promise.all(document.getAnimations().filter((a) => a.effect?.getTiming().iterations !== Infinity).map((a) => a.finished.catch(() => null)))).catch(() => null)
  }
  await ui.page.screenshot({ path: join(SHOTS, `${name}-${mode}.png`), fullPage: opts.fullPage ?? false })
}

async function setMode(ui: Harness, mode: Mode): Promise<void> {
  if (mode === 'light') return
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
}

async function addSurface(ui: Harness, surface: Record<string, unknown>): Promise<void> {
  const s = await state(ui)
  const list = s.windows[WINDOW]?.overlay.surfaces ?? []
  await host(ui, ['windows', WINDOW, 'overlay', 'surfaces'], [...list, { openedFrom: null, focus: false, rect: null, slot: null, ...surface }])
}
const passiveDoc = (ui: Harness, slot: number): FrameLocator => ui.page.frameLocator(`iframe.passive[data-slot="${slot}"]`)
const overlayReady = (ui: Harness) =>
  expect
    .poll(() => ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { surface: string; name: string }[] } }).cluFakeHost.calls.some((c) => c.surface === 'overlay' && c.name === 'ui.ready')))
    .toBe(true)
const visible = (l: Locator) => expect(l).toBeVisible()

async function openBar(ui: Harness, text?: string): Promise<void> {
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = ui.overlay.getByRole('combobox', { name: 'Search or enter URL' })
  await expect(input).toBeFocused()
  if (text) {
    await input.pressSequentially(text)
    await expect(ui.overlay.locator('.clu-cmd-row').nth(1)).toBeVisible()
  }
}

const SETTINGS_TABS = ['general', 'profiles', 'ai', 'links', 'shortcuts', 'icon', 'advanced'] as const

const surfaces: Record<string, (ui: Harness, mode: Mode) => Promise<void>> = {
  // The app settings window's document at the window's size (docs/chromium/parity-settings.md).
  settings: async (ui, mode) => {
    await ui.page.setViewportSize({ width: 684, height: 622 })
    await ui.page.goto('/settings/')
    await expect(ui.page.getByRole('tab', { name: 'General' })).toHaveAttribute('aria-selected', 'true')
    if (mode === 'dark') {
      await host(ui, ['app', 'appearance', 'resolved'], 'dark')
      await expect(ui.page.locator('html')).toHaveAttribute('data-theme', 'dark')
    }
    for (const id of SETTINGS_TABS) {
      await ui.page.evaluate((t) => (location.hash = t), id)
      await expect(ui.page.locator('main.clu-sw-body')).toHaveAttribute('data-pane', id)
      await ui.page.mouse.move(0, 621)
      await shot(ui, `settings-${id}`, mode)
    }
    await ui.page.evaluate(() => (location.hash = 'general'))
    await ui.page.locator('main button').first().focus()
    await shot(ui, 'settings-focus', mode)
  },
  onboarding: async (ui, mode) => {
    await ui.page.goto('/?onboarding=1')
    await setMode(ui, mode)
    const ob = ui.frame.locator('.clu-ob')
    // The fake has another browser's sidebar file, so setup opens on the switch screen first.
    await expect(ob.getByRole('button', { name: 'Bring everything over' })).toBeVisible()
    await shot(ui, 'onboarding-0-switch', mode)
    await ob.getByRole('button', { name: 'Not now' }).click()
    await expect(ob).toHaveAttribute('data-step', 'welcome')
    await shot(ui, 'onboarding-1-welcome', mode)
    await ob.getByRole('button', { name: 'Get started' }).click()
    await expect(ob).toHaveAttribute('data-step', 'look')
    await shot(ui, 'onboarding-2-look', mode)
    await ob.getByRole('button', { name: 'Continue' }).click()
    await expect(ob).toHaveAttribute('data-step', 'moves')
    await shot(ui, 'onboarding-3-moves', mode)
    await ob.getByRole('button', { name: 'Continue' }).click()
    await expect(ob).toHaveAttribute('data-step', 'default-browser')
    await shot(ui, 'onboarding-4-default-browser', mode)
    // The switch screen asked already, so there is no Import step after it.
    await ob.getByRole('button', { name: 'Skip', exact: true }).first().click()
    await expect(ob).toHaveAttribute('data-step', 'done')
    await shot(ui, 'onboarding-5-done', mode)
  },
  command: async (ui, mode) => {
    await setMode(ui, mode)
    await openBar(ui)
    await ui.page.mouse.move(1279, 799)
    await shot(ui, 'command-empty', mode)
    await ui.overlay.getByRole('combobox', { name: 'Search or enter URL' }).pressSequentially('mo')
    await expect(ui.overlay.locator('.clu-cmd-row').nth(1)).toBeVisible()
    await shot(ui, 'command-typing', mode)
  },
  menus: async (ui, mode) => {
    await setMode(ui, mode)
    await ui.frame.locator(REST_ROW).first().click({ button: 'right' })
    await visible(ui.overlay.getByRole('menu').first())
    await shot(ui, 'menu-row', mode)
    await ui.overlay.getByRole('menuitem', { name: /Move To/i }).hover()
    await expect(ui.overlay.getByRole('menu')).toHaveCount(2)
    await shot(ui, 'menu-submenu', mode)
  },
  library: async (ui, mode) => {
    await ui.page.goto('/?downloads=1')
    await setMode(ui, mode)
    await ui.frame.locator('nav.clu-sidebar').getByRole('button', { name: 'View Library' }).click()
    await ui.frame.getByRole('tab', { name: 'Archived Tabs' }).click()
    await visible(ui.frame.locator('section.clu-archive'))
    await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.sidebarRect.width).toBe(417)
    await ui.page.mouse.move(1279, 799)
    await shot(ui, 'library-archive', mode)
    await ui.frame.getByRole('tab', { name: 'Downloads' }).click()
    await shot(ui, 'library-downloads', mode)
  },
  toast: async (ui, mode) => {
    await setMode(ui, mode)
    await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'archive.clearToday' }))
    await addSurface(ui, { instanceId: 'toast-1', surface: 'toast', layer: 'passive', modality: 'passive', slot: 0, rect: { x: 480, y: 724, width: 320, height: 60 }, props: {} })
    await visible(passiveDoc(ui, 0).locator('.clu-toast'))
    await ui.page.mouse.move(1279, 799)
    await shot(ui, 'toast', mode)
  },
  'space-editor': async (ui, mode) => {
    await setMode(ui, mode)
    await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'spaces.newSpace' }))
    await visible(ui.overlay.getByRole('dialog', { name: 'Create a Space' }))
    await shot(ui, 'dialog-space-editor', mode)
  },
  confirm: async (ui, mode) => {
    await setMode(ui, mode)
    await overlayReady(ui)
    await addSurface(ui, { instanceId: 'confirm-1', surface: 'confirm', layer: 'top', modality: 'modal', focus: true, props: { title: 'Delete your “Work” Space?', body: 'Its 2 pinned tabs close and its 3 Today tabs move to Archive.', confirmLabel: 'Delete', destructive: true } })
    await visible(ui.overlay.getByRole('dialog'))
    // Each button wears its key.
    await expect(ui.overlay.getByRole('button', { name: 'Cancel' }).locator('kbd')).toHaveText('esc')
    await expect(ui.overlay.getByRole('button', { name: 'Delete' }).locator('kbd')).toHaveText('return')
    await shot(ui, 'dialog-confirm', mode)
  },
  rename: async (ui, mode) => {
    await setMode(ui, mode)
    await ui.frame.locator(REST_ROW).first().dblclick()
    await expect(ui.frame.locator('.clu-tab-tree input')).toBeFocused()
    await shot(ui, 'dialog-rename', mode)
  },
  'pinned-url': async (ui, mode) => {
    await setMode(ui, mode)
    await overlayReady(ui)
    await addSurface(ui, { instanceId: 'pin-1', surface: 'tabs.pinnedUrl', layer: 'top', modality: 'modal', focus: true, props: { tabId: tab(1), url: 'https://mail.example.com/' } })
    await visible(ui.overlay.getByRole('dialog'))
    await shot(ui, 'dialog-pinned-url', mode)
  },
  'clear-data': async (ui, mode) => {
    await setMode(ui, mode)
    await overlayReady(ui)
    await ui.frameDoc().evaluate((profileId) => window.clu.browsingData.open({ profileId }), PROFILE)
    await visible(ui.overlay.getByRole('dialog', { name: 'Clear browsing data' }))
    await shot(ui, 'dialog-clear-data', mode)
  },
  'theme-picker': async (ui, mode) => {
    await setMode(ui, mode)
    await overlayReady(ui)
    await ui.frameDoc().evaluate((spaceId) => window.clu.theme.openPicker({ spaceId, anchor: { x: 12, y: 44, width: 236, height: 28 } }), HOME)
    await visible(ui.overlay.getByRole('dialog'))
    await shot(ui, 'theme-picker', mode)
  },
  'site-controls': async (ui, mode) => {
    await ui.page.goto('/?extensions=1')
    await setMode(ui, mode)
    await ui.page.mouse.move(130, 25)
    await ui.frame.locator('[data-site-controls-button]').click()
    await visible(ui.overlay.locator('.clu-site-controls'))
    await shot(ui, 'site-controls', mode)
  },
  'extensions-menu': async (ui, mode) => {
    await ui.page.goto('/?extensions=1')
    await setMode(ui, mode)
    // Extensions live in Site controls now; the menu opens where its section's button sits.
    await ui.frameDoc().evaluate(() => window.clu.extensions.openMenu({ anchor: { x: 200, y: 60, width: 28, height: 28 } }))
    await visible(ui.overlay.locator('.clu-extensions-menu'))
    await shot(ui, 'extensions-menu', mode)
  },
  downloads: async (ui, mode) => {
    await ui.page.goto('/?downloads=1')
    await setMode(ui, mode)
    await overlayReady(ui)
    await addSurface(ui, { instanceId: 'dl-1', surface: 'downloads', layer: 'top', modality: 'light-dismiss', focus: true, props: { left: 8, bottom: 44 } })
    await visible(ui.overlay.getByRole('dialog', { name: 'Downloads' }))
    await shot(ui, 'downloads', mode)
  },
  'hover-card': async (ui, mode) => {
    await setMode(ui, mode)
    const row = ui.frame.locator(`[data-tab-id="${tab(20)}"]`)
    const box = await row.boundingBox()
    if (!box) throw new Error('row not laid out')
    await ui.page.mouse.move(box.x + 60, box.y + box.height / 2)
    await ui.page.mouse.move(box.x + 90, box.y + box.height / 2 + 2)
    const card = passiveDoc(ui, 2).locator('[role="tooltip"]')
    await expect(card).toHaveText('Morning headlines', { timeout: 4000 })
    // Dark: a dark accent pill with white text.
    if (mode === 'dark') await expect(card).toHaveCSS('color', 'rgb(255, 255, 255)')
    await shot(ui, 'hover-card', mode)
  },
  peek: async (ui, mode) => {
    await setMode(ui, mode)
    const s = await state(ui)
    const page = s.windows[WINDOW]?.shell.layout.pageRect ?? { x: 270, y: 10, width: 1000, height: 780 }
    const width = Math.round(page.width * 0.81)
    const cardRect = { x: Math.round(page.x + (page.width - width) / 2), y: 12, width, height: 776 }
    await host(ui, ['windows', WINDOW, 'peek'], { seq: 1, url: 'https://peek.example.com/b', host: 'peek.example.com', title: 'Peek B', favicon: null, canGoBack: false, originRect: { x: 400, y: 120, width: 120, height: 20 }, cardRect })
    await addSurface(ui, { instanceId: 'peek-1', surface: 'peek', layer: 'under-peek', modality: 'passive', props: {} })
    await visible(ui.page.frameLocator('#under-peek').locator('[data-peek-rail]'))
    await ui.page.mouse.move(1279, 799)
    await shot(ui, 'peek', mode)
  },
  mini: async (ui, mode) => {
    await setMode(ui, mode)
    // The empty Mini window: the prompt is the whole 490 x 329 window.
    await ui.page.setViewportSize({ width: 490, height: 329 })
    await ui.frameDoc().evaluate(() => window.clu.command.open({ mode: 'mini' }))
    const bar = ui.overlay.locator('.clu-cmd')
    await visible(bar)
    const box = await bar.evaluate((el) => {
      const r = el.getBoundingClientRect()
      return { x: r.x, y: r.y, width: r.width, height: r.height, vw: window.innerWidth, vh: window.innerHeight }
    })
    expect(box).toEqual({ x: 0, y: 0, width: box.vw, height: box.vh, vw: box.vw, vh: box.vh })
    await ui.page.mouse.move(489, 328)
    await shot(ui, 'mini-prompt', mode)
  }
}

for (const [name, run] of Object.entries(surfaces)) {
  for (const mode of ['light', 'dark'] as const) {
    test(`polish shots: ${name} (${mode})`, async ({ ui }) => {
      await run(ui, mode)
    })
  }
}

/** A computed rgb() or rgba() color composited over `under`. */
function flatten(color: string, under: [number, number, number]): [number, number, number] {
  const [r = 0, g = 0, b = 0, a = 1] = (color.match(/[\d.]+/g) ?? []).map(Number)
  return [r * a + under[0] * (1 - a), g * a + under[1] * (1 - a), b * a + under[2] * (1 - a)]
}
/** Chroma (0..1): how far a color is from gray. A navy like #151c30 is 0.11. */
function chroma([r, g, b]: [number, number, number]): number {
  return (Math.max(r, g, b) - Math.min(r, g, b)) / 255
}

for (const mode of ['light', 'dark'] as const) {
  test(`settings controls (${mode}): the window's 24 pt pop-up and push buttons, no border, no shadow, one color family with the card and window`, async ({ ui }) => {
    await ui.page.goto('/settings/#profiles')
    const engine = ui.page.getByRole('combobox', { name: 'Search engine' })
    await expect(engine).toBeVisible()
    if (mode === 'dark') {
      await host(ui, ['app', 'appearance', 'resolved'], 'dark')
      await expect(ui.page.locator('html')).toHaveAttribute('data-theme', 'dark')
    }
    const look = await engine.evaluate((select) => {
      const css = (el: Element) => getComputedStyle(el)
      const card = select.closest('.clu-group') as Element
      return {
        height: select.getBoundingClientRect().height,
        border: css(select).borderTopWidth,
        shadow: css(select).boxShadow,
        radius: css(select).borderTopLeftRadius,
        control: css(select).backgroundColor,
        card: css(card).backgroundColor,
        window: css(document.body).backgroundColor,
        fontSize: css(select).fontSize
      }
    })
    expect(look.height).toBe(24)
    expect(look.border).toBe('0px')
    expect(look.shadow).toBe('none')
    expect(look.radius).toBe('6px')
    expect(look.fontSize).toBe('13px')
    const win = flatten(look.window, [0, 0, 0])
    const card = flatten(look.card, win)
    const control = flatten(look.control, card)
    for (const [name, c] of [['window', win], ['card', card], ['control', control]] as const) expect(chroma(c), name).toBeLessThan(0.04)
  })
}

test('dialogs (dark): the clear data dialog, its select and its buttons share the neutral family and the 28 pt height', async ({ ui }) => {
  await setMode(ui, 'dark')
  await overlayReady(ui)
  await ui.frameDoc().evaluate((profileId) => window.clu.browsingData.open({ profileId }), PROFILE)
  const dialog = ui.overlay.getByRole('dialog', { name: 'Clear browsing data' })
  await expect(dialog).toBeVisible()
  const look = await dialog.evaluate((d) => {
    const css = (el: Element) => getComputedStyle(el)
    const select = d.querySelector('select') as Element
    return {
      dialog: css(d).backgroundColor,
      control: css(select).backgroundColor,
      heights: [select, ...d.querySelectorAll('.clu-btn')].map((el) => el.getBoundingClientRect().height)
    }
  })
  expect(new Set(look.heights)).toEqual(new Set([28]))
  const dialogColor = flatten(look.dialog, [0, 0, 0])
  expect(chroma(dialogColor)).toBeLessThan(0.04)
  expect(chroma(flatten(look.control, dialogColor))).toBeLessThan(0.04)
})
