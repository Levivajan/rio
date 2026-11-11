// FakeBridge: an in-memory browser process for tests, the ui:fake harness and screenshots. One FakeHost owns the
// state; each surface (frame UI, overlay layers) connects for its own CluBridge. Payloads pass the same guard as the
// real browser (surface allow list, 256 KiB, the zod schema), so the UI cannot send what C++ would reject. Commands
// without a fake handler answer `not-implemented`, which the UI shows as a disabled control.
import './zod-jitless'
import type { CluBridge } from '../../../shared/bridge'
import { paletteRows } from '../../../shared/command/actions'
import type { ActionRow, IndexRow } from '../../../shared/ipc/command'
import { commandEntry } from '../../../shared/commands'
import { settingDef } from '../../../shared/settings'
import { contract, MAX_PAYLOAD_BYTES, type CommandDef, type CommandName, type IpcResult, type Surface } from '../../../shared/ipc'
import { computeLayout, storedSidebarWidth, SIDEBAR_WIDTH } from '../../../shared/shell/layout'
import { applyPatch, defaultProfileState, defaultSpaceState, delIn, type CluState, type OverlaySurfaceState, type PatchOp, type StateMessage, type TabState } from '../../../shared/state'
import { SPACE_SYMBOLS, type Json, type MenuItem, type Rect } from '../../../shared/types'
import type { ArchiveRow } from '../../../shared/ipc/archive'
import type { LibrarySource } from '../../../shared/ipc/capabilities'
import { isActiveDownload, type DownloadState } from '../../../shared/capabilities'
import { HOVER_CARD, hoverCardModel, hoverCardRect, isOffPinned, mailCardHeight, MAX_FAVORITES, type DropTarget } from '../../../shared/tabs'
import { isSpaceColor, MAX_PROFILES, NEW_SPACE_COLOR, MAX_SPACES, nextDefaultName, normalizeName, parseEmoji, SPACE_COLOR_HEX, SPACE_EDITOR_SURFACE } from '../../../shared/spaces'
import type { SpaceColor, SpaceIcon, SpaceTheme } from '../../../shared/types'
import { spaceAccent } from '../../../shared/theme/space'
import { FAKE, fakeArchive, fakeDownloads, fakeHistory, fakeIndex, fakeLibraryFiles, fakeSeed } from './fake-seed'
import type { UpdateStatus } from '../../../shared/state/platform'
import { siteHandlers, withSiteSeed } from './fake-site'
import { createFakeMultitask } from './fake-multitask'

// The snapshot a preview card shows in the harness: a neutral fake page, never a real site.

export interface FakeSurface { surface: Surface; windowId: string | null; layer: CluBridge['layer']; slot: number | null }
export interface FakeCall { surface: Surface; name: string; payload: unknown; result: IpcResult<unknown> }

export interface FakeHost {
  connect(surface: Partial<FakeSurface> & { surface: Surface }): CluBridge
  state(): CluState
  /** Every call any surface made, with its answer (tests assert on these). */
  readonly calls: FakeCall[]
  setContentSize(size: { width: number; height: number }): void
  /** Runs `fn` after each published change. */
  subscribe(fn: (s: CluState) => void): () => void
  /** Publishes queued patches now (the host otherwise coalesces them per 16 ms, as main does). */
  flush(): void
  /** Sets one state path as the browser would publish it (screenshot states the seed lacks: 12 favorites, no tab). */
  edit(path: string[], value: unknown): void
}

type Ctx = { surface: FakeSurface; windowId: string }
type Handler = (payload: never, ctx: Ctx) => unknown

const uuid = () => crypto.randomUUID()
const textBytes = (v: unknown) => new TextEncoder().encode(JSON.stringify(v ?? null)).length

/**
 * `historyRows` replaces the seed's history index with that many synthetic rows (the perf spec's 10,000);
 * `downloads` seeds the profile's downloads (fakeDownloads). `onboarding` starts as a fresh install, with first-run
 * onboarding showing; `extensions` seeds three extensions and the blocker's fields (fake-site.ts). `update` starts the
 * updater in that status (`ready` carries FAKE_UPDATE_VERSION); without it the build has no updater.
 */
export interface FakeHostOptions { historyRows?: number; downloads?: boolean; onboarding?: boolean; extensions?: boolean; update?: UpdateStatus; noArc?: boolean; keychainDenied?: boolean }

export const FAKE_UPDATE_VERSION = '158.0.8127.19'

function syntheticIndex(n: number): IndexRow[] {
  return Array.from({ length: n }, (_, i) => {
    const url = `https://site${i % 997}.example/page/${i}`
    return { key: url, kind: 'history' as const, url, title: `Page ${i} about harbors`, visitCount: 1 + (i % 7), typedCount: i % 3, lastVisitAt: FAKE.now - i * 60_000 }
  })
}

type DownloadAction = 'pause' | 'resume' | 'cancel' | 'retry' | 'open' | 'reveal' | 'remove'

/** The row after `action`, null when it leaves the list, undefined when the row cannot do it now (CluDownloads::Act). */
export function actOnDownload(row: DownloadState, action: DownloadAction): DownloadState | null | undefined {
  switch (action) {
    case 'pause':
      return row.state === 'progressing' ? { ...row, state: 'paused', bytesPerSecond: 0, etaMs: null } : undefined
    case 'resume':
    case 'retry':
      return row.state === 'paused' || row.state === 'interrupted' ? { ...row, state: 'progressing' } : undefined
    case 'cancel':
      return isActiveDownload(row.state) ? { ...row, state: 'cancelled', bytesPerSecond: 0, etaMs: null } : undefined
    case 'open':
      return row.state === 'completed' && row.exists ? row : undefined
    case 'reveal':
      return row.exists && row.state !== 'cancelled' ? row : undefined
    case 'remove':
      return isActiveDownload(row.state) ? undefined : null
  }
}

export function createFakeHost(seed: CluState = fakeSeed(), options: FakeHostOptions = {}): FakeHost {
  const sited = options.extensions ? withSiteSeed(seed) : seed
  const downloaded: CluState = options.downloads ? { ...sited, downloads: fakeDownloads() } : sited
  /** The width the Library's section asked for (archive.setWidth, CluShell::SetLibraryWidth); it resets on close. */
  const libraryWidths = new Map<string, number>()
  const updated: CluState = options.update
    ? { ...downloaded, app: { ...downloaded.app, update: { status: options.update, version: options.update === 'ready' ? FAKE_UPDATE_VERSION : null, checkedAt: null, dismissed: false } } }
    : downloaded
  let state: CluState = updated
  let rev = seed.rev
  let queued: PatchOp[] = []
  let timer: ReturnType<typeof setTimeout> | null = null
  const streams = new Set<(msg: StateMessage) => void>()
  const watchers = new Set<(s: CluState) => void>()
  const calls: FakeCall[] = []
  let archived: ArchiveRow[] = fakeArchive()
  let pendingClear: { spaceId: string; ids: string[] } | null = null
  // The committed theme of each space that shows a picker preview (M8: the preview reverts when the picker closes).
  const committedThemes = new Map<string, SpaceTheme | null>()
  // Import runs so far: a second run adds nothing, as the browser's import map makes it.
  const imported = new Set<'chrome' | 'arc'>()
  if (options.onboarding) state = { ...state, app: { ...state.app, onboarding: { show: true } } }

  function flush(): void {
    if (timer) clearTimeout(timer)
    timer = null
    if (queued.length === 0) return
    const ops = queued
    queued = []
    rev += 1
    state = { ...state, rev }
    for (const cb of streams) cb({ kind: 'patch', rev, ops })
    for (const fn of watchers) fn(state)
  }
  function set(path: string[], value: unknown): void {
    state = applyPatch(state, [{ op: 'set', path, value }])
    queued.push({ op: 'set', path, value })
    timer ??= setTimeout(flush, 16)
  }
  function del(path: string[]): void {
    state = delIn(state, path)
    queued.push({ op: 'del', path })
    timer ??= setTimeout(flush, 16)
  }
  const win = (id: string) => state.windows[id]

  // ---- overlay host ----
  const TRANSIENT = new Set(['menu', 'command'])
  // The collapsed-folder popover's passive slot (the browser's hover-card slot) and whether the pointer is over it.
  const FOLDER_PEEK_SURFACE = 'folder-peek'
  const PEEK_SLOT = 2
  let folderPeekHeld = false
  // Whether the pointer is on Gmail's card. The fake reads no network; Gmail account 9 (/mail/u/9/) answers
  // from a made-up feed so the harness can drive the message list (never anyone's mail).
  let mailCardHeld = false
  const FAKE_GMAIL_FEED = [
    { author: 'Fake Sender', title: 'Fake message one', issued: '2026-11-10T15:00:00Z', url: 'https://mail.google.com/mail/u/9/#inbox/fake1' },
    { author: 'Another Fake', title: 'Fake message two', issued: '2026-11-09T09:30:00Z', url: 'https://mail.google.com/mail/u/9/#inbox/fake2' }
  ]
  function openSurface(windowId: string, spec: Omit<OverlaySurfaceState, 'instanceId' | 'slot' | 'openedFrom' | 'layer'> & { layer?: OverlaySurfaceState['layer'] }): string {
    const instanceId = uuid()
    const kept = (win(windowId)?.overlay.surfaces ?? []).filter((s) => !TRANSIENT.has(s.surface))
    set(['windows', windowId, 'overlay', 'surfaces'], [...kept, { instanceId, layer: 'top', openedFrom: null, slot: null, ...spec }])
    return instanceId
  }
  function closeSurface(windowId: string, instanceId: string): void {
    const all = win(windowId)?.overlay.surfaces ?? []
    const gone = all.find((s) => s.instanceId === instanceId)
    if (!gone) return
    set(['windows', windowId, 'overlay', 'surfaces'], all.filter((s) => s.instanceId !== instanceId))
    if (gone.surface === 'command') set(['windows', windowId, 'command'], null)
    const pickerSpace = gone.surface === 'theme-picker' ? (gone.props as { spaceId?: string } | null)?.spaceId : undefined
    if (pickerSpace && committedThemes.has(pickerSpace)) {
      showTheme(pickerSpace, committedThemes.get(pickerSpace) ?? null)
      committedThemes.delete(pickerSpace)
    }
  }

  // ---- theme (M8) ----
  function showTheme(spaceId: string, theme: SpaceTheme | null): void {
    const space = state.spaces[spaceId]
    if (!space) return
    set(['spaces', spaceId, 'theme'], theme)
    set(['spaces', spaceId, 'accent'], spaceAccent(theme, space.color))
  }

  // ---- shell ----
  /** The archive panel is the Library: opening it widens the sidebar (CluShell::SetLibraryOpen). */
  function setLibrary(windowId: string, open: boolean): void {
    if (open === (state.archivePanel[windowId] === true)) return
    if (open) set(['archivePanel', windowId], true)
    else {
      del(['archivePanel', windowId])
      libraryWidths.delete(windowId)
    }
    relayout(windowId)
  }
  function setLibraryWidth(windowId: string, width: number): void {
    if ((libraryWidths.get(windowId) ?? 0) === width) return
    libraryWidths.set(windowId, width)
    if (state.archivePanel[windowId] === true) relayout(windowId)
  }
  function relayout(windowId: string, patch: { hidden?: boolean; width?: number; toolbar?: boolean; contentSize?: { width: number; height: number } } = {}): void {
    const w = win(windowId)
    if (!w) return
    const sidebar = { ...w.shell.sidebar, ...(patch.hidden !== undefined ? { hidden: patch.hidden } : {}), ...(patch.width !== undefined ? { width: patch.width } : {}) }
    const toolbar = patch.toolbar ?? w.shell.toolbar
    const front = w.activeTabId ? state.tabs[w.activeTabId] : undefined
    const devStrip = !w.mini && !!front?.site && !!front.devMode
    const layout = computeLayout(patch.contentSize ?? w.shell.layout.contentSize, { sidebarWidth: sidebar.width, sidebarHidden: sidebar.hidden, focusMode: w.shell.focusMode, fullscreen: w.fullscreen, libraryOpen: state.archivePanel[windowId] === true, libraryWidth: libraryWidths.get(windowId) ?? 0, toolbar, devStrip })
    set(['windows', windowId, 'shell'], { ...w.shell, sidebar: { ...sidebar, effectiveWidth: layout.effectiveWidth, anim: null }, toolbar, layout })
    multitask.refresh(windowId)
  }

  // ---- tabs ----
  function activate(windowId: string, tabId: string): void {
    const tab = state.tabs[tabId]
    if (!tab) return
    set(['windows', windowId, 'activeTabId'], tabId)
    set(['windows', windowId, 'focusedTabId'], tabId)
    set(['tabs', tabId, 'lastActiveAt'], Date.now())
    // An unloaded row loads again once it is shown (CluWindowState::ActivateRow).
    if (tab.lifecycle !== 'live') set(['tabs', tabId, 'lifecycle'], 'live')
    if (tab.spaceId && tab.spaceId !== win(windowId)?.activeSpaceId) set(['windows', windowId, 'activeSpaceId'], tab.spaceId)
    multitask.refresh(windowId)
  }
  function createTab(windowId: string, url: string, spaceId?: string): string {
    const w = win(windowId)
    const space = state.spaces[spaceId ?? w?.activeSpaceId ?? '']
    const id = uuid()
    const base = state.tabs[FAKE.tab(20)]
    if (!space || !base) return id
    const tab: TabState = {
      ...base,
      id,
      spaceId: space.id,
      url,
      title: url.replace(/^https?:\/\//, ''),
      favicon: null,
      audible: false,
      mediaPlaying: false,
      loading: false,
      progress: 1,
      muted: false,
      lastActiveAt: Date.now()
    }
    set(['tabs', id], tab)
    set(['spaces', space.id, 'today'], [id, ...space.today])
    return id
  }
  function closeTab(windowId: string, tabId: string | undefined): void {
    const id = tabId ?? win(windowId)?.activeTabId
    const tab = id ? state.tabs[id] : undefined
    if (!id || !tab) return
    if (tab.section === 'today' && tab.spaceId) {
      const space = state.spaces[tab.spaceId]
      if (space) set(['spaces', space.id, 'today'], space.today.filter((t) => t !== id))
      del(['tabs', id])
    } else set(['tabs', id, 'lifecycle'], 'unloaded')
    // As CluWindowState::CloseRowCommand: a closed Today row hands over to the next, an unloaded pinned one to nothing.
    if (win(windowId)?.activeTabId === id) {
      const next = tab.section === 'today' && tab.spaceId ? state.spaces[tab.spaceId]?.today[0] : undefined
      set(['windows', windowId, 'activeTabId'], next ?? null)
    }
  }
  /** tabs.delete (CluTabsHandler::Delete): the row leaves the sidebar for the archive. */
  function deleteRow(windowId: string, id: string): void {
    const tab = state.tabs[id]
    if (!tab) return
    if (tab.section === 'favorite') set(['favorites', tab.profileId], (state.favorites[tab.profileId] ?? []).filter((t) => t !== id))
    const folder = tab.parentId ? state.folders[tab.parentId] : undefined
    if (folder) set(['folders', folder.id, 'children'], folder.children.filter((r) => r.id !== id))
    const space = tab.spaceId ? state.spaces[tab.spaceId] : undefined
    if (space) {
      if (tab.section === 'pinned') set(['spaces', space.id, 'pinned'], space.pinned.filter((r) => r.id !== id))
      else set(['spaces', space.id, 'today'], space.today.filter((t) => t !== id))
    }
    if (win(windowId)?.activeTabId === id) set(['windows', windowId, 'activeTabId'], null)
    del(['tabs', id])
  }
  function rowMenu(tab: TabState): MenuItem[] {
    const item = (id: string, label: string, enabled = true, keys?: string): MenuItem => ({ kind: 'item', id, label, enabled, ...(keys ? { keys } : {}) })
    const sep: MenuItem = { kind: 'separator' }
    const spaces = state.spaceOrder.map((s) => item(`tabs.moveTo:${s}:${tab.section === 'today' ? 'today' : 'pinned'}`, state.spaces[s]?.name ?? 'Space', tab.section === 'favorite' || s !== tab.spaceId))
    const moveTo: MenuItem = { kind: 'item', id: 'tabs.moveTo', label: 'Move to', submenu: [...spaces, sep, item('tabs.moveTo.newSpace', 'New Space…')] }
    const share: MenuItem = { kind: 'item', id: 'tabs.share', label: 'Share', submenu: [item('tabs.share:0', 'Mail'), item('tabs.share:1', 'Messages'), item('tabs.share:2', 'AirDrop')] }
    const mute = item('tabs.mute', tab.muted ? 'Unmute' : 'Mute', tab.muted || tab.audible)
    const icon = item('tabs.setIcon', 'Change Icon…')
    const pinnedPage: MenuItem = { kind: 'item', id: 'tabs.pinnedUrl', label: 'Edit Pinned Page', submenu: [item('tabs.pinnedUrl.current', 'Replace Pinned URL with Current', tab.offPinned), item('tabs.pinnedUrl.enter', 'Edit…')] }
    // CluTabMenu's model (clu_menu_items.cc): each row kind gets the same menu, in the same order.
    if (tab.section === 'pinned')
      return [item('tabs.copyLink', 'Copy Link', true, '⇧⌘C'), share, sep, icon, item('tabs.rename', 'Rename…'), mute, item('tabs.customize', 'Customize Page…'), pinnedPage, sep, item('tabs.openInSplit', 'Add Split View'), item('tabs.duplicate', 'Duplicate'), moveTo, sep, item('tabs.pin', 'Remove Pin')]
    if (tab.section === 'favorite')
      return [item('tabs.copyLink', 'Copy Link'), share, sep, icon, item('tabs.rename', 'Rename…'), mute, pinnedPage, sep, item('tabs.openInSplit', 'Open in Split View'), item('tabs.duplicate', 'Duplicate'), moveTo, sep, item('tabs.favorite', 'Remove Favorite')]
    return [
      item('tabs.copyLink', 'Copy Link'),
      share,
      sep,
      icon,
      item('tabs.rename', 'Rename…'),
      mute,
      item('tabs.customize', 'Customize Page…'),
      sep,
      item('tabs.openInSplit', 'Open in Split View'),
      item('tabs.duplicate', 'Duplicate'),
      moveTo,
      sep,
      item('tabs.archive', 'Archive Tab'),
      item('tabs.archiveBelow', 'Archive Tabs Below')
    ]
  }
  /** The + and "..." popovers (NewMenuItems and MoreMenuItems in clu_menu_items.cc, drawn by menu-popover). */
  function popoverMenu(menu: string): Json[] {
    const item = (id: string, label: string, command?: string): Json => ({ id, label, enabled: true, ...(command ? { command } : {}) })
    if (menu === 'more') return [item('nav.reload', 'Reload', 'nav.reload'), item('sidebar.toggle', 'Toggle Sidebar', 'sidebar.toggle')]
    if (menu === 'new') return [item('spaces.newSpace', 'New Space'), item('folders.new', 'New Folder'), { separator: true }, item('split.add', 'New Split', 'split.add'), item('tabs.newTab', 'New Tab', 'commandbar.newTab')]
    return []
  }
  /** The space header's menu (CluSpaceMenu::BuildSpaceMenu). */
  function spaceMenu(spaceId: string): MenuItem[] {
    const item = (id: string, label: string, enabled = true): MenuItem => ({ kind: 'item', id, label, enabled })
    const sep: MenuItem = { kind: 'separator' }
    const profiles = Object.values(state.profiles).map((p) => item(`space.profile:${p.id}`, p.name, p.id !== state.spaces[spaceId]?.profileId))
    return [
      item('space.icon', 'Change Space Icon…'),
      item('space.rename', 'Rename Space…'),
      item('space.theme', 'Edit Theme Color…'),
      { kind: 'item', id: 'space.profile', label: 'Set Profile', submenu: profiles },
      sep,
      item('space.newFolder', 'New Folder'),
      { kind: 'item', id: 'space.live', label: 'Live Folders', submenu: [item('space.live:feed', 'RSS or Atom Feed…'), item('space.live:githubReleases', 'GitHub Releases…'), item('space.live:githubCommits', 'GitHub Commits…')] },
      sep,
      item('space.share', 'Share Space…'),
      {
        kind: 'item',
        id: 'space.export',
        label: `Export “${state.spaces[spaceId]?.name ?? ''}” into Folder`,
        submenu: state.spaceOrder.filter((id) => id !== spaceId).map((id) => item(`space.export:${id}`, state.spaces[id]?.name ?? ''))
      },
      sep,
      item('space.manage', 'Manage Spaces…'),
      sep,
      item('delete', 'Delete Space', state.spaceOrder.length > 1)
    ]
  }
  function menuAnswer(windowId: string, instanceId: string, result: unknown): void {
    const surface = win(windowId)?.overlay.surfaces.find((s) => s.instanceId === instanceId)
    const tabId = (surface?.props as { tabId?: string } | undefined)?.tabId
    if (surface?.surface === 'menu' && tabId && (result === 'tabs.close' || result === 'tabs.archive' || (result === 'tabs.pin' && state.tabs[tabId]?.section === 'pinned'))) closeTab(windowId, tabId)
    if (surface?.surface === 'menu' && tabId && result === 'tabs.mute') set(['tabs', tabId, 'muted'], !state.tabs[tabId]?.muted)
    if ((surface?.surface === 'menu' || surface?.surface === 'menu-popover') && result === 'sidebar.toggle') relayout(windowId, { hidden: !win(windowId)?.shell.sidebar.hidden })
    // Live Folders ▸ <kind> asks for the feed, as the browser's space menu does.
    const spaceId = (surface?.props as { spaceId?: string } | undefined)?.spaceId
    if (surface?.surface === 'menu' && spaceId && typeof result === 'string' && result.startsWith('space.live:')) {
      openSurface(windowId, { surface: 'folders.live', modality: 'modal', focus: true, rect: null, props: { spaceId, kind: result.slice('space.live:'.length) } })
    }
  }

  // ---- updates (docs/chromium/m12.md): the toasts CluUpdateService shows; a check finds nothing new ----
  function checkForUpdates(windowId: string): { status: 'checking' | 'busy' | 'ready' | 'unavailable' } {
    const status = state.app.update.status
    if (status === 'unavailable') {
      toast(windowId, "Updates are off in this copy of Rio")
      return { status }
    }
    if (status === 'ready') {
      toast(windowId, 'Relaunch Rio to install the update')
      return { status }
    }
    if (status === 'checking' || status === 'downloading') {
      toast(windowId, 'Rio is already checking for updates')
      return { status: 'busy' }
    }
    set(['app', 'update', 'status'], 'checking')
    setTimeout(() => {
      set(['app', 'update'], { status: 'up-to-date', version: null, checkedAt: Date.now(), dismissed: false })
      toast(windowId, 'Rio is up to date')
    }, 50)
    return { status: 'checking' }
  }

  // ---- archive and moves (docs/chromium/m3.md: the same rules the browser applies, enough for the harness) ----
  function bumpArchive(): void {
    set(['archiveRevision'], state.archiveRevision + 1)
  }
  function toast(windowId: string, text: string, commandId?: string): void {
    const toasts = [...(win(windowId)?.toasts ?? []), { id: uuid(), text, ...(commandId ? { action: { label: 'Undo', commandId } } : {}), until: Date.now() + 5000 }]
    set(['windows', windowId, 'toasts'], toasts.slice(-1))
  }
  function clearToday(windowId: string, spaceId: string): void {
    const space = state.spaces[spaceId]
    if (!space || space.today.length === 0) return
    pendingClear = { spaceId, ids: [...space.today] }
    set(['spaces', spaceId, 'today'], [])
    if (pendingClear.ids.includes(win(windowId)?.activeTabId ?? '')) set(['windows', windowId, 'activeTabId'], null)
    toast(windowId, `Cleared ${pendingClear.ids.length} ${pendingClear.ids.length === 1 ? 'tab' : 'tabs'}`, 'archive.undoClear')
  }
  function undoClear(windowId: string): void {
    const p = pendingClear
    pendingClear = null
    const space = p ? state.spaces[p.spaceId] : undefined
    if (!p || !space) return
    set(['spaces', p.spaceId, 'today'], [...space.today, ...p.ids.filter((id) => state.tabs[id])])
    set(['windows', windowId, 'toasts'], (win(windowId)?.toasts ?? []).filter((t) => t.action?.commandId !== 'archive.undoClear'))
  }
  /** tabs.move for tab rows: Today, a space's pinned root and Favorites, with the 12-favorite cap (02-tabs F8). */
  function moveTab(windowId: string, tabId: string, target: DropTarget): { ok: true } | { ok: false; reason: string } {
    const tab = state.tabs[tabId]
    if (!tab) return { ok: false, reason: 'not-found' }
    const profile = state.spaces[win(windowId)?.activeSpaceId ?? '']?.profileId ?? tab.profileId
    const favorites = state.favorites[profile] ?? []
    if (target.section === 'favorite' && tab.section !== 'favorite' && favorites.length >= MAX_FAVORITES) return { ok: false, reason: 'favorites-full' }
    if (target.section !== 'favorite' && !state.spaces[target.spaceId]) return { ok: false, reason: 'bad-target' }
    // Out of its list.
    if (tab.section === 'favorite') set(['favorites', profile], favorites.filter((id) => id !== tabId))
    else if (tab.spaceId && state.spaces[tab.spaceId]) {
      const from = state.spaces[tab.spaceId]
      if (from && tab.section === 'today') set(['spaces', from.id, 'today'], from.today.filter((id) => id !== tabId))
      if (from && tab.section === 'pinned' && !tab.parentId) set(['spaces', from.id, 'pinned'], from.pinned.filter((r) => r.id !== tabId))
      const folder = tab.parentId ? state.folders[tab.parentId] : undefined
      if (folder) set(['folders', folder.id, 'children'], folder.children.filter((r) => r.id !== tabId))
    }
    const insert = <T,>(list: readonly T[], item: T, index: number) => [...list.slice(0, index), item, ...list.slice(index)]
    if (target.section === 'favorite') {
      set(['favorites', profile], insert((state.favorites[profile] ?? []).filter((id) => id !== tabId), tabId, target.index))
      set(['tabs', tabId, 'spaceId'], null)
    } else if (target.section === 'today') {
      set(['spaces', target.spaceId, 'today'], insert(state.spaces[target.spaceId]?.today ?? [], tabId, target.index))
      set(['tabs', tabId, 'spaceId'], target.spaceId)
    } else {
      const ref = { kind: 'tab' as const, id: tabId }
      const folder = target.parentId ? state.folders[target.parentId] : undefined
      if (folder) set(['folders', folder.id, 'children'], insert(folder.children, ref, target.index))
      else set(['spaces', target.spaceId, 'pinned'], insert(state.spaces[target.spaceId]?.pinned ?? [], ref, target.index))
      set(['tabs', tabId, 'spaceId'], target.spaceId)
    }
    const anchored = target.section !== 'today'
    set(['tabs', tabId, 'section'], target.section)
    set(['tabs', tabId, 'parentId'], target.section === 'pinned' ? target.parentId : null)
    set(['tabs', tabId, 'pinnedUrl'], anchored ? (tab.pinnedUrl ?? tab.url) : null)
    return { ok: true }
  }

  // ---- command bar ----
  function openBar(windowId: string, mode: 'new-tab' | 'edit-url' | 'split-add' | 'mini'): void {
    const w = win(windowId)
    if (!w) return
    if (w.command?.mode === mode) {
      const open = w.overlay.surfaces.find((s) => s.surface === 'command')
      if (open) closeSurface(windowId, open.instanceId)
      return
    }
    const target = mode === 'edit-url' ? w.activeTabId : null
    const fields = { mode, targetTabId: target, initialText: target ? (state.tabs[target]?.url ?? '') : '' }
    // The other mode switches the open session in place, as the browser does.
    if (w.command) {
      set(['windows', windowId, 'command'], { sessionId: w.command.sessionId, ...fields })
      return
    }
    const sessionId = uuid()
    // The bar closes any open Peek, as the browser does.
    if (w.peek) set(['windows', windowId, 'peek'], null)
    set(['windows', windowId, 'command'], { sessionId, ...fields })
    openSurface(windowId, { surface: 'command', modality: 'modal', focus: true, rect: null, props: { sessionId } })
  }
  function runCommand(windowId: string, id: string): void {
    const w = win(windowId)
    if (!w) return
    if (multitask.runCommand(windowId, id)) return
    const order = state.spaceOrder
    const at = order.indexOf(w.activeSpaceId)
    const goto = /^spaces\.goto(\d)$/.exec(id)
    if (id === 'commandbar.newTab') openBar(windowId, 'new-tab')
    else if (id === 'commandbar.editUrl') openBar(windowId, 'edit-url')
    else if (id === 'sidebar.toggle') relayout(windowId, { hidden: !w.shell.sidebar.hidden })
    else if (id === 'shell.toolbar') relayout(windowId, { toolbar: !w.shell.toolbar })
    else if (id === 'tabs.close') closeTab(windowId, undefined)
    else if (id === 'archive.open') setLibrary(windowId, true)
    else if (id === 'archive.clearToday') clearToday(windowId, w.activeSpaceId)
    else if (id === 'archive.undoClear') undoClear(windowId)
    // As in the browser: no wrap at either end, an index past the end does nothing.
    else if (id === 'spaces.next' || id === 'spaces.prev') switchSpace(windowId, at < 0 ? order[0] : order[at + (id === 'spaces.next' ? 1 : -1)])
    else if (goto) switchSpace(windowId, order[Number(goto[1]) - 1])
    else if (id === 'spaces.newSpace') openEditor(windowId, null, 'space')
    else if (id === 'spaces.editSpace') openEditor(windowId, w.activeSpaceId, 'space')
    else if (id === 'profiles.newProfile') openEditor(windowId, null, 'profiles')
    else if (id === 'platform.checkForUpdates') checkForUpdates(windowId)
    // Tabs ▸ Reveal Tab in Sidebar: the sidebar shows and scrolls to the active row, as in the browser.
    else if (id === 'tabs.reveal' && w.activeTabId) {
      if (w.shell.sidebar.hidden) relayout(windowId, { hidden: false })
      set(['windows', windowId, 'sidebarRequest'], { seq: (w.sidebarRequest?.seq ?? 0) + 1, kind: 'reveal', nodeId: w.activeTabId })
    }
  }
  function openEditor(windowId: string, spaceId: string | null, view: 'space' | 'profiles'): void {
    if (win(windowId)?.private) return
    for (const s of win(windowId)?.overlay.surfaces ?? []) if (s.surface === SPACE_EDITOR_SURFACE) closeSurface(windowId, s.instanceId)
    openSurface(windowId, { surface: SPACE_EDITOR_SURFACE, modality: 'modal', focus: true, rect: null, props: { spaceId, view } })
  }
  const iconOf = (icon: SpaceIcon | undefined): SpaceIcon | null => {
    if (!icon || icon.kind === 'letter') return { kind: 'letter' }
    if (icon.kind === 'symbol') return SPACE_SYMBOLS.includes(icon.value) ? { kind: 'symbol', value: icon.value } : null
    const value = parseEmoji(icon.value)
    return value ? { kind: 'emoji', value } : null
  }
  const fail = (code: string, reason?: string) => ({ __fakeError: { code, ...(reason ? { reason } : {}) } })
  function switchSpace(windowId: string, spaceId: string | undefined): void {
    if (!spaceId || !state.spaces[spaceId]) return
    set(['windows', windowId, 'activeSpaceId'], spaceId)
    const first = state.spaces[spaceId]?.today[0] ?? null
    set(['windows', windowId, 'activeTabId'], first)
    set(['windows', windowId, 'focusedTabId'], first)
  }

  // ---- M6: split view, the drop preview, the media card, the Peek (fake-multitask.ts) ----
  const multitask = createFakeMultitask({
    state: () => state,
    set,
    del,
    activate,
    createTab,
    closeTab,
    toast: (windowId, text) => toast(windowId, text),
    openSurface,
    closeSurface,
    openBar,
    fail: (code, reason) => fail(code, reason)
  })

  const handlers: Partial<Record<CommandName, Handler>> = {
    ...(siteHandlers({ state: () => state, set, openSurface, relayoutAll: () => Object.keys(state.windows).forEach((id) => relayout(id)) }) as Partial<Record<CommandName, Handler>>),
    ...multitask.handlers,
    'state.get': () => state,
    'ui.ready': () => null,
    'log.write': () => null,
    'perf.mark': () => null,
    'overlay.respond': (p: { instanceId: string; result: Json }, { windowId }) => {
      menuAnswer(windowId, p.instanceId, p.result)
      closeSurface(windowId, p.instanceId)
    },
    'overlay.close': (p: { instanceId: string }, { windowId }) => closeSurface(windowId, p.instanceId),
    'commands.run': (p: { id: string }, { windowId }) => (commandEntry(p.id) ? runCommand(windowId, p.id) : undefined),
    'tabs.activate': (p: { tabId: string }, { windowId }) => activate(windowId, p.tabId),
    'tabs.create': (p: { url: string; spaceId?: string; activate?: boolean }, { windowId }) => {
      const tabId = createTab(windowId, p.url, p.spaceId)
      if (p.activate !== false) activate(windowId, tabId)
      return { tabId }
    },
    'tabs.close': (p: { tabId?: string }, { windowId }) => closeTab(windowId, p.tabId),
    'tabs.delete': (p: { nodeId: string }, { windowId }) => deleteRow(windowId, p.nodeId),
    'tabs.setMuted': (p: { tabId: string; muted: boolean }) => (state.tabs[p.tabId] ? set(['tabs', p.tabId, 'muted'], p.muted) : undefined),
    'tabs.rename': (p: { nodeId: string; title: string }) => (state.tabs[p.nodeId] ? set(['tabs', p.nodeId, 'titleOverride'], p.title || null) : undefined),
    // The hover card, as CluWindowState::Hover publishes it: passive slot 2, hung from the pointer the anchor carries.
    'tabs.hover': (p: { tabId: string | null; anchor?: Rect; held?: boolean }, { windowId }) => {
      const w = win(windowId)
      if (!w) return null
      const kept = w.overlay.surfaces.filter((s) => s.surface !== 'tab-hover' && s.surface !== 'tooltip')
      const open = w.overlay.surfaces.find((s) => s.surface === 'tab-hover')
      // Gmail's card holding itself open (CluWindowState::HoldHover): false closes it, a leave while held keeps it.
      if (p.held !== undefined) {
        if (!open || !p.tabId || (open.props as { tabId?: string } | null)?.tabId !== p.tabId) return null
        mailCardHeld = p.held
        if (!p.held) set(['windows', windowId, 'overlay', 'surfaces'], kept)
        return null
      }
      if (!p.tabId && mailCardHeld && open) return null
      mailCardHeld = false
      const t = p.tabId ? state.tabs[p.tabId] : undefined
      if (!t || !p.anchor) return set(['windows', windowId, 'overlay', 'surfaces'], kept)
      const model = hoverCardModel({ title: t.title, titleOverride: t.titleOverride, url: t.url, pinnedUrl: t.pinnedUrl, offPinned: isOffPinned(t.url, t.pinnedUrl), audible: t.audible, muted: t.muted, lifecycle: t.lifecycle })
      if (model.kind === 'mail' && /^https:\/\/mail\.google\.com\/mail\/u\/9\//.test(t.url)) {
        Object.assign(model, { unread: FAKE_GMAIL_FEED.length, mailSource: 'feed', mailItems: FAKE_GMAIL_FEED, height: mailCardHeight(FAKE_GMAIL_FEED.length) + 2 * HOVER_CARD.margin })
      }
      const rect = hoverCardRect(w.shell.layout.contentSize, p.anchor, model)
      set(['windows', windowId, 'overlay', 'surfaces'], [...kept, { instanceId: uuid(), surface: 'tab-hover', layer: 'passive', modality: 'passive', focus: false, openedFrom: null, slot: 2, rect, props: { tabId: t.id, ...model } as unknown as Json }])
      return null
    },
    // A message of Gmail's card opens in the row's own tab, which wakes and activates; the card closes (OpenMail).
    'tabs.openMail': (p: { tabId: string; url: string }, { windowId }) => {
      const t = state.tabs[p.tabId]
      if (!t) return fail('not-found')
      let target: URL
      try {
        target = new URL(p.url)
      } catch {
        return fail('rejected', 'blocked-url')
      }
      if (target.protocol !== 'https:' || target.hostname !== 'mail.google.com' || !/^https:\/\/mail\.google\.com\//.test(t.url)) return fail('rejected', 'blocked-url')
      mailCardHeld = false
      set(['windows', windowId, 'overlay', 'surfaces'], (win(windowId)?.overlay.surfaces ?? []).filter((s) => s.surface !== 'tab-hover'))
      set(['tabs', p.tabId, 'url'], target.href)
      activate(windowId, p.tabId)
      return null
    },
    // The popover is the hover card's passive slot in the browser (CluWindowState::FolderPeek); the window clamps it.
    'folders.peek': (p: { folderId: string | null; rect?: Rect; held?: boolean }, { windowId }) => {
      const open = (win(windowId)?.overlay.surfaces ?? []).find((s) => s.surface === FOLDER_PEEK_SURFACE)
      const openFolder = (open?.props as { folderId?: string } | null)?.folderId
      if (p.folderId === null) {
        if (open && !folderPeekHeld) closeSurface(windowId, open.instanceId)
        return null
      }
      if (!state.folders[p.folderId]) return fail('not-found')
      if (p.held !== undefined) {
        if (openFolder !== p.folderId || !open) return null
        folderPeekHeld = p.held
        if (!p.held) closeSurface(windowId, open.instanceId)
        return null
      }
      if (!p.rect) return fail('bad-payload')
      const size = win(windowId)?.shell.layout.contentSize ?? { width: 0, height: 0 }
      const x = Math.min(Math.max(0, p.rect.x), size.width)
      const y = Math.min(Math.max(0, p.rect.y), size.height)
      const rect = { x, y, width: Math.min(p.rect.width, size.width - x), height: Math.min(p.rect.height, size.height - y) }
      if (open) closeSurface(windowId, open.instanceId)
      folderPeekHeld = false
      set(['windows', windowId, 'overlay', 'surfaces'], [
        ...(win(windowId)?.overlay.surfaces ?? []),
        { instanceId: uuid(), surface: FOLDER_PEEK_SURFACE, layer: 'passive', modality: 'passive', focus: false, slot: PEEK_SLOT, openedFrom: null, rect, props: { folderId: p.folderId } }
      ])
      return null
    },
    'tabs.openMenu': (p: { menu: string; nodeId?: string; anchor: Rect | { x: number; y: number } }, { windowId }) => {
      const tab = p.nodeId ? state.tabs[p.nodeId] : undefined
      const anchor = 'width' in p.anchor ? p.anchor : { ...p.anchor, width: 0, height: 0 }
      if (p.menu === 'more' || p.menu === 'new') {
        openSurface(windowId, { surface: 'menu-popover', modality: 'light-dismiss', focus: true, rect: anchor, props: { items: popoverMenu(p.menu), menu: p.menu } as unknown as Json })
        return { rename: null }
      }
      if (p.menu === 'space' && p.nodeId && state.spaces[p.nodeId]) {
        openSurface(windowId, { surface: 'menu', modality: 'light-dismiss', focus: true, rect: anchor, props: { items: spaceMenu(p.nodeId), anchor, spaceId: p.nodeId } as unknown as Json })
        return { rename: null }
      }
      if (p.menu !== 'row' || !tab) return { rename: null }
      const items = rowMenu(tab)
      openSurface(windowId, { surface: 'menu', modality: 'light-dismiss', focus: true, rect: anchor, props: { items, anchor, tabId: tab.id } as unknown as Json })
      return { rename: null }
    },
    'folders.setCollapsed': (p: { folderId: string; collapsed: boolean }) => (state.folders[p.folderId] ? set(['folders', p.folderId, 'collapsed'], p.collapsed) : undefined),
    // A live folder named after its source, empty until the feed answers (the fake reads no network).
    'folders.createLive': (p: { spaceId: string; kind: string; source: string }) => {
      const source = p.source.trim()
      if (!state.spaces[p.spaceId] || !source || /\s/.test(source) || (p.kind !== 'feed' && source.split('/').filter(Boolean).length < 2)) return fail('bad-payload')
      const folderId = uuid()
      set(['folders', folderId], { id: folderId, spaceId: p.spaceId, name: p.kind === 'feed' ? source.replace(/^https?:\/\//, '').split('/')[0] : `${source} ${p.kind === 'githubReleases' ? 'Releases' : 'Commits'}`, icon: null, collapsed: false, children: [] })
      set(['spaces', p.spaceId, 'pinned'], [...(state.spaces[p.spaceId]?.pinned ?? []), { kind: 'folder', id: folderId }])
      return { folderId }
    },
    'sections.setPinnedCollapsed': (p: { spaceId: string; collapsed: boolean }) => (state.spaces[p.spaceId] ? set(['spaces', p.spaceId, 'pinnedCollapsed'], p.collapsed) : undefined),
    'shell.toggleSidebar': (_p, { windowId }) => relayout(windowId, { hidden: !win(windowId)?.shell.sidebar.hidden }),
    'shell.setSidebarWidth': (p: { width: number; commit?: boolean }, { windowId }) => {
      relayout(windowId, { width: storedSidebarWidth(p.width) })
      // The end of a drag persists the width for the next window (M8, shell.lastSidebarWidth).
      if (p.commit) set(['settings', 'global', 'shell.lastSidebarWidth'], storedSidebarWidth(p.width))
    },
    'shell.resetSidebarWidth': (_p, { windowId }) => relayout(windowId, { width: SIDEBAR_WIDTH }),
    // The frame's tooltip, as CluWindowState::Tooltip publishes it: the hover card's passive slot 2 unless a card or a
    // folder popover holds it, at the frame's rect clamped to the window.
    'shell.tooltip': (p: { label?: string; keys?: string[]; rect?: Rect; above?: boolean; closing?: boolean }, { windowId }) => {
      const w = win(windowId)
      if (!w) return null
      const open = w.overlay.surfaces.find((s) => s.slot === 2)
      if (open && open.surface !== 'tooltip') return null
      const kept = w.overlay.surfaces.filter((s) => s.surface !== 'tooltip')
      const size = w.shell.layout.contentSize
      const r = p.rect
      const width = r ? Math.min(Math.max(0, r.width), size.width) : 0
      const height = r ? Math.min(Math.max(0, r.height), size.height) : 0
      if (!p.label || !r || !width || !height) return set(['windows', windowId, 'overlay', 'surfaces'], kept)
      const rect = { x: Math.min(Math.max(0, r.x), size.width - width), y: Math.min(Math.max(0, r.y), size.height - height), width, height }
      const props = { label: p.label, keys: p.keys ?? [], above: !!p.above, closing: !!p.closing }
      set(['windows', windowId, 'overlay', 'surfaces'], [...kept, { instanceId: open?.instanceId ?? uuid(), surface: 'tooltip', layer: 'passive', modality: 'passive', focus: false, openedFrom: null, slot: 2, rect, props }])
      return null
    },
    'shell.setSidebarPeek': (p: { open: boolean }, { windowId }) => set(['windows', windowId, 'shell', 'sidebar', 'peeking'], p.open),
    // The browser holds the peek's widget open and gives it the keyboard; the harness has no widget to hold.
    'shell.holdSidebarPeek': () => undefined,
    // The browser shows a native menu; its one item runs shell.toolbar (commands.run here).
    'shell.openToolbarMenu': () => undefined,
    'spaces.switch': (p: { spaceId?: string; index?: number; step?: 1 | -1 }, { windowId }) => {
      const order = state.spaceOrder
      const at = order.indexOf(win(windowId)?.activeSpaceId ?? '')
      // No wrap (docs/plan/03-spaces.md): a step past either end and an index past the end do nothing.
      const target = p.spaceId ?? (p.index !== undefined ? order[p.index] : at < 0 ? order[0] : order[at + (p.step ?? 1)])
      switchSpace(windowId, target)
    },
    'spaces.swipe': (p: { phase: 'begin' | 'update' | 'end' | 'cancel'; dx: number; momentum: boolean }, { windowId }) => {
      const w = win(windowId)
      if (!w || w.private) return
      const order = state.spaceOrder
      const at = order.indexOf(w.activeSpaceId)
      if (p.phase === 'begin' || p.phase === 'update') {
        if (p.momentum) return
        const width = w.shell.layout.sidebarRect.width > 0 ? w.shell.layout.sidebarRect.width : w.shell.sidebar.width
        const fraction = Math.max(-1, Math.min(1, p.dx / Math.max(1, width)))
        const targetSpaceId = fraction === 0 || at < 0 ? null : (order[at + Math.sign(fraction)] ?? null)
        set(['windows', windowId, 'swipe'], { fraction, targetSpaceId })
        return
      }
      const step = Math.sign(p.dx || (w.swipe?.fraction ?? 0))
      set(['windows', windowId, 'swipe'], null)
      if (p.phase === 'end' && step !== 0 && at >= 0) switchSpace(windowId, order[at + step])
    },
    'spaces.openEditor': (p: { spaceId?: string; view?: 'space' | 'profiles' }, { windowId }) => {
      if (p.spaceId && !state.spaces[p.spaceId]) return fail('not-found')
      openEditor(windowId, p.spaceId ?? null, p.view ?? 'space')
    },
    'spaces.create': (p: { name?: string; icon?: SpaceIcon; color?: SpaceColor; profileId?: string }, { windowId }) => {
      if (win(windowId)?.private) return fail('not-allowed')
      if (state.spaceOrder.length >= MAX_SPACES) return fail('rejected', 'max-spaces')
      const profileId = p.profileId ?? Object.values(state.profiles).find((x) => x.isDefault)?.id ?? FAKE.profile
      if (!state.profiles[profileId]) return fail('not-found')
      const icon = iconOf(p.icon)
      if (!icon || (p.color && !isSpaceColor(p.color))) return fail('bad-payload')
      const color = p.color ?? NEW_SPACE_COLOR
      const id = uuid()
      const name = normalizeName(p.name) ?? nextDefaultName('Space', state.spaceOrder.length + 1, state.spaceOrder.map((s) => state.spaces[s]?.name ?? ''))
      set(['spaces', id], { ...defaultSpaceState({ id, name, icon, color, profileId, ephemeral: false }), accent: SPACE_COLOR_HEX[color] })
      // Right after the window's space, at the end without one.
      const at = state.spaceOrder.indexOf(win(windowId)?.activeSpaceId ?? '')
      const order = [...state.spaceOrder]
      order.splice(at < 0 ? order.length : at + 1, 0, id)
      set(['spaceOrder'], order)
      switchSpace(windowId, id)
      return { spaceId: id }
    },
    'spaces.update': (p: { spaceId: string; name?: string; icon?: SpaceIcon; color?: SpaceColor }) => {
      const space = state.spaces[p.spaceId]
      if (!space) return fail('not-found')
      const name = p.name === undefined ? space.name : normalizeName(p.name)
      const icon = p.icon ? iconOf(p.icon) : space.icon
      if (!name || !icon || (p.color && !isSpaceColor(p.color))) return fail('bad-payload')
      set(['spaces', p.spaceId, 'name'], name)
      set(['spaces', p.spaceId, 'icon'], icon)
      if (p.color) set(['spaces', p.spaceId, 'color'], p.color)
    },
    'spaces.reorder': (p: { spaceId: string; toIndex: number }) => {
      const order = state.spaceOrder.filter((id) => id !== p.spaceId)
      if (order.length === state.spaceOrder.length) return fail('not-found')
      order.splice(Math.max(0, Math.min(order.length, Math.trunc(p.toIndex))), 0, p.spaceId)
      set(['spaceOrder'], order)
    },
    'spaces.delete': (p: { spaceId: string }) => {
      const order = state.spaceOrder
      const at = order.indexOf(p.spaceId)
      if (at < 0) return fail('not-found')
      if (order.length <= 1) return fail('rejected', 'last-space')
      const fallback = (order[at - 1] ?? order[at + 1]) as string
      for (const w of Object.values(state.windows)) if (w.activeSpaceId === p.spaceId) switchSpace(w.id, fallback)
      set(['spaceOrder'], order.filter((id) => id !== p.spaceId))
      del(['spaces', p.spaceId])
    },
    'profiles.create': (p: { name?: string }) => {
      const all = Object.values(state.profiles).filter((x) => !x.ephemeral)
      if (all.length >= MAX_PROFILES) return fail('rejected', 'max-profiles')
      const id = uuid()
      const name = normalizeName(p.name) ?? nextDefaultName('Profile', all.length + 1, all.map((x) => x.name))
      set(['profiles', id], defaultProfileState({ id, name, isDefault: false, ephemeral: false }))
      return { profileId: id }
    },
    'window.newPrivate': () => null,
    'platform.checkForUpdates': (_p, { windowId }) => checkForUpdates(windowId),
    // The browser quits into the update here; the fake only answers.
    'platform.installUpdate': () => ({ status: state.app.update.status === 'ready' ? 'installing' : 'not-ready' }),
    // The card's close button: hidden for this version, still installs on quit.
    'platform.dismissUpdate': () => {
      if (state.app.update.status !== 'ready') return { status: 'not-ready' }
      set(['app', 'update', 'dismissed'], true)
      return { status: 'dismissed' }
    },
    'command.open': (p: { mode: 'new-tab' | 'edit-url' | 'split-add' | 'mini' }, { windowId }) => openBar(windowId, p.mode),
    'command.close': (p: { sessionId: string }, { windowId }) => {
      const w = win(windowId)
      const open = w?.overlay.surfaces.find((s) => s.surface === 'command')
      if (w?.command?.sessionId === p.sessionId && open) closeSurface(windowId, open.instanceId)
    },
    'command.execute': (p: { sessionId: string; result: { kind: string; url?: string; tabId?: string; query?: string; id?: string; providerId?: string; token?: string } }, { windowId }) => {
      const w = win(windowId)
      if (!w?.command || w.command.sessionId !== p.sessionId) return { ok: false, error: 'stale' }
      const r = p.result
      const closeBar = () => {
        const open = w.overlay.surfaces.find((s) => s.surface === 'command')
        if (open) closeSurface(windowId, open.instanceId)
      }
      // Palette commands and provider rows run as the browser runs them: the bar closes first.
      if (r.kind === 'command') {
        if (!r.id || !commandEntry(r.id)?.palette) return { ok: false, error: 'invalid' }
        closeBar()
        runCommand(windowId, r.id)
        return { ok: true }
      }
      if (r.kind === 'provided') {
        if (r.providerId !== 'spaces' || !r.token || !state.spaces[r.token]) return { ok: false, error: 'invalid' }
        closeBar()
        switchSpace(windowId, r.token)
        return { ok: true }
      }
      const url = r.kind === 'search' ? `https://search.example.com/?q=${encodeURIComponent(r.query ?? '')}` : r.url
      if (r.kind === 'tab' && r.tabId) activate(windowId, r.tabId)
      else if (url && w.command.mode === 'edit-url' && w.command.targetTabId) set(['tabs', w.command.targetTabId, 'url'], url)
      else if (url) activate(windowId, createTab(windowId, url))
      const open = w.overlay.surfaces.find((s) => s.surface === 'command')
      if (open) closeSurface(windowId, open.instanceId)
      return { ok: true }
    },
    'command.syncIndex': () => ({ rev: 1, full: true, upserts: options.historyRows ? syntheticIndex(options.historyRows) : fakeIndex(), deletes: [] }),
    // Every action the window offers, unmatched, as the browser answers: the overlay matches them per keystroke.
    'command.actions': (_p, { windowId }) => {
      const rows: ActionRow[] = paletteRows({ isEnabled: () => true, title: (id) => commandEntry(id)?.title ?? id })
      const active = win(windowId)?.activeSpaceId
      for (const id of state.spaceOrder) {
        const space = state.spaces[id]
        if (space && id !== active) rows.push({ id: 'spaces', providerId: 'spaces', token: id, title: `Switch to ${space.name}` })
      }
      return rows
    },
    'command.suggest': (p: { requestId: number; query: string }) => ({
      requestId: p.requestId,
      items: p.query.trim() ? [`${p.query.trim()} tutorial`, `${p.query.trim()} examples`] : []
    }),
    'command.deleteEntry': () => null,
    'history.query': (p: { text?: string }) => {
      const text = p.text?.toLowerCase() ?? ''
      return { rows: fakeHistory().filter((r) => !text || r.title.toLowerCase().includes(text) || r.url.includes(text)), nextCursor: null }
    },
    'archive.list': (p: { profileId: string; query?: string; cursor?: string; limit?: number }) => {
      // The query matches title and URL literally, as the browser's escaped LIKE does (% and _ are plain characters).
      const query = p.query?.trim().toLowerCase() ?? ''
      const rows = archived.filter((r) => r.profileId === p.profileId && (!query || r.title.toLowerCase().includes(query) || r.url.toLowerCase().includes(query)))
      const start = p.cursor ? Math.max(0, rows.findIndex((r) => r.id === p.cursor) + 1) : 0
      const limit = p.limit ?? 100
      const page = rows.slice(start, start + limit)
      return { rows: page, nextCursor: start + limit < rows.length ? (page.at(-1)?.id ?? null) : null }
    },
    'archive.open': (_p, { windowId }) => setLibrary(windowId, true),
    'archive.close': (_p, { windowId }) => setLibrary(windowId, false),
    'archive.setWidth': (p: { width: number }, { windowId }) => setLibraryWidth(windowId, p.width),
    'archive.restore': (p: { archiveId: string }, { windowId }) => {
      const row = archived.find((r) => r.id === p.archiveId)
      if (!row) return undefined
      archived = archived.filter((r) => r.id !== p.archiveId)
      const tabId = createTab(windowId, row.url, row.spaceId && state.spaces[row.spaceId] ? row.spaceId : undefined)
      set(['tabs', tabId, 'title'], row.title)
      activate(windowId, tabId)
      bumpArchive()
      return null
    },
    'archive.delete': (p: { archiveId: string }) => {
      archived = archived.filter((r) => r.id !== p.archiveId)
      bumpArchive()
    },
    'archive.clearToday': (p: { spaceId?: string }, { windowId }) => clearToday(windowId, p.spaceId ?? win(windowId)?.activeSpaceId ?? ''),
    'archive.undoClear': (_p, { windowId }) => undoClear(windowId),
    // The harness has no on-device model, so Tidy Today falls back to its rules.
    'tabs.tidyToday': () => ({ status: 'no-model' }),
    // Only the move form, as Tidy Today sends it: the tabs land together in the given order (TabStore.moveMany).
    'tabs.bulk': (p: { tabIds: string[]; action: string; target?: DropTarget }, { windowId }) => {
      if (p.action !== 'move' || !p.target) return fail('not-implemented')
      const target = p.target
      p.tabIds.forEach((id, i) => moveTab(windowId, id, 'index' in target ? { ...target, index: target.index + i } : target))
      return { status: 'moved' }
    },
    'tabs.move': (p: { nodeId: string; target: DropTarget; ask?: boolean }, { windowId }) => {
      const r = moveTab(windowId, p.nodeId, p.target)
      if (r.ok) return { status: 'moved' }
      if (p.ask && r.reason === 'favorites-full') toast(windowId, 'Favorites are full')
      return undefined
    },
    // The browser answers these as CluDownloadHandler does (docs/chromium/m9.md): rows by id only, Chromium's
    // bubble and Finder outside the WebUIs, so openPopover and openFolder draw nothing here.
    'download.openPopover': () => null,
    // The browser keeps the button's rect for the bubble's anchor; the harness has no native bubble.
    'download.setButtonRect': () => null,
    'download.openFolder': () => null,
    'download.act': (p: { id: string; action: DownloadAction }) => {
      const rows = state.downloads ?? []
      const row = rows.find((d) => d.id === p.id)
      if (!row) return fail('not-found')
      const next = actOnDownload(row, p.action)
      if (next === undefined) return fail('rejected', 'unavailable')
      set(['downloads'], next === null ? rows.filter((d) => d.id !== p.id) : rows.map((d) => (d.id === p.id ? next : d)))
      return null
    },
    // The Library's folders: seeded with downloads, else empty, as a Mac that has not granted access.
    'download.files': (p: { source: LibrarySource }) => (options.downloads ? fakeLibraryFiles()[p.source] : []),
    // Copy and Copy Path toast what they put on the clipboard, as CluDownloadHandler does.
    'download.fileAct': (p: { source: LibrarySource; name: string; action: string }, { windowId }) => {
      if (!options.downloads || !fakeLibraryFiles()[p.source].some((f) => f.name === p.name)) return fail('not-found', 'no such file')
      if (p.action === 'copy' || p.action === 'copyPath') toast(windowId, p.action === 'copy' ? 'Copied File' : 'Copied Path')
      return null
    },
    // QuickLook stand-in: a flat swatch for the seeded images, nothing for other files.
    'download.thumb': (p: { source: LibrarySource; name: string }) => {
      const file = options.downloads ? fakeLibraryFiles()[p.source].find((f) => f.name === p.name) : undefined
      // Rio's own finished downloads sit in the Downloads folder too: QuickLook has nothing for the seeded PDF.
      const ours = p.source === 'downloads' && (state.downloads ?? []).some((d) => d.filename === p.name && d.state === 'completed' && d.exists)
      if (!file) return ours ? '' : fail('not-found', 'no such file')
      return file.kind === 'image' ? `data:image/svg+xml,${encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="80" height="80"><rect width="80" height="80" fill="#9ab"/></svg>')}` : ''
    },
    'download.clear': () => set(['downloads'], (state.downloads ?? []).filter((d) => isActiveDownload(d.state))),
    'nav.getHistory': () => [],
    // As CluSettingsHandler::Set: the registry decides, so an unknown key, the wrong scope or a value it refuses is
    // bad-payload and changes nothing; appearance.mode applies at once.
    'settings.set': (p: { key: string; scope: string; value: Json }) => {
      const def = settingDef(p.key)
      if (!def || (def.scope === 'global') !== (p.scope === 'global') || !def.validate(p.value)) return fail('bad-payload')
      if (p.key === 'appearance.mode' && typeof p.value === 'string') {
        set(['app', 'appearance', 'mode'], p.value)
        set(['app', 'appearance', 'resolved'], p.value === 'dark' ? 'dark' : 'light')
      }
      return p.scope === 'global' ? set(['settings', 'global', p.key], p.value) : set(['settings', 'byProfile', p.scope, p.key], p.value)
    },
    'theme.setMode': (p: { mode: 'auto' | 'light' | 'dark' }) => {
      set(['app', 'appearance', 'mode'], p.mode)
      set(['app', 'appearance', 'resolved'], p.mode === 'dark' ? 'dark' : 'light')
      set(['settings', 'global', 'appearance.mode'], p.mode)
    },
    // M8, as CluThemeHandler, CluOnboardingHandler, CluBrowsingDataHandler and CluImportHandler answer.
    'theme.previewSpaceTheme': (p: { spaceId: string; theme: SpaceTheme }) => {
      const space = state.spaces[p.spaceId]
      if (!space) return { __fakeError: { code: 'not-found' } }
      if (!committedThemes.has(p.spaceId)) committedThemes.set(p.spaceId, space.theme)
      showTheme(p.spaceId, p.theme)
      return undefined
    },
    'theme.setSpaceTheme': (p: { spaceId: string; theme: SpaceTheme }) => {
      if (!state.spaces[p.spaceId]) return { __fakeError: { code: 'not-found' } }
      committedThemes.delete(p.spaceId)
      showTheme(p.spaceId, p.theme)
      return undefined
    },
    'theme.resetSpaceTheme': (p: { spaceId: string }) => {
      if (!state.spaces[p.spaceId]) return { __fakeError: { code: 'not-found' } }
      committedThemes.delete(p.spaceId)
      showTheme(p.spaceId, null)
      return undefined
    },
    'theme.openPicker': (p: { spaceId: string; anchor: Rect }, { windowId }) => {
      if (!state.spaces[p.spaceId]) return { __fakeError: { code: 'not-found' } }
      openSurface(windowId, { surface: 'theme-picker', modality: 'light-dismiss', focus: true, rect: null, props: { spaceId: p.spaceId, anchor: p.anchor } as unknown as Json })
      return undefined
    },
    'onboarding.complete': () => {
      if (!state.app.onboarding.show) return
      set(['app', 'onboarding'], { show: false })
      set(['settings', 'global', 'onboarding.completedAt'], new Date(FAKE.now).toISOString())
    },
    'browsingData.open': (p: { profileId?: string }, { windowId }) => {
      openSurface(windowId, { surface: 'clear-data', modality: 'modal', focus: true, rect: null, props: { profileId: p.profileId ?? FAKE.profile } })
    },
    'browsingData.clear': (p: { profileId: string; types: { archive: boolean } }, { windowId }) => {
      if (!state.profiles[p.profileId]) return { __fakeError: { code: 'not-found' } }
      if (p.types.archive) {
        archived = archived.filter((r) => r.profileId !== p.profileId)
        bumpArchive()
      }
      toast(windowId, 'Browsing data cleared')
      return undefined
    },
    'import.detect': (_p, { windowId }) => ({
      chrome: {
        profiles: [
          { dir: 'Default', name: 'Personal', hasBookmarks: true, hasHistory: false },
          { dir: 'Profile 1', name: 'Work', hasBookmarks: true, hasHistory: true }
        ]
      },
      // ?arc=0: no other browser's sidebar file, so setup has no switch screen and its Import step asks.
      arc: { found: !options.noArc, previous: imported.has('arc') ? 2 : 0 },
      currentSpaceId: win(windowId)?.activeSpaceId ?? null
    }),
    'import.run': (p: { source: 'chrome' | 'arc'; passwords?: boolean; cookies?: boolean; replace?: boolean }) => {
      // A replaced import comes over in full again.
      const again = imported.has(p.source) && !p.replace
      imported.add(p.source)
      if (p.source === 'chrome') {
        // ?keychain=deny: the user said no to "Chrome Safe Storage", so the sign-ins stay behind.
        const denied = options.keychainDenied === true && !!(p.passwords || p.cookies)
        // Site storage and permissions come with the sign-ins (not encrypted, so a denied prompt keeps them).
        const signins = {
          passwords: again || denied || !p.passwords ? 0 : 3,
          cookies: again || denied || !p.cookies ? 0 : 2,
          secretsDenied: denied,
          siteStorage: again || !p.cookies ? 0 : 2,
          permissions: again || !p.cookies ? 0 : 1,
          addresses: again ? 0 : 1,
          searchEngine: null
        }
        return again ? { bookmarks: 0, history: 0, ...signins, skipped: 2, error: null } : { bookmarks: 13, history: 4, ...signins, skipped: 2, error: null }
      }
      const skipped = { unsupported: 2, missing: 1, cycles: 1, badUrls: 1, overCap: 0, duplicates: 1, historyFiles: 0 }
      // Site storage, permissions and addresses with the sign-ins.
      const site = !again && p.cookies ? 1 : 0
      const secrets = {
        passwords: !again && p.passwords ? 3 : 0,
        cookies: !again && p.cookies ? 2 : 0,
        secretsDenied: false,
        secretsSkipped: p.passwords || p.cookies ? 2 : 0,
        siteStorage: site * 2,
        permissions: site,
        addresses: site
      }
      return again
        ? { profiles: 0, spaces: 0, pinned: 0, today: 0, favorites: 0, history: 0, ...secrets, skipped, error: null }
        : { profiles: 2, spaces: 4, pinned: 10, today: 3, favorites: 3, history: 3, ...secrets, skipped, extensions: [{ id: 'a'.repeat(32), name: 'Notes' }], error: null }
    },
    'import.openPasswords': () => undefined,
    'settings.open': () => undefined,
    'settings.openBrowser': () => undefined,
    'history.open': () => undefined
  }

  async function invoke(surface: FakeSurface, name: CommandName, payload: unknown): Promise<IpcResult<unknown>> {
    const def: CommandDef = contract[name]
    const record = (result: IpcResult<unknown>) => {
      calls.push({ surface: surface.surface, name, payload, result })
      return result
    }
    if (!def.allow.includes(surface.surface)) return record({ ok: false, error: { code: 'not-allowed' } })
    if (textBytes(payload) > MAX_PAYLOAD_BYTES) return record({ ok: false, error: { code: 'too-large' } })
    const parsed = def.input.safeParse(payload ?? {})
    if (!parsed.success) return record({ ok: false, error: { code: 'bad-payload', message: parsed.error.issues[0]?.message ?? 'invalid' } })
    const handler = handlers[name]
    const windowId = surface.windowId ?? ''
    if (!handler) return record({ ok: false, error: { code: 'not-implemented' } })
    if (surface.windowId && !win(windowId)) return record({ ok: false, error: { code: 'not-found' } })
    const value = await (handler as (p: unknown, ctx: Ctx) => unknown)(parsed.data, { surface, windowId })
    // A handler refuses as the browser's handlers do: a typed error, nothing changed.
    if (value && typeof value === 'object' && '__fakeError' in value) {
      return record({ ok: false, error: (value as { __fakeError: { code: string; reason?: string } }).__fakeError } as IpcResult<unknown>)
    }
    // Answers cross the bridge as JSON, as they do over Mojo.
    return record({ ok: true, value: JSON.parse(JSON.stringify(value ?? null)) as unknown })
  }

  return {
    connect(spec) {
      const surface: FakeSurface = { windowId: FAKE.window, layer: null, slot: null, ...spec }
      const areas: Record<string, Record<string, (p?: unknown) => Promise<IpcResult<unknown>>>> = {}
      for (const name of Object.keys(contract) as CommandName[]) {
        const dot = name.indexOf('.')
        ;(areas[name.slice(0, dot)] ??= {})[name.slice(dot + 1)] = (p?: unknown) => invoke(surface, name, p)
      }
      const bridge = {
        ...areas,
        onState(cb: (msg: StateMessage) => void) {
          streams.add(cb)
          queueMicrotask(() => {
            if (streams.has(cb)) cb({ kind: 'snapshot', rev, state })
          })
          return () => {
            streams.delete(cb)
          }
        },
        windowId: surface.windowId,
        layer: surface.layer,
        slot: surface.slot
      }
      return bridge as unknown as CluBridge
    },
    state: () => state,
    calls,
    setContentSize(size) {
      for (const id of Object.keys(state.windows)) relayout(id, { contentSize: size })
      flush()
    },
    subscribe(fn) {
      watchers.add(fn)
      return () => watchers.delete(fn)
    },
    flush,
    edit(path, value) {
      set(path, value)
      flush()
    }
  }
}

