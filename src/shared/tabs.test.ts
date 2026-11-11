import { randomUUID } from 'node:crypto'
import { describe, expect, it } from 'vitest'
import { defaultSpaceState, defaultState, defaultTabState, defaultWindowState, type CluState, type NodeRef } from './state'
import { cleanName, FOLDER_PEEK, folderPeekCard, folderPeekRect, folderPeekTabs, HOVER_CARD, hostAndPath, hoverCardModel, hoverCardRect, hoverChips, isMailPreview, isOffPinned, LIVE_PREVIEW, MAIL_CARD, mailCardAnchor, mailCardHeight, previewSite, isOneEmoji, MAX_FOLDER_NAME, middleTruncate, sidebarOrder, timeAgo, typedPinnedUrl, unreadCount } from './tabs'

describe('typedPinnedUrl', () => {
  it('adds https:// to a bare host, keeps a scheme, and handles host:port', () => {
    expect(typedPinnedUrl('  example.com/docs ')).toBe('https://example.com/docs')
    expect(typedPinnedUrl('http://example.com')).toBe('http://example.com/')
    expect(typedPinnedUrl('localhost:8080/a')).toBe('https://localhost:8080/a')
  })
  it('refuses empty text, text with spaces and schemes a tab may not open', () => {
    for (const bad of ['', '   ', 'two words', 'javascript:alert(1)', 'file:///etc/passwd', 'data:text/html,hi']) expect(typedPinnedUrl(bad)).toBeNull()
  })
})

describe('isOffPinned', () => {
  it('ignores the fragment and a trailing slash, and is false for unanchored tabs', () => {
    expect(isOffPinned('https://a.com/x/#top', 'https://a.com/x')).toBe(false)
    expect(isOffPinned('https://a.com/y', 'https://a.com/x')).toBe(true)
    expect(isOffPinned('https://a.com/y', null)).toBe(false)
  })
})

describe('isOneEmoji', () => {
  it('accepts one emoji grapheme, including ZWJ sequences, flags, keycaps and skin tones', () => {
    for (const ok of ['🚀', '❤️', '👨‍👩‍👧‍👦', '🇦🇲', '1️⃣', '👍🏽', '🏳️‍🌈']) expect(isOneEmoji(ok)).toBe(true)
  })
  it('refuses letters, two emoji, an emoji with text, empty and non-strings', () => {
    for (const bad of ['ab', 'a', '🚀🚀', '🚀a', ' 🚀', '', '1', '#', 42, null, undefined, '🚀'.repeat(20)]) expect(isOneEmoji(bad)).toBe(false)
  })
})

describe('cleanName', () => {
  it('trims, collapses white space and control characters, and caps at 200', () => {
    expect(cleanName('  Work \n\t stuff\u0000 ')).toBe('Work stuff')
    expect(cleanName('   ')).toBe('')
    expect(cleanName('x'.repeat(500))).toHaveLength(MAX_FOLDER_NAME)
  })
})

describe('middleTruncate', () => {
  it('keeps short text, and keeps the start and end of long text around one ellipsis at exactly max characters', () => {
    expect(middleTruncate('example.com/a', 20)).toBe('example.com/a')
    expect(middleTruncate('abcdefghij', 10)).toBe('abcdefghij')
    expect(middleTruncate('abcdefghijk', 10)).toBe('abcde…hijk')
    expect(middleTruncate('example.com/very/long/path/to/a/page.html', 21)).toBe('example.co…/page.html')
    expect(Array.from(middleTruncate('x'.repeat(5000), 44))).toHaveLength(44)
  })
  it('never splits an emoji or a surrogate pair, and handles tiny limits', () => {
    const out = middleTruncate('🚀'.repeat(30), 7)
    expect(Array.from(out)).toEqual(['🚀', '🚀', '🚀', '…', '🚀', '🚀', '🚀'])
    expect(middleTruncate('abc', 1)).toBe('…')
    expect(middleTruncate('abc', 0)).toBe('…')
    expect(middleTruncate('', 5)).toBe('')
  })
})

describe('hover card', () => {
  const base = { title: 'Docs', titleOverride: null, url: 'https://www.example.com/docs/', pinnedUrl: null, offPinned: false, audible: false, muted: false, lifecycle: 'live' }
  it('chips: Playing while audible, Muted instead of Playing, Asleep without a live page', () => {
    expect(hoverChips(base)).toEqual([])
    expect(hoverChips({ ...base, audible: true })).toEqual(['Playing'])
    expect(hoverChips({ ...base, audible: true, muted: true })).toEqual(['Muted'])
    expect(hoverChips({ ...base, muted: true, lifecycle: 'hibernated' })).toEqual(['Muted', 'Asleep'])
    expect(hoverChips({ ...base, lifecycle: 'unloaded' })).toEqual(['Asleep'])
    expect(hoverChips({ ...base, lifecycle: 'restoring' })).toEqual([])
  })
  it('shows host and path without the scheme, www, query or fragment; junk URLs as typed', () => {
    expect(hostAndPath('https://www.example.com/a/b?q=1#x')).toBe('example.com/a/b')
    expect(hostAndPath('http://localhost:8080/')).toBe('localhost:8080')
    expect(hostAndPath('about:blank')).toBe('about:blank')
    expect(hostAndPath('rio://settings/')).toBe('rio://settings')
    expect(hostAndPath('not a url')).toBe('not a url')
  })
  it('sizes the view to the widest title pill plus its shadow margin, whatever the tab shows', () => {
    const one = hoverCardModel(base)
    expect(one).toMatchObject({ title: 'Docs', titleLines: 1, location: 'example.com/docs/', pinned: null, chips: [] })
    expect(one.width).toBe(HOVER_CARD.maxPill + 2 * HOVER_CARD.margin)
    expect(one.height).toBe(HOVER_CARD.pill + 2 * HOVER_CARD.margin)
    const full = hoverCardModel({ ...base, title: 'A title that is long enough to wrap onto a second line', pinnedUrl: 'https://example.com/home', offPinned: true, audible: true })
    expect(full).toMatchObject({ titleLines: 1, pinned: 'example.com/home', chips: ['Playing'], width: one.width, height: one.height })
  })
  it('gives Gmail the mail card with the title count until the feed answers, never a live page, and the plain card for a title with no count', () => {
    const mail = hoverCardModel({ ...base, title: 'Inbox (12) - me@example.com - Mail', url: 'https://mail.google.com/mail/u/0/#inbox', audible: true, lifecycle: 'unloaded' })
    expect(mail).toMatchObject({ kind: 'mail', unread: 12, chips: [], site: 'Gmail', mailSource: 'title', mailItems: [], width: MAIL_CARD.width + 2 * HOVER_CARD.margin, height: MAIL_CARD.short + 2 * HOVER_CARD.margin })
    // A title without a count says nothing about the mail, loaded or asleep (a message view, a label, a search, a
    // page still loading): the plain card, never a made-up "No unread emails".
    for (const title of ['Inbox - Mail', 'Re: invoice - me@x.com - Gmail', 'Gmail', ''])
      for (const lifecycle of ['live', 'unloaded', 'hibernated'])
        expect(hoverCardModel({ ...base, title, url: 'https://mail.google.com/', lifecycle }), `${title} ${lifecycle}`).toMatchObject({ kind: 'title', unread: 0, site: null, height: HOVER_CARD.pill + 2 * HOVER_CARD.margin })
    expect(hoverCardModel({ ...base, title: '(2) Gmail', url: 'https://mail.google.com/', lifecycle: 'hibernated' })).toMatchObject({ kind: 'mail', unread: 2, mailSource: 'title' })
    expect([0, 3, 5, 40, -1, Number.NaN].map(mailCardHeight)).toEqual([64, 6 + 30 + 3 * 44 + 6, 6 + 30 + 5 * 44 + 6, 6 + 30 + 5 * 44 + 6, 64, 64])
    expect(hoverCardModel(base).kind).toBe('title')
    expect(isMailPreview('not a url')).toBe(false)
    for (const [t, n] of [['(3) Inbox', 3], ['Inbox (99+)', 99], ['Inbox (0)', 0], ['f(3) x', 0], ['(3)x', 0], ['(1234567) x', 0], ['('.repeat(400), 0]] as const) expect(unreadCount(t), t).toBe(n)
  })
  it('gives a live page on a preview site the 239 x 256 live card named for its site', () => {
    const cal = hoverCardModel({ ...base, title: 'Google Calendar - Today', url: 'https://calendar.google.com/calendar/u/0/r/day', audible: true })
    expect(cal).toMatchObject({ kind: 'preview', site: 'Google Calendar', unread: 0, chips: [], width: LIVE_PREVIEW.width + 2 * HOVER_CARD.margin, height: LIVE_PREVIEW.height + 2 * HOVER_CARD.margin })
    expect(hoverCardModel({ ...base, title: 'Inbox (3) - Mail', url: 'https://mail.google.com/mail/' })).toMatchObject({ kind: 'mail', site: 'Gmail', unread: 3 })
    // An asleep Calendar or Meet row (a restored pinned row) gets the live card as a loaded one does.
    for (const lifecycle of ['unloaded', 'hibernated', 'restoring']) {
      expect(hoverCardModel({ ...base, url: 'https://calendar.google.com/', lifecycle }).kind, lifecycle).toBe('preview')
      expect(hoverCardModel({ ...base, url: 'https://meet.google.com/', lifecycle }).kind, lifecycle).toBe('preview')
    }
    expect(hoverCardModel({ ...base, url: 'https://example.com/', lifecycle: 'unloaded' }).kind).toBe('title')
    const sites = { 'https://meet.google.com/abc-defg-hij': 'Google Meet', 'https://calendar.google.com/calendar/u/0/r': 'Google Calendar', 'https://mail.google.com/mail/u/1/#inbox': 'Gmail' }
    for (const [url, site] of Object.entries(sites)) expect(previewSite(url), url).toBe(site)
    for (const url of ['https://example.com/', 'ftp://github.com/', 'not a url', '', 'https://calendar.google.com.evil.test/', 'https://evil.test/calendar.google.com', 'https://constructor.github.io/', 'https://github.com/x', 'https://outlook.office.com/calendar/', 'https://www.notion.so/p']) expect(previewSite(url), url).toBeNull()
    expect(hoverCardModel({ ...base, url: 'https://calendar.google.com.evil.test/' }).kind).toBe('title')
  })
  it('hangs the card 11 right of and 12 below the pointer, inside the window', () => {
    const m = hoverCardModel(base)
    const r = hoverCardRect({ width: 1280, height: 800 }, { x: 100, y: 400 }, m)
    expect(r).toEqual({ x: 100 + 11 - HOVER_CARD.margin, y: 400 + 12 - HOVER_CARD.margin, width: m.width, height: m.height })
    expect(hoverCardRect({ width: 1280, height: 800 }, { x: 1270, y: 795 }, m)).toMatchObject({ x: 1280 - m.width, y: 800 - m.height })
    expect(hoverCardRect({ width: 100, height: 20 }, { x: Number.NaN, y: -50 }, m)).toMatchObject({ x: 0, y: 0 })
  })
  it('handles junk: a 2000-character title with control characters, a huge URL, no title at all', () => {
    const junk = hoverCardModel({ ...base, title: `a\u0000b${'x'.repeat(2000)}`, url: `https://example.com/${'p'.repeat(10_000)}` })
    expect(junk.title.length).toBeLessThanOrEqual(300)
    expect(junk.title.startsWith('a b')).toBe(true)
    expect(Array.from(junk.location)).toHaveLength(HOVER_CARD.locationChars)
    expect(hoverCardModel({ ...base, title: '', url: 'https://www.example.com/a/b' }).title).toBe('example.com')
    expect(hoverCardModel({ ...base, title: '  ', titleOverride: 'Mine' }).title).toBe('Mine')
  })
})

// ---- F12 row order: favorites, visible pinned rows, Today; folders collapsed and expanded, active descendant counted.

const P = randomUUID()
const Q = randomUUID()
const S = randomUUID()
const OTHER = randomUUID()
const W = randomUUID()

/** Favorites f1 f2; pinned: p1, folder A [a1, folder B [b1, b2], a2], p2; Today t1 t2. */
function fixture() {
  const s: CluState = defaultState()
  const ids = Object.fromEntries(['f1', 'f2', 'p1', 'a1', 'b1', 'b2', 'a2', 'p2', 't1', 't2', 'x1', 'qf'].map((k) => [k, randomUUID()])) as Record<string, string>
  const A = randomUUID()
  const B = randomUUID()
  const tab = (id: string): NodeRef => ({ kind: 'tab', id })
  for (const id of Object.values(ids)) s.tabs[id] = defaultTabState({ id, profileId: P, spaceId: S, section: 'today', url: 'https://a.test/' })
  s.profiles[P] = { id: P, name: 'P', isDefault: true, ephemeral: false, extensions: [] }
  s.spaces[S] = { ...defaultSpaceState({ id: S, name: 'S', icon: { kind: 'letter' }, color: 'blue', profileId: P, ephemeral: false }), pinned: [tab(ids.p1 ?? ''), { kind: 'folder', id: A }, tab(ids.p2 ?? '')], today: [ids.t1 ?? '', ids.t2 ?? ''] }
  s.spaces[OTHER] = { ...defaultSpaceState({ id: OTHER, name: 'O', icon: { kind: 'letter' }, color: 'red', profileId: Q, ephemeral: false }), today: [ids.x1 ?? ''] }
  s.folders[A] = { id: A, spaceId: S, name: 'A', icon: null, collapsed: false, children: [tab(ids.a1 ?? ''), { kind: 'folder', id: B }, tab(ids.a2 ?? '')] }
  s.folders[B] = { id: B, spaceId: S, name: 'B', icon: null, collapsed: false, children: [tab(ids.b1 ?? ''), tab(ids.b2 ?? '')] }
  s.favorites[P] = [ids.f1 ?? '', ids.f2 ?? '']
  s.favorites[Q] = [ids.qf ?? '']
  s.windows[W] = { ...defaultWindowState({ id: W, kind: 'main', private: false, activeSpaceId: S }), activeTabId: null }
  const names = (order: string[]) => order.map((id) => Object.entries(ids).find(([, v]) => v === id)?.[0])
  const folder = (id: string) => s.folders[id] ?? { collapsed: false }
  return { s, ids, A, B, names, folder }
}

describe('sidebarOrder (F12)', () => {
  it('expanded: favorites, every pinned tab depth first, then Today; folder rows do not count', () => {
    const { s, names } = fixture()
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 'p1', 'a1', 'b1', 'b2', 'a2', 'p2', 't1', 't2'])
  })

  it('a collapsed folder hides its children', () => {
    const { s, A, B, names, folder } = fixture()
    folder(B).collapsed = true
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 'p1', 'a1', 'a2', 'p2', 't1', 't2'])
    folder(A).collapsed = true
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 'p1', 'p2', 't1', 't2'])
  })

  it('counts the active descendant of a collapsed folder at the folder place, even two levels down', () => {
    const { s, ids, A, B, names, folder } = fixture()
    const w = s.windows[W]
    if (w) w.activeTabId = ids.b2 ?? null
    folder(B).collapsed = true
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 'p1', 'a1', 'b2', 'a2', 'p2', 't1', 't2'])
    folder(A).collapsed = true
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 'p1', 'b2', 'p2', 't1', 't2'])
    // Collapsed outer, expanded inner: still only the active row shows.
    folder(B).collapsed = false
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 'p1', 'b2', 'p2', 't1', 't2'])
  })

  it('a collapsed Pinned section shows only its active tab', () => {
    const { s, ids, names } = fixture()
    const space = s.spaces[S]
    if (space) space.pinnedCollapsed = true
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 't1', 't2'])
    const w = s.windows[W]
    if (w) w.activeTabId = ids.a2 ?? null
    expect(names(sidebarOrder(s, W))).toEqual(['f1', 'f2', 'a2', 't1', 't2'])
  })

  it('follows the window space and its profile favorites; unknown window or junk refs give no rows', () => {
    const { s, names } = fixture()
    const w = s.windows[W]
    if (w) w.activeSpaceId = OTHER
    expect(names(sidebarOrder(s, W))).toEqual(['qf', 'x1'])
    expect(sidebarOrder(s, randomUUID())).toEqual([])
    const space = s.spaces[OTHER]
    if (space) space.pinned = [{ kind: 'folder', id: randomUUID() }, { kind: 'tab', id: randomUUID() }]
    expect(names(sidebarOrder(s, W))).toEqual(['qf', 'x1'])
  })
})

describe('collapsed folder popover', () => {
  const tabId = (n: number) => `00000000-0000-4000-8000-0000000000${String(n).padStart(2, '0')}`
  it('lists the folder tabs depth first, skips missing rows and survives a cycle', () => {
    const s = defaultState()
    for (const n of [1, 2, 3]) s.tabs[tabId(n)] = defaultTabState({ id: tabId(n), profileId: 'p', spaceId: 's', section: 'pinned', url: 'https://a.test/' })
    s.folders.f = { id: 'f', spaceId: 's', name: 'F', icon: null, collapsed: true, children: [{ kind: 'tab', id: tabId(1) }, { kind: 'folder', id: 'g' }, { kind: 'tab', id: tabId(9) }, { kind: 'tab', id: tabId(3) }] }
    s.folders.g = { id: 'g', spaceId: 's', name: 'G', icon: null, collapsed: true, children: [{ kind: 'tab', id: tabId(2) }, { kind: 'folder', id: 'f' }] }
    expect(folderPeekTabs(s, 'f')).toEqual([tabId(1), tabId(2), tabId(3)])
    expect(folderPeekTabs(s, 'missing')).toEqual([])
  })

  it('sits 4 right of the sidebar, centered on the folder row, inside the window margins', () => {
    // The capture: a 36 tall row centered at 369 with one tab gives a popover from 324 to 413.5.
    expect(folderPeekCard({ y: 351, height: 36 }, 214, 800, 1)).toEqual({ x: 218, y: 324.5, width: 213, height: 89 })
    // The view adds the shadow's room above, right and below, none toward the sidebar.
    expect(folderPeekRect({ y: 351, height: 36 }, 214, 800, 1)).toEqual({ x: 218, y: 316.5, width: 229, height: 117 })
    // Near the bottom it stops 25.5 above the window's bottom edge; near the top, 10 below its top.
    const low = folderPeekCard({ y: 700, height: 36 }, 260, 800, 6)
    expect(low.y + low.height).toBe(774.5)
    expect(folderPeekCard({ y: 0, height: 36 }, 260, 800, 6).y).toBe(10)
    // A long folder scrolls inside the window's height; an empty one keeps one row.
    expect(folderPeekCard({ y: 400, height: 36 }, 260, 800, 50).height).toBe(800 - 10 - 25.5)
    expect(folderPeekCard({ y: 400, height: 36 }, 260, 800, 0).height).toBe(FOLDER_PEEK.header + FOLDER_PEEK.inset + FOLDER_PEEK.row + FOLDER_PEEK.bottom)
  })

  it('labels times', () => {
    const now = 1_800_000_000_000
    expect(timeAgo(now - 5_000, now)).toBe('Just now')
    expect(timeAgo(now + 60_000, now)).toBe('Just now')
    expect(timeAgo(now - 60_000, now)).toBe('1 minute ago')
    expect(timeAgo(now - 17 * 3_600_000, now)).toBe('17 hours ago')
    expect(timeAgo(now - 4 * 86_400_000, now)).toBe('4 days ago')
    expect(timeAgo(now - 100 * 86_400_000, now)).toBe('3 months ago')
    expect(timeAgo(now - 800 * 86_400_000, now)).toBe('2 years ago')
    expect(timeAgo(0, now)).toBe('')
    expect(timeAgo(Number.NaN, now)).toBe('')
  })
})

describe('mailCardAnchor', () => {
  it('hangs the first column\'s card from the tile\'s lower right corner and every later column\'s at the same place', () => {
    // The design's first tile: right 68.5, bottom 137, so the card at left 65, top 133.5 once HOVER_CARD's offsets
    // are added back.
    const first = mailCardAnchor({ right: 68.5, bottom: 137 }, 68.5)
    expect(first).toEqual({ x: 68.5 - 3.5 - 11, y: 137 - 3.5 - 12 })
    expect(first.x + HOVER_CARD.offsetX).toBe(65)
    // Second and third columns, and a second row: inside the sidebar at the first column's place, under its own tile.
    expect(mailCardAnchor({ right: 137, bottom: 137 }, 68.5)).toEqual(first)
    expect(mailCardAnchor({ right: 205.5, bottom: 137 }, 68.5)).toEqual(first)
    expect(mailCardAnchor({ right: 137, bottom: 188 }, 68.5)).toEqual({ x: first.x, y: 188 - 3.5 - 12 })
  })

  it('falls back to the tile on a junk first edge', () => {
    for (const junk of [Number.NaN, Number.POSITIVE_INFINITY, 0, -4]) {
      expect(mailCardAnchor({ right: 137, bottom: 137 }, junk)).toEqual({ x: 137 - 3.5 - 11, y: 137 - 3.5 - 12 })
    }
  })
})
