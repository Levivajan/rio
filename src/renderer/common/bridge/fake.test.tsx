import { act, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { App } from '../../ui/App'
import { installBridge } from '../clu'
import { createStateMirror, setStateMirrorForTest } from '../store'
import { createFakeHost } from './fake'
import { FAKE } from './fake-seed'

afterEach(() => {
  installBridge(null)
  vi.unstubAllGlobals()
})

describe('FakeBridge guard', () => {
  it('rejects what the browser would: wrong surface, bad payload, and commands it has no handler for', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    expect(await ui.command.execute({ sessionId: FAKE.window, result: { kind: 'navigate', url: 'https://example.com/' }, disposition: 'default', input: '' })).toMatchObject({ ok: false, error: { code: 'not-allowed' } })
    expect(await (ui.tabs.activate as unknown as (p: unknown) => Promise<unknown>)({ tabId: 'not-a-uuid' })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    expect(await ui.tabs.rename({ nodeId: FAKE.tab(20), title: 'x'.repeat(201) })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    // Permission prompts are Chromium's own bubbles, so the overlay's respond has no handler.
    const overlay = host.connect({ surface: 'overlay' })
    expect(await overlay.permission.respond({ promptId: FAKE.tab(20), action: 'allow' })).toMatchObject({ ok: false, error: { code: 'not-implemented' } })
    expect(host.state().tabs[FAKE.tab(20)]?.titleOverride).toBeNull()
  })
})

describe('FakeBridge site permissions', () => {
  it('answers permission.* as the browser does over Chromium site settings: web origins and known kinds only', async () => {
    const host = createFakeHost()
    const overlay = host.connect({ surface: 'overlay' })
    const settings = host.connect({ surface: 'internal:settings' })
    const all = await settings.permission.list({ profileId: FAKE.profile })
    expect(all.ok && [...new Set(all.value.map((r) => r.origin))]).toEqual(['https://meet.example.com', 'https://alerts.example.org'])
    const origin = 'https://docs.example.com'
    expect(await overlay.permission.set({ profileId: FAKE.profile, origin: `${origin}/a/b`, permission: 'camera', decision: 'block' })).toMatchObject({ ok: true })
    expect(await overlay.permission.set({ profileId: FAKE.profile, origin, permission: 'midi', decision: 'allow' })).toMatchObject({ ok: true })
    const mine = await overlay.permission.list({ profileId: FAKE.profile, origin })
    expect(mine.ok && mine.value.map((r) => `${r.origin} ${r.permission} ${r.decision}`)).toEqual([`${origin} camera deny`, `${origin} midi allow`])
    // Ask removes the decision; junk is refused and changes nothing.
    expect(await overlay.permission.set({ profileId: FAKE.profile, origin, permission: 'camera', decision: 'ask' })).toMatchObject({ ok: true })
    expect(await overlay.permission.set({ profileId: FAKE.profile, origin, permission: 'junk', decision: 'allow' })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    expect(await overlay.permission.set({ profileId: FAKE.profile, origin: 'file:///etc/hosts', permission: 'camera', decision: 'allow' })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    expect(await overlay.permission.list({ profileId: FAKE.profile, origin: 'chrome://settings/' })).toEqual({ ok: true, value: [] })
    const after = await overlay.permission.list({ profileId: FAKE.profile, origin })
    expect(after.ok && after.value.map((r) => r.permission)).toEqual(['midi'])
    expect(await settings.permission.clear({ profileId: FAKE.profile })).toMatchObject({ ok: true })
    expect(await settings.permission.list({ profileId: FAKE.profile })).toEqual({ ok: true, value: [] })
  })
})

describe('FakeBridge state channel', () => {
  it('sends a snapshot, then one coalesced patch with rev + 1 that the store mirror applies', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const mirror = createStateMirror(async () => host.state())
    ui.onState((m) => mirror.receive(m))
    await Promise.resolve()
    const rev = mirror.get().rev
    await ui.tabs.activate({ tabId: FAKE.tab(22) })
    await ui.tabs.setMuted({ tabId: FAKE.tab(22), muted: true })
    host.flush()
    expect(mirror.get().rev).toBe(rev + 1)
    expect(mirror.get().windows[FAKE.window]?.activeTabId).toBe(FAKE.tab(22))
    expect(mirror.get().tabs[FAKE.tab(22)]?.muted).toBe(true)
  })

  it('runs the command bar: open shows the surface, execute opens the page and closes the bar', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const overlay = host.connect({ surface: 'overlay', layer: 'top' })
    await ui.commands.run({ id: 'commandbar.newTab' })
    const w = () => host.state().windows[FAKE.window]
    const sessionId = w()?.command?.sessionId ?? ''
    expect(w()?.overlay.surfaces.map((s) => [s.surface, s.layer, s.modality])).toEqual([['command', 'top', 'modal']])
    expect(await overlay.command.execute({ sessionId, result: { kind: 'navigate', url: 'https://example.org/new' }, disposition: 'default', input: 'example.org/new' })).toEqual({ ok: true, value: { ok: true } })
    const active = w()?.activeTabId ?? ''
    expect(host.state().tabs[active]?.url).toBe('https://example.org/new')
    expect(w()?.command).toBeNull()
    expect(w()?.overlay.surfaces).toEqual([])
  })

  it('lists every action unmatched and runs palette and provider rows; the other mode switches in place', async () => {
    const host = createFakeHost(undefined, { historyRows: 10_000 })
    const ui = host.connect({ surface: 'ui' })
    const overlay = host.connect({ surface: 'overlay', layer: 'top' })
    const w = () => host.state().windows[FAKE.window]
    await ui.commands.run({ id: 'commandbar.newTab' })
    const sessionId = w()?.command?.sessionId ?? ''
    await ui.commands.run({ id: 'commandbar.editUrl' })
    expect(w()?.command).toMatchObject({ sessionId, mode: 'edit-url' })
    const index = await overlay.command.syncIndex({ sinceRev: 0 })
    expect(index.ok && index.value.upserts).toHaveLength(10_000)
    const actions = await overlay.command.actions({ sessionId, query: 'zzz no match' })
    if (!actions.ok) throw new Error('command.actions failed')
    expect(actions.value.some((r) => r.id === 'sidebar.toggle')).toBe(true)
    const provider = actions.value.find((r) => r.providerId === 'spaces')
    expect(provider?.title).toMatch(/^Switch to /)
    expect(await overlay.command.execute({ sessionId, result: { kind: 'command', id: 'sidebar.toggle' }, disposition: 'default', input: '' })).toEqual({ ok: true, value: { ok: true } })
    expect(w()?.shell.sidebar.hidden).toBe(true)
    expect(w()?.command).toBeNull()
    await ui.commands.run({ id: 'commandbar.newTab' })
    const next = w()?.command?.sessionId ?? ''
    expect(await overlay.command.execute({ sessionId: next, result: { kind: 'provided', providerId: 'spaces', token: provider?.token ?? '' }, disposition: 'default', input: '' })).toEqual({ ok: true, value: { ok: true } })
    expect(w()?.activeSpaceId).toBe(provider?.token)
  })

  it('opens a row menu as a light-dismiss overlay surface and acts on the picked item', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const overlay = host.connect({ surface: 'overlay', layer: 'top' })
    await ui.tabs.openMenu({ menu: 'row', nodeId: FAKE.tab(24), anchor: { x: 10, y: 20 } })
    const menu = host.state().windows[FAKE.window]?.overlay.surfaces[0]
    expect(menu).toMatchObject({ surface: 'menu', modality: 'light-dismiss', rect: { x: 10, y: 20, width: 0, height: 0 } })
    await overlay.overlay.respond({ instanceId: menu?.instanceId ?? '', result: 'tabs.close' })
    expect(host.state().tabs[FAKE.tab(24)]).toBeUndefined()
    expect(host.state().windows[FAKE.window]?.overlay.surfaces).toEqual([])
  })

  it('hides the sidebar and lays the page out again with the shared layout math', async () => {
    const host = createFakeHost()
    host.setContentSize({ width: 1000, height: 700 })
    await host.connect({ surface: 'ui' }).shell.toggleSidebar()
    expect(host.state().windows[FAKE.window]?.shell.layout.pageRect).toEqual({ x: 10, y: 10, width: 980, height: 680 })
  })
})

describe('frame UI over the FakeBridge', () => {
  it('renders the seeded sidebar and activates a row through clu()', async () => {
    vi.stubGlobal('ResizeObserver', class { observe() {} unobserve() {} disconnect() {} })
    const host = createFakeHost()
    installBridge(host.connect({ surface: 'ui' }))
    setStateMirrorForTest(null)
    render(<App />)
    expect(await screen.findByText('Morning headlines')).toBeInTheDocument()
    expect(screen.getByText('Chromium M2')).toBeInTheDocument()
    await act(async () => {
      screen.getByText('Morning headlines').click()
      await Promise.resolve()
    })
    expect(host.state().windows[FAKE.window]?.activeTabId).toBe(FAKE.tab(20))
    expect(host.calls.filter((c) => !c.result.ok && c.result.error.code !== 'not-implemented')).toEqual([])
  })
})

describe('FakeBridge M3 rows and archive', () => {
  it('searches the archive literally and pages through it', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const percent = await ui.archive.list({ profileId: FAKE.profile, query: '100%' })
    expect(percent.ok && percent.value.rows.map((r) => r.title)).toEqual(['100% done: deals of the day'])
    const first = await ui.archive.list({ profileId: FAKE.profile, limit: 2 })
    expect(first.ok && first.value.rows).toHaveLength(2)
    const cursor = first.ok ? (first.value.nextCursor ?? undefined) : undefined
    const second = await ui.archive.list({ profileId: FAKE.profile, limit: 2, ...(cursor ? { cursor } : {}) })
    const ids = [...(first.ok ? first.value.rows : []), ...(second.ok ? second.value.rows : [])].map((r) => r.id)
    expect(new Set(ids).size).toBe(4)
  })

  it('restores an archived row at the top of Today and bumps the revision', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const list = await ui.archive.list({ profileId: FAKE.profile })
    const row = list.ok ? list.value.rows[0] : undefined
    const revision = host.state().archiveRevision
    expect(await ui.archive.restore({ archiveId: row?.id ?? '' })).toMatchObject({ ok: true })
    const today = host.state().spaces[FAKE.spaces.home]?.today ?? []
    expect(host.state().tabs[today[0] ?? '']?.url).toBe(row?.url)
    expect(host.state().windows[FAKE.window]?.activeTabId).toBe(today[0])
    expect(host.state().archiveRevision).toBe(revision + 1)
  })

  it('clears Today with an Undo toast and undoes it', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const before = host.state().spaces[FAKE.spaces.home]?.today ?? []
    await ui.archive.clearToday({})
    expect(host.state().spaces[FAKE.spaces.home]?.today).toEqual([])
    expect(host.state().windows[FAKE.window]?.toasts.at(-1)).toMatchObject({ text: `Cleared ${before.length} tabs`, action: { label: 'Undo', commandId: 'archive.undoClear' } })
    await ui.commands.run({ id: 'archive.undoClear' })
    expect(host.state().spaces[FAKE.spaces.home]?.today).toEqual(before)
    expect(host.state().windows[FAKE.window]?.toasts).toEqual([])
  })

  it('moves a Today row to Pinned and takes a thirteenth favorite (the grid grows by rows)', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    expect(await ui.tabs.move({ nodeId: FAKE.tab(20), target: { section: 'pinned', spaceId: FAKE.spaces.home, parentId: null, index: 0 } })).toEqual({ ok: true, value: { status: 'moved' } })
    expect(host.state().tabs[FAKE.tab(20)]).toMatchObject({ section: 'pinned', pinnedUrl: 'https://news.example.com/' })
    expect(host.state().spaces[FAKE.spaces.home]?.pinned[0]).toEqual({ kind: 'tab', id: FAKE.tab(20) })
    for (const n of [21, 22, 23, 24, 31, 40, 10, 11]) {
      await ui.tabs.move({ nodeId: FAKE.tab(n), target: { section: 'favorite', index: 0 } })
    }
    expect(host.state().favorites[FAKE.profile]).toHaveLength(12)
    await ui.tabs.move({ nodeId: FAKE.tab(30), target: { section: 'favorite', index: 0 }, ask: true })
    expect(host.state().favorites[FAKE.profile]).toHaveLength(13)
  })
})

describe('FakeBridge M4 spaces', () => {
  const { home, work, reading } = FAKE.spaces

  it('switches without wrapping, and an index past the end does nothing', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const active = () => host.state().windows[FAKE.window]?.activeSpaceId
    await ui.spaces.switch({ step: -1 })
    expect(active()).toBe(home)
    await ui.spaces.switch({ index: 8 })
    expect(active()).toBe(home)
    await ui.spaces.switch({ index: 2 })
    expect(active()).toBe(reading)
    await ui.commands.run({ id: 'spaces.next' })
    expect(active()).toBe(reading)
    await ui.commands.run({ id: 'spaces.prev' })
    expect(active()).toBe(work)
  })

  it('follows a swipe in windows[id].swipe, ignores momentum, and switches one space on the end', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    await ui.spaces.swipe({ phase: 'update', dx: 60, dy: 0, momentum: false })
    const swipe = host.state().windows[FAKE.window]?.swipe
    expect(swipe?.targetSpaceId).toBe(work)
    expect(swipe?.fraction).toBeGreaterThan(0)
    await ui.spaces.swipe({ phase: 'update', dx: 5000, dy: 0, momentum: true })
    expect(host.state().windows[FAKE.window]?.swipe?.fraction).toBe(swipe?.fraction)
    await ui.spaces.swipe({ phase: 'end', dx: 60, dy: 0, momentum: false })
    expect(host.state().windows[FAKE.window]?.swipe).toBeNull()
    expect(host.state().windows[FAKE.window]?.activeSpaceId).toBe(work)
  })

  it('creates, edits, reorders and deletes spaces with the browser rules', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const made = await ui.spaces.create({ icon: { kind: 'emoji', value: 'abc' } })
    expect(made).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    const created = await ui.spaces.create({ color: 'teal' })
    if (!created.ok) throw new Error('create failed')
    const id = created.value.spaceId
    expect(host.state().spaces[id]?.name).toBe('Space 4')
    expect(host.state().windows[FAKE.window]?.activeSpaceId).toBe(id)
    expect(await ui.spaces.update({ spaceId: id, name: '  ' })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    await ui.spaces.update({ spaceId: id, name: 'Garden' })
    expect(host.state().spaces[id]?.name).toBe('Garden')
    await ui.spaces.reorder({ spaceId: id, toIndex: -5 })
    expect(host.state().spaceOrder[0]).toBe(id)
    // A new space is lavender by default and lands right after the window's space.
    const before = host.state().spaceOrder
    const next = await ui.spaces.create({})
    if (!next.ok) throw new Error('create failed')
    expect(host.state().spaces[next.value.spaceId]?.color).toBe('indigo')
    expect(host.state().spaceOrder).toEqual([before[0], next.value.spaceId, ...before.slice(1)])
    await ui.spaces.delete({ spaceId: next.value.spaceId })
    // Deleting the shown space falls back to the previous one, else the next.
    await ui.spaces.delete({ spaceId: id })
    expect(host.state().spaces[id]).toBeUndefined()
    expect(host.state().windows[FAKE.window]?.activeSpaceId).toBe(home)
    await ui.spaces.delete({ spaceId: work })
    await ui.spaces.delete({ spaceId: reading })
    expect(await ui.spaces.delete({ spaceId: home })).toMatchObject({ ok: false, error: { code: 'rejected', reason: 'last-space' } })
  })

  it('opens the space editor as a modal overlay surface', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    await ui.spaces.openEditor({ spaceId: work })
    const surfaces = host.state().windows[FAKE.window]?.overlay.surfaces ?? []
    expect(surfaces.map((s) => [s.surface, s.props])).toEqual([['space-editor', { spaceId: work, view: 'space' }]])
    expect(await ui.spaces.openEditor({ spaceId: FAKE.tab(99) })).toMatchObject({ ok: false, error: { code: 'not-found' } })
  })
})

describe('FakeBridge M12 updates', () => {
  const texts = (host: ReturnType<typeof createFakeHost>) => (host.state().windows[FAKE.window]?.toasts ?? []).map((t) => t.text)

  it('has no updater by default, and says so for a check', async () => {
    const host = createFakeHost()
    const overlay = host.connect({ surface: 'overlay' })
    expect(host.state().app.update).toEqual({ status: 'unavailable', version: null, checkedAt: null, dismissed: false })
    expect(await overlay.platform.checkForUpdates()).toEqual({ ok: true, value: { status: 'unavailable' } })
    expect(await overlay.platform.installUpdate()).toEqual({ ok: true, value: { status: 'not-ready' } })
    expect(texts(host)).toEqual(["Updates are off in this copy of Rio"])
  })

  it('checks once, toasts up to date, and refuses a second check while one runs', async () => {
    vi.useFakeTimers()
    try {
      const host = createFakeHost(undefined, { update: 'idle' })
      const ui = host.connect({ surface: 'ui' })
      expect(await ui.platform.checkForUpdates()).toEqual({ ok: true, value: { status: 'checking' } })
      expect(host.state().app.update.status).toBe('checking')
      expect(await ui.platform.checkForUpdates()).toEqual({ ok: true, value: { status: 'busy' } })
      vi.advanceTimersByTime(60)
      expect(host.state().app.update.status).toBe('up-to-date')
      // One toast at a time: the up-to-date toast replaces the busy one.
      expect(texts(host)).toEqual(['Rio is up to date'])
    } finally {
      vi.useRealTimers()
    }
  })

  it('starts ready with a version and installs from the sidebar', async () => {
    const host = createFakeHost(undefined, { update: 'ready' })
    const ui = host.connect({ surface: 'ui' })
    expect(host.state().app.update).toMatchObject({ status: 'ready', version: '158.0.8127.19' })
    expect(await ui.platform.installUpdate()).toEqual({ ok: true, value: { status: 'installing' } })
    expect(await ui.platform.checkForUpdates()).toEqual({ ok: true, value: { status: 'ready' } })
    expect(texts(host)).toEqual(['Relaunch Rio to install the update'])
    // Surfaces the contract does not allow are refused before the handler runs.
    const mini = host.connect({ surface: 'mini-ui' })
    expect(await mini.platform.installUpdate()).toMatchObject({ ok: false, error: { code: 'not-allowed' } })
  })

  it('dismisses only a ready update, from the sidebar only, and keeps it ready', async () => {
    const idle = createFakeHost(undefined, { update: 'idle' })
    expect(await idle.connect({ surface: 'ui' }).platform.dismissUpdate()).toEqual({ ok: true, value: { status: 'not-ready' } })
    const host = createFakeHost(undefined, { update: 'ready' })
    expect(await host.connect({ surface: 'overlay' }).platform.dismissUpdate()).toMatchObject({ ok: false, error: { code: 'not-allowed' } })
    expect(await host.connect({ surface: 'ui' }).platform.dismissUpdate()).toEqual({ ok: true, value: { status: 'dismissed' } })
    expect(host.state().app.update).toMatchObject({ status: 'ready', version: '158.0.8127.19', dismissed: true })
  })
})

describe('FakeBridge M9 downloads', () => {
  const id = (n: number) => `00000000-0000-4000-8000-0007${n.toString(16).padStart(8, '0')}`

  it('acts on rows by id with the browser rules and refuses what a row cannot do now', async () => {
    const host = createFakeHost(undefined, { downloads: true })
    const ui = host.connect({ surface: 'ui' })
    const state = (n: number) => host.state().downloads.find((d) => d.id === id(n))?.state
    expect(await ui.download.act({ id: id(1), action: 'pause' })).toMatchObject({ ok: true })
    expect(state(1)).toBe('paused')
    expect(await ui.download.act({ id: id(1), action: 'pause' })).toMatchObject({ ok: false, error: { code: 'rejected', reason: 'unavailable' } })
    expect(await ui.download.act({ id: id(1), action: 'resume' })).toMatchObject({ ok: true })
    expect(state(1)).toBe('progressing')
    // A running file cannot open, a removed one neither; a well-formed unknown id changes nothing.
    expect(await ui.download.act({ id: id(1), action: 'open' })).toMatchObject({ ok: false, error: { code: 'rejected' } })
    expect(await ui.download.act({ id: id(4), action: 'open' })).toMatchObject({ ok: false, error: { code: 'rejected' } })
    expect(await ui.download.act({ id: id(9), action: 'cancel' })).toMatchObject({ ok: false, error: { code: 'not-found' } })
    // A path is never a row id.
    expect(await (ui.download.act as unknown as (p: unknown) => Promise<unknown>)({ id: '/Users/x/Downloads/a.dmg', action: 'open' })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    expect(await ui.download.act({ id: id(3), action: 'remove' })).toMatchObject({ ok: true })
    expect(state(3)).toBeUndefined()
  })

  it('clears finished rows and keeps running ones', async () => {
    const host = createFakeHost(undefined, { downloads: true })
    const ui = host.connect({ surface: 'ui' })
    expect(await ui.download.clear()).toMatchObject({ ok: true })
    expect(host.state().downloads.map((d) => d.state)).toEqual(['progressing', 'paused'])
    expect(await ui.download.openPopover()).toMatchObject({ ok: true })
  })
})

// M8: the fake answers theme, onboarding, clear data and import as CluThemeHandler, CluOnboardingHandler,
// CluBrowsingDataHandler and CluImportHandler do, so the harness specs mean what the browser tests do.
describe('FakeBridge M8 data and theme', () => {
  const home = FAKE.spaces.home
  const ember = { colors: [{ h: 12, s: 0.82 }, { h: 36, s: 0.88 }], intensity: 0.5, grain: 0 }

  it('previews a theme in state only and puts the committed one back when the picker closes', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const blue = host.state().spaces[home]?.accent
    await ui.theme.openPicker({ spaceId: home, anchor: { x: 0, y: 0, width: 10, height: 10 } })
    const picker = host.state().windows[FAKE.window]?.overlay.surfaces.find((s) => s.surface === 'theme-picker')
    expect(picker?.props).toMatchObject({ spaceId: home })
    await ui.theme.previewSpaceTheme({ spaceId: home, theme: ember })
    expect(host.state().spaces[home]?.theme).toEqual(ember)
    expect(host.state().spaces[home]?.accent).not.toBe(blue)
    const overlay = host.connect({ surface: 'overlay', layer: 'top' })
    expect(await overlay.overlay.close({ instanceId: picker?.instanceId ?? '' })).toMatchObject({ ok: true })
    expect(host.state().spaces[home]?.theme).toBeNull()
    expect(host.state().spaces[home]?.accent).toBe(blue)
    // A commit stays; a reset goes back to the color.
    await ui.theme.setSpaceTheme({ spaceId: home, theme: ember })
    expect(host.state().spaces[home]?.theme).toEqual(ember)
    await ui.theme.resetSpaceTheme({ spaceId: home })
    expect(host.state().spaces[home]?.accent).toBe(blue)
    expect(await ui.theme.setSpaceTheme({ spaceId: FAKE.tab(99), theme: ember })).toMatchObject({ ok: false, error: { code: 'not-found' } })
    // The guard refuses a theme the browser would: 4 colors, h 360.
    expect(await ui.theme.setSpaceTheme({ spaceId: home, theme: { ...ember, colors: [{ h: 360, s: 0.5 }] } })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
  })

  it('ends onboarding once, with completedAt in settings.global', async () => {
    const host = createFakeHost(undefined, { onboarding: true })
    const ui = host.connect({ surface: 'ui' })
    expect(host.state().app.onboarding.show).toBe(true)
    await ui.onboarding.complete({ skipped: true })
    expect(host.state().app.onboarding.show).toBe(false)
    expect(typeof host.state().settings.global['onboarding.completedAt']).toBe('string')
  })

  it('opens the clear data dialog and clears archive rows of that profile only when asked', async () => {
    const host = createFakeHost()
    const ui = host.connect({ surface: 'ui' })
    const overlay = host.connect({ surface: 'overlay', layer: 'top' })
    await ui.browsingData.open({})
    expect(host.state().windows[FAKE.window]?.overlay.surfaces.map((s) => [s.surface, s.props])).toEqual([['clear-data', { profileId: FAKE.profile }]])
    const before = host.state().archiveRevision
    const none = { history: true, cookies: true, cache: false, downloads: false, permissions: false, archive: false }
    await overlay.browsingData.clear({ profileId: FAKE.profile, range: 'hour', types: none })
    expect(host.state().archiveRevision).toBe(before)
    await overlay.browsingData.clear({ profileId: FAKE.profile, range: 'all', types: { ...none, archive: true } })
    expect(host.state().archiveRevision).toBeGreaterThan(before)
    // Only the overlay may clear.
    expect(await ui.browsingData.clear({ profileId: FAKE.profile, range: 'all', types: none })).toMatchObject({ ok: false, error: { code: 'not-allowed' } })
  })

  it('detects from the frame and settings, imports from settings, and a second run adds nothing', async () => {
    const host = createFakeHost()
    const settings = host.connect({ surface: 'internal:settings' })
    const overlay = host.connect({ surface: 'overlay' })
    // The frame's first-run switch screen detects too; the overlay never does.
    expect(await overlay.import.detect()).toMatchObject({ ok: false, error: { code: 'not-allowed' } })
    const detected = await settings.import.detect()
    expect(detected).toMatchObject({ ok: true, value: { arc: { found: true }, currentSpaceId: home } })
    const run = { source: 'chrome' as const, profileDir: 'Profile 1', bookmarks: true, history: true, passwords: true, cookies: true, autofill: true, searchEngine: true, targetSpaceId: home }
    expect(await settings.import.run(run)).toMatchObject({ ok: true, value: { bookmarks: 13, history: 4, passwords: 3, cookies: 2, secretsDenied: false, siteStorage: 2, permissions: 1, addresses: 1 } })
    expect(await settings.import.run(run)).toMatchObject({ ok: true, value: { bookmarks: 0, history: 0, passwords: 0, cookies: 0 } })
    expect(await settings.import.run({ ...run, profileDir: '../Chrome' })).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
    // The sign-ins are part of the request.
    const { passwords: _p, ...noPasswords } = run
    expect(await settings.import.run(noPasswords as typeof run)).toMatchObject({ ok: false, error: { code: 'bad-payload' } })
  })

  it('a denied keychain prompt brings the rest of the Chrome import and says the sign-ins were skipped', async () => {
    const settings = createFakeHost(undefined, { keychainDenied: true }).connect({ surface: 'internal:settings' })
    const run = { source: 'chrome' as const, profileDir: 'Profile 1', bookmarks: true, history: true, passwords: true, cookies: true, autofill: true, searchEngine: true, targetSpaceId: home }
    expect(await settings.import.run(run)).toMatchObject({ ok: true, value: { bookmarks: 13, history: 4, passwords: 0, cookies: 0, secretsDenied: true, siteStorage: 2, permissions: 1 } })
  })
})
