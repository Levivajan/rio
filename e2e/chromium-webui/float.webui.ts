// Floating and split surfaces: the command bar, the
// Ctrl+Tab switcher, hover cards, tooltips, menus, the Library panel, toasts, Peek, split view, the Mini window prompt
// and the hidden sidebar's peek, shot on the FakeBridge at 1280 x 800, 2x, into shots/parity/look-float/ in the
// repo, and their sizes pinned.
import { mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import type { FrameLocator } from '@playwright/test'
import { expect, harnessTest, state, tab, WINDOW, type Harness } from './fixture'

const test = harnessTest('parity-look-float')
const SHOTS = resolve('shots/parity/look-float')
const REST_ROW = '.clu-tab-tree .clu-tab-row[data-active="false"]'

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

async function shoot(ui: Harness, name: string): Promise<void> {
  mkdirSync(SHOTS, { recursive: true })
  await ui.page.mouse.move(1279, 799)
  await ui.page.screenshot({ path: join(SHOTS, `${name}.png`) })
}
async function shootHere(ui: Harness, name: string): Promise<void> {
  mkdirSync(SHOTS, { recursive: true })
  await ui.page.screenshot({ path: join(SHOTS, `${name}.png`) })
}

const dark = async (ui: Harness) => {
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
}

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

/** Adds a surface as the browser publishes it. */
async function addSurface(ui: Harness, surface: Record<string, unknown>): Promise<void> {
  const s = await state(ui)
  const list = s.windows[WINDOW]?.overlay.surfaces ?? []
  await host(ui, ['windows', WINDOW, 'overlay', 'surfaces'], [...list, { openedFrom: null, focus: false, rect: null, slot: null, ...surface }])
}
const passiveDoc = (ui: Harness, slot: number): FrameLocator => ui.page.frameLocator(`iframe.passive[data-slot="${slot}"]`)

const states: Record<string, (ui: Harness) => Promise<void>> = {
  'command-empty': async (ui) => {
    await openBar(ui)
    await shoot(ui, 'command-empty')
  },
  'command-typing': async (ui) => {
    await openBar(ui, 'mo')
    await shoot(ui, 'command-typing')
    // The selection is the space's main color as a muted tone (--accent-select); no group heading shows; Switch
    // to Tab ends in a 24 pt arrow pill 23
    // from the panel's edge.
    const style = await ui.page.frame({ url: /layer=top/ })?.evaluate(() => {
      const css = (sel: string) => getComputedStyle(document.querySelector(sel) as Element)
      const panel = (document.querySelector('.clu-cmd') as HTMLElement).getBoundingClientRect()
      const arrow = (document.querySelector('.clu-cmd-arrow') as HTMLElement).getBoundingClientRect()
      return {
        selected: css('.clu-cmd-row[aria-selected="true"]').backgroundColor,
        accent: getComputedStyle(document.documentElement).getPropertyValue('--accent-select').trim(),
        tabUrls: document.querySelectorAll('.clu-cmd-row[data-kind="tab"] .clu-cmd-url').length,
        heading: (document.querySelector('.clu-cmd-group-label') as HTMLElement | null)?.getBoundingClientRect().height ?? 0,
        follows: (document.querySelector('.clu-cmd-field-icon') as HTMLElement).dataset.follows ?? null,
        selectedKind: (document.querySelector('.clu-cmd-row[aria-selected="true"]') as HTMLElement).dataset.kind ?? null,
        arrow: [arrow.width, Math.round(panel.right - arrow.right)]
      }
    })
    expect(style?.selected).toBe(await ui.page.evaluate((hex) => { const d = document.createElement('div'); d.style.color = hex; document.body.append(d); const c = getComputedStyle(d).color; d.remove(); return c }, style?.accent ?? ''))
    // Tab rows show the title alone; the field's icon follows the selected row.
    expect(style?.tabUrls).toBe(0)
    expect(style?.heading).toBeLessThanOrEqual(1)
    expect(style?.follows).toBe(style?.selectedKind)
    expect(style?.arrow).toEqual([24, 23])
  },
  'command-typing-dark': async (ui) => {
    await dark(ui)
    await openBar(ui, 'mo')
    await shoot(ui, 'command-typing-dark')
  },
  'mini-prompt': async (ui) => {
    await ui.frameDoc().evaluate(() => window.clu.command.open({ mode: 'mini' }))
    await expect(ui.overlay.locator('.clu-cmd')).toBeVisible()
    await shoot(ui, 'mini-prompt')
  },
  'context-menu': async (ui) => {
    await ui.frame.locator(REST_ROW).first().click({ button: 'right' })
    await expect(ui.overlay.getByRole('menu').first()).toBeVisible()
    await shootHere(ui, 'context-menu')
    // Nothing is highlighted until the pointer or a key reaches an item; the right-clicked row keeps its hover fill.
    await expect(ui.overlay.locator('.clu-menu-item:focus')).toHaveCount(0)
    await expect(ui.frame.locator('.clu-tab-row[data-menu-open]')).toHaveCount(1)
  },
  'context-submenu': async (ui) => {
    await ui.frame.locator(REST_ROW).first().click({ button: 'right' })
    const item = ui.overlay.getByRole('menuitem', { name: /Move To/i })
    await item.hover()
    await expect(ui.overlay.getByRole('menu')).toHaveCount(2)
    await shootHere(ui, 'context-submenu')
  },
  'context-menu-dark': async (ui) => {
    await dark(ui)
    await ui.frame.locator(REST_ROW).first().click({ button: 'right' })
    await expect(ui.overlay.getByRole('menu').first()).toBeVisible()
    await shootHere(ui, 'context-menu-dark')
  },
  tooltip: async (ui) => {
    await ui.frame.locator('[data-shell="sidebar-toggle"]').hover()
    await expect(ui.frame.locator('.clu-tooltip')).toBeVisible()
    await shootHere(ui, 'tooltip')
  },
  'hover-card': async (ui) => {
    const row = ui.frame.locator(`[data-tab-id="${tab(20)}"]`)
    const box = await row.boundingBox()
    if (!box) throw new Error('row not laid out')
    await ui.page.mouse.move(box.x + 60, box.y + box.height / 2)
    await ui.page.mouse.move(box.x + 90, box.y + box.height / 2 + 2)
    const pill = passiveDoc(ui, 2).locator('[role="tooltip"]')
    await expect(pill).toHaveText('Morning headlines', { timeout: 4000 })
    await shootHere(ui, 'hover-card')
  },
  toast: async (ui) => {
    await ui.frameDoc().evaluate(() => window.clu.commands.run({ id: 'archive.clearToday' }))
    await addSurface(ui, { instanceId: 'toast-1', surface: 'toast', layer: 'passive', modality: 'passive', slot: 0, rect: { x: 480, y: 724, width: 320, height: 60 }, props: {} })
    await expect(passiveDoc(ui, 0).locator('.clu-toast')).toBeVisible()
    await shoot(ui, 'toast')
  },
  switcher: async (ui) => {
    const ids = [20, 21, 22, 23, 24]
    const s = await state(ui)
    const cards = ids.map((n) => ({ tabId: tab(n), title: s.tabs[tab(n)]?.title ?? '', host: new URL(s.tabs[tab(n)]?.url ?? 'https://x.test/').host, favicon: null, thumb: null }))
    await addSurface(ui, { instanceId: 'switch-1', surface: 'tab-switcher', layer: 'passive', modality: 'passive', slot: 1, rect: { x: 180, y: 315, width: 920, height: 170 }, props: { cards, selected: 1 } })
    await expect(passiveDoc(ui, 1).locator('.clu-switcher')).toBeVisible()
    await shoot(ui, 'switcher')
  },
  library: async (ui) => {
    await ui.frame.locator('nav.clu-sidebar').getByRole('button', { name: 'View Library' }).click()
    await expect(ui.frame.locator('section.clu-archive')).toBeVisible()
    await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.sidebarRect.width).toBe(417)
    await shoot(ui, 'library-archive')
    await ui.frame.getByRole('tab', { name: 'Downloads' }).click()
    await shoot(ui, 'library-downloads')
  },
  peek: async (ui) => {
    const s = await state(ui)
    const page = s.windows[WINDOW]?.shell.layout.pageRect ?? { x: 270, y: 10, width: 1000, height: 780 }
    const width = Math.round(page.width * 0.81)
    const cardRect = { x: Math.round(page.x + (page.width - width) / 2), y: 12, width, height: 776 }
    await host(ui, ['windows', WINDOW, 'peek'], { seq: 1, url: 'https://peek.example.com/b', host: 'peek.example.com', title: 'Peek B', favicon: null, canGoBack: false, originRect: { x: 400, y: 120, width: 120, height: 20 }, cardRect })
    await addSurface(ui, { instanceId: 'peek-1', surface: 'peek', layer: 'under-peek', modality: 'passive', props: {} })
    await expect(ui.page.frameLocator('#under-peek').locator('[data-peek-rail]')).toBeVisible()
    await expect(ui.page.frameLocator('#under-peek').locator('[data-peek-rail] button')).toHaveCount(3)
    await expect(ui.page.frameLocator('#under-peek').locator('[data-peek-wash]')).toBeVisible()
    // The page card behind shrinks to 0.983 around its center: the wash sits at the shrunk rect and the window's own
    // background covers the ring around it.
    const wash = await ui.page.frameLocator('#under-peek').locator('[data-peek-wash]').boundingBox()
    expect(wash?.width).toBeCloseTo(page.width * 0.983, 0)
    expect((wash?.x ?? 0) + (wash?.width ?? 0) / 2).toBeCloseTo(page.x + page.width / 2, 0)
    const ring = ui.page.frameLocator('#under-peek').locator('[data-peek-ring]')
    await expect(ring.locator('.clu-theme-bg')).toBeVisible()
    expect(await ring.evaluate((el) => getComputedStyle(el).clipPath)).toContain('evenodd')
    await expect.poll(() => ui.page.frame({ url: /layer=under-peek/ })?.evaluate(() => document.getAnimations().filter((a) => a.playState === 'running').length)).toBe(0)
    await shoot(ui, 'peek')
  },
  split: async (ui) => {
    const made = await ui.frameDoc().evaluate(([a, b]) => window.clu.split.create({ tabIds: [a, b], dir: 'row' }), [tab(20), tab(21)] as const)
    expect(made.ok).toBe(true)
    await shoot(ui, 'split')
    const pill = await ui.frame.locator('.clu-split-row[data-active="true"] .clu-split-member').first().boundingBox()
    expect(pill?.height).toBe(28)
    await ui.frameDoc().evaluate((id) => window.clu.tabs.activate({ tabId: id }), tab(22))
    await shoot(ui, 'split-inactive')
  },
  'sidebar-peek': async (ui) => {
    await ui.frameDoc().evaluate(() => window.clu.shell.toggleSidebar({}))
    await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
    const s = await state(ui)
    const h = s.windows[WINDOW]?.shell.layout.sidebarRect.height ?? 800
    const w = s.windows[WINDOW]?.shell.sidebar.width ?? 260
    await addSurface(ui, { instanceId: 'speek-1', surface: 'sidebar-peek', layer: 'passive', modality: 'passive', slot: 3, rect: { x: 0, y: 0, width: w + 16, height: h } })
    await expect(passiveDoc(ui, 3).locator('nav')).toBeVisible()
    await expect(passiveDoc(ui, 3).locator('[data-shell="sidebar-peek"]')).toHaveAttribute('data-motion', 'shown')
    await expect.poll(() => ui.page.frame({ url: /layer=passive&slot=3/ })?.evaluate(() => document.getAnimations().filter((x) => x.playState === 'running' && x.effect?.getTiming().iterations !== Infinity).length)).toBe(0)
    await shootHere(ui, 'sidebar-peek')
  }
}

for (const [name, run] of Object.entries(states)) {
  test(`float: ${name}`, async ({ ui }) => {
    await run(ui)
  })
}
