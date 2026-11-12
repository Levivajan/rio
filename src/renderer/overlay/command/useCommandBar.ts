// Command bar state (04-command "Keyboard", "Inline autocompletion", "Result kinds"). Holds what was
// typed, the inline completion, the selected result key, the actions-only filter and the rows deleted this session;
// ranks with src/shared/command/rank.ts and merges history, actions and suggestions as they arrive. The view only
// renders what this hook returns and forwards input, key and pointer events.
import { useCallback, useEffect, useMemo, useReducer, useRef, useState } from 'react'
import type { ActionRow } from '../../../shared/ipc/command'
import type { CommandResult, SpaceIcon } from '../../../shared/types'
import { matchActions, namesQueryAtWord } from '../../../shared/command/actions'
import { ASSISTANTS, askUrl, assistantById, assistantByPrefix, firstResultUrl, offersAsk, type Assistant, type AssistantId } from '../../../shared/command/ask'
import { classifyInput, MAX_INPUT } from '../../../shared/command/classify'
import { contextRows, type ContextRow } from '../../../shared/command/context'
import { MAX_SUGGEST_QUERY } from '../../../shared/command/limits'
import { effectiveEntry, SHORTCUTS_KEY } from '../../../shared/shortcuts'
import { buildHistoryIndex, buildTabIndex, rank, suggestionRows, tabEntries, type HistoryIndex, type RankedRow, type RankState } from '../../../shared/command/rank'
import { DEFAULT_ENGINE_ID } from '../../../shared/search/engines'
import { AI, aiOn } from '../../../shared/ai'
import { invoke } from '../../common/invoke'
import { useCluState } from '../../common/useCluState'
import { applyIndexDelta, dropUrls, emptyHistoryCache } from './historyCache'
import { clu } from '../../common/clu'

export type Disposition = 'default' | 'new-tab' | 'split'
export type BarGroup = RankedRow['group'] | 'actions' | 'ask'
export interface BarRow extends Omit<RankedRow, 'group'> {
  group: BarGroup
  /** The Ask row's assistant. */
  ask?: AssistantId
  /** Shortcut hint of an action row, as written in the command table (`Cmd+Shift+C`). */
  accelerator?: string
  /** An action row whose title or alias equals the query. */
  exact?: boolean
  /** A contextual action: its verb is the title, its path chip follows. */
  context?: ContextRow
  /** The space a Switch to or Move tab to row names: the row draws its icon. */
  space?: { name: string; icon: SpaceIcon }
}
/** How the last edit changed the input, read from the native input event by the view. */
export interface InputMeta {
  /** One character was typed at the end of the input (no paste, no edit in the middle). */
  insertedAtEnd: boolean
  /** The edit is part of an IME composition. */
  composing: boolean
}
export interface KeyLike {
  key: string
  metaKey?: boolean
  altKey?: boolean
  ctrlKey?: boolean
  shiftKey?: boolean
  preventDefault(): void
}

/** The list never shows more rows than this, and never scrolls. */
export const MAX_ROWS = 5
/** Action rows next to other results. */
export const MAX_ACTIONS = 3
/** Typing pause before suggestions are asked for (04-command F7). */
const SUGGEST_DEBOUNCE_MS = 60
const NONE: readonly string[] = []
const NO_CONTEXT: ContextRow[] = []

/** Context rows are a few small plain objects, rebuilt from state on every patch. */
const sameJson = <T,>(a: T, b: T): boolean => a === b || JSON.stringify(a) === JSON.stringify(b)

type TabList = ReturnType<typeof tabEntries>

function sameTabList(a: TabList, b: TabList): boolean {
  if (a.activeSpaceId !== b.activeSpaceId || a.activeTabId !== b.activeTabId || a.entries.length !== b.entries.length) return false
  return a.entries.every((e, i) => {
    const f = b.entries[i]
    return !!f && e.tabId === f.tabId && e.spaceId === f.spaceId && e.url === f.url && e.title === f.title && e.pinned === f.pinned && e.split === f.split && e.lastActiveAt === f.lastActiveAt
  })
}
const NO_ACTIONS: readonly ActionRow[] = []
const NO_HISTORY = buildHistoryIndex([])
/** History rows this overlay view already holds; each open syncs only what changed since. */
const historyCache = emptyHistoryCache()

interface BarState {
  /** What the user typed; the input shows this plus the completion suffix. */
  typed: string
  /** The last edit typed one character at the end outside IME composition, so an inline completion may show. */
  complete: boolean
  /** Key of the selected row; null selects the first row. */
  selectedKey: string | null
  actionsOnly: boolean
  /** History URLs deleted in this session, hidden until the next sync drops them. */
  deleted: ReadonlySet<string>
  closing: boolean
  /** Bumped when a session starts or switches mode, so the view refocuses and selects the text. */
  resetSeq: number
  /** The assistant prompt Tab opened from its typed name (the field shows its chip), or null. */
  askMode: AssistantId | null
  /** The Ask row's assistant menu: open, and its highlighted item. */
  picker: { open: boolean; index: number }
  /** The selected row's text, which replaces the input text after Up or Down; null shows what was typed. */
  preview: string | null
}

type Action =
  | { type: 'reset'; text: string }
  | { type: 'input'; value: string; complete: boolean }
  | { type: 'select'; key: string | null; preview?: string | null }
  | { type: 'dropCompletion' }
  | { type: 'acceptCompletion'; text: string }
  | { type: 'actionsOnly'; on: boolean }
  | { type: 'deleted'; url: string; nextKey: string | null }
  | { type: 'undeleted'; url: string }
  | { type: 'closing' }
  | { type: 'askMode'; id: AssistantId | null }
  | { type: 'askRow' }
  | { type: 'picker'; open: boolean; index: number }

const PICKER_CLOSED = { open: false, index: 0 } as const
const initial = (text: string): BarState => ({
  typed: text,
  complete: false,
  selectedKey: null,
  actionsOnly: false,
  deleted: new Set(),
  closing: false,
  resetSeq: 0,
  askMode: null,
  picker: PICKER_CLOSED,
  preview: null
})

function reducer(s: BarState, a: Action): BarState {
  switch (a.type) {
    case 'reset':
      return { ...initial(a.text), deleted: s.deleted, resetSeq: s.resetSeq + 1 }
    case 'input':
      // A new query preselects the first row again.
      return { ...s, typed: a.value.slice(0, MAX_INPUT), complete: a.complete, selectedKey: null, picker: PICKER_CLOSED, preview: null }
    case 'select':
      return { ...s, selectedKey: a.key, preview: a.preview === undefined ? s.preview : a.preview }
    case 'dropCompletion':
      return { ...s, complete: false }
    case 'acceptCompletion':
      return { ...s, typed: a.text, complete: false, preview: null }
    case 'actionsOnly':
      return { ...s, actionsOnly: a.on, selectedKey: null }
    case 'deleted':
      return { ...s, deleted: new Set([...s.deleted, a.url]), selectedKey: a.nextKey }
    case 'undeleted':
      return { ...s, deleted: new Set([...s.deleted].filter((url) => url !== a.url)) }
    case 'closing':
      return { ...s, closing: true, picker: PICKER_CLOSED }
    case 'askMode':
      // The prompt starts empty: the assistant's name was the way in, not the question.
      return { ...s, askMode: a.id, typed: a.id ? '' : s.typed, complete: false, selectedKey: null, picker: PICKER_CLOSED, preview: null }
    case 'askRow':
      return { ...s, selectedKey: ASK_KEY, preview: null }
    case 'picker':
      return { ...s, picker: { open: a.open, index: a.index } }
  }
}

/** The Ask row's key: one per list. */
export const ASK_KEY = 'ask'
/** The row index the Ask row takes (after the top row and one result). */
const ASK_INDEX = 2

/** The row that asks `assistant` the text, or null for empty text. It is a navigate result: the browser checks the
 *  address again before it opens it. */
export function askRow(assistant: Assistant, text: string): BarRow | null {
  const url = askUrl(assistant, text)
  if (!url) return null
  return { key: ASK_KEY, group: 'ask', result: { kind: 'navigate', url }, title: text.trim(), url: '', titleRanges: [], urlRanges: [], ask: assistant.id }
}

/** What the input shows while a row is selected by Up or Down: a search's query, an action's title, a page's
 *  address; the Ask row keeps the typed text. */
export function rowText(row: BarRow, typed: string): string {
  if (row.group === 'ask') return typed
  // "Open Rio folder": the verb and the chip's last part.
  if (row.context) return `${row.context.verb} ${row.context.chip.split(' / ').at(-1) ?? ''}`
  switch (row.result.kind) {
    case 'search':
      return row.result.query
    case 'command':
    case 'provided':
    case 'action':
    case 'reload':
      return row.title
    default:
      return row.url || row.title
  }
}

/** A palette row as main sends it ({ id, title }) with its aliases and chord from this overlay's copy of the table,
 *  the chord as the user set it in Settings, Shortcuts. */
function withTableFields(a: ActionRow, custom: unknown): ActionRow {
  if (a.providerId) return a
  const entry = effectiveEntry(a.id, custom)
  if (!entry) return a
  return { ...a, ...(!a.aliases && entry.aliases?.length ? { aliases: [...entry.aliases] } : {}), ...(!a.accelerator && entry.accelerator ? { accelerator: entry.accelerator } : {}) }
}

const TOGGLE_PIN = 'tabs.togglePin'

/** A contextual row: Cmd+D's command for Move to Today, an overlay-run action for Open and Pin to. */
function contextRow(c: ContextRow, custom: unknown): BarRow {
  const accelerator = c.run.kind === 'togglePin' ? effectiveEntry(TOGGLE_PIN, custom)?.accelerator : undefined
  const result: CommandResult = c.run.kind === 'togglePin' ? { kind: 'command', id: TOGGLE_PIN } : { kind: 'action', actionId: c.key }
  return { key: c.key, group: 'actions', result, title: c.verb, url: '', titleRanges: [], urlRanges: [], context: c, ...(accelerator ? { accelerator } : {}) }
}

/** Contextual rows join the list: the first one ahead of a plain search top row (else after the top row),
 *  the rest after the top row. Cmd+D's generic Pin or Unpin row goes when Move to Today shows. */
function withContext(list: readonly BarRow[], context: readonly BarRow[]): BarRow[] {
  if (context.length === 0) return [...list]
  const pinRow = context.some((r) => r.result.kind === 'command' && r.result.id === TOGGLE_PIN)
  const rest = list.filter((r) => !(pinRow && r.group === 'actions' && r.result.kind === 'command' && r.result.id === TOGGLE_PIN))
  const top = rest[0]
  if (top?.group === 'top' && top.result.kind === 'search') return [context[0] as BarRow, top, ...context.slice(1), ...rest.slice(1)]
  // The top rows (a tab or page, then the plain search for the typed text) stay first.
  let at = 0
  while (rest[at]?.group === 'top') at++
  return [...rest.slice(0, at), ...context, ...rest.slice(at)]
}

/** The providers whose token is a space id (clu_command_bar.h kSpacesProvider, kMoveTabProvider). */
const SPACE_PROVIDERS: ReadonlySet<string> = new Set(['spaces', 'tabs.moveTab'])

function actionRow(a: ActionRow, spaces: Readonly<Record<string, { name: string; icon: SpaceIcon } | undefined>>): BarRow {
  const result: CommandResult = a.providerId && a.token ? { kind: 'provided', providerId: a.providerId, token: a.token } : { kind: 'command', id: a.id }
  const key = a.providerId ? `provided:${a.providerId}:${a.token ?? ''}` : `command:${a.id}`
  const space = a.providerId && a.token && SPACE_PROVIDERS.has(a.providerId) ? spaces[a.token] : undefined
  return {
    key,
    group: 'actions',
    result,
    title: a.title,
    url: a.subtitle ?? '',
    titleRanges: [],
    urlRanges: [],
    ...(a.accelerator ? { accelerator: a.accelerator } : {}),
    ...(a.exact ? { exact: true } : {}),
    ...(space ? { space: { name: space.name, icon: space.icon } } : {})
  }
}

/** Ranked rows with action rows placed after the tabs and suggestions appended, at most MAX_ROWS. An action whose
 *  title or alias is exactly the query goes first, so typing "Toggle Sidebar" and Enter runs it; the search row
 *  stays one Down away. */
function compose(ranked: readonly RankedRow[], actions: readonly BarRow[], suggestions: readonly RankedRow[], deleted: ReadonlySet<string>): BarRow[] {
  const visible = ranked.filter((r) => !r.historyUrl || !deleted.has(r.historyUrl))
  let at = visible.length
  while (at > 0 && visible[at - 1]?.group === 'history') at--
  const seen = new Set(visible.map((r) => r.key))
  const extra = actions.filter((r) => !seen.has(r.key))
  const exact = extra.filter((r) => r.exact).slice(0, 1).map((r) => ({ ...r, group: 'top' as const }))
  const rest = exact.length ? extra.filter((r) => r.key !== exact[0]?.key) : extra
  return [...exact, ...visible.slice(0, at), ...rest, ...visible.slice(at), ...suggestions.filter((r) => !r.historyUrl || !deleted.has(r.historyUrl))].slice(0, MAX_ROWS)
}

export function useCommandBar(windowId: string) {
  const session = useCluState((s) => s.windows[windowId]?.command ?? null)
  const spaces = useCluState((s) => s.spaces)
  // The bar's tab index: only the fields the matcher reads, so a background tab's loading or favicon patch keeps it.
  const tabList = useCluState((s) => tabEntries(s, windowId), sameTabList)
  const target = useCluState((s) => {
    const id = s.windows[windowId]?.command?.targetTabId
    return id ? s.tabs[id] : undefined
  })
  const currentUrl = target?.url ?? null
  const currentTitle = target ? (target.titleOverride ?? target.title) || null : null
  // The profile's engine labels the search rows; main reads the same setting when it runs one.
  const engineId = useCluState((s) => {
    const profileId = s.spaces[s.windows[windowId]?.activeSpaceId ?? '']?.profileId
    const id = profileId ? s.settings.byProfile[profileId]?.['search.engineId'] : undefined
    return typeof id === 'string' ? id : DEFAULT_ENGINE_ID
  })
  // The AI tab's switches and the Ask row's assistant (global settings).
  const askOn = useCluState((s) => aiOn(s.settings.global, AI.askRow))
  const instantLinks = useCluState((s) => aiOn(s.settings.global, AI.instantLinks))
  const storedAssistant = useCluState((s) => assistantById(s.settings.global[AI.askAssistant]).id)
  /** A pick on its way to the browser shows at once; the state patch that follows replaces it. */
  const [picked, setPicked] = useState<AssistantId | null>(null)
  const assistant = assistantById(picked ?? storedAssistant)
  useEffect(() => {
    setPicked((p) => (p === storedAssistant ? null : p))
  }, [storedAssistant])
  const [st, dispatch] = useReducer(reducer, session?.initialText ?? '', initial)
  const [history, setHistory] = useState<HistoryIndex>(() => historyCache.built ?? NO_HISTORY)
  const [suggestions, setSuggestions] = useState<readonly string[]>(NONE)
  const [actionPool, setActionPool] = useState<readonly ActionRow[]>(NO_ACTIONS)
  const custom = useCluState((s) => s.settings.global[SHORTCUTS_KEY])
  const prevRank = useRef<RankState | null>(null)
  const suggestRequest = useRef(0)
  const closed = useRef(false)
  /** History URLs deleted in this view; a sync answered before main applied the delete must not bring them back. */
  const deletedUrls = useRef(new Set<string>())
  const sessionId = session?.sessionId ?? null

  // A new session, or a mode switch in place, starts over from the session's text.
  useEffect(() => {
    closed.current = false
    dispatch({ type: 'reset', text: session?.initialText ?? '' })
  }, [sessionId, session?.mode, session?.initialText, session?.targetTabId])

  // History arrives after the first frame; tab, search and navigate rows render from state right away. The sync starts
  // once that frame is presented (two frames after commit), so a full first sync of 10,000 rows never delays the open.
  useEffect(() => {
    if (!sessionId) return
    let live = true
    let frame = requestAnimationFrame(() => {
      frame = requestAnimationFrame(() => {
        void invoke(clu().command.syncIndex({ sinceRev: historyCache.rev })).then((r) => {
          if (!r.ok) return
          applyIndexDelta(historyCache, r.value)
          const built = dropUrls(historyCache, deletedUrls.current)
          if (live) setHistory(built)
        })
      })
    })
    return () => {
      live = false
      cancelAnimationFrame(frame)
    }
  }, [sessionId])

  const query = st.typed.trim()

  // Engine suggestions after a short pause in typing; only the newest request's answer is kept.
  useEffect(() => {
    const requestId = ++suggestRequest.current
    // The last answer stays listed until the next one arrives, so the list does not shrink and grow on every key.
    if (!sessionId || st.actionsOnly || !query || query.length > MAX_SUGGEST_QUERY) {
      setSuggestions(NONE)
      return
    }
    const timer = setTimeout(() => {
      void invoke(clu().command.suggest({ sessionId, requestId, query })).then((r) => {
        if (r.ok && r.value.requestId === suggestRequest.current) setSuggestions(r.value.items)
      })
    }, SUGGEST_DEBOUNCE_MS)
    return () => clearTimeout(timer)
  }, [query, sessionId, st.actionsOnly])

  // Main lists every action the window offers once per session (the palette commands it can run and the providers'
  // rows); the bar matches them on each keystroke with the shared matcher, so no keystroke waits on main.
  useEffect(() => {
    setActionPool(NO_ACTIONS)
    if (!sessionId) return
    let live = true
    void invoke(clu().command.actions({ sessionId, query: '' })).then((r) => {
      if (live && r.ok && Array.isArray(r.value)) setActionPool(r.value)
    })
    return () => {
      live = false
    }
  }, [sessionId])

  // The actions-only filter lists them for any query; next to other results they need the query at a word start.
  const actions = useMemo<readonly ActionRow[]>(() => {
    const q = query.slice(0, MAX_SUGGEST_QUERY)
    const matched = matchActions(
      actionPool.map((a) => withTableFields(a, custom)),
      q
    )
    return st.actionsOnly ? matched : matched.filter((a) => namesQueryAtWord(a, q))
  }, [actionPool, custom, query, st.actionsOnly])

  const tabIndex = useMemo(() => buildTabIndex(tabList.entries, { activeSpaceId: tabList.activeSpaceId, activeTabId: tabList.activeTabId }), [tabList])

  const ranked = useMemo(() => {
    if (!session) return null
    const out = rank(
      {
        text: st.typed,
        mode: session.mode,
        engineId,
        ...(session.engineName ? { engineName: session.engineName } : {}),
        tabs: tabIndex,
        history,
        targetTabId: session.targetTabId,
        currentUrl,
        currentTitle,
        complete: st.complete
      },
      prevRank.current
    )
    prevRank.current = out.state
    return out
  }, [st.typed, st.complete, session, tabIndex, history, currentUrl, currentTitle, engineId])

  const contextQuery = st.actionsOnly || st.askMode ? null : query.slice(0, MAX_SUGGEST_QUERY)
  const contextList = useCluState((s) => (contextQuery === null ? NO_CONTEXT : contextRows(s, windowId, contextQuery)), sameJson)
  const context = useMemo<BarRow[]>(() => contextList.map((c) => contextRow(c, custom)), [contextList, custom])

  const modeAssistant = st.askMode ? assistantById(st.askMode) : null
  const rows = useMemo<BarRow[]>(() => {
    if (!ranked) return []
    // The assistant prompt lists only its Ask row.
    if (modeAssistant) {
      const row = askRow(modeAssistant, st.typed)
      return row ? [row] : []
    }
    const actionRows = actions.map((a) => actionRow(a, spaces))
    if (st.actionsOnly) return actionRows.slice(0, MAX_ROWS)
    const list = withContext(compose(ranked.rows, actionRows.slice(0, MAX_ACTIONS), suggestionRows(suggestions, st.typed, ranked.rows, engineId), st.deleted), context).slice(0, MAX_ROWS)
    const ask = askOn && offersAsk(st.typed) ? askRow(assistant, st.typed) : null
    if (!ask) return list
    const at = Math.min(ASK_INDEX, list.length)
    return [...list.slice(0, at), ask, ...list.slice(at)].slice(0, MAX_ROWS)
  }, [ranked, actions, spaces, context, suggestions, st.actionsOnly, st.typed, st.deleted, engineId, askOn, assistant, modeAssistant])

  const completion = st.actionsOnly || st.askMode || st.preview !== null ? null : (ranked?.completion ?? null)
  /** The assistant whose name was typed: the field's right side hints that Tab opens its prompt. */
  const tabAssistant = askOn && !st.actionsOnly && !st.askMode ? assistantByPrefix(st.typed) : null
  const text = st.preview ?? (completion ? completion.text : st.typed)
  const found = st.selectedKey === null ? -1 : rows.findIndex((r) => r.key === st.selectedKey)
  const active = rows.length === 0 ? -1 : Math.max(found, 0)

  const finishClose = useCallback(() => {
    if (closed.current || !sessionId) return
    closed.current = true
    void clu().command.close({ sessionId })
  }, [sessionId])

  /** Plays the close animation; the view calls finishClose when it ends. */
  const close = useCallback(() => dispatch({ type: 'closing' }), [])

  const execute = (result: CommandResult, disposition: Disposition) => {
    if (!sessionId || closed.current) return
    void clu().command.execute({ sessionId, result, disposition, input: text.slice(0, MAX_INPUT) })
  }

  const run = (index: number, disposition: Disposition = 'default') => {
    const row = rows[index]
    const local = row?.context?.run
    if (local && local.kind !== 'togglePin') {
      if (!sessionId || closed.current || st.closing) return
      // Open shows the folder's tabs in the sidebar; Pin to moves the active tab to the folder's end.
      if (local.kind === 'openFolder') void clu().folders.setCollapsed({ folderId: local.folderId, collapsed: false })
      else void clu().tabs.move({ nodeId: local.tabId, target: { section: 'pinned', spaceId: local.spaceId, parentId: local.folderId, index: local.index }, ask: true })
      close()
      return
    }
    if (row) execute(row.result, disposition)
  }

  /** Shift+Enter ("Open Top Result"): a web search opens the engine's first result. The selected suggestion's
   *  query, else the typed query; anything else runs as Enter does. */
  const runFirstResult = (disposition: Disposition) => {
    const row = rows[active]
    const typed = classifyInput(st.typed)
    const query = row?.result.kind === 'search' ? row.result.query : row?.group !== 'ask' && typed.kind === 'search' ? typed.query : null
    const url = query ? firstResultUrl(engineId, query) : null
    if (url) execute({ kind: 'navigate', url }, disposition)
    else run(active, disposition)
  }

  /** Saves the Ask row's assistant; it stays until the next pick. */
  const pick = (id: AssistantId) => {
    setPicked(id)
    dispatch({ type: 'picker', open: false, index: 0 })
    dispatch({ type: 'select', key: ASK_KEY })
    void invoke(clu().settings.set({ key: AI.askAssistant, scope: 'global', value: id })).then((r) => {
      if (!r.ok) setPicked(null)
    })
  }
  const openPicker = () => dispatch({ type: 'picker', open: true, index: Math.max(0, ASSISTANTS.findIndex((a) => a.id === assistant.id)) })
  const closePicker = () => dispatch({ type: 'picker', open: false, index: 0 })

  /** Deletes a history or past-search row from history and drops it from the list (F6). */
  const remove = (index: number) => {
    const row = rows[index]
    if (!row?.historyUrl) return
    const next = rows[index + 1] ?? rows[index - 1]
    const url = row.historyUrl
    const cached = historyCache.rows.get(url)
    void invoke(clu().command.deleteEntry({ url })).then((r) => {
      if (r.ok) return
      // Main kept the entry (a policy forbids deleting history): the row comes back.
      deletedUrls.current.delete(url)
      if (cached) setHistory(applyIndexDelta(historyCache, { rev: historyCache.rev, full: false, upserts: [cached], deletes: [] }))
      dispatch({ type: 'undeleted', url })
    })
    deletedUrls.current.add(url)
    setHistory(dropUrls(historyCache, [url]))
    dispatch({ type: 'deleted', url, nextKey: index === active ? (next?.key ?? null) : st.selectedKey })
  }

  const move = (by: number) => {
    if (rows.length === 0) return
    const next = (active + by + rows.length) % rows.length
    const row = rows[next]
    // The first row is what Enter on the typed text does, so it shows the typed text again.
    const preview = next === 0 || !row || st.actionsOnly || st.askMode ? null : rowText(row, st.typed).slice(0, MAX_INPUT)
    dispatch({ type: 'select', key: row?.key ?? null, preview })
  }

  const input = (value: string, meta: InputMeta) => {
    dispatch({ type: 'input', value, complete: meta.insertedAtEnd && !meta.composing })
  }

  const keyDown = (e: KeyLike) => {
    const plain = !e.metaKey && !e.altKey && !e.ctrlKey
    if (st.closing) {
      e.preventDefault()
      return
    }
    // The assistant menu takes Up, Down, Enter, Space and Escape while it is open; any other key closes it.
    if (st.picker.open) {
      const n = ASSISTANTS.length
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault()
        dispatch({ type: 'picker', open: true, index: (st.picker.index + (e.key === 'ArrowDown' ? 1 : -1) + n) % n })
        return
      }
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault()
        const a = ASSISTANTS[st.picker.index]
        if (a) pick(a.id)
        return
      }
      if (e.key === 'Escape') {
        e.preventDefault()
        closePicker()
        return
      }
      closePicker()
    }
    const onAsk = rows[active]?.group === 'ask'
    if (e.key === 'ArrowDown' && e.altKey && !e.metaKey && !e.ctrlKey && onAsk && !st.askMode) {
      e.preventDefault()
      openPicker()
    } else if (e.key === 'Enter' && e.shiftKey && instantLinks && !st.actionsOnly && !st.askMode) {
      e.preventDefault()
      runFirstResult(e.metaKey ? 'new-tab' : e.altKey ? 'split' : 'default')
    } else if (e.key === 'Backspace' && plain && st.askMode && !st.typed) {
      e.preventDefault()
      dispatch({ type: 'askMode', id: null })
    } else if (e.key === 'Backspace' && e.metaKey && e.altKey) {
      e.preventDefault()
      remove(active)
    } else if ((e.key === 'Backspace' || e.key === 'Delete') && plain && completion) {
      // Only the selected completion goes; what was typed stays.
      e.preventDefault()
      dispatch({ type: 'dropCompletion' })
    } else if (e.key === 'Backspace' && plain && st.actionsOnly && !st.typed) {
      e.preventDefault()
      dispatch({ type: 'actionsOnly', on: false })
    } else if (e.key === 'Enter') {
      e.preventDefault()
      run(active, e.metaKey ? 'new-tab' : e.altKey ? 'split' : 'default')
    } else if (e.key === 'Escape') {
      e.preventDefault()
      close()
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault()
      move(e.key === 'ArrowDown' ? 1 : -1)
    } else if (e.ctrlKey && !e.metaKey && !e.altKey && (e.key === 'n' || e.key === 'p')) {
      e.preventDefault()
      move(e.key === 'n' ? 1 : -1)
    } else if (e.key === 'Tab' && !e.metaKey && !e.altKey && !e.ctrlKey) {
      e.preventDefault()
      // Tab on an assistant's typed name opens its prompt; on other text it jumps to the Ask row.
      if (tabAssistant && !e.shiftKey) dispatch({ type: 'askMode', id: tabAssistant.id })
      else if (completion && !e.shiftKey) dispatch({ type: 'acceptCompletion', text: completion.text })
      else if (!st.typed && !st.actionsOnly && !st.askMode && !e.shiftKey) dispatch({ type: 'actionsOnly', on: true })
      else if (!e.shiftKey && askOn && !st.actionsOnly && !st.askMode && !onAsk && offersAsk(st.typed)) dispatch({ type: 'askRow' })
      else move(e.shiftKey ? -1 : 1)
    } else if (completion && (e.key === 'ArrowRight' || e.key === 'End' || e.key === 'ArrowLeft' || e.key === 'Home')) {
      // Moving the caret keeps the completed text; the browser then moves the caret as usual.
      dispatch({ type: 'acceptCompletion', text: completion.text })
    }
  }

  return {
    session,
    text,
    /** Selected suffix of the input while an inline completion shows. */
    completionRange: completion ? ([st.typed.length, completion.text.length] as const) : null,
    rows,
    active,
    actionsOnly: st.actionsOnly,
    /** The assistant prompt's assistant (the field's chip), or null. */
    askMode: modeAssistant,
    /** The assistant whose name was typed, for the field's "Tab" hint. */
    tabAssistant,
    /** The Ask row's assistant. */
    assistant,
    picker: st.picker,
    pick,
    togglePicker: () => (st.picker.open ? closePicker() : openPicker()),
    highlightPicker: (index: number) => dispatch({ type: 'picker', open: true, index }),
    closePicker,
    closing: st.closing,
    resetSeq: st.resetSeq,
    input,
    keyDown,
    hover: (index: number) => {
      const key = rows[index]?.key
      if (key && index !== active) dispatch({ type: 'select', key })
    },
    run,
    remove,
    close,
    finishClose
  }
}

export type CommandBarModel = ReturnType<typeof useCommandBar>
