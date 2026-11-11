// The command bar on the fake seed at 1280 x 800:
// the empty bar's favorites and Contact the Team, contextual rows with path chips and the keycap, Cmd+L's rows, the
// five-row cap and the type. Stills go to $CLU_SHOTS_DIR/g-command-bar/.
import { expect, harnessTest, type Harness } from './fixture'

const test = harnessTest('g-command-bar')
const rows = (ui: Harness) => ui.overlay.getByRole('option')

async function openBar(ui: Harness) {
  await ui.page.locator('#frame').focus()
  await ui.page.keyboard.press('Meta+t')
  const input = ui.overlay.getByRole('combobox')
  await expect(input).toBeFocused()
  return input
}

test('empty Cmd+T: favorites as title-only Switch to Tab rows, then Contact the Team', async ({ ui }) => {
  const input = await openBar(ui)
  await expect(input).toHaveAttribute('placeholder', 'Search or Enter URL...')
  await expect(input).toHaveCSS('font-weight', '500')
  await expect(rows(ui)).toHaveCount(5)
  await expect(rows(ui).first()).toHaveAttribute('data-kind', 'tab')
  await expect(rows(ui).first().locator('.clu-cmd-title')).toHaveText('Inbox (3)')
  await expect(rows(ui).first().locator('.clu-cmd-url')).toHaveCount(0)
  await expect(rows(ui).first().locator('.clu-cmd-text')).toHaveCSS('font-weight', '400')
  await expect(rows(ui).last()).toHaveAttribute('data-group', 'help')
  await expect(rows(ui).last()).toHaveText('Contact the Team')
  // The empty field keeps the magnifier.
  await expect(ui.overlay.locator('.clu-cmd-field-icon')).not.toHaveAttribute('data-follows', /./)
  // The list never scrolls.
  await expect(ui.overlay.locator('.clu-cmd-list')).toHaveCSS('overflow-y', 'hidden')
  await ui.page.mouse.move(1279, 799)
  await ui.shot('empty')
})

test('contextual rows: Pin to the space with the Cmd+D keycap, Open and Pin to a folder with path chips', async ({ ui }) => {
  const input = await openBar(ui)
  await input.pressSequentially('home')
  const pin = rows(ui).first()
  await expect(pin).toHaveAttribute('data-kind', 'command')
  await expect(pin.locator('.clu-cmd-title')).toHaveText('Pin to')
  await expect(pin.locator('.clu-cmd-path')).toHaveText('Home')
  await expect(pin.locator('.clu-cmd-keys')).toHaveCSS('background-color', 'rgb(255, 255, 255)')
  await expect(pin.locator('.clu-cmd-keys')).toHaveCSS('border-radius', '5px')
  await expect(rows(ui).nth(1)).toHaveAttribute('data-kind', 'search')
  await ui.page.mouse.move(1279, 799)
  await ui.shot('context-home')

  await input.fill('')
  await input.pressSequentially('chromium m')
  await expect(ui.overlay.locator('.clu-cmd-path', { hasText: 'Home / Chromium M2' })).toHaveCount(2)
  // After a tab top row the typed search stays second; the Ask row and the contextual rows follow.
  await expect(rows(ui).nth(1)).toHaveAttribute('data-kind', 'search')
  expect(await rows(ui).count()).toBeLessThanOrEqual(5)
  await ui.shot('context-folder')
})

test('typed: the plain search comes second after a tab, generic actions stay out of short queries; Cmd+L lists the tab and Contact the Team', async ({ ui }) => {
  const input = await openBar(ui)
  await input.pressSequentially('mojo')
  await expect(rows(ui).first()).toHaveAttribute('data-kind', 'tab')
  await expect(rows(ui).nth(1)).toHaveAttribute('data-kind', 'search')
  await expect(ui.overlay.locator('.clu-cmd-field-icon')).toHaveAttribute('data-follows', 'tab')
  await input.press('ArrowDown')
  await expect(ui.overlay.locator('.clu-cmd-field-icon')).toHaveAttribute('data-follows', 'search')
  // The selected search row's magnifier is full white.
  await expect(rows(ui).nth(1).locator('.clu-cmd-icon')).toHaveCSS('color', 'rgb(255, 255, 255)')
  await input.fill('')
  await input.pressSequentially('wi')
  await expect(rows(ui).first()).toBeVisible()
  await expect(ui.overlay.locator('.clu-cmd-row[data-group="actions"]')).toHaveCount(0)
  await ui.shot('typed')

  await ui.page.keyboard.press('Meta+l')
  await expect(ui.overlay.locator('.clu-cmd')).toHaveAttribute('data-command-mode', 'edit-url')
  await expect(rows(ui).first()).toHaveAttribute('data-kind', 'reload')
  await expect(rows(ui).first().locator('.clu-cmd-url')).toHaveCount(0)
  await expect(rows(ui).last()).toHaveText('Contact the Team')
  expect(await rows(ui).count()).toBeLessThanOrEqual(5)
  await ui.page.mouse.move(1279, 799)
  await ui.shot('edit-url')
})
