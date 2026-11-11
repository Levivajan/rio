// Navigation history kept on tab rows: 50 entries, 256 KB, allowed schemes only.
import { classifyUrl } from './url'

export interface NavEntry { url: string; title: string; pageState?: string }
export interface NavState { index: number; entries: NavEntry[] }

export const NAV_MAX_ENTRIES = 50
export const NAV_MAX_BYTES = 256 * 1024
const MAX_TITLE = 1024

function byteSize(value: unknown): number {
  return new TextEncoder().encode(JSON.stringify(value)).length
}

function cleanEntry(raw: unknown): NavEntry | null {
  if (!raw || typeof raw !== 'object') return null
  const r = raw as Record<string, unknown>
  if (typeof r.url !== 'string') return null
  const c = classifyUrl(r.url)
  if (c.kind !== 'web' && !(c.kind === 'internal' && c.page !== 'ui' && c.page !== 'overlay' && c.page !== 'mini')) return null
  const entry: NavEntry = { url: c.url, title: typeof r.title === 'string' ? r.title.slice(0, MAX_TITLE) : '' }
  if (typeof r.pageState === 'string' && r.pageState.length <= NAV_MAX_BYTES) entry.pageState = r.pageState
  return entry
}

/** Validates and caps a nav state from any source (DB row, engine capture). Returns null when nothing usable is left. */
export function capNavState(input: unknown): NavState | null {
  if (!input || typeof input !== 'object') return null
  const raw = input as Record<string, unknown>
  if (!Array.isArray(raw.entries)) return null
  const rawIndex = typeof raw.index === 'number' && Number.isInteger(raw.index) ? raw.index : raw.entries.length - 1
  const entries: NavEntry[] = []
  let index = -1
  raw.entries.forEach((e, i) => {
    const clean = cleanEntry(e)
    if (!clean) return
    if (i <= rawIndex) index = entries.length
    entries.push(clean)
  })
  if (entries.length === 0) return null
  if (index < 0) index = 0
  // Keep a window of at most 50 entries that contains the current one.
  if (entries.length > NAV_MAX_ENTRIES) {
    const start = Math.min(Math.max(0, index - Math.floor(NAV_MAX_ENTRIES / 2)), entries.length - NAV_MAX_ENTRIES)
    entries.splice(0, start)
    entries.length = NAV_MAX_ENTRIES
    index -= start
  }
  const state: NavState = { index, entries }
  if (byteSize(state) <= NAV_MAX_BYTES) return state
  for (const e of state.entries) delete e.pageState
  // Drop the entries farthest from the current one until it fits.
  while (byteSize(state) > NAV_MAX_BYTES && state.entries.length > 1) {
    if (state.index > state.entries.length - 1 - state.index) {
      state.entries.shift()
      state.index -= 1
    } else {
      state.entries.pop()
    }
  }
  return byteSize(state) <= NAV_MAX_BYTES ? state : null
}

export function parseNavState(text: string | null | undefined): NavState | null {
  if (!text) return null
  try {
    return capNavState(JSON.parse(text))
  } catch {
    return null
  }
}
