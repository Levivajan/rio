// Spaces types and validators (docs/plan/03-spaces.md "Shared types"). Pure; used by main and the UI.
import { isUuid } from './schema'
import { SPACE_COLORS, type SpaceColor } from './types'

export { SPACE_COLORS }
export type { SpaceColor, SpaceIcon, SpaceSymbol } from './types'
export const MAX_SPACES = 50
export const MAX_PROFILES = 20
export const MAX_NAME = 60
/** UTF-16 cap on a stored name, the IPC string cap, so 60 heavy graphemes cannot grow a row without bound. */
export const MAX_NAME_UNITS = 240
export const MAX_EMOJI_UNITS = 16

export function isSpaceColor(v: unknown): v is SpaceColor {
  return typeof v === 'string' && (SPACE_COLORS as readonly string[]).includes(v)
}
export const isId = isUuid

const segmenter = new Intl.Segmenter(undefined, { granularity: 'grapheme' })
const graphemes = (s: string): string[] => Array.from(segmenter.segment(s), (g) => g.segment)

// Control characters (DEL) and bidi overrides and isolates, which could flip how a name renders.
const CONTROL = /[\p{Cc}‪-‮⁦-⁩]/gu

/**
 * Collapses whitespace to single spaces, strips control characters, trims and cuts to 60 graphemes
 * (and at most 240 UTF-16 units, cut at a grapheme boundary). Returns null when nothing is left.
 */
export function normalizeName(raw: unknown): string | null {
  if (typeof raw !== 'string') return null
  // Only the first 60 graphemes survive, so a long input is cut early (4 KiB covers 60 graphemes of any real text).
  const clean = raw.slice(0, 4096).replace(/\s+/g, ' ').replace(CONTROL, '').replace(/ {2,}/g, ' ').trim()
  if (!clean) return null
  let out = ''
  let count = 0
  for (const g of graphemes(clean)) {
    if (count >= MAX_NAME || out.length + g.length > MAX_NAME_UNITS) break
    out += g
    count += 1
  }
  out = out.trim()
  return out || null
}

const PICTOGRAPHIC = /\p{Extended_Pictographic}/u
const FLAG = /^\p{Regional_Indicator}{2}$/u
// Inside an emoji cluster only pictographs, joiners, variation selectors, skin tones, keycap and tag characters belong.
const EMOJI_PARTS = /^(?:\p{Extended_Pictographic}|\p{Emoji_Modifier}|\p{Emoji_Component}|‍|️|︎|⃣|[\u{E0020}-\u{E007F}])+$/u

/** Exactly one emoji grapheme (a ZWJ sequence, a flag pair, a skin-tone or tag sequence), at most 16 UTF-16 units; else null. */
export function parseEmoji(raw: unknown): string | null {
  if (typeof raw !== 'string') return null
  const value = raw.trim()
  if (!value || value.length > MAX_EMOJI_UNITS) return null
  const parts = graphemes(value)
  if (parts.length !== 1) return null
  if (FLAG.test(value)) return value
  return PICTOGRAPHIC.test(value) && EMOJI_PARTS.test(value) ? value : null
}

/** The first `Space N` (or `Profile N`) name from `start` that no existing name uses. */
export function nextDefaultName(prefix: string, start: number, taken: Iterable<string>): string {
  const used = new Set(taken)
  let n = Math.max(1, start)
  while (used.has(`${prefix} ${n}`)) n += 1
  return `${prefix} ${n}`
}

/** The letter icon: the first grapheme of the name, upper-cased; '?' for an empty name. */
export function spaceLetter(name: string): string {
  const first = graphemes(String(name).trim())[0]
  return first ? first.toLocaleUpperCase() : '?'
}

/** A new space's color when none is chosen: the lavender default (`#dadbf1` to `#efe4e4`). */
export const NEW_SPACE_COLOR: SpaceColor = 'indigo'

/** Hex fill for each named color (the theme lane derives the accent from it when no theme is set). */
export const SPACE_COLOR_HEX: Record<SpaceColor, string> = {
  slate: '#64748b', red: '#ef4444', orange: '#f97316', amber: '#f59e0b', lime: '#84cc16', green: '#22c55e',
  teal: '#14b8a6', cyan: '#06b6d4', blue: '#3b82f6', indigo: '#6366f1', violet: '#8b5cf6', pink: '#ec4899'
}

/** Overlay surface id of the space editor and its props (the profile manager is a view of the same dialog). */
export const SPACE_EDITOR_SURFACE = 'space-editor'
export interface SpaceEditorProps { spaceId: string | null; view: 'space' | 'profiles' }

const tabsWord = (n: number, kind: string) => `${n} ${kind} ${n === 1 ? 'tab' : 'tabs'}`

/** Title and body of the delete-space confirmation (docs/plan/03-spaces.md "Delete space"). */
export function deleteSpaceText(name: string, pinned: number, today: number): { title: string; body: string } {
  const p = Math.max(0, Math.trunc(pinned) || 0)
  const t = Math.max(0, Math.trunc(today) || 0)
  const title = `Delete your “${name}” Space?`
  if (p === 0 && t === 0) return { title, body: 'This space has no tabs.' }
  const parts: string[] = []
  if (p > 0) parts.push(`its ${tabsWord(p, 'pinned')} ${p === 1 ? 'closes' : 'close'}`)
  if (t > 0) parts.push(`its ${tabsWord(t, 'Today')} ${t === 1 ? 'moves' : 'move'} to Archive`)
  const body = parts.join(' and ')
  return { title, body: `${body.charAt(0).toUpperCase()}${body.slice(1)}.` }
}
