// Command bar actions (04-command F8, PLAN decision 53). There is no action list here: palette rows come from the
// command table (every `palette: true` entry with a registered handler), dynamic rows from the providers registered
// with services.commandBar. This module turns both into ActionRows and matches them against the query.
import { commandTable, type CommandEntry } from '../commands'
import type { ActionRow } from '../ipc/command'
import { cleanTitle, matchText, queryTokens, SCORE_SUBSTRING } from './match'

/** Lowest match score an action needs to be listed for a query (04-command "Result kinds"). */
export const ACTION_MIN_SCORE = SCORE_SUBSTRING
/** Most action rows main returns for one query; the bar shows at most 10. */
export const MAX_ACTION_RESULTS = 50
/** Longest provider token (the `provided` result schema caps it the same). */
export const MAX_ACTION_TOKEN = 256
/** Longest provider row title kept. */
export const MAX_ACTION_TITLE = 120

/** What the rows need from the commands registry, for the window kind the bar runs in. */
export interface PaletteRegistry {
  isEnabled(id: string): boolean
  /** The current title (`commands.setTitle` may have changed it). */
  title(id: string): string
}

/** One row per palette entry that has a handler, in table order. An entry without a handler is hidden. */
export function paletteRows(registry: PaletteRegistry, table: readonly CommandEntry[] = commandTable): ActionRow[] {
  const rows: ActionRow[] = []
  for (const e of table) {
    if (!e.palette || !registry.isEnabled(e.id)) continue
    rows.push({ id: e.id, title: registry.title(e.id), ...(e.aliases?.length ? { aliases: [...e.aliases] } : {}), ...(e.accelerator ? { accelerator: e.accelerator } : {}) })
  }
  return rows
}

const folded = (s: string) => queryTokens(s).join(' ')

/** Rows matching the query (title or alias at ACTION_MIN_SCORE or better), best first and stable within a score.
 *  An empty query lists every row in order (the actions-only filter). */
export function matchActions(rows: readonly ActionRow[], query: string, limit = MAX_ACTION_RESULTS): ActionRow[] {
  const q = folded(query)
  if (!q) return rows.slice(0, limit)
  const scored: { row: ActionRow; score: number; at: number }[] = []
  rows.forEach((row, at) => {
    const names = [row.title, ...(row.aliases ?? [])]
    const score = Math.max(...names.map((n) => matchText(n, query)))
    if (score < ACTION_MIN_SCORE) return
    const exact = names.some((n) => folded(n) === q)
    scored.push({ row: exact ? { ...row, exact } : row, score: exact ? 2 : score, at })
  })
  scored.sort((a, b) => b.score - a.score || a.at - b.at)
  return scored.slice(0, limit).map((s) => s.row)
}

/** Next to other results an action shows only when its title or an alias has the query at a word start ("new w" for
 *  New Window), so short queries like "wi" or "weather" keep tabs, the search and history. */
export const ACTION_MIN_QUERY = 3
export function namesQueryAtWord(row: ActionRow, query: string): boolean {
  const q = folded(query)
  if (q.length < ACTION_MIN_QUERY) return false
  return [row.title, ...(row.aliases ?? [])].some((n) => ` ${folded(n)}`.includes(` ${q}`))
}

/** A clean single-line title, or null for junk. */
export function cleanActionTitle(title: unknown): string | null {
  if (typeof title !== 'string') return null
  const clean = cleanTitle(title.slice(0, MAX_ACTION_TITLE * 2).replace(/\s+/g, ' ')).slice(0, MAX_ACTION_TITLE).trim()
  return clean || null
}
