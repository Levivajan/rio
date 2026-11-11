// Every control that shows on hover fades in and out (the shared .clu-reveal in base.css): the 150 ms
// sidebar-hover fade, ease-out in and ease-in out, opacity and scale only, once per enter or leave, reversed from
// where it is drawn. The pill's host text narrows and widens with its buttons. Transitions are paused and seeked
// through the Web Animations API, so no step waits on the wall clock.
import { expect, harnessTest, type Harness } from './fixture'

const test = harnessTest('hover-reveal')

const FOLDER = '00000000-0000-4000-8000-000400000001'
const onPage = (ui: Harness) => ui.page.mouse.move(900, 400)

type Run = { el: string; prop: string; ms: number; ease: string }
/** Pauses every running transition and lists it: the element's class, the property, its duration and easing. */
const pauseAll = (ui: Harness) =>
  ui.frameDoc().evaluate(() =>
    document.getAnimations().flatMap((a) => {
      if (!(a instanceof CSSTransition)) return []
      a.pause()
      const t = a.effect?.getTiming()
      const target = (a.effect as KeyframeEffect | null)?.target as Element | null
      return [{ el: target?.getAttribute('class') ?? '', prop: a.transitionProperty, ms: Number(t?.duration ?? 0), ease: String(t?.easing ?? '') }]
    }),
  ) as Promise<Run[]>
/** Seeks every paused transition to `ms` and returns the computed opacity of `selector`. */
const seek = (ui: Harness, ms: number, selector: string) =>
  ui.frameDoc().evaluate(
    ([at, sel]) => {
      for (const a of document.getAnimations()) if (a instanceof CSSTransition) a.currentTime = at as number
      const el = document.querySelector(sel as string)
      return el ? Number(getComputedStyle(el).opacity) : NaN
    },
    [ms, selector] as const,
  )
const finish = (ui: Harness) => ui.frameDoc().evaluate(() => document.getAnimations().forEach((a) => a.finish()))
const runningCount = (ui: Harness) => ui.frameDoc().evaluate(() => document.getAnimations().filter((a) => a.playState === 'running').length)

test('pill: copy link and site controls fade in over 150 ms ease-out, the host narrowing with them, once per enter', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const copy = nav.locator('[data-shell="copy-url"]')
  const host = nav.locator('[data-shell="url-host"]')
  await onPage(ui)
  await expect(copy).toHaveCSS('opacity', '0')
  await expect(host).toHaveCSS('padding-right', '0px')
  await expect.poll(() => runningCount(ui)).toBe(0)
  const textWidth = () => host.evaluate((el) => el.getBoundingClientRect().width - parseFloat(getComputedStyle(el).paddingRight))
  const wide = await textWidth()

  await nav.locator('.clu-tab-list').hover({ position: { x: 20, y: 5 } })
  const runs = (await pauseAll(ui)).filter((r) => /clu-url-(action|slot|host)/.test(r.el))
  // Opacity and scale on both buttons, the padding on the host: each once, 150 ms, ease-out.
  const props = runs.map((r) => `${r.el.includes('clu-url-host') ? 'host' : r.el.includes('clu-url-slot') ? 'slot' : 'copy'}:${r.prop}`).sort()
  expect(props).toEqual(['copy:opacity', 'copy:scale', 'host:padding-right', 'slot:opacity', 'slot:scale'])
  for (const r of runs) {
    expect(r.ms).toBe(150)
    expect(r.ease).toBe('cubic-bezier(0.2, 0, 0, 1)')
  }
  // Halfway it is part way in, never a one-frame pop.
  const mid = await seek(ui, 75, '[data-shell="copy-url"]')
  expect(mid).toBeGreaterThan(0.3)
  expect(mid).toBeLessThan(1)
  await ui.shot('pill-reveal-mid')
  await finish(ui)
  await expect(copy).toHaveCSS('opacity', '1')
  await expect(host).toHaveCSS('padding-right', '58.5px')
  // The host's text box ends where the buttons begin: 58.5 narrower than at rest.
  expect(wide - (await textWidth())).toBeCloseTo(58.5, 1)
  // A pointer move inside the sidebar starts nothing again.
  await nav.locator('.clu-tab-list').hover({ position: { x: 40, y: 8 } })
  expect(await runningCount(ui)).toBe(0)
  await ui.shot('pill-reveal-in')
})

test('pill: leaving fades out over 150 ms ease-in, and turning back mid-fade reverses from where it is', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const copy = nav.locator('[data-shell="copy-url"]')
  await nav.locator('.clu-tab-list').hover({ position: { x: 20, y: 5 } })
  await expect(copy).toHaveCSS('opacity', '1')
  await onPage(ui)
  const out = (await pauseAll(ui)).filter((r) => r.el.includes('clu-url-action'))
  expect(out.map((r) => r.prop).sort()).toEqual(['opacity', 'scale'])
  for (const r of out) expect(r.ease).toBe('cubic-bezier(0.4, 0, 1, 1)')
  const at = await seek(ui, 90, '[data-shell="copy-url"]')
  expect(at).toBeGreaterThan(0.05)
  expect(at).toBeLessThan(0.95)
  // Back in: the new fade starts at the drawn opacity, not at 0 or 1.
  await nav.locator('.clu-tab-list').hover({ position: { x: 20, y: 5 } })
  await pauseAll(ui)
  const from = await seek(ui, 0, '[data-shell="copy-url"]')
  expect(Math.abs(from - at)).toBeLessThan(0.1)
  await finish(ui)
  await expect(copy).toHaveCSS('opacity', '1')
})

test('space header: the chevron and "…" fade in and out with the row hover', async ({ ui }) => {
  const header = ui.frame.locator('nav.clu-sidebar .clu-space-header').first()
  const more = header.locator('.clu-space-header-more')
  await onPage(ui)
  await expect(more).toHaveCSS('opacity', '0')
  await header.hover()
  const runs = (await pauseAll(ui)).filter((r) => r.el.includes('clu-space-header-more') && r.prop === 'opacity')
  expect(runs).toHaveLength(1)
  expect(runs.map((r) => r.ms)).toEqual([150])
  await finish(ui)
  await expect(more).toHaveCSS('opacity', '1')
  await expect(header.locator('.clu-space-header-toggle')).toHaveCSS('opacity', '1')
  await ui.shot('space-header-reveal')
  await onPage(ui)
  expect((await pauseAll(ui)).filter((r) => r.el.includes('clu-space-header-more') && r.prop === 'opacity')).toHaveLength(1)
  await finish(ui)
  await expect(more).toHaveCSS('opacity', '0')
})

// The folder glyph stays as it is on hover (no dotted menu glyph), so it has nothing to reveal.
test('folder row: the glyph stays still on hover', async ({ ui }) => {
  const row = ui.frame.locator(`.clu-folder-row[data-folder-id="${FOLDER}"]`)
  await onPage(ui)
  await row.hover()
  expect((await pauseAll(ui)).filter((r) => r.el.includes('clu-folder-glyph'))).toHaveLength(0)
  await finish(ui)
  await expect(row.locator('.clu-folder-glyph svg')).toHaveCount(1)
})

test('reduced motion: the reveals switch at once', async ({ ui }) => {
  await ui.frameDoc().evaluate(() => (document.documentElement.dataset.reducedMotion = 'true'))
  const nav = ui.frame.locator('nav.clu-sidebar')
  await onPage(ui)
  await nav.locator('.clu-tab-list').hover({ position: { x: 20, y: 5 } })
  await expect(nav.locator('[data-shell="copy-url"]')).toHaveCSS('opacity', '1')
  expect(await nav.locator('[data-shell="copy-url"]').evaluate((el) => getComputedStyle(el).transitionDuration)).toMatch(/^0s, 0s/)
})
