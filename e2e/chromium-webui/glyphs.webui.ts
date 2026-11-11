// Toolbar glyph extents: the sidebar toggle, back, forward, reload and the pill's copy link and sliders
// are drawn at fixed absolute sizes, within 0.5 pt. The extent is the
// drawn geometry plus its stroke, in CSS px, so the check follows the SVG and not an ink threshold. Nothing is scaled
// by the traffic lights: the glyphs are compared at absolute sizes.
import { expect, harnessTest, type Harness } from './fixture'

const test = harnessTest('glyphs')

/** The drawn width and height of the svg inside `selector`: its geometry's bounding box plus the stroke, in CSS px. */
const extent = (ui: Harness, selector: string, stroke: number) =>
  ui.frameDoc().evaluate(([sel, sw]) => {
    const svg = document.querySelector(`${sel} svg`)
    if (!(svg instanceof SVGSVGElement)) throw new Error(`no svg in ${sel}`)
    const b = svg.getBBox()
    const scale = svg.width.baseVal.value / svg.viewBox.baseVal.width
    return [(b.width + sw) * scale, (b.height + sw) * scale] as [number, number]
  }, [selector, stroke] as const)

const near = ([w, h]: [number, number], [ww, wh]: [number, number]) => {
  expect(Math.abs(w - ww), `width ${w} vs ${ww}`).toBeLessThanOrEqual(0.5)
  expect(Math.abs(h - wh), `height ${h} vs ${wh}`).toBeLessThanOrEqual(0.5)
}

test('the top-row and pill glyphs are drawn at the design\'s absolute sizes', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await expect(nav.locator('[data-shell="sidebar-toggle"] svg')).toBeVisible()
  near(await extent(ui, '[data-shell="sidebar-toggle"]', 1.5), [18.7, 14.2])
  near(await extent(ui, '[data-shell="nav-back"]', 1.6), [14.7, 11.6])
  near(await extent(ui, '[data-shell="nav-forward"]', 1.6), [14.7, 11.6])
  // Reload's arc reads 14 x 16.5 by geometry.
  near(await extent(ui, '[data-shell="nav-reload"]', 1.6), [14, 16.5])
  // Back and forward mirror each other.
  expect(await extent(ui, '[data-shell="nav-back"]', 1.6)).toEqual(await extent(ui, '[data-shell="nav-forward"]', 1.6))
  await ui.page.mouse.move(100, 600)
  await expect(nav.locator('[data-shell="copy-url"] svg')).toBeVisible()
  // The pill's link is 14.7 pt; its diagonal shape reads a little less tall than wide.
  expect(await ui.frameDoc().evaluate(() => document.querySelector('[data-shell="copy-url"] svg')?.getAttribute('width'))).toBe('16')
  const link = await extent(ui, '[data-shell="copy-url"]', 1.6)
  expect(Math.abs(link[0] - 14.7), `copy link width ${link[0]}`).toBeLessThanOrEqual(0.5)
  expect(link[1]).toBeLessThanOrEqual(14.7 + 0.5)
  const sliders = nav.locator('[data-site-controls-button] svg')
  if (await sliders.count()) near(await extent(ui, '[data-site-controls-button]', 1.4), [16.2, 16.2])
})
