// The frame and overlay WebUI bundles in headless Chromium over the FakeBridge (npm run ui:fake), under the WebUI CSP.
// Each test fails on any console error (CSP and Trusted Types violations print there), page error, or a call the
// browser's guard would reject. Screenshots go to $CLU_SHOTS_DIR/chromium-m2-ui (test-results/webui-shots without it).
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { join } from 'node:path'
import type { Frame } from '@playwright/test'
import { expect, harnessTest, shotsDir, state, tab, WINDOW } from './fixture'

const SHOTS = shotsDir('chromium-m2-ui')
const test = harnessTest('chromium-m2-ui')

test('sidebar: seeded spaces, pinned folder and Today rows; a click activates a tab, a switcher click a space', async ({ ui }) => {
  const nav = ui.frame.locator('nav.clu-sidebar')
  await expect(nav.locator(`[data-tab-id="${tab(21)}"]`)).toHaveAttribute('data-active', 'true')
  await expect(nav.getByText('Chromium M2')).toBeVisible()
  await expect(nav.locator(`[data-tab-id="${tab(24)}"]`)).toHaveAttribute('data-muted', 'true')
  await ui.shot('sidebar')

  await nav.getByText('Morning headlines').click()
  await expect(nav.locator(`[data-tab-id="${tab(20)}"]`)).toHaveAttribute('data-active', 'true')
  expect((await state(ui)).windows[WINDOW]?.activeTabId).toBe(tab(20))

  await nav.getByRole('group', { name: 'Spaces' }).getByRole('button', { name: 'Work' }).click()
  await expect(nav).toHaveAttribute('data-active-space', '00000000-0000-4000-8000-000300000002')
  await expect(nav.getByText('Q4 plan')).toBeVisible()
  await expect(nav.locator('.clu-space-slider')).toHaveAttribute('data-state', 'idle')
  await expect(nav.locator('[data-outgoing="true"]')).toHaveCount(0)
  await expect(ui.frame.locator('[data-shell="url-host"]')).toHaveText('sheets.example.com')
  await ui.shot('sidebar-work-space')
})

test('command bar: Cmd+T opens it over the page, typing lists history and open tabs, Enter goes there', async ({ ui }) => {
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const bar = ui.overlay.locator('[data-surface="command"]')
  const input = bar.getByRole('combobox', { name: 'Search or enter URL' })
  await expect(input).toBeFocused()
  await ui.shot('command-bar-empty')

  await input.pressSequentially('mojo')
  await expect(bar.getByRole('option').filter({ hasText: 'Mojo bindings in WebUI' }).first()).toBeVisible()
  await ui.shot('command-bar')

  await input.press('Enter')
  await expect(bar).toHaveCount(0)
  const s = await state(ui)
  expect(s.windows[WINDOW]?.command).toBeNull()
  expect(s.windows[WINDOW]?.activeTabId).not.toBe(tab(21))
})

test('row menu: right-click opens the menu in the overlay, Move To opens its submenu, Archive Tab closes the tab', async ({ ui }) => {
  const row = ui.frame.locator(`[data-tab-id="${tab(24)}"]`)
  await row.click({ button: 'right' })
  const menu = ui.overlay.locator('[data-surface="menu"]').getByRole('menu').first()
  // A Today row's menu ends with the archive items; it has no Close (the row's x and Cmd+W close it).
  await expect(menu.getByRole('menuitem', { name: 'Archive Tab', exact: true })).toBeVisible()
  await ui.shot('row-menu')

  await menu.getByRole('menuitem', { name: 'Move To' }).hover()
  await expect(ui.overlay.getByRole('menuitem', { name: 'Work' })).toBeVisible()
  await ui.shot('row-menu-submenu')

  await ui.page.keyboard.press('Escape')
  await ui.page.keyboard.press('Escape')
  await expect(ui.overlay.locator('[data-surface="menu"]')).toHaveCount(0)

  await row.click({ button: 'right' })
  await ui.overlay.getByRole('menuitem', { name: 'Archive Tab', exact: true }).click()
  await expect(row).toHaveCount(0)
})

test('Cmd+S hides the sidebar and gives the page the window; again brings it back', async ({ ui }) => {
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+s')
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.layout.pageRect.x).toBe(10)
  await ui.shot('sidebar-hidden')
  await ui.page.keyboard.press('Meta+s')
  await expect.poll(async () => (await state(ui)).windows[WINDOW]?.shell.sidebar.hidden).toBe(false)
})

test('dark appearance', async ({ ui }) => {
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
  // The row background eases over --t-base; shoot once the active card has its dark color.
  await expect(ui.frame.locator('.clu-row[data-active="true"]')).toHaveCSS('background-color', 'rgba(250, 251, 255, 0.2)')
  await ui.shot('sidebar-dark')
})

// axe-core needs to inject a script, which the CSP forbids, so this context bypasses CSP (the other tests keep it on).
test.describe('accessibility', () => {
  test.use({ bypassCSP: true })

  const axeSource = readFileSync(createRequire(import.meta.url).resolve('axe-core/axe.min.js'), 'utf8')
  type AxeViolation = { id: string; impact: string | null; help: string; nodes: { target: string[] }[] }
  // These secondary labels take lighter grays; all other text keeps 4.5:1.
  const SECONDARY_LABELS = ".clu-space-header-name, .clu-new-tab, .clu-url-host[data-empty='true'], .clu-today-clear"
  async function audit(doc: Frame, include?: string): Promise<AxeViolation[]> {
    await doc.addScriptTag({ content: axeSource })
    return doc.evaluate(
      async ([sel, secondary]) => {
        const axe = (window as unknown as { axe: { run(ctx: unknown, opts: unknown): Promise<{ violations: AxeViolation[]; passes: unknown[] }> } }).axe
        const r = await axe.run(sel ? { include: [sel] } : document, { runOnly: ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa'] })
        const exempt = (target: string[]) => target.length === 1 && document.querySelector(target[0] ?? '')?.closest(secondary) != null
        return r.violations
          .map((v) => ({ id: v.id, impact: v.impact, help: v.help, nodes: v.nodes.filter((n) => v.id !== 'color-contrast' || !exempt(n.target)).map((n) => ({ target: n.target })) }))
          .filter((v) => v.nodes.length > 0)
          .concat(r.passes.length ? [] : [{ id: 'no-rules-ran', impact: 'critical', help: 'axe found no rules to run', nodes: [] }])
      },
      [include, SECONDARY_LABELS] as const
    )
  }

  test('frame, command bar and row menu have no serious or critical WCAG 2.1 AA violations', async ({ ui }) => {
    const overlayDoc = () => {
      const f = ui.page.frame({ url: /\/overlay\/index\.html\?layer=top/ })
      if (!f) throw new Error('overlay WebUI missing')
      return f
    }
    const report: Record<string, AxeViolation[]> = {}
    report.frame = await audit(ui.frameDoc())

    await ui.page.locator('#frame').focus()
    await ui.page.keyboard.press('Meta+t')
    await expect(ui.overlay.getByRole('combobox')).toBeFocused()
    await ui.overlay.getByRole('combobox').pressSequentially('wiki')
    await expect(ui.overlay.getByRole('option').first()).toBeVisible()
    report.commandBar = await audit(overlayDoc(), '[data-surface="command"]')
    await ui.page.keyboard.press('Escape')
    await expect(ui.overlay.locator('[data-surface="command"]')).toHaveCount(0)

    await ui.frame.locator(`[data-tab-id="${tab(20)}"]`).click({ button: 'right' })
    await expect(ui.overlay.getByRole('menu').first()).toBeVisible()
    report.rowMenu = await audit(overlayDoc(), '[data-surface="menu"]')

    mkdirSync(SHOTS, { recursive: true })
    writeFileSync(join(SHOTS, 'a11y.json'), `${JSON.stringify(report, null, 2)}\n`)
    // The command bar's colors are fixed by design:
    // white on the space accent in the selected row and the 33% secondary text (the Ask row's "Ask ChatGPT" too) are
    // under 4.5:1 there too. Only those nodes may fail color-contrast; any other node, or any other rule, still blocks.
    const REFERENCE_COLORS = [/\.clu-cmd-url/, /\.clu-cmd-badge/, /\.clu-cmd-ask/, /^#[\w-]+-\d+ > \.clu-cmd-text > \.clu-cmd-title$/]
    const copied = (v: AxeViolation & { nodes?: { target: string[] }[] }) =>
      v.id === 'color-contrast' && (v.nodes ?? []).every((n) => n.target.some((t) => REFERENCE_COLORS.some((r) => r.test(t))))
    const blocking = Object.entries(report).flatMap(([where, vs]) =>
      vs.filter((v) => !(where === 'commandBar' && copied(v))).filter((v) => v.impact === 'serious' || v.impact === 'critical').map((v) => `${where}: ${v.id} (${v.impact}) ${v.help} at ${v.nodes.map((n) => n.target.join(' ')).join(', ')}`)
    )
    expect(blocking).toEqual([])
  })
})
