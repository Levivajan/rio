// First run with another browser's sidebar file on the Mac: setup opens on the switch screen, which sends
// the ticked items, shows the summary, and moves on to the welcome step; without the file it opens on welcome.
import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { IpcResult } from '../../../../shared/ipc'
import type { ImportDetect, SidebarImportResult } from '../../../../shared/ipc/data'
import { defaultSpaceState, defaultState, defaultWindowState, type CluState } from '../../../../shared/state'
import { stubClu } from '../../../../test-utils/stubClu'
import { clu } from '../../../common/clu'
import { ALL_ITEMS } from '../../../common/SwitchImport'
import { Onboarding } from './Onboarding'
import { SWATCH_PAGES } from '../../../../shared/theme/picker'
import type { SpaceTheme } from '../../../../shared/theme/types'

const RESULT: SidebarImportResult = {
  profiles: 1,
  spaces: 2,
  pinned: 4,
  today: 1,
  favorites: 2,
  history: 3,
  bookmarks: 2,
  searchEngine: null,
  extensions: [{ id: 'b'.repeat(32), name: 'Reader' }],
  passwords: 0,
  cookies: 0,
  secretsDenied: true,
  secretsSkipped: 0,
  skipped: { unsupported: 0, missing: 0, cycles: 0, badUrls: 0, overCap: 0, duplicates: 0, historyFiles: 0 },
  error: null
}

async function setup(found: boolean, previous = 0, over: Partial<CluState> = {}) {
  const stub = stubClu()
  const runs: unknown[] = []
  const created: unknown[] = []
  const installs: IpcResult<null>[] = []
  let finish: (r: IpcResult<SidebarImportResult>) => void = () => undefined
  const area = stub.clu.import as unknown as Record<string, (p: unknown) => Promise<unknown>>
  const detect: ImportDetect = { chrome: { profiles: [] }, arc: { found, previous }, currentSpaceId: null }
  area.detect = async () => ({ ok: true, value: detect })
  area.run = (p) => {
    runs.push(p)
    return new Promise((res) => (finish = res))
  }
  // Reinstall asks the browser for the store's install prompt; `installs` answers each click in turn.
  ;(stub.clu.extensions as unknown as Record<string, (p: unknown) => Promise<unknown>>).installFromStore = (p) => {
    created.push(p)
    return Promise.resolve(installs.shift() ?? { ok: true, value: null })
  }
  window.clu = stub.clu
  const state: CluState = { ...defaultState(), rev: 1, app: { ...defaultState().app, onboarding: { show: true } }, ...over }
  render(<Onboarding />)
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  await act(async () => undefined)
  return { calls: stub.calls, runs, created, installs, finish: (r: IpcResult<SidebarImportResult>) => act(async () => finish(r)) }
}

describe('Onboarding, switch from another browser', () => {
  it('opens on the switch screen, brings the ticked items over, and continues to welcome', async () => {
    const { runs, created, finish } = await setup(true)
    const dialog = within(screen.getByRole('dialog'))
    expect(dialog.getByRole('heading', { name: 'Bring everything over' })).toBeTruthy()
    fireEvent.click(dialog.getByRole('checkbox', { name: 'Today tabs' }))
    fireEvent.click(dialog.getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS, today: false }])
    await finish({ ok: true, value: RESULT })
    expect(dialog.getByRole('status')).toHaveTextContent('Imported 1 profile, 2 spaces, 4 pinned tabs, 1 Today tab, 2 favorites, 3 history entries and 2 bookmarks')
    expect(dialog.getByRole('status')).toHaveTextContent('Passwords and sign-ins were skipped')
    await act(async () => fireEvent.click(dialog.getByRole('button', { name: 'Reinstall' })))
    expect(created).toEqual([{ id: 'b'.repeat(32) }])
    expect(dialog.getByText('Installed')).toBeTruthy()
    expect(dialog.queryByRole('button', { name: 'Reinstall' })).toBeNull()
    fireEvent.click(dialog.getByRole('button', { name: 'Continue' }))
    expect(dialog.getByRole('heading', { name: 'Welcome to Rio' })).toBeTruthy()
  })

  it('turns a Reinstall row back after a no, to Try again when the store fails, and Installed when it lands', async () => {
    const { created, installs, finish } = await setup(true)
    const dialog = within(screen.getByRole('dialog'))
    fireEvent.click(dialog.getByRole('button', { name: 'Bring everything over' }))
    await finish({ ok: true, value: RESULT })
    installs.push({ ok: false, error: { code: 'rejected', reason: 'cancelled' } }, { ok: false, error: { code: 'rejected', reason: 'failed' } })
    await act(async () => fireEvent.click(dialog.getByRole('button', { name: 'Reinstall' })))
    await act(async () => fireEvent.click(dialog.getByRole('button', { name: 'Reinstall' })))
    await act(async () => fireEvent.click(dialog.getByRole('button', { name: 'Try again' })))
    expect(created).toHaveLength(3)
    expect(dialog.getByText('Installed')).toBeTruthy()
  })

  it('disables the row while the prompt is up', async () => {
    const { finish } = await setup(true)
    const dialog = within(screen.getByRole('dialog'))
    fireEvent.click(dialog.getByRole('button', { name: 'Bring everything over' }))
    await finish({ ok: true, value: RESULT })
    let answer: (r: IpcResult<null>) => void = () => undefined
    ;(clu().extensions as unknown as Record<string, () => Promise<unknown>>).installFromStore = () => new Promise((res) => (answer = res))
    fireEvent.click(dialog.getByRole('button', { name: 'Reinstall' }))
    expect(dialog.getByRole('button', { name: 'Installing…' })).toBeDisabled()
    await act(async () => answer({ ok: true, value: null }))
    expect(dialog.getByText('Installed')).toBeTruthy()
  })

  it('shows progress while it runs, and Not now moves on from there', async () => {
    const { runs } = await setup(true)
    const dialog = within(screen.getByRole('dialog'))
    fireEvent.click(dialog.getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toHaveLength(1)
    expect(dialog.getByRole('status')).toHaveTextContent('Starting…')
    expect(dialog.getByRole('button', { name: 'Bringing it over…' })).toBeDisabled()
    const notNow = dialog.getByRole('button', { name: 'Not now' })
    expect(notNow).toBeEnabled()
    fireEvent.click(notNow)
    expect(dialog.getByRole('heading', { name: 'Welcome to Rio' })).toBeTruthy()
  })

  it('Not now moves on from the checklist and from the replace question', async () => {
    const first = await setup(true)
    fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Not now' }))
    expect(within(screen.getByRole('dialog')).getByRole('heading', { name: 'Welcome to Rio' })).toBeTruthy()
    expect(first.runs).toEqual([])
    cleanup()

    const again = await setup(true, 2)
    const dialog = within(screen.getByRole('dialog'))
    fireEvent.click(dialog.getByRole('button', { name: 'Bring everything over' }))
    expect(dialog.getByRole('alertdialog', { name: 'Replace the previous import?' })).toHaveTextContent('The 2 profiles it made')
    fireEvent.click(dialog.getByRole('button', { name: 'Not now' }))
    expect(dialog.getByRole('heading', { name: 'Welcome to Rio' })).toBeTruthy()
    expect(again.runs).toEqual([])
  })

  it('opens on welcome when no other browser is found', async () => {
    await setup(false)
    expect(within(screen.getByRole('dialog')).getByRole('heading', { name: 'Welcome to Rio' })).toBeTruthy()
  })
})

const WIN = '00000000-0000-4000-8000-000000000001'
const FIRST = '00000000-0000-4000-8000-000000000002'
const SHOWN = '00000000-0000-4000-8000-000000000003'
const PROFILE = '00000000-0000-4000-8000-000000000004'

const click = (name: string) => fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name }))
const heading = () => within(screen.getByRole('dialog')).getByRole('heading').textContent
const dots = () => screen.getByRole('list').getAttribute('aria-label')

describe('Onboarding asks once to bring everything over', () => {
  it('after the switch screen, Not now skips the Import step both ways and counts five steps', async () => {
    await setup(true)
    click('Not now')
    expect(dots()).toBe('Step 1 of 5')
    click('Get started')
    click('Continue')
    click('Continue')
    expect(heading()).toBe('Make Rio your default browser')
    click('Skip')
    expect(heading()).toBe("You're all set")
    expect(dots()).toBe('Step 5 of 5')
    expect(screen.getAllByRole('listitem')).toHaveLength(5)
    click('Back')
    expect(heading()).toBe('Make Rio your default browser')
  })

  it('after an import on the switch screen, the Import step is skipped as well', async () => {
    const { finish } = await setup(true)
    click('Bring everything over')
    await finish({ ok: true, value: RESULT })
    click('Continue')
    click('Get started')
    click('Continue')
    click('Continue')
    click('Skip')
    expect(heading()).toBe("You're all set")
  })

  it('after a failed import on the switch screen, the Import step still asks, in six steps', async () => {
    const { finish } = await setup(true)
    click('Bring everything over')
    await finish({ ok: true, value: { ...RESULT, error: 'Rio can hold 20 profiles' } })
    click('Continue')
    expect(dots()).toBe('Step 1 of 6')
    click('Get started')
    click('Continue')
    click('Continue')
    click('Skip')
    expect(heading()).toBe('Bring everything over')
    expect(dots()).toBe('Step 5 of 6')
  })

  it('when the import on the switch screen cannot start, the Import step still asks', async () => {
    const { finish } = await setup(true)
    click('Bring everything over')
    await finish({ ok: false, error: { code: 'rejected', message: 'busy', reason: 'busy' } } as IpcResult<SidebarImportResult>)
    click('Continue')
    click('Get started')
    click('Continue')
    click('Continue')
    click('Skip')
    expect(heading()).toBe('Bring everything over')
  })

  it('without the switch screen, the Import step is the one ask, in six steps', async () => {
    await setup(false)
    expect(dots()).toBe('Step 1 of 6')
    click('Get started')
    click('Continue')
    click('Continue')
    click('Skip')
    expect(heading()).toBe('Bring everything over')
    expect(dots()).toBe('Step 5 of 6')
    click('Skip')
    expect(heading()).toBe("You're all set")
  })

  // From Google Chrome opened Settings at General; it runs Import from Google Chrome, which opens the Chrome
  // import itself, and Switch from another browser opens the Import from Arc card.
  it('the Import step opens each source’s own import and ends setup', async () => {
    const { calls } = await setup(false)
    const runs: unknown[] = []
    ;(clu().commands as unknown as { run: (p: unknown) => Promise<unknown> }).run = async (p) => {
      runs.push(p)
      return { ok: true, value: null }
    }
    click('Get started')
    click('Continue')
    click('Continue')
    click('Skip')
    expect(heading()).toBe('Bring everything over')
    await act(async () => click('From Google Chrome'))
    expect(runs).toEqual([{ id: 'settings.importChrome' }])
    expect(calls.some((c) => c.name === 'settings.open')).toBe(false)
    expect(calls.filter((c) => c.name === 'onboarding.complete')).toEqual([{ name: 'onboarding.complete', payload: { skipped: false } }])
    await act(async () => click('Switch from another browser'))
    expect(runs.at(-1)).toEqual({ id: 'settings.importArc' })
  })

  it('the Import step says so when the Chrome import does not open', async () => {
    await setup(false)
    click('Get started')
    click('Continue')
    click('Continue')
    click('Skip')
    await act(async () => click('From Google Chrome'))
    expect(within(screen.getByRole('dialog')).getByRole('status')).toHaveTextContent('That did not open. You can import any time from Settings.')
  })
})

describe('Onboarding, pick a look', () => {
  const space = (id: string) => defaultSpaceState({ id, name: id, icon: { kind: 'letter' }, color: 'blue', profileId: PROFILE, ephemeral: false })
  const look = async () => {
    cleanup()
    const r = await setup(false, 0, {
      windows: { [WIN]: defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SHOWN }) },
      spaces: { [FIRST]: space(FIRST), [SHOWN]: space(SHOWN) },
      spaceOrder: [FIRST, SHOWN]
    })
    click('Get started')
    expect(heading()).toBe('Pick a look')
    return r
  }

  it('shows the whole space theme editor, every control of it, on the space this window shows', async () => {
    await look()
    const editor = screen.getByRole('group', { name: `Theme for ${SHOWN}` })
    expect(editor.hasAttribute('data-inline')).toBe(true)
    expect(editor.style.width).toBe('356px')
    expect(within(editor).getByTestId('theme-field')).toBeTruthy()
    expect(within(editor).getAllByRole('radio').map((r) => r.getAttribute('aria-label'))).toEqual(['Auto', 'Light', 'Dark'])
    expect(within(editor).getByRole('button', { name: 'Add color' })).toBeTruthy()
    expect(within(editor).getByRole('button', { name: 'Remove color' })).toBeTruthy()
    expect(within(editor).getByRole('button', { name: 'Previous presets' })).toBeTruthy()
    expect(within(editor).getByRole('button', { name: 'Next presets' })).toBeTruthy()
    // All 5 pages of 9 swatches, not only the first page.
    expect(editor.querySelectorAll('.clu-tp-swatch')).toHaveLength(SWATCH_PAGES.length * 9)
    expect(within(editor).getByRole('slider', { name: 'Intensity' })).toBeTruthy()
    expect(within(editor).getByRole('slider', { name: 'Grain' })).toBeTruthy()
    // No separate popover to open any more, and the heading keeps focus, not the main handle.
    expect(screen.queryByRole('button', { name: 'More colors' })).toBeNull()
    expect(document.activeElement?.tagName).toBe('H1')
  })

  it('a swatch, + and the intensity commit to the space this window shows; Esc does not close the editor', async () => {
    const { calls } = await look()
    const editor = screen.getByRole('group', { name: `Theme for ${SHOWN}` })
    fireEvent.click(editor.querySelector(`[data-swatch="${SWATCH_PAGES[0]?.colors[2]}"]`) as HTMLElement)
    fireEvent.click(within(editor).getByRole('button', { name: 'Add color' }))
    fireEvent.keyDown(within(editor).getByRole('slider', { name: 'Intensity' }), { key: 'End' })
    const set = calls.filter((c) => c.name === 'theme.setSpaceTheme').map((c) => c.payload as { spaceId: string; theme: SpaceTheme })
    expect(set).toHaveLength(3)
    expect(set.every((p) => p.spaceId === SHOWN)).toBe(true)
    expect(set[1]?.theme.colors).toHaveLength(2)
    expect(set[2]?.theme.intensity).toBe(1)
    fireEvent.keyDown(editor, { key: 'Escape' })
    expect(screen.getByRole('group', { name: `Theme for ${SHOWN}` })).toBeTruthy()
    expect(calls.some((c) => c.name === 'theme.openPicker')).toBe(false)
  })
})
