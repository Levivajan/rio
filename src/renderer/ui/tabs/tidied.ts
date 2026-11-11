// "Double-click to rename" under a tidied title: when a Today row is
// pinned and Tidy Tab Titles renames it soon after, the row shows that subtitle under the new title.
// The browser marks nothing, so the frame notices it: a row seen in Today
// and then in Pinned whose title override appears within TIDY_WINDOW_MS is tidied. The subtitle is a hint for right
// after the pin (gone later in the same session): it goes once the user leaves the row
// (the tidied row was the current tab and another one is now), after TIDY_SUBTITLE_MS, or when the title changes.
import type { TabState } from '../../../shared/state'

/** The on-device model answers in about a second; the rules at once. */
export const TIDY_WINDOW_MS = 10_000
/** How long the hint stays under a tidied title the user has not left. */
export const TIDY_SUBTITLE_MS = 30_000

const lastSection = new Map<string, TabState['section']>()
/** When a row went from Today to Pinned, and the title override it had in Today. */
const pinnedAt = new Map<string, { at: number; override: string | null }>()
const tidied = new Map<string, { title: string; at: number; wasActive: boolean }>()
const lastTodayOverride = new Map<string, string | null>()

/** Feeds one render of a row; true while its title is the tidy one. */
export function observeTidy(tab: Pick<TabState, 'id' | 'section' | 'titleOverride'>, active = false, now: number = Date.now()): boolean {
  const before = lastSection.get(tab.id)
  lastSection.set(tab.id, tab.section)
  if (tab.section === 'today') lastTodayOverride.set(tab.id, tab.titleOverride ?? null)
  if (before === 'today' && tab.section === 'pinned') pinnedAt.set(tab.id, { at: now, override: lastTodayOverride.get(tab.id) ?? null })
  if (tab.section !== 'pinned') {
    pinnedAt.delete(tab.id)
    tidied.delete(tab.id)
    return false
  }
  const at = pinnedAt.get(tab.id)
  if (tab.titleOverride && at !== undefined && at.override === null && now - at.at <= TIDY_WINDOW_MS) {
    tidied.set(tab.id, { title: tab.titleOverride, at: now, wasActive: active })
    pinnedAt.delete(tab.id)
  }
  const t = tidied.get(tab.id)
  if (t === undefined) return false
  if (active) t.wasActive = true
  if (t.title !== tab.titleOverride || (t.wasActive && !active) || now - t.at >= TIDY_SUBTITLE_MS) {
    tidied.delete(tab.id)
    return false
  }
  return true
}

/** The tidy title's letter morph (about 340 ms). */
export const TIDY_MORPH_MS = 340

/** True for the morph's length after the tidy title arrived, so the row draws it then and never on later renders. */
export function tidiedJustNow(id: string, now: number = Date.now()): boolean {
  const t = tidied.get(id)
  return !!t && now - t.at < TIDY_MORPH_MS
}

/** Milliseconds until a tidied row's hint expires, for the row's re-render timer; null when it shows none. */
export function tidyExpiresIn(id: string, now: number = Date.now()): number | null {
  const t = tidied.get(id)
  return t ? Math.max(0, t.at + TIDY_SUBTITLE_MS - now) : null
}

export function resetTidyForTesting(): void {
  lastSection.clear()
  lastTodayOverride.clear()
  pinnedAt.clear()
  tidied.clear()
}
