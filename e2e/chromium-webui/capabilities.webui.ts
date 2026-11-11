// M9 in the frame WebUI over the FakeBridge (docs/chromium/m9.md): downloads live in the Library (no bottom-bar
// button), and Cmd+Shift+J asks the browser for Chromium's downloads bubble. The bubble itself, permission prompts, passwords and autofill are Chromium's native UI, covered by
// clu_browsertests CluNative*. Screenshots go to $CLU_SHOTS_DIR/chromium-m9-ui.
import { expect, harnessTest, state } from './fixture'

const test = harnessTest('chromium-m9-ui')

type HostCall = { name: string; payload: unknown }
const hostCalls = (ui: { page: import('@playwright/test').Page }, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: HostCall[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)


test('downloads: no button joins the bottom bar while files run; they live in the Library, and the bubble still opens', async ({ ui }) => {
  await ui.page.goto('/?downloads=1')
  await expect.poll(async () => (await state(ui)).downloads.length).toBeGreaterThan(0)
  const footer = ui.frame.locator('nav.clu-sidebar footer.clu-footer')
  await expect(footer.getByRole('button', { name: 'Downloads' })).toHaveCount(0)
  await expect(footer.getByRole('button')).toHaveCount(2 + (await state(ui)).spaceOrder.length)
  await ui.shot('sidebar-downloads-library-only')
  // The bubble opens from the Library's Downloads section and Cmd+Shift+J (clu_browsertests CluNative*).
  await ui.frameDoc().evaluate(() => window.clu.download.openPopover({}))
  await expect.poll(() => hostCalls(ui, 'download.openPopover')).toEqual([{}])
})
