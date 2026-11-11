// The command bar's Ask row and Shift+Enter (docs/chromium/parity-command.md): which assistant a query
// goes to, the address that asks it, when the row shows, and the first-result address of a web search. Pure, so the
// overlay and its tests share it; the browser checks every address again before it opens one.
import { classifyInput } from './classify'
import { engineById, MAX_SEARCH_QUERY } from '../search/engines'

export const ASSISTANT_IDS = ['grok', 'chatgpt', 'claude'] as const
export type AssistantId = (typeof ASSISTANT_IDS)[number]
export const DEFAULT_ASSISTANT: AssistantId = 'chatgpt'

export interface Assistant {
  id: AssistantId
  name: string
  /** Opens the assistant with `q` as the first message. */
  base: string
}

/** In the dropdown's order. */
export const ASSISTANTS: readonly Assistant[] = [
  { id: 'grok', name: 'Grok', base: 'https://grok.com/?q=' },
  { id: 'chatgpt', name: 'ChatGPT', base: 'https://chatgpt.com/?q=' },
  { id: 'claude', name: 'Claude', base: 'https://claude.ai/new?q=' }
]

export function isAssistantId(v: unknown): v is AssistantId {
  return typeof v === 'string' && (ASSISTANT_IDS as readonly string[]).includes(v)
}

/** The assistant for a stored setting; junk reads as the default. */
export function assistantById(id: unknown): Assistant {
  return ASSISTANTS.find((a) => a.id === id) ?? (ASSISTANTS.find((a) => a.id === DEFAULT_ASSISTANT) as Assistant)
}

/** The address that asks `assistant` the trimmed text, or null for empty text. Long text is cut to the engine cap
 *  (never inside a surrogate pair). */
export function askUrl(assistant: Assistant, text: string): string | null {
  const q = cut(text.trim(), MAX_SEARCH_QUERY)
  return q ? assistant.base + encodeURIComponent(q.toWellFormed()) : null
}

/** The row shows for any typed text that is not an address, one word included (one typed word such as "hello" expects
 *  to ask it). */
export function offersAsk(text: string): boolean {
  return classifyInput(text).kind === 'search'
}

/** The assistant whose name the text starts (four letters or more, or the whole name), for Tab into its prompt. */
export function assistantByPrefix(text: string): Assistant | null {
  const t = text.trim().toLowerCase()
  if (!t) return null
  return ASSISTANTS.find((a) => {
    const name = a.name.toLowerCase()
    return t === name || (t.length >= 4 && name.startsWith(t))
  }) ?? null
}

/** The first result of a web search where the engine has such an address (Google's I'm Feeling Lucky, DuckDuckGo's
 *  backslash), else null and the search runs as usual. */
export function firstResultUrl(engineId: string, query: string): string | null {
  const q = cut(query.trim(), MAX_SEARCH_QUERY)
  if (!q) return null
  const engine = engineById(engineId)
  if (engine.id === 'google') return `${engine.searchUrl(q)}&btnI=1`
  if (engine.id === 'duckduckgo') return engine.searchUrl(`\\${q}`)
  return null
}

/** Long addresses lose their middle ("accounts.go…ail&service=lso"). */
export function middleEllipsis(text: string, max = 28): string {
  const chars = Array.from(text)
  if (chars.length <= max) return text
  const head = Math.ceil((max - 1) * 0.4)
  const tail = max - 1 - head
  return `${chars.slice(0, head).join('')}…${chars.slice(chars.length - tail).join('')}`
}

function cut(text: string, max: number): string {
  if (text.length <= max) return text
  let end = max
  const code = text.charCodeAt(end - 1)
  if (code >= 0xd800 && code <= 0xdbff) end--
  return text.slice(0, end)
}
