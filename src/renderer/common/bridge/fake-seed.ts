// Seeded state for FakeBridge (tests, the ui:fake harness, screenshots): one window, three spaces on one profile,
// favorites, a pinned folder, Today tabs in every page state the sidebar draws, and browsing history.
// Ids are fixed UUID v4 strings so tests and screenshots are stable. Hosts are example domains, never real brands.
import type { DownloadState } from '../../../shared/capabilities'
import type { ArchiveRow } from '../../../shared/ipc/archive'
import type { LibraryFile, LibrarySource } from '../../../shared/ipc/capabilities'
import type { HistoryRow } from '../../../shared/ipc/data'
import type { IndexRow } from '../../../shared/ipc/command'
import { computeLayout } from '../../../shared/shell/layout'
import { defaultProfileState, defaultSpaceState, defaultState, defaultTabState, defaultWindowState, type CluState, type TabState } from '../../../shared/state'

const uuid = (prefix: number, n: number) => `00000000-0000-4000-8000-${prefix.toString(16).padStart(4, '0')}${n.toString(16).padStart(8, '0')}`

export const FAKE = {
  window: uuid(1, 1),
  profile: uuid(2, 1),
  spaces: { home: uuid(3, 1), work: uuid(3, 2), reading: uuid(3, 3) },
  folder: uuid(4, 1),
  tab: (n: number) => uuid(5, n),
  /** The fixed epoch the seed's timestamps count from (2026-11-07 09:00 UTC). */
  now: Date.UTC(2026, 10, 7, 9, 0, 0)
} as const

interface TabSeed { n: number; url: string; title: string; space: string | null; section: TabState['section']; extra?: Partial<TabState> }

const TABS: TabSeed[] = [
  { n: 1, url: 'https://mail.example.com/inbox', title: 'Inbox (3)', space: null, section: 'favorite' },
  { n: 2, url: 'https://calendar.example.com/week', title: 'Calendar, week of Nov 2', space: null, section: 'favorite' },
  { n: 3, url: 'https://notes.example.org/', title: 'Notes', space: null, section: 'favorite' },
  { n: 4, url: 'https://music.example.net/now', title: 'Now playing', space: null, section: 'favorite', extra: { audible: true, mediaPlaying: true } },
  { n: 10, url: 'https://tracker.example.com/board', title: 'Roadmap board', space: 'home', section: 'pinned' },
  { n: 11, url: 'https://docs.example.com/spec/clu-sidebar', title: 'Sidebar spec, draft 4', space: 'home', section: 'pinned', extra: { parentId: null } },
  { n: 12, url: 'https://design.example.com/file/frame', title: 'Frame WebUI mocks', space: 'home', section: 'pinned' },
  { n: 13, url: 'https://wiki.example.org/chromium/mojo', title: 'Mojo bindings in WebUI', space: 'home', section: 'pinned' },
  { n: 20, url: 'https://news.example.com/', title: 'Morning headlines', space: 'home', section: 'today' },
  { n: 21, url: 'https://code.example.com/clu/pull/412', title: 'feat(chromium): M2 bridge layer by a long-titled author who writes long titles', space: 'home', section: 'today' },
  { n: 22, url: 'https://video.example.net/watch?v=42', title: 'Conference talk: browsers in 2026', space: 'home', section: 'today', extra: { audible: true, mediaPlaying: true } },
  { n: 23, url: 'https://search.example.com/?q=webui+csp', title: 'webui csp - Search', space: 'home', section: 'today', extra: { loading: true, progress: 0.4 } },
  { n: 24, url: 'https://shop.example.com/cart', title: 'Cart', space: 'home', section: 'today', extra: { muted: true, audible: true } },
  { n: 30, url: 'https://crm.example.com/accounts', title: 'Accounts', space: 'work', section: 'pinned' },
  { n: 31, url: 'https://sheets.example.com/q4', title: 'Q4 plan', space: 'work', section: 'today' },
  { n: 40, url: 'https://read.example.org/essay', title: 'An essay on sidebars', space: 'reading', section: 'today' }
]

export function fakeSeed(contentSize = { width: 1280, height: 800 }): CluState {
  const s = defaultState()
  s.app = { ...s.app, version: '0.1.0', versions: { chrome: '158.0.8127.18', node: '' } }
  s.profiles[FAKE.profile] = defaultProfileState({ id: FAKE.profile, name: 'Personal', isDefault: true, ephemeral: false })
  const space = (id: string, name: string, color: 'blue' | 'violet' | 'green', icon: CluState['spaces'][string]['icon']) =>
    (s.spaces[id] = defaultSpaceState({ id, name, icon, color, profileId: FAKE.profile, ephemeral: false }))
  space(FAKE.spaces.home, 'Home', 'blue', { kind: 'letter' })
  space(FAKE.spaces.work, 'Work', 'violet', { kind: 'emoji', value: '💼' })
  space(FAKE.spaces.reading, 'Reading', 'green', { kind: 'emoji', value: '📚' })
  s.spaceOrder = [FAKE.spaces.home, FAKE.spaces.work, FAKE.spaces.reading]

  for (const t of TABS) {
    const id = FAKE.tab(t.n)
    const spaceId = t.space ? FAKE.spaces[t.space as keyof typeof FAKE.spaces] : null
    s.tabs[id] = {
      ...defaultTabState({ id, profileId: FAKE.profile, spaceId, section: t.section, url: t.url, title: t.title, lastActiveAt: FAKE.now - t.n * 60_000 }),
      lifecycle: 'live',
      pinnedUrl: t.section === 'today' ? null : t.url,
      ...t.extra
    }
  }
  s.favorites[FAKE.profile] = [1, 2, 3, 4].map(FAKE.tab)
  s.folders[FAKE.folder] = {
    id: FAKE.folder,
    spaceId: FAKE.spaces.home,
    name: 'Chromium M2',
    icon: null,
    collapsed: false,
    children: [{ kind: 'tab', id: FAKE.tab(12) }, { kind: 'tab', id: FAKE.tab(13) }]
  }
  for (const n of [12, 13]) {
    const tab = s.tabs[FAKE.tab(n)]
    if (tab) tab.parentId = FAKE.folder
  }
  const home = s.spaces[FAKE.spaces.home]
  if (home) {
    home.pinned = [{ kind: 'tab', id: FAKE.tab(10) }, { kind: 'tab', id: FAKE.tab(11) }, { kind: 'folder', id: FAKE.folder }]
    home.today = [20, 21, 22, 23, 24].map(FAKE.tab)
  }
  const work = s.spaces[FAKE.spaces.work]
  if (work) {
    work.pinned = [{ kind: 'tab', id: FAKE.tab(30) }]
    work.today = [FAKE.tab(31)]
  }
  const reading = s.spaces[FAKE.spaces.reading]
  if (reading) reading.today = [FAKE.tab(40)]

  const w = defaultWindowState({ id: FAKE.window, kind: 'main', private: false, activeSpaceId: FAKE.spaces.home })
  w.focused = true
  w.activeTabId = FAKE.tab(21)
  w.focusedTabId = FAKE.tab(21)
  w.shell = { ...w.shell, layout: computeLayout(contentSize) }
  s.windows[FAKE.window] = w
  return { ...s, rev: 1 }
}

const HISTORY: [url: string, title: string][] = [
  ['https://docs.example.com/spec/clu-sidebar', 'Sidebar spec, draft 4'],
  ['https://wiki.example.org/chromium/mojo', 'Mojo bindings in WebUI'],
  ['https://wiki.example.org/chromium/webui-csp', 'Content Security Policy for WebUI'],
  ['https://code.example.com/clu/pull/412', 'feat(chromium): M2 bridge layer'],
  ['https://code.example.com/clu/issues', 'Issues'],
  ['https://news.example.com/', 'Morning headlines'],
  ['https://news.example.com/tech', 'Tech headlines'],
  ['https://video.example.net/watch?v=42', 'Conference talk: browsers in 2026'],
  ['https://recipes.example.org/soup', 'Weeknight soup'],
  ['https://maps.example.com/route', 'Route to the office'],
  ['https://weather.example.com/today', 'Weather today'],
  ['https://shop.example.com/cart', 'Cart']
]

export function fakeHistory(): HistoryRow[] {
  return HISTORY.map(([url, title], i) => ({ id: i + 1, urlId: i + 1, url, title, visitedAt: FAKE.now - i * 3_600_000, transition: 'link', favicon: null }))
}

export function fakeIndex(): IndexRow[] {
  return HISTORY.map(([url, title], i) => ({ key: url, kind: 'history', url, title, visitCount: 12 - i, typedCount: i % 3, lastVisitAt: FAKE.now - i * 3_600_000 }))
}

const ARCHIVE: [url: string, title: string, reason: string, hoursAgo: number][] = [
  ['https://docs.example.com/spec/archive', 'Archive panel spec', 'close', 1],
  ['https://deals.example.com/', '100% done: deals of the day', 'auto', 3],
  ['https://deals.example.com/1000', '1000 deals', 'auto', 4],
  ['https://code.example.com/clu/pull/400', 'feat(chromium): M1 branding', 'clear', 26],
  ['https://read.example.org/old-essay', 'An older essay', 'delete', 50]
]

/** Archived tabs of the seed's profile, newest first (archive.list pages through them). */
export function fakeArchive(): ArchiveRow[] {
  return ARCHIVE.map(([url, title, reason, hoursAgo], i) => ({
    id: uuid(6, i + 1),
    profileId: FAKE.profile,
    spaceId: FAKE.spaces.home,
    title,
    url,
    favicon: null,
    reason,
    archivedAt: FAKE.now - hoursAgo * 3_600_000
  }))
}

/**
 * Downloads of the seed's profile, newest first, as Chromium's download manager reports them (docs/chromium/m9.md):
 * one running with a known size, one paused, one finished and one finished but removed from disk.
 */
export function fakeDownloads(): DownloadState[] {
  const row = (n: number, filename: string, patch: Partial<DownloadState>): DownloadState => ({
    id: uuid(7, n),
    profileId: FAKE.profile,
    filename,
    url: `https://files.example.com/${filename}`,
    state: 'completed',
    receivedBytes: 4_200_000,
    totalBytes: 4_200_000,
    bytesPerSecond: 0,
    etaMs: null,
    startedAt: FAKE.now - n * 60_000,
    endedAt: FAKE.now - n * 60_000 + 5_000,
    exists: true,
    ...patch
  })
  return [
    row(1, 'quarterly-report.pdf', { state: 'progressing', receivedBytes: 1_680_000, bytesPerSecond: 420_000, etaMs: 6_000, endedAt: null }),
    row(2, 'clu-sidebar-mocks.zip', { state: 'paused', receivedBytes: 900_000, totalBytes: 12_400_000, endedAt: null }),
    row(3, 'invoice-2026-11.pdf', {}),
    row(4, 'old-notes.txt', { exists: false })
  ]
}

/**
 * The Library's folders as download.files reads them, newest first: the Mac's Downloads folder (one of its
 * files is also a Rio download, which the Library shows once) and the screenshot folder.
 */
export function fakeLibraryFiles(): Record<LibrarySource, LibraryFile[]> {
  const file = (name: string, minutesAgo: number, kind: LibraryFile['kind'], size = 240_000): LibraryFile => ({ name, size, modifiedAt: FAKE.now - minutesAgo * 60_000, kind })
  return {
    downloads: [
      file('invoice-2026-11.pdf', 3, 'file', 4_200_000),
      file('Team offsite.key', 90, 'file', 18_000_000),
      file('logo-final.png', 26 * 60, 'image')
    ],
    media: [
      file('Screenshot 2026-11-09 at 18.58.12.png', 2, 'image'),
      file('Screen Recording 2026-11-09 at 18.40.03.mov', 20, 'video', 9_800_000),
      file('Screenshot 2026-11-08 at 22.10.44.png', 21 * 60, 'image')
    ]
  }
}
