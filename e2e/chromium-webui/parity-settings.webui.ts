// Rio's two settings: the app settings window's
// document at the window's own size (684 x 618 under the native 32 pt title bar of macOS 26, 2x) in light and dark, each tab shot
// into $CLU_SHOTS_DIR/parity/settings with its sizes pinned, the Shortcuts tab's whole
// flow, a custom shortcut taking effect in the frame and its tooltips, and Chromium's settings as a normal sidebar row.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import type { Page } from '@playwright/test'
import { expect, harnessTest, shotsDir, tab, WINDOW, type Harness } from './fixture'

const LABEL = 'parity/settings'
const test = harnessTest(LABEL)
const SHOTS = shotsDir(LABEL)
const WINDOW_SIZE = { width: 684, height: 618 }
const TABS = ['general', 'profiles', 'ai', 'links', 'shortcuts', 'icon', 'advanced'] as const
type Mode = 'light' | 'dark'

async function settled(page: Page): Promise<void> {
  await page.evaluate(() => Promise.all(document.getAnimations().map((a) => a.finished.catch(() => null))))
}
async function shot(page: Page, name: string): Promise<void> {
  mkdirSync(SHOTS, { recursive: true })
  await settled(page)
  await page.screenshot({ path: join(SHOTS, `${name}.png`) })
}

/** The settings document as the window shows it: its own size, the mode set through Rio's own appearance setting. */
async function openWindow(ui: Harness, mode: Mode, tabId: (typeof TABS)[number] = 'general'): Promise<Page> {
  const page = ui.page
  await page.setViewportSize(WINDOW_SIZE)
  await page.goto(`/settings/#${tabId}`)
  await expect(page.getByRole('tab', { selected: true })).toBeVisible()
  if (mode === 'dark') {
    await page.evaluate(() => window.clu.settings.set({ key: 'appearance.mode', scope: 'global', value: 'dark' }))
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark')
  }
  await page.mouse.move(0, WINDOW_SIZE.height - 1)
  await settled(page)
  return page
}

for (const mode of ['light', 'dark'] as const) {
  test(`app settings (${mode}): every tab, shot at the window's size`, async ({ ui }) => {
    const page = await openWindow(ui, mode)
    for (const id of TABS) {
      await page.getByRole('tab', { name: { general: 'General', profiles: 'Profiles', ai: 'AI features', links: 'Links', shortcuts: 'Shortcuts', icon: 'Icon', advanced: 'Advanced' }[id] }).click()
      await expect(page.locator('main.clu-sw-body')).toHaveAttribute('data-pane', id)
      await expect.poll(() => page.title()).toBe({ general: 'General', profiles: 'Profiles', ai: 'AI features', links: 'Links', shortcuts: 'Shortcuts', icon: 'Icon', advanced: 'Advanced' }[id])
      await page.mouse.move(0, WINDOW_SIZE.height - 1)
      await shot(page, `${id}-${mode}`)
    }
  })

  test(`app settings (${mode}): the design's toolbar, card, row, switch and button measurements`, async ({ ui }) => {
    const page = await openWindow(ui, mode, 'links')
    const m = await page.evaluate(() => {
      const css = (el: Element | null) => (el ? getComputedStyle(el) : null)
      const box = (el: Element | null) => el?.getBoundingClientRect()
      const toolbar = document.querySelector('.clu-sw-toolbar')
      const selected = document.querySelector('.clu-sw-tab[aria-selected="true"]')
      const card = document.querySelector('.clu-sw-body .clu-group')
      const row = document.querySelector('.clu-sw-body .clu-group-row')
      const toggle = document.querySelector('.clu-sw .clu-toggle')
      const knob = document.querySelector('.clu-sw .clu-toggle > span')
      return {
        toolbarBottom: box(toolbar)?.bottom,
        separator: css(toolbar)?.borderBottomColor,
        toolbarBg: css(toolbar)?.backgroundColor,
        body: css(document.body)?.backgroundColor,
        selected: { w: box(selected)?.width, h: box(selected)?.height, top: box(selected)?.top, color: css(selected)?.color, bg: css(selected)?.backgroundColor },
        icon: box(selected?.querySelector('svg') ?? null),
        label: box(selected?.querySelector('span') ?? null),
        card: { x: box(card)?.left, w: box(card)?.width, top: box(card)?.top, bg: css(card)?.backgroundColor, border: css(card)?.borderTopColor, radius: css(card)?.borderTopLeftRadius },
        row: { h: box(row)?.height, fontSize: css(row?.querySelector('.clu-group-row-label') ?? null)?.fontSize },
        toggle: { w: box(toggle)?.width, h: box(toggle)?.height, bg: css(toggle)?.backgroundColor, knobW: box(knob)?.width, knobH: box(knob)?.height }
      }
    })
    // The toolbar is window y 32 to 88 under its 32 pt title bar, its tabs 55 x 56 with icons centered at
    // y 47 and labels at 68, so 15 and 36 below the document's top.
    expect(m.toolbarBottom).toBe(56)
    expect(m.selected.w).toBeGreaterThanOrEqual(55)
    expect(m.selected.h).toBe(47)
    expect(m.selected.top).toBe(0)
    expect(Math.abs((m.icon?.top ?? 0) + (m.icon?.height ?? 0) / 2 - 15)).toBeLessThanOrEqual(0.5)
    expect(Math.abs((m.label?.top ?? 0) + (m.label?.height ?? 0) / 2 - 36)).toBeLessThanOrEqual(0.5)
    expect(m.card).toMatchObject({ x: 64.5, w: 555, radius: '7.5px' })
    expect(m.row).toEqual({ h: 36, fontSize: '13px' })
    expect(m.toggle).toMatchObject({ w: 36, h: 16, knobW: 21, knobH: 12 })
    const want =
      mode === 'light'
        ? { separator: 'rgb(230, 230, 230)', toolbarBg: 'rgb(255, 255, 255)', body: 'rgb(253, 253, 253)', tab: 'rgb(47, 110, 237)', card: 'rgb(250, 250, 250)', border: 'rgb(229, 229, 229)', on: 'rgb(58, 122, 236)' }
        : { separator: 'rgb(35, 35, 35)', toolbarBg: 'rgb(37, 40, 42)', body: 'rgb(37, 40, 42)', tab: 'rgb(53, 119, 243)', card: 'rgb(40, 43, 45)', border: 'rgb(61, 64, 66)', on: 'rgb(57, 124, 246)' }
    expect(m.separator).toBe(want.separator)
    expect(m.toolbarBg).toBe(want.toolbarBg)
    expect(m.body).toBe(want.body)
    expect(m.selected.color).toBe(want.tab)
    expect(m.card.bg).toBe(want.card)
    expect(m.card.border).toBe(want.border)
    expect(m.toggle.bg).toBe(want.on)
    // No navy: every gray of the window stays gray (chroma under 0.04; the navy it once had was 0.11).
    for (const c of [m.body, m.card.bg, m.toolbarBg]) {
      const [r = 0, g = 0, b = 0] = (c?.match(/\d+/g) ?? []).map(Number)
      expect((Math.max(r, g, b) - Math.min(r, g, b)) / 255).toBeLessThan(0.04)
    }
  })
}

test('app settings: a switch flips its global key, and the pop-up and push buttons are 24 pt with no border', async ({ ui }) => {
  const page = await openWindow(ui, 'light', 'ai')
  const tidy = page.getByRole('switch', { name: 'Tidy Tabs' })
  await expect(tidy).toHaveAttribute('aria-checked', 'true')
  await tidy.click()
  await expect(tidy).toHaveAttribute('aria-checked', 'false')
  expect(await page.evaluate(async () => ((await window.clu.state.get()) as { ok: true; value: { settings: { global: Record<string, unknown> } } }).value.settings.global['ai.tidyTabs'])).toBe(false)
  await shot(page, 'ai-switch-off-light')
  await page.getByRole('tab', { name: 'Profiles' }).click()
  const look = await page.evaluate(() => {
    const select = document.querySelector('.clu-sw-body select') as Element
    const button = document.querySelector('.clu-sw-body .clu-btn') as Element
    return [select, button].map((el) => ({ h: el.getBoundingClientRect().height, border: getComputedStyle(el).borderTopWidth, shadow: getComputedStyle(el).boxShadow, radius: getComputedStyle(el).borderTopLeftRadius }))
  })
  for (const l of look) expect(l).toEqual({ h: 24, border: '0px', shadow: 'none', radius: '6px' })
})

test('app settings: General and Icon fit the window without scrolling, and the icon radios are the design’s discs', async ({ ui }) => {
  const page = await openWindow(ui, 'dark', 'general')
  const fits = () => page.evaluate(() => { const b = document.querySelector('main.clu-sw-body') as HTMLElement; return b.scrollHeight <= b.clientHeight })
  expect(await fits()).toBe(true)
  await page.getByRole('tab', { name: 'Icon' }).click()
  await expect(page.getByRole('radio')).toHaveCount(7)
  expect(await fits()).toBe(true)
  const radios = await page.evaluate(() =>
    [...document.querySelectorAll('.clu-sw-icon-row input[type="radio"]')].slice(0, 2).map((r) => {
      const c = getComputedStyle(r)
      return { w: r.getBoundingClientRect().width, bg: c.backgroundColor, ring: c.borderTopWidth, weight: getComputedStyle(r.parentElement?.querySelector('.clu-sw-icon-name') as Element).fontWeight }
    })
  )
  expect(radios).toEqual([
    { w: 22, bg: 'rgb(40, 43, 45)', ring: '6px', weight: '600' },
    { w: 22, bg: 'rgb(255, 255, 255)', ring: '0px', weight: '600' }
  ])
  await shot(page, 'icon-fit-dark')
})

for (const mode of ['light', 'dark'] as const) {
  test(`shortcuts (${mode}): search, record, a conflict and Replace, a reset, Reset All`, async ({ ui }) => {
    const page = await openWindow(ui, mode, 'shortcuts')
    const field = (title: string) => page.getByRole('button', { name: new RegExp(`^${title}: `) })
    await expect(field('Close Tab')).toHaveText('⌘W')
    await shot(page, `shortcuts-list-${mode}`)

    await page.getByRole('searchbox', { name: 'Search shortcuts' }).fill('cmd w')
    await expect(page.locator('.clu-sw-sc-row')).toHaveCount(1)
    await shot(page, `shortcuts-search-${mode}`)
    await page.getByRole('searchbox', { name: 'Search shortcuts' }).fill('')

    await field('Close Tab').click()
    await expect(field('Close Tab')).toHaveText('Type shortcut')
    await shot(page, `shortcuts-recording-${mode}`)
    await page.keyboard.press('Meta+y')
    await expect(page.getByRole('alert')).toHaveText(/⌘Y is used by Show History\./)
    await shot(page, `shortcuts-conflict-${mode}`)
    await page.getByRole('button', { name: 'Replace' }).click()
    await expect(field('Close Tab')).toHaveText('⌘Y')
    await expect(field('Show History')).toHaveText('---')
    await expect(page.getByRole('list', { name: 'Custom shortcuts' })).toContainText('Close Tab')
    await shot(page, `shortcuts-custom-${mode}`)

    await page.getByRole('button', { name: 'Reset All Shortcuts' }).click()
    await expect(page.getByRole('dialog', { name: 'Reset all shortcuts?' })).toBeVisible()
    await shot(page, `shortcuts-reset-all-${mode}`)
    await page.getByRole('dialog').getByRole('button', { name: 'Reset All' }).click()
    await expect(field('Close Tab')).toHaveText('⌘W')
    await expect(field('Show History')).toHaveText('⌘Y')
    await expect(page.getByRole('button', { name: 'Reset All Shortcuts' })).toBeDisabled()
  })
}

test('shortcuts: the keycap field and the list are the design’s sizes', async ({ ui }) => {
  const page = await openWindow(ui, 'dark', 'shortcuts')
  const m = await page.evaluate(() => {
    const box = (s: string) => document.querySelector(s)?.getBoundingClientRect()
    const rows = [...document.querySelectorAll('.clu-sw-sc-row')].slice(0, 2).map((r) => r.getBoundingClientRect())
    const cap = document.querySelector('.clu-sw-keycap') as Element
    return { list: box('.clu-sw-sc-list'), side: box('.clu-sw-sc-side'), search: box('.clu-sw-sc-search'), cap: cap.getBoundingClientRect(), capBg: getComputedStyle(cap).backgroundColor, pitch: (rows[1]?.top ?? 0) - (rows[0]?.top ?? 0) }
  })
  expect(m.list).toMatchObject({ x: 68, width: 340 })
  expect(m.side?.x).toBe(424)
  expect(m.search?.height).toBe(40)
  expect(m.cap.height).toBe(24)
  expect(m.cap.width).toBeGreaterThanOrEqual(63)
  expect(m.capBg).toBe('rgb(255, 255, 255)')
  expect(m.pitch).toBe(48)
  // The list fills the window to 26 above its bottom edge.
  expect(Math.round((m.list?.bottom ?? 0) - WINDOW_SIZE.height)).toBe(-26)
})

test('custom shortcuts take effect in the frame: the new key runs the command, the old one is free, tooltips follow', async ({ ui }) => {
  const calls = () => ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === 'commands.run').map((c) => (c.payload as { id: string }).id))
  await ui.page.locator('#frame').focus()
  // Back's tooltip shows its own keys first.
  const back = ui.frame.getByRole('button', { name: 'Back' })
  await back.hover()
  await expect(ui.frame.getByRole('tooltip')).toContainText('⌘[')
  await ui.page.mouse.move(640, 790)
  await ui.frameDoc().evaluate(() => window.clu.settings.set({ key: 'shortcuts.custom', scope: 'global', value: { 'nav.back': 'Cmd+Ctrl+B', 'history.open': null } }))
  await back.hover()
  await expect(ui.frame.getByRole('tooltip')).toContainText('⌃⌘B')
  await ui.page.mouse.move(640, 790)
  const before = (await calls()).length
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Control+Meta+b')
  await ui.page.keyboard.press('Meta+y')
  await ui.page.keyboard.press('Meta+[')
  await expect.poll(async () => (await calls()).slice(before)).toEqual(['nav.back'])
  await ui.shot('custom-shortcut-tooltip')
})

test('browser settings: rio://settings is a normal row in the sidebar, titled Settings', async ({ ui }) => {
  const host = (path: string[], value: unknown) => ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)
  // The active Today row (seed tab 21) shows Chromium's settings, as the browser publishes it (StateUrl: rio://).
  await host(['tabs', tab(21), 'url'], 'rio://settings/autofill')
  await host(['tabs', tab(21), 'title'], 'Settings - Autofill and passwords')
  await host(['windows', WINDOW, 'activeTabId'], tab(21))
  const row = ui.frame.locator('.clu-tab-row', { hasText: 'Settings - Autofill and passwords' })
  await expect(row).toBeVisible()
  await expect(row).toHaveAttribute('data-active', 'true')
  await ui.page.mouse.move(1279, 799)
  await ui.shot('browser-settings-row')
})
