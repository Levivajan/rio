import { describe, expect, it } from 'vitest'
import { defaultProfileState, defaultSpaceState, defaultState, defaultTabState, defaultWindowState } from '../state'
import { displayUrl } from './normalize'
import {
  buildHistoryIndex, buildTabIndex, CONTACT_URL, filterSuggestions, suggestionRows, historyFrecency, inlineCompletion, rank, recencyWeight, tabEntries,
  type HistoryEntry, type RankInput, type TabEntry
} from './rank'

const NOW = Date.UTC(2026, 10, 7)
const DAY = 86_400_000
const S1 = '11111111-1111-4111-8111-111111111111'
const S2 = '22222222-2222-4222-8222-222222222222'
const tid = (n: number) => `00000000-0000-4000-8000-${String(n).padStart(12, '0')}`

const tab = (n: number, url: string, title: string, extra: Partial<TabEntry> = {}): TabEntry => ({
  tabId: tid(n), spaceId: S1, url, title, pinned: false, split: false, lastActiveAt: NOW - n * 1000, ...extra
})
const hist = (url: unknown, title: unknown, extra: Partial<HistoryEntry> = {}): HistoryEntry => ({ url, title, visitCount: 1, typedCount: 0, lastVisitAt: NOW - DAY, ...extra })

function setup(tabs: TabEntry[], history: HistoryEntry[], activeTabId: string | null = null) {
  const t = buildTabIndex(tabs, { activeSpaceId: S1, activeTabId, now: NOW })
  const h = buildHistoryIndex(history, NOW)
  return (text: string, extra: Partial<RankInput> = {}) => rank({ text, mode: 'new-tab', engineId: 'google', tabs: t, history: h, complete: true, ...extra })
}

describe('frecency', () => {
  it('weights recency in steps and typed visits double', () => {
    expect([1, 5, 20, 60, 200].map((d) => recencyWeight(NOW - d * DAY, NOW))).toEqual([1, 0.7, 0.5, 0.3, 0.1])
    expect(historyFrecency(hist('x', 'x', { visitCount: 3, typedCount: 2, lastVisitAt: NOW - 20 * DAY }), NOW)).toBe(3.5)
    expect(historyFrecency(hist('x', 'x', { visitCount: -5, typedCount: Number.NaN, lastVisitAt: 'junk' }), NOW)).toBe(0)
  })
})

describe('rank: top row', () => {
  it("completes 'git' to github.com from a typed github.com history row", () => {
    const r = setup([], [hist('https://github.com/', 'GitHub', { typedCount: 1 }), hist('https://gitlab.com/', 'GitLab', { visitCount: 1 })])('git')
    expect(r.completion).toEqual({ text: 'github.com', url: 'https://github.com/' })
    expect(r.rows[0]?.result).toEqual({ kind: 'history', url: 'https://github.com/', title: 'GitHub' })
    // The completed row is not repeated under History.
    expect(r.rows.filter((x) => x.result.kind === 'history' && x.result.url === 'https://github.com/')).toHaveLength(1)
  })

  it('completes to the host while inside it and to the full URL past it', () => {
    const go = setup([], [hist('https://github.com/anthropics/clu', 'Clu repo', { typedCount: 2 })])
    expect(go('GitH').completion?.text).toBe('GitHub.com')
    expect(go('GitH').rows[0]?.result).toEqual({ kind: 'navigate', url: 'https://github.com/' })
    expect(go('github.com/an').completion).toEqual({ text: 'github.com/anthropics/clu', url: 'https://github.com/anthropics/clu' })
    expect(go('https://www.git').completion?.text).toBe('https://www.github.com')
    expect(go('http://git').completion).toBeNull()
    expect(go('git', { complete: false }).completion).toBeNull()
    expect(go('github.com/anthropics/clu').completion).toBeNull()
    expect(go('git hub').completion).toBeNull()
  })

  it('completes only from typed or often visited history, preferring frecency then the shortest URL', () => {
    const t = buildTabIndex([], { activeSpaceId: S1, activeTabId: null, now: NOW })
    const once = buildHistoryIndex([hist('https://github.com/', 'GitHub', { visitCount: 2 })], NOW)
    expect(inlineCompletion('git', t, once)).toBeNull()
    const often = buildHistoryIndex([hist('https://github.com/', 'GitHub', { visitCount: 3 })], NOW)
    expect(inlineCompletion('git', t, often)?.text).toBe('github.com')
    const two = buildHistoryIndex([
      hist('https://gist.github.com/', 'Gist', { typedCount: 5 }),
      hist('https://github.com/', 'GitHub', { typedCount: 1 })
    ], NOW)
    expect(inlineCompletion('gi', t, two)?.text).toBe('gist.github.com')
    const tie = buildHistoryIndex([hist('https://docs.example/deep/path', 'Deep', { typedCount: 1 }), hist('https://docs.example/a', 'A', { typedCount: 1 })], NOW)
    expect(inlineCompletion('docs.example/', t, tie)?.text).toBe('docs.example/a')
  })

  it('completes from an open tab and switches to it', () => {
    const r = setup([tab(1, 'http://localhost:5173/app', 'Dev server')], [])('localh')
    expect(r.completion?.text).toBe('localhost:5173')
    expect(r.rows[0]?.result).toEqual({ kind: 'navigate', url: 'http://localhost:5173/' })
    const full = setup([tab(1, 'http://localhost:5173/app', 'Dev server')], [])('localhost:5173/a')
    expect(full.rows[0]?.badge).toBe('Switch to tab')
    expect(full.rows[0]?.result).toMatchObject({ kind: 'tab', tabId: tid(1) })
  })

  it("puts an open Gmail tab in the top row for 'gmail'", () => {
    const r = setup([tab(1, 'https://mail.google.com/mail/u/0/#inbox', 'Inbox (3) - you@example.com - Gmail'), tab(2, 'https://news.example/', 'News')], [])('gmail')
    expect(r.rows[0]?.group).toBe('top')
    expect(r.rows[0]?.result).toMatchObject({ kind: 'tab', tabId: tid(1), spaceId: S1, pinned: false })
    expect(r.rows[0]?.badge).toBe('Switch to tab')
    expect(r.rows[0]?.titleRanges.length).toBeGreaterThan(0)
    // The top tab is not repeated under Tabs.
    expect(r.rows.filter((x) => x.key === `tab:${tid(1)}`)).toHaveLength(1)
  })

  it("gives a search top row for 'gm' with only a loose tab match, and lists the tab under Tabs", () => {
    const r = setup([tab(1, 'https://docs.example/p', 'Programming guide')], [])('gm')
    expect(r.rows[0]?.result).toEqual({ kind: 'search', engineId: 'google', query: 'gm' })
    expect(r.rows[0]?.title).toBe('gm')
    expect(r.rows[1]).toMatchObject({ group: 'tabs', badge: 'Switch to tab' })
    // A loose match on a longer query never takes the top row either.
    expect(setup([tab(1, 'https://docs.example/p', 'Programming guide')], [])('pgmg').rows[0]?.result.kind).toBe('search')
  })

  it("switches to an open tab for its URL with 'Switch to tab' and no duplicate history row", () => {
    const go = setup([tab(1, 'https://example.com/page', 'Example page')], [hist('https://example.com/page#top', 'Example page', { visitCount: 9 }), hist('https://example.com/other', 'Other example')])
    const r = go('example.com/page')
    expect(r.rows[0]).toMatchObject({ group: 'top', badge: 'Switch to tab', result: { kind: 'tab', tabId: tid(1) } })
    expect(r.rows.filter((x) => x.result.kind === 'history' && x.result.url.startsWith('https://example.com/page'))).toEqual([])
    expect(r.rows.filter((x) => x.key === `tab:${tid(1)}`)).toHaveLength(1)
    expect(go('https://example.com/page', { complete: false }).rows[0]?.result.kind).toBe('tab')
  })

  it("labels a tab in a split 'Switch to split'", () => {
    const r = setup([tab(1, 'https://example.com/', 'Example', { split: true })], [])('https://example.com')
    expect(r.rows[0]?.badge).toBe('Switch to split')
  })

  it('navigates a typed URL that no tab has open, and searches text', () => {
    const go = setup([], [])
    expect(go('example.com').rows[0]).toMatchObject({ result: { kind: 'navigate', url: 'https://example.com/' }, title: 'Open https://example.com/' })
    expect(go('foo bar').rows[0]?.result).toEqual({ kind: 'search', engineId: 'google', query: 'foo bar' })
    expect(go('foo', { engineId: 'duckduckgo' }).rows[0]).toMatchObject({ title: 'foo', result: { kind: 'search', engineId: 'duckduckgo', query: 'foo' } })
    expect(go('foo', { engineId: 'nonsense' }).rows[0]?.result).toMatchObject({ engineId: 'google' })
    expect(go('?').rows).toEqual([])
  })

  it('reloads in edit-url mode while the text is unchanged: the tab row, the 3 most frecent pages, Contact the Team', () => {
    const go = setup([tab(1, 'https://example.com/a', 'Example A')], [
      hist('https://example.com/a', 'Example A', { visitCount: 50 }),
      hist('https://meet.example/landing', 'Meet', { visitCount: 9 }),
      hist('https://www.google.com/search?q=rio', 'rio - Google Search', { visitCount: 7 }),
      hist('https://app.example/', 'App', { visitCount: 5 }),
      hist('https://rare.example/', 'Rare', { visitCount: 1 }),
      hist('junk', null)
    ])
    const r = go('https://example.com/a', { mode: 'edit-url', targetTabId: tid(1), currentUrl: 'https://example.com/a', currentTitle: 'Example A' })
    expect(r.rows[0]).toMatchObject({ group: 'top', result: { kind: 'reload', tabId: tid(1) }, title: 'Example A', url: '' })
    expect(r.rows.slice(1).map((x) => x.title)).toEqual(['Meet', 'rio', 'App', 'Contact the Team'])
    expect(r.rows[1]?.url).toBe('meet.example/landing')
    expect(go('https://example.com/a', { mode: 'edit-url', targetTabId: tid(1), currentUrl: 'https://example.com/a' }).rows[0]?.title).toBe(displayUrl('https://example.com/a'))
    const edited = go('https://example.com/b', { mode: 'edit-url', targetTabId: tid(1), currentUrl: 'https://example.com/a' })
    expect(edited.rows[0]?.result).toEqual({ kind: 'navigate', url: 'https://example.com/b' })
  })

  it('never offers the edited tab itself in edit-url mode: a new fragment or a dropped scheme navigates it', () => {
    const go = setup([tab(1, 'https://example.com/a', 'Example A'), tab(2, 'https://other.test/', 'Other page')], [], tid(1))
    const edit = { mode: 'edit-url' as const, targetTabId: tid(1), currentUrl: 'https://example.com/a' }
    expect(go('https://example.com/a#part', edit).rows[0]?.result).toEqual({ kind: 'navigate', url: 'https://example.com/a#part' })
    expect(go('example.com/a', edit).rows[0]?.result).toEqual({ kind: 'navigate', url: 'https://example.com/a' })
    const titled = go('Example', { ...edit, complete: false })
    expect(titled.rows[0]?.result).toMatchObject({ kind: 'search' })
    expect(titled.rows.filter((r) => r.result.kind === 'tab')).toEqual([])
    // In new-tab mode the same URL still switches to the open tab.
    expect(go('example.com/a').rows[0]?.result).toMatchObject({ kind: 'tab', tabId: tid(1) })
  })
})

describe('rank: groups', () => {
  it('caps Tabs and History at 4, boosts the active space and pinned tabs', () => {
    const tabs = Array.from({ length: 8 }, (_, i) => tab(i + 1, `https://t${i}.example/`, `Report ${i}`, { spaceId: i === 7 ? S1 : S2, pinned: i === 6 }))
    const history = Array.from({ length: 8 }, (_, i) => hist(`https://h${i}.example/`, `Report archive ${i}`))
    const r = setup(tabs, history)('report')
    const groups = r.rows.map((x) => x.group)
    expect(groups.filter((g) => g === 'tabs')).toHaveLength(4)
    expect(groups.filter((g) => g === 'history')).toHaveLength(4)
    // The active-space tab is a strong match and takes the top row, the pinned tab leads the Tabs group.
    expect(r.rows[0]?.result).toMatchObject({ kind: 'tab', tabId: tid(8) })
    // The plain search for what was typed comes second, then the Tabs group.
    expect(r.rows[1]?.result).toEqual({ kind: 'search', engineId: 'google', query: 'report' })
    expect(r.rows[2]?.result).toMatchObject({ kind: 'tab', tabId: tid(7), pinned: true })
    expect(new Set(r.rows.map((x) => x.key)).size).toBe(r.rows.length)
  })

  it('shows past searches as search rows and drops one equal to the typed query', () => {
    const go = setup([], [
      hist('https://www.google.com/search?q=rust+traits', 'rust traits - Google Search'),
      hist('https://duckduckgo.com/?q=rust', 'rust at DuckDuckGo'),
      hist('https://www.rust-lang.org/', 'Rust Programming Language')
    ])
    const r = go('rust')
    const past = r.rows.find((x) => x.historyUrl === 'https://www.google.com/search?q=rust+traits')
    expect(past).toMatchObject({ group: 'history', title: 'rust traits', result: { kind: 'search', engineId: 'google', query: 'rust traits' } })
    expect(r.rows.filter((x) => x.result.kind === 'search' && x.result.query === 'rust')).toHaveLength(1)
    expect(r.rows.find((x) => x.historyUrl === 'https://www.rust-lang.org/')?.result.kind).toBe('history')
  })

  it('skips junk history rows and still ranks the rest', () => {
    const history = buildHistoryIndex([hist(null, 'null'), hist('ftp://f.example/x', 'ftp file'), hist('https://ok.example/file', 'File ok'), null as unknown as HistoryEntry], NOW)
    expect(history.index.skipped).toBe(3)
    const r = rank({ text: 'file', mode: 'new-tab', engineId: 'google', tabs: buildTabIndex([], { activeSpaceId: S1, activeTabId: null }), history })
    expect(r.rows.filter((x) => x.group === 'history').map((x) => x.url)).toEqual(['ok.example/file'])
  })

  it('returns the same rows when narrowing from the previous state', () => {
    const t = buildTabIndex([tab(1, 'https://mail.google.com/', 'Gmail')], { activeSpaceId: S1, activeTabId: null, now: NOW })
    const h = buildHistoryIndex([hist('https://maps.example/', 'Maps'), hist('https://mail.example/', 'Mailbox')], NOW)
    const run = (text: string, prev: Parameters<typeof rank>[1] = null) => rank({ text, mode: 'new-tab', engineId: 'google', tabs: t, history: h }, prev)
    const a = run('ma')
    expect(a.rows.length).toBeGreaterThan(1)
    expect(run('mai', a.state).rows).toEqual(run('mai').rows)
  })
})

describe('rank: empty input', () => {
  it('lists favorites and the space tabs, most recent first, without the active one, then Contact the Team', () => {
    const tabs = [
      ...Array.from({ length: 7 }, (_, i) => tab(i + 1, `https://r${i}.example/`, `Recent ${i}`, { lastActiveAt: NOW - (i + 1) * 1000 })),
      tab(20, 'https://other.example/', 'Other space', { spaceId: S2, lastActiveAt: NOW }),
      tab(21, 'https://fav.example/', 'Favorite', { spaceId: null, pinned: true, lastActiveAt: NOW - 2500 })
    ]
    const r = setup(tabs, [], tid(1))('   ')
    expect(r.rows.map((x) => x.group)).toEqual([...Array(4).fill('recent'), 'help'])
    expect(r.rows.map((x) => (x.result.kind === 'tab' ? x.result.tabId : ''))).toEqual([tid(2), tid(21), tid(3), tid(4), ''])
    expect(r.rows[4]).toMatchObject({ title: 'Contact the Team', result: { kind: 'navigate', url: CONTACT_URL } })
    expect(r.completion).toBeNull()
    expect(setup(tabs, [], tid(1))('', { mode: 'edit-url' }).rows).toEqual([])
  })

  it('lists only favorites when the space has no other tab', () => {
    const tabs = [
      tab(1, 'https://active.example/', 'Active', { lastActiveAt: NOW }),
      tab(2, 'https://mail.example/', 'Mail', { spaceId: null, pinned: true, lastActiveAt: NOW - 1000 }),
      tab(3, 'https://meet.example/', 'Meet', { spaceId: null, pinned: true, lastActiveAt: 0 })
    ]
    const r = setup(tabs, [], tid(1))('')
    expect(r.rows.map((x) => x.title)).toEqual(['Mail', 'Meet', 'Contact the Team'])
    expect(r.rows[0]?.badge).toBe('Switch to tab')
  })
})

describe('tabEntries', () => {
  it("takes every tab of the window's profile, with titles, pin and split flags", () => {
    const s = defaultState()
    const P1 = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
    const P2 = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb'
    s.profiles[P1] = defaultProfileState({ id: P1, name: 'One', isDefault: true, ephemeral: false })
    s.spaces[S1] = { ...defaultSpaceState({ id: S1, name: 'Home', icon: { kind: 'letter' }, color: 'blue', profileId: P1, ephemeral: false }), today: [tid(1)] }
    s.spaces[S2] = defaultSpaceState({ id: S2, name: 'Work', icon: { kind: 'letter' }, color: 'blue', profileId: P2, ephemeral: false })
    const W = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'
    s.windows[W] = { ...defaultWindowState({ id: W, kind: 'main', private: false, activeSpaceId: S1 }), activeTabId: tid(1) }
    s.tabs[tid(1)] = { ...defaultTabState({ id: tid(1), profileId: P1, spaceId: S1, section: 'today', url: 'https://a.example/', title: 'A' }), titleOverride: 'Renamed' }
    s.tabs[tid(2)] = { ...defaultTabState({ id: tid(2), profileId: P1, spaceId: null, section: 'favorite', url: 'https://f.example/', title: 'F' }), splitId: 'x' }
    s.tabs[tid(3)] = defaultTabState({ id: tid(3), profileId: P2, spaceId: S2, section: 'today', url: 'https://other.example/', title: 'Other' })
    const { entries, activeSpaceId, activeTabId } = tabEntries(s, W)
    expect(activeSpaceId).toBe(S1)
    expect(activeTabId).toBe(tid(1))
    expect(entries.map((e) => [e.title, e.pinned, e.split]).sort()).toEqual([['F', true, true], ['Renamed', false, false]])
    expect(tabEntries(s, 'missing').entries).toEqual([])
  })

  it('leaves out Today tabs a Clear took off the list while Undo can still bring them back', () => {
    const s = defaultState()
    const P1 = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
    const W = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'
    s.profiles[P1] = defaultProfileState({ id: P1, name: 'One', isDefault: true, ephemeral: false })
    const home = defaultSpaceState({ id: S1, name: 'Home', icon: { kind: 'letter' }, color: 'blue', profileId: P1, ephemeral: false })
    s.spaces[S1] = home
    s.windows[W] = { ...defaultWindowState({ id: W, kind: 'main', private: false, activeSpaceId: S1 }), activeTabId: tid(1) }
    for (const n of [1, 2, 3]) {
      s.tabs[tid(n)] = { ...defaultTabState({ id: tid(n), profileId: P1, spaceId: S1, section: 'today', url: `https://${n}.example/`, title: `T${n}` }), lastActiveAt: NOW - n }
    }
    s.tabs[tid(4)] = defaultTabState({ id: tid(4), profileId: P1, spaceId: S1, section: 'pinned', url: 'https://pin.example/', title: 'Pin' })
    // Tabs 2 and 3 were cleared; the state still has them until the Clear commits.
    home.today = [tid(1)]
    const { entries, activeSpaceId, activeTabId } = tabEntries(s, W)
    expect(entries.map((e) => e.title).sort()).toEqual(['Pin', 'T1'])
    const ranked = rank({
      text: '',
      mode: 'new-tab',
      engineId: 'google',
      tabs: buildTabIndex(entries, { activeSpaceId, activeTabId, now: NOW }),
      history: buildHistoryIndex([], NOW)
    })
    expect(ranked.rows.map((r) => r.title)).toEqual(['Pin', 'Contact the Team'])
    // Undo puts them back on the list, and the bar offers them again.
    home.today = [tid(1), tid(2), tid(3)]
    expect(tabEntries(s, W).entries.map((e) => e.title).sort()).toEqual(['Pin', 'T1', 'T2', 'T3'])
  })
})

describe('filterSuggestions', () => {
  it('drops the typed query, search rows already listed, blanks and repeats', () => {
    const r = setup([], [hist('https://www.google.com/search?q=rust+book', 'rust book - Google Search')])('rust')
    expect(filterSuggestions(['rust', ' Rust Book ', 'rust lang', 'RUST LANG', '  ', 'rustup'], 'rust', r.rows)).toEqual(['rust lang', 'rustup'])
  })

  it('suggestionRows gives at most 4 search rows for the engine under Suggestions', () => {
    const r = setup([], [])('rust')
    const rows = suggestionRows(['rust', 'rust lang', 'rustup', 'rust book', 'rust analyzer', 'rust traits'], 'rust', r.rows, 'duckduckgo')
    expect(rows.map((x) => [x.group, x.title, x.key])).toEqual([
      ['suggestions', 'rust lang', 'suggest:rust lang'],
      ['suggestions', 'rustup', 'suggest:rustup'],
      ['suggestions', 'rust book', 'suggest:rust book'],
      ['suggestions', 'rust analyzer', 'suggest:rust analyzer']
    ])
    expect(rows[0]?.result).toEqual({ kind: 'search', engineId: 'duckduckgo', query: 'rust lang' })
    expect(suggestionRows(['x'], 'y', [], 'junk')[0]?.result).toEqual({ kind: 'search', engineId: 'google', query: 'x' })
  })
})
