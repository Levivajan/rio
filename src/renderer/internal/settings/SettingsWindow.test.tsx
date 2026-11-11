// The app settings window (docs/chromium/parity-settings.md): the tab row and its title, each tab's rows saving through
// settings.set with the right key and scope, a refused write reverting with its error, the profile list, the links to
// Chromium's settings, the AI and Links switches, and the Shortcuts tab's search, recording, conflicts and resets.
import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { beforeEach, describe, expect, it } from 'vitest'
import type { IpcResult } from '../../../shared/ipc'
import { defaultProfileState, defaultSpaceState, defaultState, type CluState } from '../../../shared/state'
import type { Json } from '../../../shared/types'
import { stubClu } from '../../../test-utils/stubClu'
import { resetProfileChoice } from '../../settings-sections/data/parts/profile'
import { matchesQuery, shortcutRows } from './panes/ShortcutsPane'
import { initialTab, SettingsWindow } from './SettingsWindow'
import { spaceCounts } from './panes/ProfilesPane'
import { commandEntry } from '../../../shared/commands'

const A = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
const B = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb'
const P = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'
const S1 = 'dddddddd-dddd-4ddd-8ddd-dddddddddddd'
const S2 = 'eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee'
type SetCall = { key: string; scope: string; value: Json }

function setup(opts: { hash?: string; profiles?: number; settings?: CluState['settings']; reply?: (c: SetCall) => IpcResult<null> } = {}) {
  history.replaceState(null, '', opts.hash ?? '#general')
  const stub = stubClu({ 'import.detect': () => ({ chrome: { profiles: [] }, arc: { found: false, previous: 0 }, currentSpaceId: null }), 'settings.openBrowser': () => null, 'browsingData.open': () => null, 'tweaks.list': () => [] })
  const calls: SetCall[] = []
  const profiles: CluState['profiles'] = {
    [A]: defaultProfileState({ id: A, name: 'Personal', isDefault: true, ephemeral: false }),
    [P]: defaultProfileState({ id: P, name: 'Private', isDefault: false, ephemeral: true })
  }
  if ((opts.profiles ?? 1) > 1) profiles[B] = defaultProfileState({ id: B, name: 'Work', isDefault: false, ephemeral: false })
  const spaces = {
    [S1]: defaultSpaceState({ id: S1, name: 'Home', icon: { kind: 'letter', value: 'H' } as never, color: 'blue' as never, profileId: A, ephemeral: false }),
    [S2]: defaultSpaceState({ id: S2, name: 'Read', icon: { kind: 'letter', value: 'R' } as never, color: 'blue' as never, profileId: A, ephemeral: false })
  }
  let state: CluState = { ...defaultState(), rev: 1, profiles, spaces, settings: opts.settings ?? { global: {}, byProfile: {} } }
  const push = () => act(() => stub.push({ kind: 'snapshot', rev: state.rev, state }))
  ;(stub.clu.settings as unknown as { set: (p: SetCall) => Promise<IpcResult<null>> }).set = async (p) => {
    calls.push(p)
    const r = opts.reply?.(p) ?? { ok: true, value: null }
    if (r.ok) {
      const settings = structuredClone(state.settings)
      if (p.scope === 'global') settings.global[p.key] = p.value
      else settings.byProfile[p.scope] = { ...settings.byProfile[p.scope], [p.key]: p.value }
      state = { ...state, rev: state.rev + 1, settings }
      queueMicrotask(push)
    }
    return r
  }
  window.clu = stub.clu
  render(<SettingsWindow />)
  push()
  return { calls, stub, push }
}

const flush = () => act(async () => {
  await Promise.resolve()
  await Promise.resolve()
})

beforeEach(() => resetProfileChoice())

describe('the window', () => {
  it('shows seven tabs in the design’s order, no account tab, and the hash picks one', () => {
    setup({ hash: '#shortcuts' })
    const tabs = screen.getAllByRole('tab')
    expect(tabs.map((t) => t.textContent)).toEqual(['General', 'Profiles', 'AI features', 'Links', 'Shortcuts', 'Icon', 'Advanced'])
    expect(screen.getByRole('tab', { name: 'Shortcuts' })).toHaveAttribute('aria-selected', 'true')
    expect(document.title).toBe('Shortcuts')
  })
  it('switches tabs on click and with the arrow keys, and the title follows', () => {
    setup()
    expect(document.title).toBe('General')
    fireEvent.click(screen.getByRole('tab', { name: 'Links' }))
    expect(document.title).toBe('Links')
    expect(location.hash).toBe('#links')
    fireEvent.keyDown(screen.getByRole('tab', { name: 'Links' }), { key: 'ArrowRight' })
    expect(screen.getByRole('tab', { name: 'Shortcuts' })).toHaveAttribute('aria-selected', 'true')
    fireEvent.keyDown(screen.getByRole('tab', { name: 'Shortcuts' }), { key: 'ArrowLeft' })
    expect(document.title).toBe('Links')
  })
  it('reopens on the last tab used when settings.open names none', () => {
    expect(initialTab('', 'shortcuts')).toBe('shortcuts')
    expect(initialTab('#links', 'shortcuts')).toBe('links')
    expect(initialTab('', 'junk')).toBe('general')
    expect(initialTab('', null)).toBe('general')
    localStorage.clear()
    setup({ hash: '#ai' })
    expect(localStorage.getItem('clu.settings.lastTab')).toBe('ai')
  })
  it('falls back to General for a junk hash, and follows a later hash change (settings.open on an open window)', () => {
    setup({ hash: '#nope' })
    expect(document.title).toBe('General')
    act(() => {
      history.replaceState(null, '', '#ai')
      window.dispatchEvent(new HashChangeEvent('hashchange'))
    })
    expect(screen.getByRole('tab', { name: 'AI features' })).toHaveAttribute('aria-selected', 'true')
  })
})

describe('General', () => {
  it('is the design’s two short cards: the update and quit switches, then Previews and Profile Settings', () => {
    setup()
    expect(screen.getByRole('switch', { name: 'Warn before quitting' })).toBeInTheDocument()
    expect(screen.queryByRole('switch', { name: /Picture in Picture/i })).toBeNull()
    expect(screen.queryByRole('combobox', { name: 'Appearance' })).toBeNull()
    expect(screen.queryByRole('heading', { name: 'Import' })).toBeNull()
    expect(screen.getByText('Manage your default search engine, Auto-Archive, and downloads in Profile Settings.')).toBeInTheDocument()
  })
  it('Automatically update Rio comes first with its info glyph, is on by default and writes its global key', async () => {
    const { calls } = setup()
    const update = screen.getByRole('switch', { name: 'Automatically update Rio' })
    expect(update).toHaveAttribute('aria-checked', 'true')
    expect(update.compareDocumentPosition(screen.getByRole('switch', { name: 'Warn before quitting' })) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy()
    expect(document.querySelector('[data-info]')?.getAttribute('title')).toMatch(/^Rio checks for updates every hour/)
    fireEvent.click(update)
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'general.autoUpdate', scope: 'global', value: false })
    expect(screen.getByRole('switch', { name: 'Automatically update Rio' })).toHaveAttribute('aria-checked', 'false')
  })
  it('Previews Settings… goes to the AI features tab and Profile Settings… to the Profiles tab', () => {
    setup()
    fireEvent.click(screen.getByRole('button', { name: 'Previews Settings…' }))
    expect(screen.getByRole('tab', { name: 'AI features' })).toHaveAttribute('aria-selected', 'true')
    fireEvent.click(screen.getByRole('tab', { name: 'General' }))
    fireEvent.click(screen.getByRole('button', { name: 'Profile Settings…' }))
    expect(screen.getByRole('tab', { name: 'Profiles' })).toHaveAttribute('aria-selected', 'true')
  })
  it('ends with the import row; #import-arc, the command bar’s import command, opens it on General', async () => {
    setup()
    const row = within(screen.getByRole('group', { name: 'Import from Arc' }))
    expect(row.getByRole('button', { name: 'Import…' })).toBeInTheDocument()
    expect(row.queryByRole('button', { name: 'Bring everything over' })).toBeNull()
    act(() => {
      history.replaceState(null, '', '#import-arc')
      window.dispatchEvent(new HashChangeEvent('hashchange'))
    })
    await flush()
    expect(screen.getByRole('tab', { name: 'General' })).toHaveAttribute('aria-selected', 'true')
    // The stub finds no other browser, so the opened card says so.
    expect(screen.getByText('No sidebar file from another browser was found on this Mac.')).toBeInTheDocument()
  })
  // Import from Google Chrome (and Import from Another Browser…) load #import-chrome: the Chrome import
  // itself, first, on load and on a window already open.
  it('opens at #import-chrome with the Chrome import first, and moves an open window there', async () => {
    setup({ hash: '#import-chrome' })
    await flush()
    const region = screen.getByRole('region', { name: 'Import from Google Chrome' })
    expect(within(region).getByRole('group', { name: 'Google Chrome' })).toHaveTextContent('Google Chrome isn’t installed on this Mac')
    expect(region.compareDocumentPosition(screen.getByRole('switch', { name: 'Warn before quitting' })) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy()
    cleanup()
    setup()
    await flush()
    expect(screen.queryByRole('region', { name: 'Import from Google Chrome' })).toBeNull()
    act(() => {
      history.replaceState(null, '', '#import-chrome')
      window.dispatchEvent(new HashChangeEvent('hashchange'))
    })
    await flush()
    expect(screen.getByRole('region', { name: 'Import from Google Chrome' })).toBeInTheDocument()
  })
  it('opens at #import-arc on General with the import open', async () => {
    setup({ hash: '#import-arc' })
    await flush()
    expect(screen.getByRole('tab', { name: 'General' })).toHaveAttribute('aria-selected', 'true')
    expect(screen.getByRole('group', { name: 'Import from Arc' })).toBeInTheDocument()
    // The stub finds no other browser, so the opened card says so.
    expect(screen.getByText('No sidebar file from another browser was found on this Mac.')).toBeInTheDocument()
  })
})

describe('Advanced', () => {
  it('saves Appearance as a global setting and reverts with an error when refused', async () => {
    const { calls } = setup({ hash: '#advanced', reply: (c) => (c.value === 'dark' ? { ok: false, error: { code: 'bad-payload' } } : { ok: true, value: null }) })
    const select = screen.getByRole('combobox', { name: 'Appearance' }) as HTMLSelectElement
    expect(select.value).toBe('auto')
    fireEvent.change(select, { target: { value: 'light' } })
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'appearance.mode', scope: 'global', value: 'light' })
    expect(select.value).toBe('light')
    fireEvent.change(select, { target: { value: 'dark' } })
    await flush()
    expect(select.value).toBe('light')
    expect(screen.getByRole('alert')).toHaveTextContent('That value isn’t allowed')
  })
  it('opens with the design’s switches in its three cards, in its order, each with its colored tile', () => {
    setup({ hash: '#advanced' })
    const switches = screen.getAllByRole('switch').map((s) => s.getAttribute('aria-label') ?? s.textContent)
    expect(switches).toEqual([
      'Haptic feedback when reordering tabs',
      'Show full URL when Toolbar is enabled',
      'Enable Picture in Picture when you leave a video tab',
      'Enable Site tweaks on websites you visit'
    ])
    const cards = [...document.querySelectorAll('.clu-group')].slice(0, 3)
    expect(cards.map((c) => c.querySelectorAll('[role="switch"]').length)).toEqual([1, 1, 2])
    const tints = [...document.querySelectorAll('.clu-sw-tile')].map((t) => t.getAttribute('data-tint'))
    expect(tints).toEqual(['blue', 'red', 'magenta', 'magenta'])
    for (const t of document.querySelectorAll('.clu-sw-tile')) {
      expect(t).toHaveAttribute('aria-hidden', 'true')
      expect(t.querySelector('svg')).not.toBeNull()
    }
    // Nothing for the design's switches Rio has no behavior for.
    for (const name of [/sound effects/i, /restore windows/i, /window dragging/i, /Shared Quotes/i, /theme data/i]) {
      expect(screen.queryByText(name)).toBeNull()
    }
  })
  it('Haptic feedback when reordering tabs is on by default and writes its global key', async () => {
    const { calls } = setup({ hash: '#advanced' })
    const haptic = screen.getByRole('switch', { name: 'Haptic feedback when reordering tabs' })
    expect(haptic).toHaveAttribute('aria-checked', 'true')
    fireEvent.click(haptic)
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'tabs.reorderHaptic', scope: 'global', value: false })
    expect(screen.getByRole('switch', { name: 'Haptic feedback when reordering tabs' })).toHaveAttribute('aria-checked', 'false')
  })
  it('Enable Picture in Picture when you leave a video tab is on by default and writes its global key', async () => {
    const { calls } = setup({ hash: '#advanced' })
    const pip = screen.getByRole('switch', { name: 'Enable Picture in Picture when you leave a video tab' })
    expect(pip).toHaveAttribute('aria-checked', 'true')
    fireEvent.click(pip)
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'general.autoPictureInPicture', scope: 'global', value: false })
    expect(screen.getByRole('switch', { name: 'Enable Picture in Picture when you leave a video tab' })).toHaveAttribute('aria-checked', 'false')
  })
  it('Show full URL when Toolbar is enabled is off by default and writes its global key', async () => {
    const { calls } = setup({ hash: '#advanced' })
    const full = screen.getByRole('switch', { name: 'Show full URL when Toolbar is enabled' })
    expect(full).toHaveAttribute('aria-checked', 'false')
    fireEvent.click(full)
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'toolbar.fullUrl', scope: 'global', value: true })
  })
  it('Enable Site tweaks on websites you visit is on by default and writes its global key', async () => {
    const { calls } = setup({ hash: '#advanced' })
    const tweaks = screen.getByRole('switch', { name: 'Enable Site tweaks on websites you visit' })
    expect(tweaks).toHaveAttribute('aria-checked', 'true')
    fireEvent.click(tweaks)
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'tweaks.enabled', scope: 'global', value: false })
    expect(screen.getByRole('switch', { name: 'Enable Site tweaks on websites you visit' })).toHaveAttribute('aria-checked', 'false')
  })
  it('has the Default browser row and the import, and More Settings… opens Chromium’s settings', async () => {
    const { stub } = setup({ hash: '#advanced' })
    expect(screen.getByText('Default browser')).toBeInTheDocument()
    expect(screen.getByRole('heading', { name: 'Import' })).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: 'More Settings…' }))
    await flush()
    expect(stub.calls.filter((c) => c.name === 'settings.openBrowser').map((c) => c.payload)).toEqual([{ page: 'root' }])
  })
})

describe('Profiles', () => {
  it('lists saved profiles with their space counts and edits the picked one', async () => {
    const { calls } = setup({ hash: '#profiles', profiles: 2 })
    const list = screen.getByRole('listbox', { name: 'Your Profiles' })
    const options = within(list).getAllByRole('option')
    expect(options.map((o) => o.textContent)).toEqual(['Personal2 Spaces', 'WorkNot used'])
    expect(options[0]).toHaveAttribute('aria-selected', 'true')
    fireEvent.change(screen.getByRole('combobox', { name: 'Search engine' }), { target: { value: 'duckduckgo' } })
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'search.engineId', scope: A, value: 'duckduckgo' })
    fireEvent.click(options[1] as HTMLElement)
    fireEvent.change(screen.getByRole('combobox', { name: 'Archive tabs after' }), { target: { value: '168' } })
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'tabs.archiveAfterHours', scope: B, value: 168 })
  })
  it('opens Chromium’s pages and Rio’s Clear browsing data dialog for the picked profile', async () => {
    const { stub } = setup({ hash: '#profiles' })
    const links = screen.getByRole('list', { name: 'Your data and settings' })
    expect(within(links).getAllByRole('button').map((b) => b.textContent)).toEqual(['Privacy and Security', 'Passwords', 'Credit Cards', 'Notifications', 'Clear Browsing Data'])
    for (const b of within(links).getAllByRole('button')) fireEvent.click(b)
    fireEvent.click(screen.getByRole('button', { name: 'Search Settings…' }))
    fireEvent.click(screen.getByRole('button', { name: 'Downloads…' }))
    await flush()
    expect(stub.calls.filter((c) => c.name === 'settings.openBrowser').map((c) => (c.payload as { page: string }).page)).toEqual(['privacy', 'passwords', 'payments', 'notifications', 'search', 'downloads'])
    expect(stub.calls.filter((c) => c.name === 'browsingData.open').map((c) => c.payload)).toEqual([{ profileId: A }])
  })
  it('the list bar adds a profile and picks it, renames the picked one, and removes only an unused one', async () => {
    const { stub, push } = setup({ hash: '#profiles', profiles: 2 })
    ;(stub.clu.profiles as unknown as Record<string, (p: unknown) => Promise<IpcResult<unknown>>>).create = async (p) => {
      stub.calls.push({ name: 'profiles.create', payload: p })
      return { ok: true, value: { profileId: B } }
    }
    ;(stub.clu.profiles as unknown as Record<string, (p: unknown) => Promise<IpcResult<unknown>>>).rename = async (p) => {
      stub.calls.push({ name: 'profiles.rename', payload: p })
      return { ok: true, value: null }
    }
    ;(stub.clu.profiles as unknown as Record<string, (p: unknown) => Promise<IpcResult<unknown>>>).delete = async (p) => {
      stub.calls.push({ name: 'profiles.delete', payload: p })
      return { ok: false, error: { code: 'rejected' } }
    }
    // The default profile, used by two spaces, cannot be removed.
    expect(screen.getByRole('button', { name: 'Remove' })).toBeDisabled()
    fireEvent.click(screen.getByRole('button', { name: 'Add' }))
    await flush()
    push()
    expect(stub.calls.find((c) => c.name === 'profiles.create')?.payload).toEqual({ name: 'New Profile' })
    expect(within(screen.getByRole('listbox', { name: 'Your Profiles' })).getByRole('option', { selected: true })).toHaveTextContent('Work')
    fireEvent.click(screen.getByRole('button', { name: 'Rename' }))
    const field = screen.getByRole('textbox', { name: 'New name for Work' })
    fireEvent.change(field, { target: { value: '  Clients  ' } })
    fireEvent.keyDown(field, { key: 'Enter' })
    await flush()
    expect(stub.calls.find((c) => c.name === 'profiles.rename')?.payload).toEqual({ profileId: B, name: 'Clients' })
    fireEvent.click(screen.getByRole('button', { name: 'Remove' }))
    await flush()
    expect(stub.calls.find((c) => c.name === 'profiles.delete')?.payload).toEqual({ profileId: B })
    expect(screen.getByRole('alert')).toHaveTextContent('The profile could not be changed.')
  })
  it('counts spaces per profile and skips private spaces and junk rows', () => {
    const s = { ...defaultState(), profiles: { [A]: defaultProfileState({ id: A, name: 'A', isDefault: true, ephemeral: false }), [B]: defaultProfileState({ id: B, name: 'B', isDefault: false, ephemeral: false }) } }
    const sp = (id: string, profileId: string, ephemeral = false) => defaultSpaceState({ id, name: id, icon: { kind: 'letter', value: 'x' } as never, color: 'blue' as never, profileId, ephemeral })
    expect(spaceCounts({ ...s, spaces: { [S1]: sp(S1, A), [S2]: sp(S2, A, true), junk: null as never } })).toEqual({ [A]: '1 Space', [B]: 'Not used' })
  })
})

describe('AI features and Links', () => {
  it('lists the six AI features, each with a switch for its global key, on by default', async () => {
    const { calls } = setup({ hash: '#ai' })
    expect(document.querySelector('.clu-sw-hero')).toHaveAttribute('aria-hidden', 'true')
    const switches = screen.getAllByRole('switch')
    expect(switches.map((s) => s.getAttribute('aria-label'))).toEqual(['Tidy Tabs', 'Instant Links', 'Hover Previews', 'Tidy Titles', 'Tidy Downloads', 'Ask in the Command Bar'])
    expect(switches.every((s) => s.getAttribute('aria-checked') === 'true')).toBe(true)
    fireEvent.click(screen.getByRole('switch', { name: 'Tidy Tabs' }))
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'ai.tidyTabs', scope: 'global', value: false })
    expect(screen.getByRole('switch', { name: 'Tidy Tabs' })).toHaveAttribute('aria-checked', 'false')
  })
  it('reads a stored off switch, and junk as the default', () => {
    setup({ hash: '#ai', settings: { global: { 'ai.askRow': false, 'ai.tidyTitles': 'junk' }, byProfile: {} } })
    expect(screen.getByRole('switch', { name: 'Ask in the Command Bar' })).toHaveAttribute('aria-checked', 'false')
    expect(screen.getByRole('switch', { name: 'Tidy Titles' })).toHaveAttribute('aria-checked', 'true')
  })
  it('the Links switches save their keys', async () => {
    const { calls } = setup({ hash: '#links' })
    expect(document.querySelector('.clu-sw-hero')).toHaveAttribute('aria-hidden', 'true')
    expect(screen.getByText('Note: This only affects Favorites and Pinned tabs').tagName).toBe('EM')
    for (const name of ['Links from other apps open in a Mini window', 'Open a Peek when clicking links with Shift held', 'Open a Peek when clicking links to other sites']) fireEvent.click(screen.getByRole('switch', { name }))
    await flush()
    expect(calls.map((c) => [c.key, c.value])).toEqual([
      ['links.miniFromOtherApps', false],
      ['links.peekOnShiftClick', false],
      ['links.peekOtherSites', false]
    ])
  })
})

describe('Shortcuts', () => {
  const field = (title: string) => screen.getByRole('button', { name: new RegExp(`^${title.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}: `) })
  const press = (init: KeyboardEventInit) => act(() => void window.dispatchEvent(new KeyboardEvent('keydown', { bubbles: true, cancelable: true, ...init })))

  it('lists every command once, sorted, with its keys or ---', () => {
    setup({ hash: '#shortcuts' })
    expect(field('Close Tab')).toHaveTextContent('⌘W')
    expect(field('Browser Settings')).toHaveTextContent('---')
    const ids = shortcutRows().map((e) => e.id)
    expect(new Set(ids).size).toBe(ids.length)
  })
  it('searches names, aliases and keys typed as symbols or words', () => {
    const close = commandEntry('tabs.close')
    if (!close) throw new Error('tabs.close missing')
    for (const q of ['close tab', 'CLOSE', '⌘W', 'cmd w', 'Cmd+W', 'command w']) expect(matchesQuery(close, q), q).toBe(true)
    for (const q of ['⌘Q', 'shift cmd w', 'zzz']) expect(matchesQuery(close, q), q).toBe(false)
    setup({ hash: '#shortcuts' })
    expect(screen.getByRole('searchbox', { name: 'Search shortcuts' })).toHaveAttribute('placeholder', 'Type a feature name or shortcut…')
    fireEvent.change(screen.getByRole('searchbox', { name: 'Search shortcuts' }), { target: { value: 'zzz' } })
    expect(screen.getByText('No action matches “zzz”.')).toBeInTheDocument()
  })
  it('records a new shortcut: the next key with a modifier saves it, and the row moves to Custom Shortcuts', async () => {
    const { calls } = setup({ hash: '#shortcuts' })
    fireEvent.click(field('Close Tab'))
    expect(field('Close Tab')).toHaveTextContent('Type shortcut')
    press({ code: 'KeyE', key: 'e', metaKey: true, ctrlKey: true })
    await flush()
    expect(calls.at(-1)).toEqual({ key: 'shortcuts.custom', scope: 'global', value: { 'tabs.close': 'Cmd+Ctrl+E' } })
    expect(field('Close Tab')).toHaveTextContent('⌃⌘E')
    expect(within(screen.getByRole('list', { name: 'Custom shortcuts' })).getByText('Close Tab')).toBeInTheDocument()
  })
  it('Esc cancels, Backspace leaves the command no key, and a lone modifier keeps recording', async () => {
    const { calls } = setup({ hash: '#shortcuts' })
    fireEvent.click(field('Close Tab'))
    press({ code: 'MetaLeft', key: 'Meta', metaKey: true })
    expect(field('Close Tab')).toHaveTextContent('Type shortcut')
    press({ code: 'Escape', key: 'Escape' })
    expect(field('Close Tab')).toHaveTextContent('⌘W')
    expect(calls).toEqual([])
    fireEvent.click(field('Close Tab'))
    press({ code: 'Backspace', key: 'Backspace' })
    await flush()
    expect(calls.at(-1)?.value).toEqual({ 'tabs.close': null })
    expect(field('Close Tab')).toHaveTextContent('---')
  })
  it('refuses keys macOS keeps and keys without Cmd, Ctrl or Option, saying why', () => {
    const { calls } = setup({ hash: '#shortcuts' })
    fireEvent.click(field('Close Tab'))
    press({ code: 'KeyQ', key: 'q', metaKey: true })
    expect(screen.getByRole('alert')).toHaveTextContent('macOS keeps ⌘Q for itself.')
    fireEvent.click(field('Close Tab'))
    press({ code: 'KeyK', key: 'K', shiftKey: true })
    expect(screen.getByRole('alert')).toHaveTextContent('Use ⌘, ⌃ or ⌥ with the key.')
    expect(calls).toEqual([])
  })
  it('names a conflict, and Replace gives the key to this command in one write', async () => {
    const { calls } = setup({ hash: '#shortcuts' })
    fireEvent.click(field('Close Tab'))
    press({ code: 'KeyY', key: 'y', metaKey: true })
    expect(screen.getByRole('alert')).toHaveTextContent('⌘Y is used by Show History.')
    expect(calls).toEqual([])
    fireEvent.click(screen.getByRole('button', { name: 'Replace' }))
    await flush()
    expect(calls.at(-1)?.value).toEqual({ 'tabs.close': 'Cmd+Y' })
    // History lost its key to the custom one.
    expect(field('Show History')).toHaveTextContent('---')
    expect(field('Close Tab')).toHaveTextContent('⌘Y')
  })
  it('Replace takes a key from another custom entry by clearing it', async () => {
    const { calls } = setup({ hash: '#shortcuts', settings: { global: { 'shortcuts.custom': { 'history.open': 'Cmd+Ctrl+E' } }, byProfile: {} } })
    fireEvent.click(field('Close Tab'))
    press({ code: 'KeyE', key: 'e', metaKey: true, ctrlKey: true })
    fireEvent.click(screen.getByRole('button', { name: 'Replace' }))
    await flush()
    expect(calls.at(-1)?.value).toEqual({ 'history.open': null, 'tabs.close': 'Cmd+Ctrl+E' })
  })
  it('typing a command’s own key restores it, a row resets alone, and Reset All asks first', async () => {
    const { calls } = setup({ hash: '#shortcuts', settings: { global: { 'shortcuts.custom': { 'tabs.close': 'Cmd+Ctrl+E', 'history.open': null } }, byProfile: {} } })
    fireEvent.click(field('Close Tab'))
    press({ code: 'KeyW', key: 'w', metaKey: true })
    await flush()
    expect(calls.at(-1)?.value).toEqual({ 'history.open': null })
    fireEvent.click(screen.getByRole('button', { name: 'Reset Show History to ⌘Y' }))
    await flush()
    expect(calls.at(-1)?.value).toEqual({})
    expect(screen.getByRole('button', { name: 'Reset All Shortcuts' })).toBeDisabled()
  })
  it('Reset All Shortcuts clears every change after the confirm', async () => {
    const { calls } = setup({ hash: '#shortcuts', settings: { global: { 'shortcuts.custom': { 'tabs.close': 'Cmd+Ctrl+E' } }, byProfile: {} } })
    fireEvent.click(screen.getByRole('button', { name: 'Reset All Shortcuts' }))
    const dialog = screen.getByRole('dialog', { name: 'Reset all shortcuts?' })
    fireEvent.click(within(dialog).getByRole('button', { name: 'Cancel' }))
    expect(calls).toEqual([])
    fireEvent.click(screen.getByRole('button', { name: 'Reset All Shortcuts' }))
    fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Reset All' }))
    await flush()
    expect(calls.at(-1)?.value).toEqual({})
    expect(field('Close Tab')).toHaveTextContent('⌘W')
  })
})
