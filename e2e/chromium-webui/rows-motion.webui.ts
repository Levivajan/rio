// Sidebar row motion: hover, the row's close,
// press, the bottom bar's buttons, and nothing running at rest or after a state echo.
import { expect, harnessTest, WINDOW, type Harness } from './fixture'

const test = harnessTest('rows-motion')

const REST_ROW = '.clu-tab-tree .clu-tab-row[data-active="false"]'
const ms = (durations: string) => durations.split(',').map((d) => Math.round(parseFloat(d) * 1000))
const running = (ui: Harness) =>
  ui.frameDoc().evaluate(() => document.getAnimations().filter((a) => a.playState === 'running').map((a) => (a as CSSAnimation).animationName ?? (a as CSSTransition).transitionProperty))
const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

test('row hover: the fill and the close come in the same frame, the fill leaves over 33 ms; the close box fades in over 85 ms', async ({ ui }) => {
  const id = await ui.frame.locator(REST_ROW).first().getAttribute('data-tab-id')
  const row = ui.frame.locator(`.clu-tab-row[data-tab-id="${id}"]`)
  const close = row.locator('.clu-row-close')
  // Rest: the fill leaves over --t-hover-out (gone in two).
  expect(ms(await row.evaluate((el) => getComputedStyle(el).transitionDuration))).toEqual([33, 75])
  await row.hover()
  // Hovered: the fill and the close show at once; the close's own hover box fades over 85 ms.
  expect(ms(await row.evaluate((el) => getComputedStyle(el).transitionDuration))).toEqual([0, 75])
  await expect(close).toHaveCSS('opacity', '1')
  expect(await close.evaluate((el) => getComputedStyle(el).transitionProperty)).toBe('background-color')
  expect(ms(await close.evaluate((el) => getComputedStyle(el).transitionDuration))).toEqual([85])
  await ui.shot('row-hover')
})

test('row press: the row shrinks to 0.985 over 75 ms ease-out while held and selects on the press', async ({ ui }) => {
  const id = await ui.frame.locator(REST_ROW).first().getAttribute('data-tab-id')
  const row = ui.frame.locator(`.clu-tab-row[data-tab-id="${id}"]`)
  await row.hover()
  await ui.page.mouse.down()
  await expect(row).toHaveAttribute('data-active', 'true')
  await expect.poll(() => row.evaluate((el) => getComputedStyle(el).transform)).toMatch(/^matrix\(0\.985, 0, 0, 0\.985/)
  expect(await row.evaluate((el) => getComputedStyle(el).transitionTimingFunction)).toContain('cubic-bezier(0.2, 0, 0, 1)')
  await ui.page.mouse.up()
})

test('bottom bar buttons: the hover box fades over 100 ms', async ({ ui }) => {
  const button = ui.frame.locator('.clu-footer .clu-icon-button').first()
  expect(ms(await button.evaluate((el) => getComputedStyle(el).transitionDuration))).toEqual([100])
})

test('nothing animates at rest, nor after a state echo or a re-render', async ({ ui }) => {
  await expect.poll(() => running(ui)).toEqual([])
  // A state echo: the browser republishes the window with an unrelated change; no row, tooltip or bar replays.
  await host(ui, ['windows', WINDOW, 'focused'], false)
  await host(ui, ['windows', WINDOW, 'focused'], true)
  expect(await running(ui)).toEqual([])
  await ui.page.mouse.move(600, 400)
  await expect.poll(() => running(ui)).toEqual([])
})
