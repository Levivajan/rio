// The ui:fake harness fixture shared by the WebUI specs: one headless Chromium per test (macOS runs it with
// --single-process, which cannot open a second context), the frame and overlay WebUIs, screenshots under
// $CLU_SHOTS_DIR/<label>, and a check after each test that nothing hit a console error or a call the browser's guard
// would reject.
import { mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { expect, test as base, type Frame, type FrameLocator, type Page } from '@playwright/test'
import type { CluState } from '../../src/shared/state'

export const WINDOW = '00000000-0000-4000-8000-000100000001'
export const tab = (n: number) => `00000000-0000-4000-8000-0005${n.toString(16).padStart(8, '0')}`

export interface Harness {
  page: Page
  frame: FrameLocator
  overlay: FrameLocator
  /** The frame WebUI's document, for window.clu calls. */
  frameDoc(): Frame
  shot(name: string): Promise<void>
}

type HostCall = { name: string; result: { ok: boolean; error?: { code: string } } }

export function harnessTest(label: string) {
  const shots = process.env.CLU_SHOTS_DIR ? join(process.env.CLU_SHOTS_DIR, label) : resolve('test-results/webui-shots', label)
  return base.extend<{ ui: Harness; errors: string[] }>({
    page: async ({ playwright, launchOptions, baseURL, viewport, deviceScaleFactor, bypassCSP }, use) => {
      const browser = await playwright.chromium.launch({ ...launchOptions, headless: true })
      const context = await browser.newContext({ baseURL, viewport, deviceScaleFactor, bypassCSP })
      await use(await context.newPage())
      await browser.close()
    },
    errors: async ({ page }, use) => {
      const errors: string[] = []
      page.on('console', (m) => {
        if (m.type() === 'error') errors.push(`${m.location().url}: ${m.text()}`)
      })
      page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`))
      await use(errors)
    },
    ui: async ({ page, errors }, use) => {
      await page.goto('/')
      const frame = page.frameLocator('#frame')
      await expect(frame.locator('nav.clu-sidebar')).toBeVisible()
      const frameDoc = () => {
        const f = page.frame({ url: /\/frame\/index\.html/ })
        if (!f) throw new Error('frame WebUI missing')
        return f
      }
      await use({
        page,
        frame,
        overlay: page.frameLocator('#overlay'),
        frameDoc,
        async shot(name) {
          mkdirSync(shots, { recursive: true })
          await page.screenshot({ path: join(shots, `${name}.png`) })
        }
      })
      const rejected = await page.evaluate(() =>
        (window as unknown as { cluFakeHost: { calls: HostCall[] } }).cluFakeHost.calls
          .filter((c) => !c.result.ok && c.result.error?.code !== 'not-implemented')
          .map((c) => `${c.name}: ${c.result.error?.code}`)
      )
      expect(rejected, 'calls the browser guard would reject').toEqual([])
      expect(errors, 'console errors, CSP or Trusted Types violations').toEqual([])
    }
  })
}

export function shotsDir(label: string): string {
  return process.env.CLU_SHOTS_DIR ? join(process.env.CLU_SHOTS_DIR, label) : resolve('test-results/webui-shots', label)
}

export const state = (ui: Harness): Promise<CluState> =>
  ui.frameDoc().evaluate(async () => {
    const r = await window.clu.state.get()
    if (!r.ok) throw new Error('state.get failed')
    return r.value
  })

export { expect }
