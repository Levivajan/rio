import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { Toolbar } from '../shell/Toolbar'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-000000000002'
const URL = 'http://localhost:3000/app?debug=1#top'

function mount(tab: { site: string | null; devMode: boolean; url?: string }, opts: { toolbar?: boolean; focusMode?: boolean; mini?: boolean } = {}) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  render(<Toolbar />)
  const win = defaultWindowState({ id: WIN, kind: opts.mini ? 'mini' : 'main', private: false, activeSpaceId: WIN })
  win.activeTabId = TAB
  win.shell = { ...win.shell, toolbar: !!opts.toolbar, focusMode: !!opts.focusMode, layout: { ...win.shell.layout, pageRect: { x: 214, y: 48, width: 1056, height: 742 } } }
  const t = { ...defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url: tab.url ?? URL }), site: tab.site, devMode: tab.devMode }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win }, tabs: { [TAB]: t } } }))
  return stub
}

afterEach(cleanup)

describe('Developer Mode strip', () => {
  it('takes the band above the card with the full URL, port and query included, in a monospace field', () => {
    mount({ site: 'localhost', devMode: true })
    const strip = document.querySelector<HTMLElement>('[data-shell="dev-strip"]')
    expect(strip).not.toBeNull()
    expect(strip?.style.left).toBe('214px')
    expect(strip?.style.width).toBe('1056px')
    expect(strip?.style.height).toBe('48px')
    expect(document.querySelector('[data-dev-url]')?.textContent).toBe(URL)
    expect(document.querySelector('[data-shell="toolbar"]')).toBeNull()
  })

  it('has the tool groups in order: copy link | screenshot | console, DevTools, inspect, hard reload | extensions, sidebar', () => {
    mount({ site: 'localhost', devMode: true })
    const groups = [...document.querySelectorAll('.clu-dev-strip-group')].map((g) => [...g.querySelectorAll('button')].map((b) => b.getAttribute('aria-label')))
    expect(groups).toEqual([['Copy link'], ['Screenshot'], ['Console', 'Developer tools', 'Inspect element', 'Hard reload'], ['Extensions', 'Toggle sidebar']])
  })

  it('runs each tool on the page the window shows', () => {
    const stub = mount({ site: 'localhost', devMode: true })
    const click = (name: string) => fireEvent.click(screen.getByRole('button', { name }))
    click('Console')
    click('Developer tools')
    click('Inspect element')
    click('Hard reload')
    expect(stub.calls.filter((c) => c.name === 'siteControls.devTool').map((c) => c.payload)).toEqual([{ tool: 'console' }, { tool: 'devtools' }, { tool: 'inspect' }, { tool: 'hardReload' }])
    click('Copy link')
    expect(stub.calls).toContainEqual({ name: 'commands.run', payload: { id: 'edit.copyUrl' } })
    click('Screenshot')
    expect(stub.calls.some((c) => c.name === 'siteControls.screenshot')).toBe(true)
    click('Extensions')
    expect(stub.calls.some((c) => c.name === 'extensions.openMenu')).toBe(true)
    click('Toggle sidebar')
    expect(stub.calls.some((c) => c.name === 'shell.toggleSidebar')).toBe(true)
  })

  it('is absent for a site out of Developer Mode, a page without a site, focus mode and a Mini window', () => {
    mount({ site: 'a.com', devMode: false })
    expect(document.querySelector('[data-shell="dev-strip"]')).toBeNull()
    cleanup()
    mount({ site: null, devMode: true, url: 'chrome://clu-settings/' })
    expect(document.querySelector('[data-shell="dev-strip"]')).toBeNull()
    cleanup()
    mount({ site: 'localhost', devMode: true }, { focusMode: true })
    expect(document.querySelector('[data-shell="dev-strip"]')).toBeNull()
    cleanup()
    mount({ site: 'localhost', devMode: true }, { mini: true })
    expect(document.querySelector('[data-shell="dev-strip"]')).toBeNull()
  })

  it('leaves toolbar mode its URL pill when the page is not in Developer Mode', () => {
    mount({ site: 'a.com', devMode: false }, { toolbar: true })
    expect(document.querySelector('[data-shell="toolbar"]')).not.toBeNull()
  })
})
