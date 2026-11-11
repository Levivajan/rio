// In the ui:fake harness: the Library's Downloads (Rio's downloads and the Downloads folder) and
// Media (the screenshot folder) sections, and the Mini window toolbar fitting at 500, 700 and 900 with its tooltips
// whole. The folders are the fake host's (fake-seed.ts fakeLibraryFiles), never a real folder.
import { join } from 'node:path'
import { expect, harnessTest, shotsDir, state, WINDOW, type Harness } from './fixture'

const test = harnessTest('chromium-w-library')

const host = (ui: Harness, path: string[], value: unknown) =>
  ui.page.evaluate(([p, v]) => (window as unknown as { cluFakeHost: { edit(path: string[], value: unknown): void } }).cluFakeHost.edit(p as string[], v), [path, value] as const)

test('the Library lists the Downloads folder with Rio downloads, and Media lists the screenshot folder', async ({ ui }) => {
  await ui.page.goto('/?downloads=1')
  await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
  await ui.frame.locator('nav.clu-sidebar').getByRole('button', { name: 'View Library' }).click()
  await expect(ui.frame.getByRole('tab')).toHaveText(['Media', 'Downloads', 'Spaces', 'Site tweaks', 'Archived Tabs'])

  await ui.frame.getByRole('tab', { name: 'Downloads' }).click()
  const list = ui.frame.locator('.clu-library-downloads')
  await expect(list.getByText('Team offsite.key')).toBeVisible()
  await expect(list.getByText('logo-final.png')).toBeVisible()
  // invoice-2026-11.pdf is both a Rio download and a folder file: one row.
  await expect(list.getByText('invoice-2026-11.pdf')).toHaveCount(1)
  await expect(list.locator('.clu-section-label').first()).toBeVisible()
  await ui.shot('library-downloads')

  await list.getByRole('searchbox', { name: 'Search Downloads' }).fill('team')
  await expect(list.locator('[data-library-item]')).toHaveCount(1)
  await list.getByRole('searchbox', { name: 'Search Downloads' }).fill('')

  // The rail starts at the top: the first tile's center at 144. Filter sits 3.5 right of the field.
  const first = await ui.frame.getByRole('tab').first().boundingBox()
  expect((first?.y ?? 0) + (first?.height ?? 0) / 2).toBe(144)
  const field = await list.locator('.clu-archive-search-field').boundingBox()
  const filter = await list.getByRole('button', { name: 'Filter' }).boundingBox()
  expect(Math.round(((filter?.x ?? 0) - ((field?.x ?? 0) + (field?.width ?? 0))) * 2) / 2).toBe(3.5)
  expect(filter?.height).toBe(36)
  await list.getByRole('button', { name: 'Filter' }).click()
  await ui.shot('library-filter-menu')
  await ui.frame.getByRole('menuitem', { name: 'Images' }).click()
  await expect(list.locator('[data-library-item]')).toHaveCount(1)
  await expect(list.locator('[data-library-item] img')).toHaveCount(1)
  // Esc leaves the Library open.
  await ui.page.keyboard.press('Escape')
  await expect(ui.frame.getByRole('tab')).toHaveCount(5)

  // Spaces as tinted cards with header, pinned rows and foot, and Site tweaks.
  await ui.frame.getByRole('tab', { name: 'Spaces' }).click()
  const card = ui.frame.locator('.clu-library-spaces .clu-space-card').first()
  await expect(card).toBeVisible()
  await expect(card).toHaveCSS('width', '163px')
  await expect(card).toHaveCSS('background-image', /gradient/)
  await expect(card.getByRole('button', { name: 'Edit Theme Color' })).toBeVisible()
  await expect(card.getByRole('button', { name: 'Move Space' })).toBeVisible()
  await expect(card.getByRole('button', { name: 'Space options' })).toBeVisible()
  await expect(ui.frame.locator('.clu-library-spaces').getByRole('button', { name: 'New Space' })).toBeAttached()
  // The Library widens until every card and the "+" show, nothing to scroll sideways.
  await expect.poll(() => ui.frame.locator('.clu-space-cards').evaluate((row) => row.scrollWidth <= row.clientWidth + 1)).toBe(true)
  await expect(ui.frame.locator('.clu-library-spaces').getByRole('button', { name: 'New Space' })).toBeInViewport({ ratio: 1 })
  await ui.shot('library-spaces')
  await ui.frame.getByRole('tab', { name: 'Site tweaks' }).click()
  await expect(ui.frame.locator('.clu-library-tweaks [data-tweak-site]')).toHaveCount(2)
  await ui.shot('library-tweaks')

  await ui.frame.getByRole('tab', { name: 'Media' }).click()
  const media = ui.frame.locator('.clu-library-media')
  await expect(media.locator('.clu-library-tile')).toHaveCount(3)
  await media.getByRole('button', { name: 'Screenshot 2026-11-09 at 18.58.12.png' }).click()
  await ui.shot('library-media')

  // Hovering a tile shows its whole name. A right-click opens Rio's menu for the file.
  const tile = media.getByRole('button', { name: 'Screenshot 2026-11-09 at 18.58.12.png' })
  await tile.hover()
  await expect(ui.frame.getByRole('tooltip')).toHaveText('Screenshot 2026-11-09 at 18.58.12.png')
  await ui.shot('library-media-hover-name')
  await tile.click({ button: 'right' })
  const menu = ui.frame.getByRole('menu')
  await expect(menu.getByRole('menuitem')).toHaveText(['Open', 'Show in Finder', 'Copy', 'Copy Path', 'Share…', 'Rename…', 'Move to Trash'])
  await ui.shot('library-media-menu')
  await menu.getByRole('menuitem', { name: 'Rename…' }).click()
  await expect(ui.frame.getByRole('textbox', { name: 'Rename Screenshot 2026-11-09 at 18.58.12.png' })).toBeFocused()
  await ui.shot('library-media-rename')
  await ui.page.keyboard.press('Escape')
  await expect(ui.frame.getByRole('textbox', { name: /^Rename/ })).toHaveCount(0)

  await ui.frame.getByRole('tab', { name: 'Downloads' }).click()
  await ui.frame.locator('.clu-library-downloads').getByText('Team offsite.key').click({ button: 'right' })
  await expect(ui.frame.getByRole('menu').getByRole('menuitem')).toHaveCount(7)
  await ui.shot('library-downloads-menu')
  await ui.page.keyboard.press('Escape')
  await expect(ui.frame.getByRole('menu')).toHaveCount(0)

  // Copy toasts "Copied File", from the menu and from Cmd+C on the focused row; Copy Path toasts "Copied Path".
  const toasts = async () => (await state(ui)).windows[WINDOW]?.toasts.map((t) => t.text)
  const row = ui.frame.locator('.clu-library-downloads').getByText('Team offsite.key')
  await row.click({ button: 'right' })
  await ui.frame.getByRole('menu').getByRole('menuitem', { name: 'Copy Path' }).click()
  await expect.poll(toasts).toEqual(['Copied Path'])
  await row.click({ button: 'right' })
  await ui.frame.getByRole('menu').getByRole('menuitem', { name: 'Copy', exact: true }).click()
  await expect.poll(toasts).toEqual(['Copied File'])
  await row.click({ button: 'right' })
  await ui.frame.getByRole('menu').getByRole('menuitem', { name: 'Copy Path' }).click()
  await expect.poll(toasts).toEqual(['Copied Path'])
  await ui.frame.locator('.clu-library-downloads li', { hasText: 'Team offsite.key' }).locator('.clu-archive-open').focus()
  await ui.page.keyboard.press('Meta+c')
  await expect.poll(toasts).toEqual(['Copied File'])
})

// In a 1440 window the Spaces view opens wide enough for every space card side by side, 3 to 5 spaces: no
// sideways scrollbar, no card under the page; the page card shrinks instead. Closing and reopening asks again.
test('the Library opens wide enough for every space card in a 1440 window', async ({ ui }) => {
  await ui.page.setViewportSize({ width: 1440, height: 900 })
  await ui.page.goto('/')
  await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
  const library = ui.frame.locator('nav.clu-sidebar')
  await library.getByRole('button', { name: 'View Library' }).click()
  await ui.frame.getByRole('tab', { name: 'Spaces' }).click()
  const fits = async (count: number) => {
    await expect(ui.frame.locator('.clu-library-spaces .clu-space-card')).toHaveCount(count)
    await expect.poll(() => ui.frame.locator('.clu-space-cards').evaluate((row) => row.scrollWidth <= row.clientWidth + 1)).toBe(true)
    const add = await ui.frame.locator('.clu-library-spaces').getByRole('button', { name: 'New Space' }).boundingBox()
    const layout = (await state(ui)).windows[WINDOW]?.shell.layout
    expect((add?.x ?? 0) + (add?.width ?? 0)).toBeLessThanOrEqual(layout?.sidebarRect.width ?? 0)
    expect(layout?.pageRect.x ?? 0).toBeGreaterThanOrEqual(layout?.sidebarRect.width ?? 0)
  }
  const create = (name: string) => ui.frameDoc().evaluate((n) => window.clu.spaces.create({ name: n, icon: { kind: 'emoji', value: '🌍' } }), name)
  let n = await ui.frame.locator('.clu-library-spaces .clu-space-card').count()
  while (n < 3) await create(`Extra ${++n}`)
  for (;;) {
    await fits(n)
    if (n >= 5) break
    await create(`Extra ${++n}`)
  }
  await ui.shot('library-spaces-1440-five')
  await ui.frameDoc().evaluate(() => window.clu.archive.close({}))
  await ui.frameDoc().evaluate(() => window.clu.archive.open({}))
  await ui.frame.getByRole('tab', { name: 'Spaces' }).click()
  await fits(5)
  // From the Library button after the close slide has ended, it opens straight onto Spaces, wide.
  await ui.frameDoc().evaluate(() => window.clu.archive.close({}))
  await expect(ui.frame.locator('.clu-library')).toHaveCount(0)
  await library.getByRole('button', { name: 'View Library' }).click()
  await expect(ui.frame.getByRole('tab', { name: 'Spaces' })).toHaveAttribute('aria-selected', 'true')
  await fits(5)
  await ui.shot('library-spaces-1440-reopen')
})

const MINI = {
  tabId: '00000000-0000-4000-8000-000500000001', targetSpaceId: null, targetSpaceName: 'Work', profileMismatch: false, confirming: false,
  url: 'https://dashboard.stripe.com/login', title: 'Stripe', favicon: null, loading: false, progress: 1, canGoBack: true
}

for (const width of [500, 700, 900]) {
  test(`the Mini window toolbar fits at ${width} with no clipped tooltip`, async ({ ui }) => {
    await ui.page.setViewportSize({ width, height: 420 })
    await ui.page.goto('/?extensions=1')
    await expect(ui.frame.locator('nav.clu-sidebar')).toBeVisible()
    await host(ui, ['windows', WINDOW, 'mini'], MINI)
    await ui.page.evaluate(() => {
      const f = document.getElementById('frame') as HTMLIFrameElement
      f.src = './frame/index.html?mode=mini'
    })
    const bar = ui.frame.locator('[data-mini-toolbar]')
    await expect(bar).toBeVisible()
    // No extensions row: extensions live in the Site controls panel.
    await expect(bar.locator('[data-extensions-menu-button], [data-action], .clu-action-row')).toHaveCount(0)

    const boxes = await bar.evaluate((el) => {
      const pick = (sel: string) => {
        const r = el.querySelector(sel)?.getBoundingClientRect()
        return r ? { sel, left: r.left, right: r.right } : null
      }
      return ['.clu-traffic-spacer', '[aria-label="Back"]', '[data-mini-site]', '[data-slot="site-controls"]', '[data-mini-open-group]', '[aria-label="Close"]'].map(pick)
    })
    let edge = 0
    for (const b of boxes) {
      expect(b, 'toolbar part').not.toBeNull()
      if (!b) continue
      expect(b.left, `${b.sel} starts after the part before it`).toBeGreaterThanOrEqual(edge - 0.5)
      expect(b.right, `${b.sel} inside the window`).toBeLessThanOrEqual(width)
      edge = b.right
    }
    expect(boxes[0]?.right, 'the lights keep their 69 px').toBeGreaterThanOrEqual(69)

    // The space picker's tooltip shows whole: nothing between it and the window clips it.
    await bar.locator('[data-mini-pick-space]').hover()
    const tip = bar.locator('.clu-tooltip')
    await expect(tip).toHaveText('Choose a space')
    const whole = await tip.evaluate((t) => {
      const r = t.getBoundingClientRect()
      for (let a = t.parentElement; a; a = a.parentElement) {
        const s = getComputedStyle(a)
        if (s.overflowX === 'visible' && s.overflowY === 'visible') continue
        const c = a.getBoundingClientRect()
        if (r.left < c.left - 0.5 || r.right > c.right + 0.5 || r.top < c.top - 0.5 || r.bottom > c.bottom + 0.5) return `clipped by ${a.className}`
      }
      return r.left >= 0 && r.right <= window.innerWidth ? 'whole' : 'off the window'
    })
    expect(whole).toBe('whole')
    await ui.shot(`mini-toolbar-${width}`)
  })
}

// The Library button's box rests closed and nothing on it runs while it sits there; hover and the open
// Library tip the lid open and show the sparkles, and it closes again afterwards.
test('the Library button rests as a closed box and opens only on hover', async ({ ui }) => {
  await ui.page.goto('/')
  const button = ui.frame.locator('nav.clu-sidebar').getByRole('button', { name: 'View Library' })
  await expect(button).toBeVisible()
  const lid = button.locator('.clu-library-lid')
  const sparks = button.locator('.clu-library-sparkles')
  const lidTransform = () => lid.evaluate((el) => getComputedStyle(el).transform)
  const sparksOpacity = () => sparks.evaluate((el) => getComputedStyle(el).opacity)
  const running = () => button.evaluate((el) => el.getAnimations({ subtree: true }).length)
  const away = () => ui.page.mouse.move((ui.page.viewportSize()?.width ?? 1200) - 20, 300)
  await expect.poll(lidTransform).toBe('none')
  await expect.poll(sparksOpacity).toBe('0')
  expect(await running()).toBe(0)
  await button.screenshot({ path: join(shotsDir('chromium-m-panels'), 'library-button-rest.png') })

  await button.hover()
  await expect.poll(lidTransform).not.toBe('none')
  await expect.poll(sparksOpacity).toBe('1')
  await expect.poll(running).toBe(0)
  await button.screenshot({ path: join(shotsDir('chromium-m-panels'), 'library-button-hover.png') })
  await away()
  await expect.poll(lidTransform).toBe('none')
  await expect.poll(running).toBe(0)
})
