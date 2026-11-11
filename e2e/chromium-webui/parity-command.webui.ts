// The command bar's Ask row and Shift+Enter, in light and in
// dark at 1280 x 800, 2x. Screenshots go to $CLU_SHOTS_DIR/parity/command/.
// Behavior is pinned through the fake host: the saved setting and the opened address.
import { mkdirSync } from 'node:fs'
import { join } from 'node:path'
import { expect, harnessTest, shotsDir, state, WINDOW, type Harness } from './fixture'

const LABEL = 'parity/command'
const test = harnessTest(LABEL)
const SHOTS = shotsDir(LABEL)
const HOME = '00000000-0000-4000-8000-000300000001'
/** The selection red of a dark red space at the selected row. */
const SPACE_RED = '#9b3a45'
type Mode = 'light' | 'dark'

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)
const hostCalls = (ui: Harness, name: string) =>
  ui.page.evaluate((n) => (window as unknown as { cluFakeHost: { calls: { name: string; payload: unknown }[] } }).cluFakeHost.calls.filter((c) => c.name === n).map((c) => c.payload), name)

async function shot(ui: Harness, name: string, mode: Mode): Promise<void> {
  mkdirSync(SHOTS, { recursive: true })
  await ui.page.screenshot({ path: join(SHOTS, `${name}-${mode}.png`) })
}

async function setMode(ui: Harness, mode: Mode): Promise<void> {
  if (mode === 'light') return
  await ui.frameDoc().evaluate(() => window.clu.theme.setMode({ mode: 'dark' }))
  await expect.poll(async () => (await state(ui)).app.appearance.resolved).toBe('dark')
}

async function openBar(ui: Harness) {
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = ui.overlay.getByRole('combobox')
  await expect(input).toBeFocused()
  return input
}

const rows = (ui: Harness) => ui.overlay.getByRole('option')
const askRow = (ui: Harness) => ui.overlay.locator('.clu-cmd-row[data-group="ask"]')

for (const mode of ['light', 'dark'] as const) {
  test(`ask row (${mode}): third row with the picked assistant; the dropdown saves the pick; Enter asks it`, async ({ ui }) => {
    await setMode(ui, mode)
    if (mode === 'dark') await host(ui, ['spaces', HOME, 'accent'], SPACE_RED)
    const input = await openBar(ui)
    // The panel lets about 11% of the page through (89% of the panel color, frosted).
    const panel = ui.overlay.locator('.clu-cmd')
    await expect(panel).toHaveCSS('background-color', mode === 'dark' ? 'color(srgb 0.164706 0.164706 0.176471 / 0.89)' : 'color(srgb 0.972549 0.976471 0.976471 / 0.89)')
    await expect(panel).toHaveCSS('backdrop-filter', 'blur(30px) saturate(1.4)')
    await input.pressSequentially('mojo bindings in we')
    // The typed text is weight 500.
    await expect(input).toHaveCSS('font-weight', '500')
    await expect(input).toHaveCSS('font-size', '17px')
    // The order: the search row, one result, then the Ask row.
    await expect(rows(ui).nth(2)).toHaveAttribute('data-group', 'ask')
    await expect(askRow(ui)).toContainText('mojo bindings in we')
    await expect(askRow(ui).locator('[data-ask-picker]')).toHaveText('Ask ChatGPT')
    await expect(askRow(ui).locator('[data-glyph="chatgpt"]')).toBeVisible()
    // The open tab leads with its title alone, then the plain search for the typed text.
    await expect(rows(ui).first()).toHaveAttribute('data-kind', 'tab')
    await expect(rows(ui).first().locator('.clu-cmd-url')).toHaveCount(0)
    await expect(rows(ui).nth(1)).toHaveAttribute('data-kind', 'search')
    await expect(rows(ui).nth(1).locator('.clu-cmd-title')).toHaveCSS('color', mode === 'dark' ? 'rgba(255, 255, 255, 0.8)' : 'rgba(0, 0, 0, 0.8)')
    await ui.page.mouse.move(1279, 799)
    await shot(ui, 'ask-row', mode)

    // Down twice selects it in the space color; on the way the input shows the selected search.
    await input.press('ArrowDown')
    await expect(input).toHaveValue('mojo bindings in we')
    await expect(rows(ui).nth(1)).toHaveAttribute('aria-selected', 'true')
    await input.press('ArrowDown')
    await expect(input).toHaveValue('mojo bindings in we')
    await expect(askRow(ui)).toHaveAttribute('aria-selected', 'true')
    await expect(askRow(ui)).toHaveCSS('background-color', mode === 'dark' ? 'rgb(155, 58, 69)' : /rgb\(/)
    await shot(ui, 'ask-row-selected', mode)

    // The label opens the menu; picking Claude saves it and the row follows at once.
    await askRow(ui).locator('[data-ask-picker]').click()
    const menu = ui.overlay.getByRole('menu', { name: 'Assistant' })
    await expect(menu).toBeVisible()
    await expect(menu.getByRole('menuitemradio')).toHaveText(['Grok', 'ChatGPT', 'Claude'])
    await expect(menu.getByRole('menuitemradio', { name: 'ChatGPT' })).toHaveAttribute('aria-checked', 'true')
    await shot(ui, 'ask-picker', mode)
    await menu.getByRole('menuitemradio', { name: 'Claude' }).click()
    await expect(menu).toHaveCount(0)
    await expect(input).toBeFocused()
    await expect(askRow(ui).locator('[data-ask-picker]')).toHaveText('Ask Claude')
    await expect(askRow(ui).locator('[data-glyph="claude"]')).toBeVisible()
    await expect.poll(async () => (await state(ui)).settings.global['ai.askAssistant']).toBe('claude')
    expect(await hostCalls(ui, 'settings.set')).toContainEqual({ key: 'ai.askAssistant', scope: 'global', value: 'claude' })
    await shot(ui, 'ask-row-claude', mode)

    // Enter on the Ask row opens Claude with the text, URL-encoded.
    await expect(askRow(ui)).toHaveAttribute('aria-selected', 'true')
    await input.press('Enter')
    await expect(ui.overlay.locator('[data-surface="command"]')).toHaveCount(0)
    const s = await state(ui)
    const active = s.windows[WINDOW]?.activeTabId
    expect(active && s.tabs[active]?.url).toBe('https://claude.ai/new?q=mojo%20bindings%20in%20we')

    // The pick stays until changed: the next bar asks Claude too.
    const again = await openBar(ui)
    await again.pressSequentially('what is a mojom file')
    await expect(askRow(ui).locator('[data-ask-picker]')).toHaveText('Ask Claude')
  })

  test(`ask prompt (${mode}): an assistant's name and Tab open its prompt; Enter asks; Backspace leaves`, async ({ ui }) => {
    await setMode(ui, mode)
    if (mode === 'dark') await host(ui, ['spaces', HOME, 'accent'], SPACE_RED)
    const input = await openBar(ui)
    await input.pressSequentially('grok')
    const hint = ui.overlay.locator('[data-hint="ask"]')
    await expect(hint).toHaveText('Ask GrokTab')
    await shot(ui, 'ask-hint', mode)
    await input.press('Tab')
    await expect(ui.overlay.locator('.clu-cmd-chip[data-assistant="grok"]')).toHaveText('Grok')
    await expect(input).toHaveValue('')
    await expect(input).toHaveAttribute('placeholder', 'Ask anything…')
    await input.pressSequentially('why is the sky blue?')
    await expect(rows(ui)).toHaveCount(1)
    await expect(askRow(ui)).toHaveAttribute('aria-selected', 'true')
    await expect(askRow(ui).locator('[data-ask-picker]')).toHaveText('Ask Grok')
    await shot(ui, 'ask-prompt', mode)
    // Backspace on an empty prompt leaves it.
    await input.fill('')
    await input.press('Backspace')
    await expect(ui.overlay.locator('.clu-cmd-chip[data-assistant]')).toHaveCount(0)
    await input.pressSequentially('claude')
    await input.press('Tab')
    await input.pressSequentially('a & b = c?')
    await input.press('Enter')
    const s = await state(ui)
    const active = s.windows[WINDOW]?.activeTabId
    expect(active && s.tabs[active]?.url).toBe('https://claude.ai/new?q=a%20%26%20b%20%3D%20c%3F')
  })
}

test('Tab from the text jumps to the Ask row; Option+Down opens the menu; Escape closes the menu, then the bar', async ({ ui }) => {
  const input = await openBar(ui)
  // One word gets the Ask row too; Tab selects it.
  await input.pressSequentially('mojom')
  await expect(askRow(ui)).toHaveAttribute('aria-selected', 'false')
  await input.press('Tab')
  await expect(askRow(ui)).toHaveAttribute('aria-selected', 'true')
  await input.press('Alt+ArrowDown')
  const menu = ui.overlay.getByRole('menu', { name: 'Assistant' })
  await expect(menu).toBeVisible()
  await expect(menu.locator('[data-active]')).toHaveText('ChatGPT')
  await input.press('ArrowUp')
  await expect(menu.locator('[data-active]')).toHaveText('Grok')
  await input.press('Escape')
  await expect(menu).toHaveCount(0)
  await expect(ui.overlay.locator('[data-surface="command"]')).toHaveCount(1)
  await input.press('Alt+ArrowDown')
  await input.press('ArrowUp')
  await input.press('Enter')
  await expect(askRow(ui).locator('[data-ask-picker]')).toHaveText('Ask Grok')
  await expect.poll(async () => (await state(ui)).settings.global['ai.askAssistant']).toBe('grok')
  await input.press('Enter')
  const s = await state(ui)
  const active = s.windows[WINDOW]?.activeTabId
  expect(active && s.tabs[active]?.url).toBe('https://grok.com/?q=mojom')

  // An address never gets the Ask row, Tab or not.
  const next = await openBar(ui)
  await next.pressSequentially('example.com')
  await next.press('Tab')
  await expect(askRow(ui)).toHaveCount(0)
  await next.press('Escape')
  await expect(ui.overlay.locator('[data-surface="command"]')).toHaveCount(0)
})

test('the AI tab switch turns the Ask row off; Shift+Enter opens the first result of a search', async ({ ui }) => {
  await host(ui, ['settings', 'global', 'ai.askRow'], false)
  const input = await openBar(ui)
  await input.pressSequentially('weeknight soup ideas')
  await expect(rows(ui).first()).toBeVisible()
  await expect(askRow(ui)).toHaveCount(0)
  await input.press('Tab')
  await expect(askRow(ui)).toHaveCount(0)
  await input.press('Shift+Tab')
  await expect(rows(ui).first()).toHaveAttribute('aria-selected', 'true')
  await input.press('Shift+Enter')
  await expect(ui.overlay.locator('[data-surface="command"]')).toHaveCount(0)
  const s = await state(ui)
  const active = s.windows[WINDOW]?.activeTabId
  expect(active && s.tabs[active]?.url).toBe('https://www.google.com/search?q=weeknight+soup+ideas&btnI=1')

  // With Open Top Result off, Shift+Enter searches as Enter does.
  await host(ui, ['settings', 'global', 'ai.instantLinks'], false)
  const next = await openBar(ui)
  await next.pressSequentially('mojom codegen steps')
  await expect(rows(ui).first()).toHaveAttribute('data-kind', 'search')
  await next.press('Shift+Enter')
  await expect(ui.overlay.locator('[data-surface="command"]')).toHaveCount(0)
  const last = (await hostCalls(ui, 'command.execute')).at(-1) as { result: { kind: string; query?: string } }
  expect(last.result).toMatchObject({ kind: 'search', query: 'mojom codegen steps' })
})

for (const mode of ['light', 'dark'] as const) {
  test(`Tidy Today (${mode}): the broom shows past six Today tabs and puts one site's tabs together`, async ({ ui }) => {
    await setMode(ui, mode)
    const broom = ui.frame.locator('[data-tidy-today]')
    const space = (await state(ui)).windows[WINDOW]?.activeSpaceId ?? ''
    const todayUrls = async () => {
      const s = await state(ui)
      return (s.spaces[space]?.today ?? []).map((id) => s.tabs[id]?.url ?? '')
    }
    for (const url of ['https://github.com/clu/pull/7', 'https://weather.test/week', 'https://github.com/clu/pull/9', 'https://news.test/world']) {
      await ui.frameDoc().evaluate((u) => window.clu.tabs.create({ url: u, activate: false }), url)
    }
    await expect.poll(async () => (await todayUrls()).length).toBeGreaterThanOrEqual(7)
    await expect(broom).toHaveCount(1)
    await ui.frame.locator('nav.clu-sidebar').hover()
    await expect(broom).toBeVisible()
    await broom.hover()
    await shot(ui, 'tidy-today-before', mode)
    await broom.click()
    // Every site's tabs now sit together (a site: the host's last two labels).
    await expect
      .poll(async () => {
        const hosts = (await todayUrls()).map((u) => new URL(u).hostname.split('.').slice(-2).join('.'))
        return hosts.every((h, i) => hosts.indexOf(h) === i || hosts[i - 1] === h)
      })
      .toBe(true)
    await ui.page.mouse.move(900, 400)
    await shot(ui, 'tidy-today-after', mode)

    // The switch off hides the broom.
    await host(ui, ['settings', 'global', 'ai.tidyTabs'], false)
    await expect(broom).toHaveCount(0)
  })
}

// The Ask row shows for any text that is not an address, one word included.
test('ask row: one typed word gets it, an address does not', async ({ ui }) => {
  const input = await openBar(ui)
  await input.pressSequentially('whyistheskyblue')
  await expect(askRow(ui)).toContainText('whyistheskyblue')
  await expect(askRow(ui).locator('[data-ask-picker]')).toHaveText('Ask ChatGPT')
  await input.fill('')
  await input.pressSequentially('example.com')
  await expect(rows(ui).first()).toBeVisible()
  await expect(askRow(ui)).toHaveCount(0)
})

// A Rio past its first run offers the switch in the command bar by name.
test('import: "Import from Arc" is a command bar row', async ({ ui }) => {
  const input = await openBar(ui)
  await input.pressSequentially('import from arc')
  await expect(ui.overlay.locator('.clu-cmd-row[data-kind="command"]', { hasText: 'Import from Arc' })).toBeVisible()
})
