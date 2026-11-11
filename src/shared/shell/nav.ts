// Navigation controls (docs/plan/01-shell.md F7): history menu rows and the back/forward long press.
// Pure; used by the UI (NavButtons) and the overlay (NavHistoryMenu).
import type { NavHistoryRow } from '../ipc/engine'
import { isAllowedTabUrl } from '../url'
import { displayHost } from './urlDisplay'

export const NAV_HISTORY_SURFACE = 'nav-history'
export const NAV_LONG_PRESS_MS = 450
export const NAV_HISTORY_MAX = 15
export const NAV_TITLE_MAX = 120
/** The pill's progress bar fades out this long after loading stops. */
export const PROGRESS_FADE_MS = 200

export type NavDirection = 'back' | 'forward'
export interface NavHistoryProps { tabId: string; direction: NavDirection; anchor: { x: number; y: number; width: number; height: number } }

const MAX_FAVICON = 256 * 1024

// eslint-disable-next-line no-control-regex
const CONTROL = /[\u0000-\u001f\u007f-\u009f]/g

function favicon(value: unknown): string | null {
  if (typeof value !== 'string' || value.length > MAX_FAVICON) return null
  return /^(https?:|data:image\/)/i.test(value) ? value : null
}

/** Menu rows from nav.getHistory: at most 15, titles capped at 120 characters (the host when empty), and rows with
 *  a junk index or a URL a tab may not open dropped. */
export function sanitizeHistory(rows: unknown): NavHistoryRow[] {
  if (!Array.isArray(rows)) return []
  const out: NavHistoryRow[] = []
  const seen = new Set<number>()
  for (const row of rows as unknown[]) {
    if (out.length >= NAV_HISTORY_MAX) break
    if (!row || typeof row !== 'object') continue
    const r = row as Record<string, unknown>
    const index = r.index
    if (typeof index !== 'number' || !Number.isInteger(index) || index < 0 || seen.has(index)) continue
    if (typeof r.url !== 'string' || !isAllowedTabUrl(r.url)) continue
    const raw = typeof r.title === 'string' ? r.title.replace(CONTROL, ' ').replace(/\s+/g, ' ').trim() : ''
    const title = (raw || displayHost(r.url) || r.url).slice(0, NAV_TITLE_MAX)
    seen.add(index)
    out.push({ index, title, url: r.url, favicon: favicon(r.favicon) })
  }
  return out
}

/** Letter glyph for a row without a favicon. */
export function letterOf(title: string): string {
  const ch = [...title.trim()][0] ?? '?'
  return ch.toLocaleUpperCase()
}

export interface LongPress {
  down(): void
  /** Pointer up, leave or cancel: a press that has not fired yet ends without firing. */
  up(): void
  /** True once after a press that fired, so the click that follows the pointer up is skipped. */
  consume(): boolean
  dispose(): void
}

type Timers = { set(fn: () => void, ms: number): unknown; clear(handle: unknown): void }
const realTimers: Timers = { set: (fn, ms) => setTimeout(fn, ms), clear: (h) => clearTimeout(h as ReturnType<typeof setTimeout>) }

/** Fires `onLong` when the pointer stays down for NAV_LONG_PRESS_MS. */
export function createLongPress(onLong: () => void, timers: Timers = realTimers): LongPress {
  let handle: unknown = null
  let fired = false
  const cancel = () => {
    if (handle !== null) timers.clear(handle)
    handle = null
  }
  return {
    down() {
      cancel()
      fired = false
      handle = timers.set(() => {
        handle = null
        fired = true
        onLong()
      }, NAV_LONG_PRESS_MS)
    },
    up: cancel,
    consume() {
      const f = fired
      fired = false
      return f
    },
    dispose: cancel
  }
}
