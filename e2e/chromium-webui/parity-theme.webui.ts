// Space theme editor: the editor at 1280 x 800, 2x, in light and
// dark, shot into $CLU_SHOTS_DIR/parity/theme; a handle drag, the wavy slider and the grain dial repaint the sidebar and
// frame while the pointer is still down, and commit once on release.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import type { Locator } from '@playwright/test'
import { expect, harnessTest, shotsDir, state, type Harness } from './fixture'

const LABEL = 'parity/theme'
const test = harnessTest(LABEL)
const SHOTS = shotsDir(LABEL)
const HOME = '00000000-0000-4000-8000-000300000001'
/** A pink space: hue 357, 92.5 pt out (depth 0.617), intensity 0.628, grain 0.3. */
const PINK = { colors: [{ h: 357, s: 0.617 }], intensity: 0.628, grain: 0.3 }
type Mode = 'light' | 'dark'

async function settle(ui: Harness): Promise<void> {
  for (const f of ui.page.frames()) {
    await f.evaluate(() => Promise.all(document.getAnimations().filter((a) => a.effect?.getTiming().iterations !== Infinity).map((a) => a.finished.catch(() => null)))).catch(() => null)
  }
}
async function shot(ui: Harness, name: string, el?: Locator): Promise<void> {
  mkdirSync(SHOTS, { recursive: true })
  await settle(ui)
  if (el) await el.screenshot({ path: join(SHOTS, `${name}.png`) })
  else await ui.page.screenshot({ path: join(SHOTS, `${name}.png`) })
}
async function overlayReady(ui: Harness): Promise<void> {
  await expect
    .poll(() => ui.page.evaluate(() => (window as unknown as { cluFakeHost: { calls: { surface: string; name: string }[] } }).cluFakeHost.calls.some((c) => c.surface === 'overlay' && c.name === 'ui.ready')))
    .toBe(true)
}
async function open(ui: Harness, mode: Mode): Promise<Locator> {
  await overlayReady(ui)
  if (mode === 'dark') {
    await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
    await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
  }
  await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.setSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme: PINK })
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme).toEqual(PINK)
  // The space menu's Edit Theme anchors at the space row near the top of the sidebar.
  await ui.frameDoc().evaluate((spaceId) => window.clu.theme.openPicker({ spaceId, anchor: { x: 12, y: 96, width: 236, height: 28 } }), HOME)
  const picker = ui.overlay.getByRole('dialog', { name: /^Theme for / })
  await expect(picker).toBeVisible()
  await settle(ui)
  return picker
}
/** The frame's first theme stop as painted now. */
const frameTint = (ui: Harness) => ui.frame.locator('.clu-theme-bg').evaluate((el) => getComputedStyle(el).getPropertyValue('--clu-theme-c0').trim())
const center = async (l: Locator) => {
  const b = await l.boundingBox()
  if (!b) throw new Error('no box')
  return { x: b.x + b.width / 2, y: b.y + b.height / 2, box: b }
}

for (const mode of ['light', 'dark'] as const) {
  test(`theme editor: its layout beside the sidebar (${mode})`, async ({ ui }) => {
    const picker = await open(ui, mode)
    const box = await picker.boundingBox()
    expect(box).toMatchObject({ width: 356, height: 508 })
    const sidebar = await ui.frame.locator('nav.clu-sidebar').boundingBox()
    expect(Math.round((box?.x ?? 0) - ((sidebar?.x ?? 0) + (sidebar?.width ?? 0)))).toBe(13)
    expect(await picker.evaluate((el) => getComputedStyle(el).borderRadius)).toBe('19px')
    // Nothing dims the window while the editor is open, so the sidebar shows the theme as it is.
    await expect(ui.overlay.locator('.clu-overlay-root')).not.toHaveAttribute('data-scrim', 'modal')
    expect(await ui.overlay.locator('.clu-overlay-root').evaluate((el) => getComputedStyle(el).backgroundColor)).toBe('rgba(0, 0, 0, 0)')
    // It scales up from its left middle out of 0.1 with a fade, a 440 ms
    // spring on transform and a 180 ms fade, compositor properties only, and rests at full size.
    const motion = await picker.evaluate((el) => {
      const cs = getComputedStyle(el)
      return { property: cs.transitionProperty, duration: cs.transitionDuration, origin: cs.transformOrigin, transform: cs.transform, opacity: cs.opacity }
    })
    expect(motion).toEqual({ property: 'transform, opacity', duration: '0.44s, 0.18s', origin: '0px 254px', transform: 'none', opacity: '1' })
    // Neutral gray glass whatever the space's colors, #5f5f5f in dark and #f3eae8 in light.
    expect(await picker.evaluate((el) => getComputedStyle(el).backgroundColor)).toBe(mode === 'dark' ? 'rgb(95, 95, 95)' : 'rgb(243, 234, 232)')
    expect(await picker.locator('.clu-tp-glyph').first().evaluate((el) => getComputedStyle(el).color)).toMatch(mode === 'dark' ? /^rgba\(255, 255, 255,/ : /^rgba\(0, 0, 0,/)
    await expect(picker.getByRole('radio')).toHaveCount(3)
    await expect(picker.locator('.clu-tp-handle')).toHaveCount(1)
    await expect(picker.locator('.clu-tp-swatch[aria-hidden="false"]')).toHaveCount(9)
    await expect(picker.getByRole('button', { name: 'Previous presets' })).toBeDisabled()
    const handle = await center(picker.locator('.clu-tp-handle'))
    expect(handle.box.width).toBe(49)
    // Hue 357, 92.5 pt out: right of the canvas center, just above its horizontal (canvas center at 8 + 170).
    expect(Math.round(handle.x - ((box?.x ?? 0) + 178))).toBe(92)
    expect(handle.y - ((box?.y ?? 0) + 178)).toBeLessThan(0)
    await shot(ui, `window-${mode}`)
    await shot(ui, `editor-${mode}`, picker)
    // Nothing animates at rest: once open, the only running animations are none (no icon turn for the mode already on).
    expect(await picker.evaluate((el) => el.getAnimations({ subtree: true }).filter((a) => a.playState === 'running').length)).toBe(0)
    // A mode click twinkles that icon's stars once, on the compositor.
    const other = mode === 'dark' ? 'Light' : 'Dark'
    await picker.getByRole('radio', { name: other }).click()
    // The hover plate is a transition, not part of the twinkle; the twinkle starts when the mode lands in state.
    const twinkle = () => picker.getByRole('radio', { name: other }).evaluate((el) =>
      [...new Set(el.getAnimations({ subtree: true }).filter((a) => a instanceof CSSAnimation).map((a) => `${a.animationName} ${String(a.effect?.getTiming().duration)}`))])
    await expect.poll(twinkle).toEqual(['clu-tp-twinkle 700'])
    await picker.getByRole('radio', { name: mode === 'dark' ? 'Dark' : 'Light' }).click()
    await settle(ui)
    // Esc: back to scale 0.1 toward the same edge in 150 ms, transform and opacity together, then gone.
    const closing = await picker.evaluate(async (el) => {
      const flagged = new Promise<void>((done) => {
        const mo = new MutationObserver(() => {
          if (!el.hasAttribute('data-closing')) return
          mo.disconnect()
          done()
        })
        mo.observe(el, { attributes: true, attributeFilter: ['data-closing'] })
      })
      el.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
      await flagged
      const cs = getComputedStyle(el)
      return { closing: el.hasAttribute('data-closing'), property: cs.transitionProperty, duration: cs.transitionDuration }
    })
    expect(closing).toEqual({ closing: true, property: 'transform, opacity', duration: '0.15s, 0.15s' })
    await expect(picker).toBeHidden()
  })

  test(`theme editor: a handle drag repaints the frame while the pointer is down, then commits (${mode})`, async ({ ui }) => {
    const picker = await open(ui, mode)
    const before = await frameTint(ui)
    const h = await center(picker.locator('.clu-tp-handle'))
    const canvas = await center(picker.getByTestId('theme-field'))
    await ui.page.mouse.move(h.x, h.y)
    await ui.page.mouse.down()
    // Around the wheel to the blue side, still held.
    await ui.page.mouse.move(canvas.x - 40, canvas.y - 100, { steps: 6 })
    await expect.poll(() => frameTint(ui)).not.toBe(before)
    const mid = await frameTint(ui)
    expect((await state(ui)).spaces[HOME]?.theme?.colors[0]?.h).toBeGreaterThan(90)
    await shot(ui, `drag-held-${mode}`)
    await ui.page.mouse.move(canvas.x - 120, canvas.y + 20, { steps: 4 })
    await expect.poll(() => frameTint(ui)).not.toBe(mid)
    await ui.page.mouse.up()
    const committed = (await state(ui)).spaces[HOME]?.theme
    expect(committed?.colors[0]?.h).toBeGreaterThan(160)
    expect(committed?.colors[0]?.h).toBeLessThan(200)
    // Esc after a commit keeps it.
    await ui.page.keyboard.press('Escape')
    await expect(picker).toBeHidden()
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme).toEqual(committed)
  })

  test(`theme editor: the wavy slider and the grain dial repaint live (${mode})`, async ({ ui }) => {
    const picker = await open(ui, mode)
    const slider = await center(picker.getByRole('slider', { name: 'Intensity' }))
    const thumb = await center(picker.locator('.clu-tp-thumb'))
    const before = await frameTint(ui)
    await ui.page.mouse.move(thumb.x, thumb.y)
    await ui.page.mouse.down()
    await ui.page.mouse.move(slider.box.x + slider.box.width, thumb.y, { steps: 5 })
    await expect.poll(() => frameTint(ui)).not.toBe(before)
    await ui.page.mouse.up()
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.intensity).toBe(1)
    await shot(ui, `intensity-1-${mode}`)
    await ui.page.mouse.move(thumb.x + 40, thumb.y)
    await ui.page.mouse.down()
    await ui.page.mouse.move(slider.box.x - 20, thumb.y, { steps: 5 })
    await ui.page.mouse.up()
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.intensity).toBe(0)
    await shot(ui, `intensity-0-${mode}`)

    // The dial: clockwise from 3 o'clock, continuously, to near the end of its scale (345 degrees), then on across the
    // top, where it wraps back to the least grain.
    const dial = await center(picker.getByRole('slider', { name: 'Grain' }))
    const at = (deg: number) => ({ x: dial.x + 30 * Math.sin((deg * Math.PI) / 180), y: dial.y - 30 * Math.cos((deg * Math.PI) / 180) })
    await ui.page.mouse.move(at(90).x, at(90).y)
    await ui.page.mouse.down()
    for (const deg of [150, 210, 270, 320, 342]) await ui.page.mouse.move(at(deg).x, at(deg).y, { steps: 2 })
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.grain ?? 0).toBeGreaterThan(0.97)
    expect(await picker.locator('.clu-tp-dot-on').count()).toBeGreaterThanOrEqual(19)
    await shot(ui, `grain-max-${mode}`)
    await ui.page.mouse.move(at(372).x, at(372).y, { steps: 2 })
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.grain).toBe(0)
    await ui.page.mouse.up()
  })

  test(`theme editor: plus to three colors, minus, sliding preset pages, a swatch back to one color (${mode})`, async ({ ui }) => {
    const picker = await open(ui, mode)
    const add = picker.getByRole('button', { name: 'Add color' })
    await add.click()
    await add.click()
    await expect(add).toBeDisabled()
    await expect(picker.locator('.clu-tp-handle')).toHaveCount(3)
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.colors.length).toBe(3)
    expect((await picker.locator('.clu-tp-handle').nth(2).boundingBox())?.width).toBe(28)
    await shot(ui, `three-colors-${mode}`)
    const next = picker.getByRole('button', { name: 'Next presets' })
    const remove = picker.getByRole('button', { name: 'Remove color' })
    await remove.click()
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.colors.length).toBe(2)
    for (let i = 0; i < 4; i++) await next.click()
    await expect(next).toBeDisabled()
    await shot(ui, `page-5-${mode}`, picker)
    await picker.getByRole('button', { name: 'Previous presets' }).click()
    // A swatch replaces the theme with its one color.
    await picker.locator('.clu-tp-swatch[aria-hidden="false"]').nth(2).click()
    await expect.poll(async () => (await state(ui)).spaces[HOME]?.theme?.colors.length).toBe(1)
    await expect(remove).toBeDisabled()
  })

  // A tap on the bare canvas moves the main handle there (the others stay put), and the handles, the swatch
  // selection and the sidebar show the same colors after every tap.
  test(`theme editor: a tap on the canvas moves the handles, not only the sidebar (${mode})`, async ({ ui }) => {
    const picker = await open(ui, mode)
    const loose = { colors: [{ h: 250, s: 0.5 }, { h: 300, s: 0.75 }, { h: 15, s: 0.7 }], intensity: 0.6, grain: 0.2 }
    await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.setSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme: loose })
    await expect(picker.locator('.clu-tp-handle')).toHaveCount(3)
    const canvas = await center(picker.getByTestId('theme-field'))
    const handles = () => picker.locator('.clu-tp-handle').evaluateAll((els) => els.map((e) => { const r = e.getBoundingClientRect(); return { x: Math.round(r.x + r.width / 2), y: Math.round(r.y + r.height / 2) } }))
    for (const [dx, dy] of [[75, 0], [-75, 0], [0, -100]] as const) {
      const before = await frameTint(ui)
      await ui.page.mouse.click(canvas.x + dx, canvas.y + dy)
      await expect.poll(async () => (await handles())[0]).toEqual({ x: Math.round(canvas.x + dx), y: Math.round(canvas.y + dy) })
      await expect.poll(() => frameTint(ui)).not.toBe(before)
      const [, ...rest] = (await state(ui)).spaces[HOME]?.theme?.colors ?? []
      const shown = await handles()
      expect(shown).toHaveLength(3)
      // The others stay exactly where they were, and their handles show them there.
      expect(rest).toEqual(loose.colors.slice(1))
      for (const [i, c] of rest.entries()) {
        const p = shown[i + 1] as { x: number; y: number }
        const a = (Math.atan2(p.y - canvas.y, p.x - canvas.x) * 180) / Math.PI
        expect(Math.abs((((a - c.h) % 360) + 540) % 360 - 180)).toBeLessThan(1.5)
      }
    }
    await shot(ui, `tap-moves-handles-${mode}`, picker)
  })
}

// The frame's chrome follows the tint, not only the mode.
test('a full-saturation mint space accents in soft mint, not neon, and its light command bar is frosted', async ({ ui }) => {
  await overlayReady(ui)
  await ui.frameDoc().evaluate((spaceId) => window.clu.theme.setSpaceTheme({ spaceId, theme: { colors: [{ h: 139.3, s: 1 }], intensity: 0.5, grain: 0 } }), HOME)
  await expect.poll(async () => (await state(ui)).spaces[HOME]?.accent).toBe('#7edd9d')
  const overlay = ui.page.frames().find((f) => f.url().includes('overlay'))
  if (!overlay) throw new Error('no overlay frame')
  await expect.poll(() => overlay.evaluate(() => document.documentElement.dataset.theme)).toBe('light')
  const panel = await overlay.evaluate(() => {
    const d = document.createElement('div')
    d.className = 'clu-cmd'
    document.body.append(d)
    const c = getComputedStyle(d).backgroundColor
    d.remove()
    return c
  })
  expect(panel).toMatch(/[/,] ?0\.89\)$/)
})

test('the frame wears light chrome for a pastel in dark mode, dark chrome for a dark color in light mode', async ({ ui }) => {
  await overlayReady(ui)
  const theme = () => ui.frameDoc().evaluate(() => document.documentElement.dataset.theme)
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await ui.frameDoc().evaluate((spaceId) => window.clu.theme.setSpaceTheme({ spaceId, theme: { colors: [{ h: 330, s: 0.95 }], intensity: 0.5, grain: 0 } }), HOME)
  await expect.poll(theme).toBe('light')
  await shot(ui, 'chrome-pastel-in-dark')
  await ui.frameDoc().evaluate((spaceId) => window.clu.theme.setSpaceTheme({ spaceId, theme: { colors: [{ h: 0, s: 0.4 }], intensity: 0.5, grain: 0 } }), HOME)
  await expect.poll(theme).toBe('dark')
  // The dark empty card is darker than the sidebar.
  const card = await ui.frameDoc().evaluate(() => {
    const el = document.createElement('div')
    el.className = 'clu-card'
    el.dataset.empty = 'true'
    document.body.append(el)
    const bg = getComputedStyle(el).backgroundColor
    el.remove()
    return bg
  })
  const [r = 255, g = 255, b = 255] = (card.match(/[\d.]+/g) ?? []).map(Number)
  expect(Math.max(r, g, b)).toBeLessThan(40)
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'light' }))
  await ui.frameDoc().evaluate((spaceId) => window.clu.theme.setSpaceTheme({ spaceId, theme: { colors: [{ h: 0, s: 0 }], intensity: 0.5, grain: 0 } }), HOME)
  await expect.poll(theme).toBe('dark')
  await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.setSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme: PINK })
  await expect.poll(theme).toBe('light')
  await shot(ui, 'chrome-light-pink')
})

// Three colors paint corner regions: the first top left, the second top right, the third along the bottom.
test('three colors paint the first top left, the second top right and the third along the bottom', async ({ ui }) => {
  await overlayReady(ui)
  const three = { colors: [{ h: 0, s: 0.5 }, { h: 120, s: 0.5 }, { h: 230, s: 0.5 }], intensity: 0.5, grain: 0 }
  await ui.frameDoc().evaluate(({ spaceId, theme }) => window.clu.theme.setSpaceTheme({ spaceId, theme }), { spaceId: HOME, theme: three })
  const bg = ui.frame.locator('.clu-theme-bg')
  await expect(bg).toHaveAttribute('data-colors', '3')
  const box = await bg.boundingBox()
  if (!box) throw new Error('no box')
  await settle(ui)
  const png = await bg.screenshot({ path: join(SHOTS, 'three-colors.png') })
  expect(png.byteLength).toBeGreaterThan(0)
  // Reading the corners back: the image's own pixels via a canvas in the frame.
  const corners = await ui.frameDoc().evaluate(async (data) => {
    const img = new Image()
    img.src = `data:image/png;base64,${data}`
    await img.decode()
    const c = document.createElement('canvas')
    c.width = img.width
    c.height = img.height
    const g = c.getContext('2d') as CanvasRenderingContext2D
    g.drawImage(img, 0, 0)
    const at = (x: number, y: number) => Array.from(g.getImageData(x, y, 1, 1).data.slice(0, 3))
    return { tl: at(4, 4), tr: at(img.width - 5, 4), bl: at(4, img.height - 5), br: at(img.width - 5, img.height - 5) }
  }, png.toString('base64'))
  const hue = ([r = 0, g = 0, b = 0]: number[]) => (r >= g && r >= b ? 'r' : g >= b ? 'g' : 'b')
  expect([hue(corners.tl), hue(corners.tr), hue(corners.bl), hue(corners.br)]).toEqual(['r', 'g', 'b', 'b'])
})
