// Command bar ranking (04-command F4 to F6, F11, F14, F15). Pure. Turns the input, the tab index and the history index
// into the top row (what plain Enter does), the Tabs and History groups, the inline completion and, for an empty
// new-tab input, the recent tabs. Actions and engine suggestions join the list in the overlay.
import type { CluState } from '../state'
import type { CommandResult } from '../types'
import { normalizeForDedupe } from '../url'
import { ENGINES, engineById, MAX_SEARCH_QUERY } from '../search/engines'
import { classifyInput, cleanInput } from './classify'
import { buildIndex, matchIndex, SCORE_WORD, type CandidateIndex, type Hit, type MatchState, type Range } from './match'
import { displayUrl } from './normalize'
import type { CommandMode } from '../ipc/command'

export const MAX_TABS = 4
export const MAX_HISTORY = 4
/** History rows under the current tab in an unchanged Cmd+L (the tab, three history rows, Contact the Team). */
export const MAX_EDIT_HISTORY = 3
/** Tabs an empty Cmd+T lists; the help row makes the fifth (four tabs, then Contact the Team). */
export const MAX_RECENT = 4
/** Where the help row's "Contact the Team" goes: Rio's issue tracker. */
export const CONTACT_URL = 'https://github.com/Levivajan/rio-releases/issues/new'
const DAY = 86_400_000

/** One open, pinned or favorite tab of the window's profile. */
export interface TabEntry {
  tabId: string
  spaceId: string | null
  url: string
  title: string
  pinned: boolean
  split: boolean
  lastActiveAt: number
}

/** One history row as the index sync delivers it. Fields may be junk. */
export interface HistoryEntry {
  url: unknown
  title: unknown
  visitCount?: unknown
  typedCount?: unknown
  lastVisitAt?: unknown
}

export interface TabIndex {
  index: CandidateIndex
  /** Tab entry of each candidate. */
  entries: TabEntry[]
  byNorm: Map<string, number>
  /** Lowercase URL text of each candidate, for inline completion. */
  completeText: string[]
  activeSpaceId: string | null
  activeTabId: string | null
}

export interface HistoryIndex {
  index: CandidateIndex
  /** Raw frecency of each candidate. */
  frecency: Float64Array
  byNorm: Map<string, number>
  /** Candidates that may complete inline (typedCount >= 1 or visitCount >= 3) and their lowercase URL text. */
  completable: Int32Array
  completeText: string[]
}

export type RowGroup = 'top' | 'recent' | 'tabs' | 'history' | 'suggestions' | 'help'

export interface RankedRow {
  /** Stable across keystrokes, so the selection can stay on the same result. */
  key: string
  group: RowGroup
  result: CommandResult
  title: string
  /** URL for display ('' when the row has none). */
  url: string
  titleRanges: Range[]
  urlRanges: Range[]
  badge?: 'Switch to tab' | 'Switch to split'
  /** History URL this row came from, for delete (F6). */
  historyUrl?: string
}

export interface Completion {
  /** The whole input after completing: what was typed plus the selected suffix. */
  text: string
  url: string
}

export interface RankState { tabs: MatchState | null; history: MatchState | null }

export interface RankInput {
  text: string
  mode: CommandMode
  engineId: string
  tabs: TabIndex
  history: HistoryIndex
  /** edit-url mode: the target tab and its URL. */
  targetTabId?: string | null
  currentUrl?: string | null
  /** edit-url mode: the target tab's title, for its row. */
  currentTitle?: string | null
  /** The last edit inserted text at the end of the input outside IME composition (F11). */
  complete?: boolean
  /** Engine with the e2e origin override, for labels only. */
  engineName?: string
}

export interface RankOutput {
  rows: RankedRow[]
  completion: Completion | null
  state: RankState
}

/** 1.0 under 4 days, 0.7 under 14, 0.5 under 31, 0.3 under 90, else 0.1. */
export function recencyWeight(at: number, now: number): number {
  const age = (now - at) / DAY
  if (age < 4) return 1
  if (age < 14) return 0.7
  if (age < 31) return 0.5
  if (age < 90) return 0.3
  return 0.1
}

const count = (n: unknown) => (typeof n === 'number' && Number.isFinite(n) && n > 0 ? n : 0)

/** (visitCount + 2 * typedCount) * recency. */
export function historyFrecency(row: HistoryEntry, now: number): number {
  return (count(row.visitCount) + 2 * count(row.typedCount)) * recencyWeight(count(row.lastVisitAt), now)
}

export function buildHistoryIndex(rows: readonly HistoryEntry[], now = Date.now()): HistoryIndex {
  const index = buildIndex(rows.map((r) => ({ url: r?.url, title: r?.title, frecency: r ? historyFrecency(r, now) : 0, lastVisitAt: count(r?.lastVisitAt) })))
  const frecency = new Float64Array(index.size)
  const byNorm = new Map<string, number>()
  const completable: number[] = []
  const completeText: string[] = []
  for (let i = 0; i < index.size; i++) {
    const row = rows[index.source[i] ?? 0]
    if (!row) continue
    frecency[i] = historyFrecency(row, now)
    const norm = index.norm[i] ?? ''
    const seen = byNorm.get(norm)
    if (seen === undefined || (frecency[seen] ?? 0) < (frecency[i] ?? 0)) byNorm.set(norm, i)
    if (count(row.typedCount) >= 1 || count(row.visitCount) >= 3) {
      completable.push(i)
      completeText.push((index.urlText[i] ?? '').toLowerCase())
    }
  }
  return { index, frecency, byNorm, completable: Int32Array.from(completable), completeText }
}

/** Tab frecency is its recency; the active space gets +10 and pinned tabs +5. */
export function buildTabIndex(entries: readonly TabEntry[], ctx: { activeSpaceId: string | null; activeTabId: string | null; now?: number }): TabIndex {
  const now = ctx.now ?? Date.now()
  const index = buildIndex(
    entries.map((t) => ({
      url: t.url,
      title: t.title,
      frecency: recencyWeight(count(t.lastActiveAt), now),
      lastVisitAt: count(t.lastActiveAt),
      boost: (t.spaceId !== null && t.spaceId === ctx.activeSpaceId ? 10 : 0) + (t.pinned ? 5 : 0)
    }))
  )
  const kept: TabEntry[] = []
  const byNorm = new Map<string, number>()
  for (let i = 0; i < index.size; i++) {
    const entry = entries[index.source[i] ?? 0]
    if (!entry) continue
    kept.push(entry)
    const norm = index.norm[i] ?? ''
    const seen = byNorm.get(norm)
    // The same URL open twice: prefer the active space's tab, then the most recently active.
    const better = seen === undefined || (index.boost[i] ?? 0) > (index.boost[seen] ?? 0) ||
      ((index.boost[i] ?? 0) === (index.boost[seen] ?? 0) && (index.lastVisitAt[i] ?? 0) > (index.lastVisitAt[seen] ?? 0))
    if (better) byNorm.set(norm, i)
  }
  const completeText = index.urlText.map((u) => u.toLowerCase())
  return { index, entries: kept, byNorm, completeText, activeSpaceId: ctx.activeSpaceId, activeTabId: ctx.activeTabId }
}

/**
 * Tabs the bar offers for a window: every tab in the profile of its active space (Today, pinned, favorites). A Today
 * tab its space no longer lists is left out: a Clear that can still be undone takes its tabs off the list first.
 */
export function tabEntries(state: Pick<CluState, 'tabs' | 'spaces' | 'windows'>, windowId: string): { entries: TabEntry[]; activeSpaceId: string | null; activeTabId: string | null } {
  const w = state.windows[windowId]
  const activeSpaceId = w?.activeSpaceId ?? null
  const profileId = activeSpaceId ? state.spaces[activeSpaceId]?.profileId : undefined
  const entries: TabEntry[] = []
  const today = new Map<string, Set<string>>()
  const listedToday = (spaceId: string, tabId: string) => {
    let ids = today.get(spaceId)
    if (!ids) today.set(spaceId, (ids = new Set(state.spaces[spaceId]?.today ?? [])))
    return ids.has(tabId)
  }
  if (profileId !== undefined) {
    for (const t of Object.values(state.tabs)) {
      if (!t || t.profileId !== profileId || typeof t.url !== 'string') continue
      if (t.section === 'today' && t.spaceId !== null && !listedToday(t.spaceId, t.id)) continue
      entries.push({
        tabId: t.id,
        spaceId: t.spaceId,
        url: t.url,
        title: (t.titleOverride ?? t.title) || '',
        pinned: t.section !== 'today',
        split: t.splitId != null,
        lastActiveAt: count(t.lastActiveAt)
      })
    }
  }
  return { entries, activeSpaceId, activeTabId: w?.activeTabId ?? null }
}

const NO_RANGES: Range[] = []

function tabRow(tabs: TabIndex, i: number, group: RowGroup, hit?: Hit): RankedRow | null {
  const e = tabs.entries[i]
  const spaceId = e?.spaceId ?? tabs.activeSpaceId
  if (!e || !spaceId) return null
  const url = tabs.index.url[i] ?? e.url
  const title = tabs.index.title[i] || tabs.index.urlText[i] || url
  return {
    key: `tab:${e.tabId}`,
    group,
    result: { kind: 'tab', tabId: e.tabId, spaceId, pinned: e.pinned, url, title },
    title,
    url: tabs.index.urlText[i] ?? '',
    titleRanges: tabs.index.title[i] && hit ? hit.titleRanges : NO_RANGES,
    urlRanges: hit?.urlRanges ?? NO_RANGES,
    badge: e.split ? 'Switch to split' : 'Switch to tab'
  }
}

// The row reads the query alone.
function searchRow(engineId: string, query: string, group: RowGroup, title?: string): RankedRow {
  const q = query.slice(0, MAX_SEARCH_QUERY)
  return {
    key: `search:${q}`,
    group,
    result: { kind: 'search', engineId, query: q },
    title: title ?? q,
    url: '',
    titleRanges: NO_RANGES,
    urlRanges: NO_RANGES
  }
}

function historyRow(history: HistoryIndex, i: number, group: RowGroup, engineId: string, engineName: string, hit?: Hit): RankedRow {
  const url = history.index.url[i] ?? ''
  for (const engine of ENGINES) {
    const q = engine.matchesSearchUrl(url)
    if (q !== null) return { ...searchRow(engineId, q, group, q), historyUrl: url }
  }
  const title = history.index.title[i] || history.index.urlText[i] || url
  return {
    key: `history:${history.index.norm[i] ?? url}`,
    group,
    result: { kind: 'history', url, title },
    title,
    url: history.index.urlText[i] ?? '',
    titleRanges: history.index.title[i] && hit ? hit.titleRanges : NO_RANGES,
    urlRanges: hit?.urlRanges ?? NO_RANGES,
    historyUrl: url
  }
}

/** The last row of an empty Cmd+T and of Cmd+L. */
export const contactRow = (): RankedRow => ({ key: 'help:contact', group: 'help', result: { kind: 'navigate', url: CONTACT_URL }, title: 'Contact the Team', url: '', titleRanges: NO_RANGES, urlRanges: NO_RANGES })

const SCHEME_WWW = /^(https?:\/\/)?(www\.)?/i

/** Inline completion (F11): the highest-frecency open tab or completable history row whose URL text starts with what
 *  was typed. The target is the host while the typed text is inside the host, else the full URL. Shortest
 *  target wins a frecency tie. */
export function inlineCompletion(typed: string, tabs: TabIndex, history: HistoryIndex): Completion | null {
  if (!typed || /\s/.test(typed)) return null
  const prefix = SCHEME_WWW.exec(typed)?.[0] ?? ''
  const core = typed.slice(prefix.length).toLowerCase()
  if (!core) return null
  const scheme = /^https?:/i.exec(prefix)?.[0].toLowerCase() ?? null
  let best: { target: string; index: CandidateIndex; i: number; frecency: number } | null = null
  const consider = (index: CandidateIndex, i: number, text: string, frecency: number) => {
    if (text.length <= core.length || !text.startsWith(core)) return
    const url = index.url[i] ?? ''
    if (scheme !== null && !url.startsWith(scheme)) return
    const hostEnd = text.search(/[/?#]/)
    // Completes the host alone ("weather" to "weather.gov"), with no trailing slash.
    const target = hostEnd < 0 || core.length <= hostEnd ? (hostEnd < 0 ? text : text.slice(0, hostEnd)) : text
    if (target.length <= core.length) return
    if (!best || frecency > best.frecency || (frecency === best.frecency && target.length < best.target.length)) best = { target, index, i, frecency }
  }
  for (let k = 0; k < history.completable.length; k++) {
    const i = history.completable[k] ?? 0
    consider(history.index, i, history.completeText[k] ?? '', history.frecency[i] ?? 0)
  }
  for (let i = 0; i < tabs.index.size; i++) {
    const h = history.byNorm.get(tabs.index.norm[i] ?? '')
    consider(tabs.index, i, tabs.completeText[i] ?? '', h === undefined ? 0 : (history.frecency[h] ?? 0))
  }
  if (!best) return null
  const { target, index, i } = best as { target: string; index: CandidateIndex; i: number }
  const href = index.url[i] ?? ''
  const full = target === (index.urlText[i] ?? '').toLowerCase()
  let url = href
  if (!full) {
    try {
      url = `${new URL(href).origin}/`
    } catch {
      return null
    }
  }
  return { text: typed + (full ? (index.urlText[i] ?? '') : target).slice(core.length), url }
}

/** The top row for a URL: switch to the tab that has it open, else the history row, else navigate. */
function urlRow(url: string, tabs: TabIndex, history: HistoryIndex, engineId: string, engineName: string, viaHistory: boolean, skipTabId: string | null): RankedRow {
  const norm = normalizeForDedupe(url)
  const t = tabs.byNorm.get(norm)
  const tab = t === undefined || tabs.entries[t]?.tabId === skipTabId ? null : tabRow(tabs, t, 'top')
  if (tab) return tab
  const h = viaHistory ? history.byNorm.get(norm) : undefined
  if (h !== undefined) return historyRow(history, h, 'top', engineId, engineName)
  return { key: `navigate:${url}`, group: 'top', result: { kind: 'navigate', url }, title: `Open ${displayUrl(url)}`, url: '', titleRanges: NO_RANGES, urlRanges: NO_RANGES }
}

/** The MAX_EDIT_HISTORY most frecent history rows, one per URL, without `skipNorm` (open pages are listed here too). */
function topHistory(history: HistoryIndex, skipNorm: string, engineId: string, engineName: string): RankedRow[] {
  const best: number[] = []
  for (const i of history.byNorm.values()) {
    const norm = history.index.norm[i] ?? ''
    if (norm === skipNorm || !((history.frecency[i] ?? 0) > 0)) continue
    best.push(i)
    best.sort((a, b) => (history.frecency[b] ?? 0) - (history.frecency[a] ?? 0))
    if (best.length > MAX_EDIT_HISTORY) best.pop()
  }
  return best.map((i) => historyRow(history, i, 'history', engineId, engineName))
}

/** Rows for the input, best first: the top row, then Tabs and History (or Recent tabs for an empty new-tab input). */
export function rank(input: RankInput, prev: RankState | null = null): RankOutput {
  const { tabs, history, mode } = input
  const engine = engineById(input.engineId)
  const engineId = engine.id
  const engineName = input.engineName ?? engine.name
  const text = cleanInput(input.text)
  const trimmed = text.trim()
  const none: RankOutput = { rows: [], completion: null, state: { tabs: null, history: null } }

  if (!trimmed) {
    if (mode !== 'new-tab') return none
    // Favorites (no space) and the active space's pinned and Today tabs, most recently used first.
    const recent = tabs.entries
      .map((e, i) => ({ e, i }))
      .filter(({ e }) => (e.spaceId === null || e.spaceId === tabs.activeSpaceId) && e.tabId !== tabs.activeTabId)
      .sort((a, b) => b.e.lastActiveAt - a.e.lastActiveAt)
      .slice(0, MAX_RECENT)
      .flatMap(({ i }) => tabRow(tabs, i, 'recent') ?? [])
    return { ...none, rows: [...recent, contactRow()] }
  }

  // F2: edit-url mode with the text unchanged reloads. The row reads as the current tab (title only, its favicon);
  // the most frecent history and Contact the Team follow.
  const current = input.currentUrl ?? null
  if (mode === 'edit-url' && input.targetTabId && current && (trimmed === current || trimmed === displayUrl(current))) {
    const title = input.currentTitle?.trim() || displayUrl(current)
    const row: RankedRow = { key: `reload:${input.targetTabId}`, group: 'top', result: { kind: 'reload', tabId: input.targetTabId }, title, url: '', titleRanges: NO_RANGES, urlRanges: NO_RANGES }
    return { ...none, rows: [row, ...topHistory(history, normalizeForDedupe(current), engineId, engineName), contactRow()] }
  }

  const query = trimmed.replace(/^\?/, '')
  const tabMatch = matchIndex(tabs.index, query, 16, prev?.tabs ?? null)
  const historyMatch = matchIndex(history.index, query, 16, prev?.history ?? null)
  const completion = input.complete ? inlineCompletion(text, tabs, history) : null

  // Top row, first match wins. An inline completion becomes the input Enter submits, so it goes first.
  const c = classifyInput(text)
  // Edit-url mode never offers the edited tab itself: switching to it would do nothing, so its URL navigates instead.
  const skipTabId = mode === 'edit-url' ? (input.targetTabId ?? null) : null
  const offered = (i: number) => skipTabId === null || tabs.entries[i]?.tabId !== skipTabId
  let top: RankedRow | null = null
  if (completion) top = urlRow(completion.url, tabs, history, engineId, engineName, true, skipTabId)
  else if (c.kind === 'url') top = urlRow(c.url, tabs, history, engineId, engineName, false, skipTabId)
  else if (trimmed.length >= 3) {
    // Only a strong word-prefix match on every token switches to a tab; loose hits never take the top row.
    const strong = tabMatch.hits.find((h) => h.strong && h.match >= SCORE_WORD && offered(h.i))
    if (strong) top = tabRow(tabs, strong.i, 'top', strong)
  }
  if (!top && c.kind === 'search') top = searchRow(engineId, c.query, 'top')

  const rows: RankedRow[] = top ? [top] : []
  // After a tab, history or URL top row, the plain search for what was typed comes second.
  if (top && top.result.kind !== 'search' && c.kind === 'search') rows.push(searchRow(engineId, c.query, 'top'))
  const seen = new Set(rows.map((r) => r.key))
  const topNorm = top && (top.result.kind === 'navigate' || top.result.kind === 'history' || top.result.kind === 'tab') ? normalizeForDedupe(top.result.url) : null

  let added = 0
  for (const h of tabMatch.hits) {
    if (added >= MAX_TABS) break
    if (!offered(h.i)) continue
    const row = tabRow(tabs, h.i, 'tabs', h)
    if (!row || seen.has(row.key)) continue
    seen.add(row.key)
    rows.push(row)
    added++
  }

  // History: tab beats history for the same URL; a past search equal to the typed query or another row is dropped.
  const typedQuery = c.kind === 'search' ? c.query.toLowerCase() : null
  added = 0
  for (const h of historyMatch.hits) {
    if (added >= MAX_HISTORY) break
    const norm = history.index.norm[h.i] ?? ''
    if (tabs.byNorm.has(norm) || norm === topNorm) continue
    const row = historyRow(history, h.i, 'history', engineId, engineName, h)
    if (seen.has(row.key) || (row.result.kind === 'search' && row.result.query.toLowerCase() === typedQuery)) continue
    seen.add(row.key)
    rows.push(row)
    added++
  }

  return { rows, completion, state: { tabs: tabMatch.state, history: historyMatch.state } }
}

/** Engine suggestions worth showing: not the typed query and not a search row already in the list. */
export function filterSuggestions(items: readonly string[], text: string, rows: readonly RankedRow[]): string[] {
  const taken = new Set(rows.flatMap((r) => (r.result.kind === 'search' ? [r.result.query.toLowerCase()] : [])))
  taken.add(cleanInput(text).trim().replace(/^\?/, '').toLowerCase())
  const out: string[] = []
  for (const item of items) {
    const key = item.trim().toLowerCase()
    if (!key || taken.has(key)) continue
    taken.add(key)
    out.push(item.trim())
  }
  return out
}

/** Engine suggestions shown under the ranked rows (04-command "Result kinds"). */
export const MAX_SUGGESTION_ROWS = 4

/** Search rows for the engine's suggestions, after filterSuggestions, at most MAX_SUGGESTION_ROWS. */
export function suggestionRows(items: readonly string[], text: string, rows: readonly RankedRow[], engineId: string): RankedRow[] {
  const engine = engineById(engineId)
  return filterSuggestions(items, text, rows)
    .slice(0, MAX_SUGGESTION_ROWS)
    .map((q) => ({ ...searchRow(engine.id, q, 'suggestions', q), key: `suggest:${q.slice(0, MAX_SEARCH_QUERY)}` }))
}
