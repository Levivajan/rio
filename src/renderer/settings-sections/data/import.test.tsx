// Import section (data-t6): profiles from detection, the settings tab's space as the default target,
// missing files disable their box, Import sends the choices and shows the summary, progress shows while running, and
// nothing found says so.
import { act, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { IpcResult } from '../../../shared/ipc'
import type { ImportDetect, ImportResult, SidebarImportResult } from '../../../shared/ipc/data'
import { defaultSpaceState, defaultState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { ArcImport, ChromeImportSection, ImportSection, progressText } from './import'
import { clu } from '../../common/clu'
import { ALL_ITEMS } from '../../common/SwitchImport'

const S1 = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
const S2 = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb'
const PRIV = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'
const PROFILE = 'dddddddd-dddd-4ddd-8ddd-dddddddddddd'
const space = (id: string, name: string, ephemeral = false) =>
  defaultSpaceState({ id, name, icon: { kind: 'letter', value: name[0] ?? 'S' } as never, color: 'blue' as never, profileId: PROFILE, ephemeral })

const DETECT: ImportDetect = {
  chrome: {
    profiles: [
      { dir: 'Default', name: 'Personal', hasBookmarks: true, hasHistory: false },
      { dir: 'Profile 1', name: 'Work', hasBookmarks: true, hasHistory: true }
    ]
  },
  arc: { found: false, previous: 0 },
  currentSpaceId: S2
}

async function setup(detect: ImportDetect = DETECT, ui = <ImportSection />) {
  const stub = stubClu()
  const runs: unknown[] = []
  let finish: (r: IpcResult<ImportResult | SidebarImportResult>) => void = () => undefined
  const area = stub.clu.import as unknown as Record<string, (p: unknown) => Promise<unknown>>
  area.detect = async () => ({ ok: true, value: detect })
  area.run = (p) => {
    runs.push(p)
    return new Promise((res) => (finish = res))
  }
  let opened: (r: IpcResult<null>) => void = () => undefined
  const passwordOpens: unknown[] = []
  area.openPasswords = (p) => {
    passwordOpens.push(p ?? {})
    return new Promise((res) => (opened = res))
  }
  window.clu = stub.clu
  let state: CluState = { ...defaultState(), rev: 1, spaces: { [S1]: space(S1, 'Home'), [S2]: space(S2, 'Research'), [PRIV]: space(PRIV, 'Private', true) }, spaceOrder: [S1, S2, PRIV] }
  const push = (s: Partial<CluState> = {}) => {
    state = { ...state, ...s, rev: state.rev + 1 }
    act(() => stub.push({ kind: 'snapshot', rev: state.rev, state }))
  }
  render(ui)
  // Detection resolves first; the cards mount and subscribe to state after it.
  await act(async () => undefined)
  push()
  return {
    runs,
    push,
    finish: (r: IpcResult<ImportResult | SidebarImportResult>) => act(async () => finish(r)),
    passwordOpens,
    opened: (r: IpcResult<null>) => act(async () => opened(r))
  }
}

let revealed: Element[] = []
beforeEach(() => {
  revealed = []
  Element.prototype.scrollIntoView = vi.fn(function (this: Element) {
    revealed.push(this)
  })
})
afterEach(() => {
  delete (Element.prototype as { scrollIntoView?: unknown }).scrollIntoView
})

const chromeCard = () => within(screen.getByRole('group', { name: 'Google Chrome' }))
const sidebarCard = () => within(screen.getByRole('group', { name: 'Sidebar file from another browser' }))
const SIDEBAR_RESULT: SidebarImportResult = {
  profiles: 2,
  spaces: 4,
  pinned: 10,
  today: 3,
  favorites: 3,
  history: 3,
  bookmarks: 0,
  searchEngine: null,
  extensions: [],
  passwords: 0,
  cookies: 0,
  secretsDenied: false,
  secretsSkipped: 0,
  skipped: { unsupported: 2, missing: 1, cycles: 1, badUrls: 1, overCap: 0, duplicates: 1, historyFiles: 0 },
  error: null
}

describe('Import section', () => {
  it('lists profiles and non-private spaces, defaulting to the settings tab’s space', async () => {
    await setup()
    const profile = await screen.findByRole('combobox', { name: 'Google Chrome profile' })
    expect([...(profile as HTMLSelectElement).options].map((o) => o.text)).toEqual(['Personal', 'Work'])
    const target = screen.getByRole('combobox', { name: 'Into space' }) as HTMLSelectElement
    expect([...target.options].map((o) => o.text)).toEqual(['Home', 'Research'])
    expect(target.value).toBe(S2)
    // Default has no History file.
    expect(screen.getByRole('checkbox', { name: 'History (last 90 days)' })).toBeDisabled()
    expect(screen.getByRole('checkbox', { name: 'Bookmarks' })).toBeChecked()
  })

  it('imports the chosen profile into the chosen space and shows the summary', async () => {
    const { runs, push, finish } = await setup()
    fireEvent.change(await screen.findByRole('combobox', { name: 'Google Chrome profile' }), { target: { value: 'Profile 1' } })
    expect(screen.getByRole('checkbox', { name: 'History (last 90 days)' })).toBeChecked()
    fireEvent.click(chromeCard().getByRole('button', { name: 'Import' }))
    expect(runs).toEqual([{ source: 'chrome', profileDir: 'Profile 1', bookmarks: true, history: true, passwords: true, cookies: true, autofill: true, searchEngine: true, targetSpaceId: S2 }])
    expect(chromeCard().getByRole('button', { name: 'Importing…' })).toBeDisabled()
    push({ app: { ...defaultState().app, import: { source: 'chrome', phase: 'bookmarks', done: 500, total: 1200, skipped: 0 } } })
    expect(chromeCard().getByRole('status')).toHaveTextContent('Importing bookmarks… 500 of 1,200')
    await finish({ ok: true, value: { bookmarks: 13, history: 4, passwords: 0, cookies: 0, secretsDenied: false, skipped: 2, error: null } })
    expect(chromeCard().getByRole('status')).toHaveTextContent('Imported 13 bookmarks and 4 history entries (2 skipped)')
  })

  it('sends the saved addresses and the default search engine as the user left them', async () => {
    const { runs } = await setup()
    fireEvent.change(await screen.findByRole('combobox', { name: 'Google Chrome profile' }), { target: { value: 'Profile 1' } })
    for (const name of ['Bookmarks', 'Saved addresses', 'Default search engine']) expect(chromeCard().getByRole('checkbox', { name })).toBeChecked()
    for (const name of ['History (last 90 days)', 'Saved passwords', 'Sign-ins (cookies, site storage, permissions), so sites stay signed in', 'Saved addresses', 'Default search engine']) {
      fireEvent.click(chromeCard().getByRole('checkbox', { name }))
    }
    fireEvent.click(chromeCard().getByRole('button', { name: 'Import' }))
    expect(runs).toEqual([{ source: 'chrome', profileDir: 'Profile 1', bookmarks: true, history: false, passwords: false, cookies: false, autofill: false, searchEngine: false, targetSpaceId: S2 }])
  })

  it('unticking everything disables Import; an error result shows inline as an error', async () => {
    const { runs, finish } = await setup()
    fireEvent.click(await screen.findByRole('checkbox', { name: 'Bookmarks' }))
    fireEvent.click(screen.getByRole('checkbox', { name: 'Saved passwords' }))
    fireEvent.click(chromeCard().getByRole('checkbox', { name: 'Saved addresses' }))
    fireEvent.click(chromeCard().getByRole('checkbox', { name: 'Default search engine' }))
    expect(chromeCard().getByRole('button', { name: 'Import' })).toBeEnabled()
    fireEvent.click(screen.getByRole('checkbox', { name: /^Sign-ins/ }))
    expect(chromeCard().getByRole('button', { name: 'Import' })).toBeDisabled()
    fireEvent.click(screen.getByRole('checkbox', { name: 'Bookmarks' }))
    fireEvent.change(screen.getByRole('combobox', { name: 'Into space' }), { target: { value: S1 } })
    fireEvent.click(chromeCard().getByRole('button', { name: 'Import' }))
    expect(runs[0]).toMatchObject({ profileDir: 'Default', bookmarks: true, history: false, passwords: false, cookies: false, targetSpaceId: S1 })
    await finish({ ok: true, value: { bookmarks: 0, history: 0, passwords: 0, cookies: 0, secretsDenied: false, skipped: 0, error: 'Chrome’s Bookmarks file is damaged.' } })
    expect(chromeCard().getByRole('status')).toHaveTextContent('Import stopped: Chrome’s Bookmarks file is damaged.')
    expect(chromeCard().getByRole('status')).toHaveClass('clu-settings-error')
  })

  // The sign-ins come over with the rest, and a denied keychain prompt says so.
  it('says how many passwords and cookies came over', async () => {
    const { finish } = await setup()
    expect(await screen.findByRole('checkbox', { name: 'Saved passwords' })).toBeChecked()
    expect(screen.getByRole('checkbox', { name: /^Sign-ins/ })).toBeChecked()
    fireEvent.click(chromeCard().getByRole('button', { name: 'Import' }))
    await finish({ ok: true, value: { bookmarks: 13, history: 0, passwords: 3, cookies: 1200, secretsDenied: false, skipped: 1, error: null } })
    expect(chromeCard().getByRole('status')).toHaveTextContent('Imported 13 bookmarks, 0 history entries, 3 saved passwords and 1,200 cookies (1 skipped)')
    expect(chromeCard().getByRole('status')).not.toHaveTextContent('were skipped')
  })

  it('says passwords and sign-ins were skipped when keychain access was denied', async () => {
    const { runs, finish } = await setup()
    await screen.findByRole('combobox', { name: 'Google Chrome profile' })
    fireEvent.click(chromeCard().getByRole('button', { name: 'Import' }))
    expect(runs[0]).toMatchObject({ passwords: true, cookies: true })
    await finish({ ok: true, value: { bookmarks: 13, history: 0, passwords: 0, cookies: 0, secretsDenied: true, skipped: 0, error: null } })
    expect(chromeCard().getByRole('status')).toHaveTextContent(
      'Imported 13 bookmarks and 0 history entries. Passwords and sign-ins were skipped because keychain access wasn’t allowed.'
    )
    expect(chromeCard().getByRole('status')).not.toHaveClass('clu-settings-error')
  })

  it('says when no Chrome profile was found', async () => {
    await setup({ chrome: { profiles: [] }, arc: { found: false, previous: 0 }, currentSpaceId: null })
    const chrome = within(await screen.findByRole('group', { name: 'Google Chrome' }))
    expect(chrome.getByRole('status')).toHaveTextContent('Google Chrome isn’t installed on this Mac, or has no profiles to import.')
    const sidebar = within(screen.getByRole('group', { name: 'Sidebar file from another browser' }))
    expect(sidebar.getByRole('status')).toHaveTextContent('No sidebar file from another browser was found on this Mac.')
    expect(chrome.queryByRole('button', { name: 'Import' })).toBeNull()
    expect(sidebar.queryByRole('button', { name: 'Import' })).toBeNull()
    // A CSV file needs no other browser on this Mac.
    expect(within(screen.getByRole('group', { name: 'Import passwords from a CSV file' })).getByRole('button', { name: 'Import' })).toBeEnabled()
  })
})

// Import from Google Chrome (#import-chrome) opens the Chrome import itself, not a page to look for it on.
describe('Import from Google Chrome', () => {
  it('shows the Chrome import first: the profile, what to bring, Import, then the result, and the CSV passwords', async () => {
    const { runs, finish, passwordOpens } = await setup(DETECT, <ChromeImportSection />)
    const section = within(screen.getByRole('region', { name: 'Import from Google Chrome' }))
    expect(section.getByRole('heading', { name: 'Import from Google Chrome' })).toBeInTheDocument()
    expect(revealed).toContain(screen.getByRole('region', { name: 'Import from Google Chrome' }))
    // No sidebar file card here: this is the Chrome import.
    expect(screen.queryByRole('group', { name: 'Sidebar file from another browser' })).toBeNull()
    fireEvent.change(section.getByRole('combobox', { name: 'Google Chrome profile' }), { target: { value: 'Profile 1' } })
    expect(section.getByRole('checkbox', { name: 'Bookmarks' })).toBeChecked()
    expect(section.getByRole('checkbox', { name: 'History (last 90 days)' })).toBeChecked()
    fireEvent.click(chromeCard().getByRole('button', { name: 'Import' }))
    expect(runs[0]).toEqual({ source: 'chrome', profileDir: 'Profile 1', bookmarks: true, history: true, passwords: true, cookies: true, autofill: true, searchEngine: true, targetSpaceId: S2 })
    await finish({ ok: true, value: { bookmarks: 3, history: 5, passwords: 0, cookies: 0, secretsDenied: false, skipped: 0, error: null } })
    expect(chromeCard().getByRole('status')).toHaveTextContent('3 bookmarks')
    fireEvent.click(within(section.getByRole('group', { name: 'Import passwords from a CSV file' })).getByRole('button', { name: 'Import' }))
    expect(passwordOpens).toHaveLength(1)
  })

  it('says so when Google Chrome is not installed', async () => {
    await setup({ chrome: { profiles: [] }, arc: { found: false, previous: 0 }, currentSpaceId: null }, <ChromeImportSection />)
    expect(chromeCard().getByRole('status')).toHaveTextContent('Google Chrome isn’t installed on this Mac, or has no profiles to import.')
    expect(chromeCard().queryByRole('button', { name: 'Import' })).toBeNull()
  })

  it('says so when Rio couldn’t look', async () => {
    const stub = stubClu()
    ;(stub.clu.import as unknown as Record<string, () => Promise<unknown>>).detect = async () => ({ ok: false, error: { code: 'internal', message: 'x' } })
    window.clu = stub.clu
    render(<ChromeImportSection />)
    await act(async () => undefined)
    expect(screen.getByText('Rio couldn’t look for other browsers.')).toBeInTheDocument()
  })
})

describe('Import section, sidebar file', () => {
  it('imports the sidebar file with its Today tabs and history, and shows progress and the summary', async () => {
    const { runs, push, finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    expect(sidebarCard().getByText(/with what you tick below\. The other browser’s files are only read\./)).toBeTruthy()
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS }])
    expect(sidebarCard().getByRole('button', { name: 'Importing…' })).toBeDisabled()
    push({ app: { ...defaultState().app, import: { source: 'arc', phase: 'reading', done: 0, total: 0, skipped: 0 } } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent('Reading the sidebar file…')
    // The Chrome card doesn't show the other import's progress.
    expect(chromeCard().getByRole('status')).toHaveTextContent('')
    push({ app: { ...defaultState().app, import: { source: 'arc', phase: 'history', done: 16, total: 16, skipped: 6 } } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent('Importing history…')
    await finish({ ok: true, value: SIDEBAR_RESULT })
    expect(sidebarCard().getByRole('status')).toHaveTextContent(
      'Imported 2 profiles, 4 spaces, 10 pinned tabs, 3 Today tabs, 3 favorites and 3 history entries (6 skipped)'
    )
  })

  it('counts a History file it could not read as skipped', async () => {
    const { finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    await finish({ ok: true, value: { ...SIDEBAR_RESULT, history: 0, skipped: { ...SIDEBAR_RESULT.skipped, historyFiles: 1 } } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent('3 favorites and 0 history entries (7 skipped)')
  })

  it('keeps the result in view as the import adds profiles above it', async () => {
    const { push, finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(revealed).toEqual([])
    await finish({ ok: true, value: SIDEBAR_RESULT })
    const row = sidebarCard().getByRole('status').closest('.clu-group-row')
    expect(revealed).toEqual([row])
    push({ profiles: { [PROFILE]: { id: PROFILE } as never, other: { id: 'other' } as never } })
    expect(revealed).toEqual([row, row])
  })

  it('shows an unsupported file as an error, and a busy start', async () => {
    const { finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    await finish({ ok: true, value: { ...SIDEBAR_RESULT, profiles: 0, spaces: 0, pinned: 0, favorites: 0, error: "This sidebar file format isn't supported" } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent("Import stopped: This sidebar file format isn't supported")
    expect(sidebarCard().getByRole('status')).toHaveClass('clu-switch-error')
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    await finish({ ok: false, error: { code: 'rejected', reason: 'busy' } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent('An import is already running.')
  })
})

describe('Import section, sidebar re-import', () => {
  it('asks before replacing an earlier import, and replaces it only after Replace', async () => {
    const { runs, finish } = await setup({ ...DETECT, arc: { found: true, previous: 10 } })
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toEqual([])
    const ask = sidebarCard().getByRole('alertdialog', { name: 'Replace the previous import?' })
    expect(ask).toHaveTextContent('The 10 profiles it made, with their spaces and tabs, are removed and everything comes over again.')
    // Right under the button, above the long checklist, so Cancel and Replace show without scrolling.
    const list = sidebarCard().getByRole('group', { name: 'What comes over' })
    expect(ask.compareDocumentPosition(list) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy()
    fireEvent.click(within(ask).getByRole('button', { name: 'Cancel' }))
    expect(sidebarCard().queryByRole('alertdialog')).toBeNull()
    expect(runs).toEqual([])
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Replace' }))
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS, replace: true }])
    await finish({ ok: true, value: { ...SIDEBAR_RESULT, profiles: 4 } })
    expect(sidebarCard().queryByRole('alertdialog')).toBeNull()
  })

  it('asks on the second run after a first import in the same session', async () => {
    const { runs, finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS }])
    await finish({ ok: true, value: { ...SIDEBAR_RESULT, profiles: 4 } })
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toHaveLength(1)
    expect(sidebarCard().getByRole('alertdialog')).toHaveTextContent('The 4 profiles it made')
  })
})

describe('Import section, sidebar passwords and sign-ins', () => {
  it('sends the ticked boxes, shows the keychain wait, and counts passwords and cookies in the summary', async () => {
    const { runs, push, finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    fireEvent.click(sidebarCard().getByRole('checkbox', { name: 'Sign-ins (cookies, site storage, permissions), so sites stay logged in' }))
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS, cookies: false }])
    push({ app: { ...defaultState().app, import: { source: 'arc', phase: 'keychain', done: 16, total: 16, skipped: 0 } } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent('Waiting for keychain access… macOS asks you to allow it.')
    push({ app: { ...defaultState().app, import: { source: 'arc', phase: 'secrets', done: 16, total: 16, skipped: 0 } } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent('Importing passwords and sign-ins…')
    await finish({ ok: true, value: { ...SIDEBAR_RESULT, passwords: 40, cookies: 0, secretsSkipped: 2 } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent(
      'Imported 2 profiles, 4 spaces, 10 pinned tabs, 3 Today tabs, 3 favorites, 3 history entries and 40 saved passwords (8 skipped)'
    )
  })

  it('says passwords and sign-ins were skipped when keychain access was denied', async () => {
    const { finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    await finish({ ok: true, value: { ...SIDEBAR_RESULT, secretsDenied: true } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent(
      '3 favorites and 3 history entries (6 skipped) Passwords and sign-ins were skipped because keychain access wasn’t allowed.'
    )
  })

  it('unticking both leaves out the keychain note', async () => {
    const { runs } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    expect(sidebarCard().getByText(/macOS asks you once/)).toBeTruthy()
    fireEvent.click(sidebarCard().getByRole('checkbox', { name: 'Saved passwords' }))
    fireEvent.click(sidebarCard().getByRole('checkbox', { name: 'Sign-ins (cookies, site storage, permissions), so sites stay logged in' }))
    expect(sidebarCard().queryByText(/macOS asks you once/)).toBeNull()
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS, passwords: false, cookies: false }])
  })

  it('lists every item with a checkbox, sends the unticked ones as off, and lists extensions to reinstall', async () => {
    const { runs, finish } = await setup({ ...DETECT, arc: { found: true, previous: 0 } })
    for (const name of ['Pinned tabs and folders', 'Favorites', 'Today tabs', 'Bookmarks', 'History', 'Saved addresses', 'Default search engine', 'Extensions, to reinstall from the Chrome Web Store']) {
      expect(sidebarCard().getByRole('checkbox', { name })).toBeChecked()
    }
    expect(sidebarCard().getByRole('checkbox', { name: 'Profiles' })).toBeDisabled()
    fireEvent.click(sidebarCard().getByRole('checkbox', { name: 'History' }))
    fireEvent.click(sidebarCard().getByRole('checkbox', { name: 'Default search engine' }))
    fireEvent.click(sidebarCard().getByRole('checkbox', { name: 'Saved addresses' }))
    fireEvent.click(sidebarCard().getByRole('button', { name: 'Bring everything over' }))
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS, history: false, searchEngine: false, autofill: false }])
    await finish({ ok: true, value: { ...SIDEBAR_RESULT, bookmarks: 5, searchEngine: 'Kagi', extensions: [{ id: 'a'.repeat(32), name: 'Notes' }] } })
    expect(sidebarCard().getByRole('status')).toHaveTextContent(
      '3 history entries and 5 bookmarks (6 skipped) Kagi is now the default search engine. 1 extension to reinstall below.'
    )
    // Reinstall from a later import shows the store's install prompt too, and the row turns to Installed.
    const asked: unknown[] = []
    ;(clu().extensions as unknown as Record<string, (p: unknown) => Promise<unknown>>).installFromStore = async (p) => {
      asked.push(p)
      return { ok: true, value: null }
    }
    const list = within(sidebarCard().getByRole('list', { name: 'Extensions to reinstall' }))
    await act(async () => fireEvent.click(list.getByRole('button', { name: 'Reinstall' })))
    expect(asked).toEqual([{ id: 'a'.repeat(32) }])
    expect(list.getByText('Installed')).toBeTruthy()
  })
})

describe('Import section, passwords', () => {
  const passwordsCard = () => within(screen.getByRole('group', { name: 'Import passwords from a CSV file' }))

  it('opens Chromium’s password import, showing only its label and state', async () => {
    const { passwordOpens, opened } = await setup()
    expect(passwordsCard().queryByRole('status')).toBeNull()
    expect(passwordsCard().queryByText(/\./)).toBeNull()
    fireEvent.click(passwordsCard().getByRole('button', { name: 'Import' }))
    expect(passwordOpens).toEqual([{}])
    expect(passwordsCard().getByRole('button', { name: 'Opening…' })).toBeDisabled()
    await opened({ ok: true, value: null })
    expect(passwordsCard().getByRole('button', { name: 'Import' })).toBeEnabled()
    expect(passwordsCard().queryByRole('status')).toBeNull()
  })

  it('says so when the import page did not open, and can try again', async () => {
    const { passwordOpens, opened } = await setup()
    fireEvent.click(passwordsCard().getByRole('button', { name: 'Import' }))
    await opened({ ok: false, error: { code: 'not-implemented' } })
    expect(passwordsCard().getByRole('status')).toHaveTextContent('The password import didn’t open.')
    fireEvent.click(passwordsCard().getByRole('button', { name: 'Import' }))
    expect(passwordOpens).toHaveLength(2)
    await opened({ ok: true, value: null })
    expect(passwordsCard().queryByRole('status')).toBeNull()
  })
})

describe('progressText', () => {
  it('reads, then counts', () => {
    expect(progressText(null)).toBeNull()
    expect(progressText({ source: 'chrome', phase: 'history', done: 0, total: 0, skipped: 0 })).toBe('Reading history…')
    expect(progressText({ source: 'chrome', phase: 'history', done: 1000, total: 4000, skipped: 0 })).toBe('Importing history… 1,000 of 4,000')
    expect(progressText({ source: 'chrome', phase: 'keychain', done: 0, total: 0, skipped: 0 })).toBe('Waiting for keychain access… macOS asks you to allow it.')
    expect(progressText({ source: 'chrome', phase: 'secrets', done: 0, total: 0, skipped: 0 })).toBe('Importing passwords and sign-ins…')
    expect(progressText({ source: 'arc', phase: 'writing', done: 0, total: 12, skipped: 0 })).toBe('Adding spaces and tabs…')
    expect(progressText({ source: 'arc', phase: 'history', done: 12, total: 12, skipped: 0 })).toBe('Importing history…')
    expect(progressText({ source: 'chrome', phase: 'site data', done: 0, total: 0, skipped: 0 })).toBe('Bringing over site data…')
    expect(progressText({ source: 'arc', phase: 'site data', done: 0, total: 0, skipped: 0 })).toBe('Bringing over site data…')
  })
})

// Settings, General's Import from Arc for a Rio past its first run, the first run's switch in place.
describe('ArcImport', () => {
  async function arc(open: boolean, found = true) {
    const stub = stubClu()
    const runs: unknown[] = []
    const area = stub.clu.import as unknown as Record<string, (p: unknown) => Promise<unknown>>
    let detects = 0
    area.detect = async () => {
      detects++
      return { ok: true, value: { ...DETECT, arc: { found, previous: 0 } } }
    }
    area.run = async (p) => {
      runs.push(p)
      return { ok: true, value: SIDEBAR_RESULT }
    }
    window.clu = stub.clu
    const view = render(<ArcImport open={open} />)
    await act(async () => undefined)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1 } }))
    return { runs, view, detects: () => detects }
  }
  const card = () => within(screen.getByRole('group', { name: 'Import from Arc' }))

  it('is one row until opened; opening shows the checklist and Bring everything over, which runs the import', async () => {
    const { runs, detects } = await arc(false)
    expect(detects()).toBe(0)
    expect(card().queryByRole('button', { name: 'Bring everything over' })).toBeNull()
    fireEvent.click(card().getByRole('button', { name: 'Import…' }))
    await act(async () => undefined)
    expect(card().getByRole('group', { name: 'What comes over' })).toBeTruthy()
    fireEvent.click(card().getByRole('checkbox', { name: 'History' }))
    fireEvent.click(card().getByRole('button', { name: 'Bring everything over' }))
    await act(async () => undefined)
    expect(runs).toEqual([{ source: 'arc', ...ALL_ITEMS, history: false }])
    expect(card().getByRole('status')).toHaveTextContent('Imported 2 profiles, 4 spaces')
  })

  it('opens on its own when the command bar asked for it, and says when nothing was found', async () => {
    const { view } = await arc(true, false)
    expect(card().getByRole('status')).toHaveTextContent('No sidebar file from another browser was found on this Mac.')
    expect(revealed).toEqual([view.container.firstElementChild])
  })
})
