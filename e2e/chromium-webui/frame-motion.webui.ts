// Frame and motion checks in the frame and overlay WebUIs. Shots go to $CLU_SHOTS_DIR/frame-motion.
import { expect, harnessTest, state, WINDOW, type Harness } from './fixture'

const test = harnessTest('frame-motion')

test('the grabber above the card shows only while the pointer is in the top margin', async ({ ui }) => {
  const zone = ui.frame.locator('[data-shell="card-grabber-zone"]')
  const grabber = zone.locator('[data-shell="card-grabber"]')
  const opacity = () => grabber.evaluate((el) => getComputedStyle(el).opacity)
  const box = await zone.boundingBox()
  if (!box) throw new Error('no grabber zone')
  expect(box.height).toBe(10)
  await ui.page.mouse.move(box.x + box.width / 2, 300)
  await expect.poll(opacity).toBe('0')
  await ui.shot('grabber-rest')
  await ui.page.mouse.move(box.x + box.width / 2, box.y + 5)
  await expect.poll(opacity).toBe('1')
  await ui.shot('grabber-hover')
  await ui.page.mouse.move(box.x + box.width / 2, 300)
  await expect.poll(opacity).toBe('0')
})

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)
const hostCalls = (ui: Harness, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

/** Hides the sidebar and opens the floating one as the browser publishes it. */
async function float(ui: Harness, props: Record<string, unknown>) {
  await ui.frameDoc().evaluate(() => window.clu.shell.toggleSidebar({}))
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(true)
  const s = await state(ui)
  const w = s.windows[WINDOW]
  const width = (w?.shell.sidebar.width ?? 214) + 16
  const surfaces = w?.overlay.surfaces ?? []
  await host(ui, ['windows', WINDOW, 'overlay', 'surfaces'], [
    ...surfaces,
    { instanceId: 'peek-g241', surface: 'sidebar-peek', layer: 'passive', modality: 'passive', slot: 3, focus: false, openedFrom: null, rect: { x: 0, y: 0, width, height: w?.shell.layout.sidebarRect.height ?? 800 }, props }
  ])
  const doc = ui.page.frameLocator('iframe.passive[data-slot="3"]')
  await expect(doc.locator('[data-shell="sidebar-peek"]')).toBeVisible()
  return doc
}

test('the floating sidebar carries the window buttons at their docked place; they run the window commands', async ({ ui }) => {
  await host(ui, ['windows', WINDOW, 'focused'], true)
  const doc = await float(ui, {})
  const lights = doc.locator('[data-shell="peek-lights"]')
  await expect(lights).toHaveAttribute('data-focused', 'true')
  await expect(doc.locator('[data-shell="sidebar-peek"]')).toHaveAttribute('data-motion', 'shown')
  await expect.poll(() => doc.locator('[data-shell="sidebar-peek"]').evaluate((el) => el.getAnimations().filter((a) => a.playState === 'running').length)).toBe(0)
  const close = await lights.locator('[data-light="close"]').boundingBox()
  const full = await lights.locator('[data-light="fullscreen"]').boundingBox()
  if (!close || !full) throw new Error('no lights')
  // The panel floats 5 in, the lights sit on its top row at the docked lights' place: centers 19.75 + 5, 23.75 + 5.
  expect(close.x + close.width / 2).toBeCloseTo(24.75, 1)
  expect(close.y + close.height / 2).toBeCloseTo(28.75, 1)
  expect(close.width).toBe(12)
  expect(full.x - close.x).toBe(46)
  await ui.shot('peek-lights-rest')
  const glyph = lights.locator('[data-light="minimize"] svg')
  await expect(glyph).toBeHidden()
  await lights.locator('[data-light="minimize"]').hover()
  await expect(glyph).toBeVisible()
  await ui.shot('peek-lights-hover')
  await lights.locator('[data-light="minimize"]').click()
  await lights.locator('[data-light="fullscreen"]').click()
  await expect.poll(() => hostCalls(ui, 'window.minimize')).toHaveLength(1)
  await expect.poll(() => hostCalls(ui, 'window.toggleFullscreen')).toHaveLength(1)
  await host(ui, ['windows', WINDOW, 'focused'], false)
  await expect(lights).toHaveAttribute('data-focused', 'false')
})

// The timing (no slide out, no close request after the grace) is SidebarPeek.test.tsx's, on fake timers.
test('pinned (no page open), the floating sidebar shows at once, with no slide', async ({ ui }) => {
  const doc = await float(ui, { pinned: true })
  const peek = doc.locator('[data-shell="sidebar-peek"]')
  await expect(peek).toHaveAttribute('data-pinned', 'true')
  expect(await peek.evaluate((el) => el.getAnimations().filter((a) => a.playState === 'running').length)).toBe(0)
  await ui.shot('peek-pinned')
})

test('in full screen the top row keeps its room for the window buttons, so the toggle sits after them', async ({ ui }) => {
  const toggle = ui.frame.locator('[data-shell="sidebar-toggle"]')
  const left = async () => (await toggle.boundingBox())?.x
  const windowed = await left()
  // The zoom button's right edge: 19.75 + 2 * 23 + 6.
  expect(windowed).toBeGreaterThanOrEqual(71.75)
  await host(ui, ['windows', WINDOW, 'fullscreen'], true)
  // Hiding and showing the sidebar makes the fake browser lay the window out again, now in full screen.
  for (let i = 0; i < 2; i++) await ui.frameDoc().evaluate(() => window.clu.shell.toggleSidebar({}))
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(false)
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.trafficSpacer).toBe(66)
  await expect.poll(left).toBe(windowed)
  await ui.shot('fullscreen-top-row')
})

test('the revealed full-screen bar moves the whole sidebar column down, nothing over the pill or the tiles', async ({ ui }) => {
  const box = async (sel: string) => {
    const b = await ui.frame.locator(sel).first().boundingBox()
    if (!b) throw new Error(`no ${sel}`)
    return b
  }
  const parts = ['[data-surface="top-row"]', '[data-shell="url-pill"]', '.clu-favorites']
  const rest = await Promise.all(parts.map(box))
  await host(ui, ['windows', WINDOW, 'fullscreen'], true)
  await host(ui, ['windows', WINDOW, 'shell', 'titlebarReveal'], 52)
  await expect.poll(async () => (await box(parts[0] as string)).y).toBeCloseTo((rest[0]?.y ?? 0) + 52, 0)
  const shown = await Promise.all(parts.map(box))
  for (const [i, b] of shown.entries()) expect(b.y).toBeCloseTo((rest[i]?.y ?? 0) + 52, 0)
  // Top row, pill and tiles keep their order with no overlap, and the pill is drawn where it is.
  const [row, pill, tiles] = shown
  if (!row || !pill || !tiles) throw new Error('missing part')
  expect(row.y + row.height).toBeLessThanOrEqual(pill.y + 0.5)
  expect(pill.y + pill.height).toBeLessThanOrEqual(tiles.y + 0.5)
  await expect(ui.frame.locator('[data-shell="url-pill"]')).toBeVisible()
  const onTop = await ui.frame.locator('[data-shell="url-pill"]').evaluate((el) => {
    const r = el.getBoundingClientRect()
    const at = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2)
    return !!at && el.contains(at)
  })
  expect(onTop).toBe(true)
  // The footer stays in the window.
  const footer = await box('.clu-footer')
  expect(footer.y + footer.height).toBeLessThanOrEqual(ui.page.viewportSize()?.height ?? 800)
  await ui.shot('fullscreen-bar-shown')
  await host(ui, ['windows', WINDOW, 'shell', 'titlebarReveal'], 0)
  await expect.poll(async () => (await box(parts[1] as string)).y).toBeCloseTo(rest[1]?.y ?? 0, 0)
})

// The floating sidebar slides in on the spring and out on the ease-in, both transitions on transform, watched
// through the DevTools Animation domain and the panel's right edge on every frame the overlay draws.
test('the floating sidebar slides in with the overshoot and out after the grace', async ({ ui }) => {
  const cdp = await ui.page.context().newCDPSession(ui.page)
  const started: { type: string; name: string; duration: number; easing: string }[] = []
  cdp.on('Animation.animationStarted', ({ animation }) => {
    started.push({ type: animation.type, name: animation.name, duration: animation.source?.duration ?? 0, easing: animation.source?.easing ?? '' })
  })
  await cdp.send('Animation.enable')
  // The panel's right edge on every frame, from the overlay document's start, before the peek mounts.
  const attached = ui.page.waitForEvent('frameattached')
  const doc = float(ui, {})
  const frame = await attached
  await frame.waitForLoadState('domcontentloaded')
  await frame.evaluate(() => {
    const w = window as unknown as { cluB64: [number, number, string][] }
    w.cluB64 = []
    const tick = (t: number) => {
      const el = document.querySelector<HTMLElement>('[data-shell="sidebar-peek"]')
      if (el) w.cluB64.push([t, el.getBoundingClientRect().right, el.dataset.motion ?? ''])
      requestAnimationFrame(tick)
    }
    requestAnimationFrame(tick)
  })
  const peek = (await doc).locator('[data-shell="sidebar-peek"]')
  await expect(peek).toHaveAttribute('data-motion', 'shown')
  await expect.poll(() => peek.evaluate((el) => el.getAnimations().filter((a) => a.playState === 'running').length)).toBe(0)
  const rest = await peek.evaluate((el) => (el as HTMLElement).offsetLeft + (el as HTMLElement).offsetWidth)
  const samples = await frame.evaluate(() => (window as unknown as { cluB64: [number, number, string][] }).cluB64)
  const first = samples[0]
  if (!first) throw new Error('no frames')
  expect(first[1]).toBeLessThan(0)
  expect(first[2]).toBe('enter')
  const moving = samples.filter(([, right]) => right > 0 && right < rest - 1)
  expect(moving.length).toBeGreaterThanOrEqual(3)
  const peak = Math.max(...samples.map(([, right]) => right))
  expect(peak).toBeGreaterThan(rest + 1)
  expect(peak).toBeLessThan(rest + 4)
  const settled = samples.find(([, right], i) => i > 0 && Math.abs(right - rest) < 0.5 && samples.slice(i).every(([, r]) => Math.abs(r - rest) < 0.5))
  if (!settled) throw new Error('never settled')
  // About 250 ms from the first frame to rest (the frame clock's resolution aside).
  expect(settled[0] - first[0]).toBeGreaterThan(150)
  expect(settled[0] - first[0]).toBeLessThan(330)
  expect(started).toContainEqual(expect.objectContaining({ type: 'CSSTransition', name: 'transform', duration: 250 }))
  await ui.shot('peek-slid-in')

  // Out: the pointer leaves (a synthetic pointerout, no real input), the grace, then the 90 ms ease-in and the close.
  await frame.evaluate(() => {
    const w = window as unknown as { cluB64: [number, number, string][] }
    w.cluB64 = []
  })
  await peek.dispatchEvent('pointerover', { pointerId: 1, bubbles: true })
  await peek.dispatchEvent('pointerout', { pointerId: 1, bubbles: true, relatedTarget: null })
  await expect(peek).toHaveAttribute('data-motion', 'leaving')
  await expect.poll(() => hostCalls(ui, 'shell.setSidebarPeek')).toContainEqual({ open: false })
  const out = await frame.evaluate(() => (window as unknown as { cluB64: [number, number, string][] }).cluB64)
  const leaving = out.filter(([, , motion]) => motion === 'leaving')
  expect(leaving.filter(([, right]) => right > 0 && right < rest - 1).length).toBeGreaterThanOrEqual(2)
  expect(started).toContainEqual(expect.objectContaining({ type: 'CSSTransition', name: 'transform', duration: 90 }))
  await cdp.detach()
})
