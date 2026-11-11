// Motion of pinning, unpinning, Clear and row moves: each motion
// runs once per trigger, nothing animates at rest, and Clear's faded rows never show again before they go.
import { expect, harnessTest, type Harness } from './fixture'

const test = harnessTest('pin-folders-motion')

type Logged = { tab: string | null; id: string; ms: unknown }

/** Logs every Element.animate call in the frame WebUI from now on. */
const logAnimations = (ui: Harness) =>
  ui.frameDoc().evaluate(() => {
    const log: Logged[] = []
    ;(window as unknown as { cluAnimLog: Logged[] }).cluAnimLog = log
    const original = Element.prototype.animate
    Element.prototype.animate = function (this: Element, keyframes: Keyframe[] | PropertyIndexedKeyframes | null, options?: number | KeyframeAnimationOptions) {
      const o = typeof options === 'object' ? options : {}
      log.push({ tab: this.getAttribute('data-tab-id'), id: o.id ?? '', ms: o.duration })
      return original.call(this, keyframes, options)
    }
  })
const animLog = (ui: Harness) => ui.frameDoc().evaluate(() => (window as unknown as { cluAnimLog: Logged[] }).cluAnimLog)
/** Finite animations running in the sidebar (an equalizer that loops while audio plays is not motion at rest). */
const running = (ui: Harness) =>
  ui.frame.locator('nav.clu-sidebar').evaluate((nav) =>
    nav.ownerDocument
      .getAnimations()
      .filter((a) => a.playState === 'running' && a.effect?.getComputedTiming().iterations !== Infinity)
      .filter((a) => {
        const t = (a.effect as KeyframeEffect | null)?.target
        return t instanceof Element && nav.contains(t)
      })
      .map((a) => (a instanceof CSSAnimation ? a.animationName : a.id || 'script'))
  )

test('nothing in the sidebar animates at rest', async ({ ui }) => {
  await expect.poll(() => running(ui)).toEqual([])
  await ui.page.mouse.move(100, 600)
  await ui.page.mouse.move(100, 640)
  await expect.poll(() => running(ui)).toEqual([])
})

test('a pinned row slides once in 110 ms, its favicon does not fade again, and a state echo starts nothing', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await expect.poll(() => running(ui)).toEqual([])
  const row = nav.locator('.clu-new-tab ~ .clu-tab-row[data-tab-id]').first()
  const id = await row.getAttribute('data-tab-id')
  if (!id) throw new Error('no Today row')
  const space = await ui.frameDoc().evaluate(async () => {
    const r = await window.clu.state.get()
    if (!r.ok) throw new Error('state')
    return Object.values(r.value.windows)[0]?.activeSpaceId ?? ''
  })
  await logAnimations(ui)
  await ui.frameDoc().evaluate(([tabId, spaceId]) => window.clu.tabs.move({ nodeId: tabId as string, target: { section: 'pinned', spaceId: spaceId as string, parentId: null, index: 0 } }), [id, space])
  await expect(nav.locator(`.clu-new-tab ~ .clu-tab-row[data-tab-id="${id}"]`)).toHaveCount(0)
  expect(await running(ui)).not.toContain('clu-fade-in')
  await expect.poll(async () => (await animLog(ui)).filter((a) => a.tab === id)).toEqual([{ tab: id, id: 'clu-row-flip', ms: 110 }])
  // A re-render that moves nothing (another tab's rename) starts no motion on the pinned row.
  const other = await nav.locator('.clu-tab-row[data-tab-id]').last().getAttribute('data-tab-id')
  await ui.frameDoc().evaluate((tabId) => window.clu.tabs.rename({ nodeId: tabId as string, title: 'Echo' }), other)
  await expect(nav.locator(`.clu-tab-row[data-tab-id="${other}"] .clu-row-title`)).toHaveText('Echo')
  expect((await animLog(ui)).filter((a) => a.tab === id)).toHaveLength(1)
  await expect.poll(() => running(ui)).toEqual([])
})

test('Clear starts at once, and its rows never show again before they go', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  const rows = nav.locator('.clu-new-tab ~ .clu-tab-row[data-tab-id]')
  await expect(rows.first()).toBeVisible()
  await ui.page.mouse.move(100, 600)
  const clear = nav.locator('[data-clear-today]')
  await expect(clear).toHaveCSS('opacity', '1')
  // Samples the Today rows' opacity every frame from the click until they are gone.
  await ui.frameDoc().evaluate(() => {
    const samples: number[] = []
    ;(window as unknown as { cluClearSamples: number[] }).cluClearSamples = samples
    const tick = () => {
      const left = [...document.querySelectorAll('.clu-new-tab ~ .clu-tab-row[data-tab-id]')]
      if (left.length === 0) return
      samples.push(Math.max(...left.map((el) => Number(getComputedStyle(el).opacity))))
      requestAnimationFrame(tick)
    }
    document.querySelector<HTMLElement>('[data-clear-today]')?.addEventListener('click', () => requestAnimationFrame(tick), { once: true })
  })
  await clear.click()
  await expect(rows).toHaveCount(0)
  const samples = await ui.frameDoc().evaluate(() => (window as unknown as { cluClearSamples: number[] }).cluClearSamples)
  // The move starts in the click's first frame (no fade phase first) and nothing comes back while it runs.
  expect(samples.length, `opacity per frame: ${samples.join(', ')}`).toBeLessThanOrEqual(6)
  expect(samples.every((o, i) => i === 0 || o <= (samples[i - 1] ?? 1)), `opacity per frame: ${samples.join(', ')}`).toBe(true)
  await expect(nav.locator('.clu-row-gone')).toHaveCount(0)
})

test('a collapsing folder has no chevron and its collapsing children keep only their favicons', async ({ ui }) => {
  const row = ui.frame.locator('.clu-folder-row[data-folder-id="00000000-0000-4000-8000-000400000001"]')
  await expect(row.locator('.clu-folder-chevron')).toHaveCount(0)
  await row.hover()
  await row.getByRole('button', { name: 'Collapse folder' }).click()
  const clip = ui.frame.locator('.clu-row-collapse-clip')
  await expect(clip).toHaveCount(1)
  expect(await clip.locator('.clu-row-title').evaluateAll((els) => els.map((el) => getComputedStyle(el).visibility))).not.toContain('visible')
  await expect(clip).toHaveCount(0)
  await expect.poll(() => running(ui)).toEqual([])
})
