// Custom shortcuts (docs/chromium/parity-settings.md section 4): the `shortcuts.custom` setting maps a command id to a
// chord in the table's syntax (`Cmd+Shift+K`) or null (no key). One resolution turns it and the key table into the
// keys each command answers to now; chromium/clu/browser/commands/clu_shortcuts.cc does the same with the same cases.
import { chordKey, commandTable, parseChord, type Chord, type CommandEntry, type Variant } from './commands'

export const SHORTCUTS_KEY = 'shortcuts.custom'
export type CustomShortcuts = Record<string, string | null>

const MAX_ENTRIES = 256
const MAX_ID = 64
const MAX_CHORD = 32

/** Keys macOS keeps for itself (quit, hide, minimize, the app switcher, Spotlight, the clipboard, undo). */
export const RESERVED_CHORDS = ['Cmd+Q', 'Cmd+H', 'Cmd+Option+H', 'Cmd+M', 'Cmd+Tab', 'Cmd+Space', 'Cmd+C', 'Cmd+V', 'Cmd+X', 'Cmd+A', 'Cmd+Z', 'Cmd+Shift+Z', 'Cmd+`'] as const
const reserved = new Set<string>(RESERVED_CHORDS.map((c) => chordKey(parseChord(c))))

function tryParse(chord: string): Chord | null {
  try {
    return parseChord(chord)
  } catch {
    return null
  }
}

export function isReservedChord(chord: string): boolean {
  const c = tryParse(chord)
  return !!c && reserved.has(chordKey(c))
}

/** A chord a user may give a command: it parses, has Cmd, Ctrl or Option, and is not one macOS keeps. */
export function isAssignableChord(chord: unknown): chord is string {
  if (typeof chord !== 'string' || chord.length === 0 || chord.length > MAX_CHORD) return false
  const c = tryParse(chord)
  if (!c || (!c.meta && !c.ctrl && !c.alt)) return false
  return !reserved.has(chordKey(c))
}

/** The registry's validator. Unknown command ids pass and are ignored, so a later table change keeps the user's keys. */
export function isCustomShortcuts(value: unknown): value is CustomShortcuts {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return false
  const entries = Object.entries(value)
  if (entries.length > MAX_ENTRIES) return false
  return entries.every(([id, chord]) => id.length > 0 && id.length <= MAX_ID && (chord === null || isAssignableChord(chord)))
}

const NONE: CustomShortcuts = Object.freeze({})

/** The custom map in a resolved settings dict, or none when it is missing or junk. */
export function customShortcuts(value: unknown): CustomShortcuts {
  return isCustomShortcuts(value) ? value : NONE
}

const meet = (a: Variant, b: Variant) => a === 'both' || b === 'both' || a === b

interface Claim { key: string; variant: Variant }

/**
 * The table with the user's keys in it. A custom entry replaces the command's chord and its alternates (null leaves it
 * none); a command without one keeps its own, minus any chord a custom entry took where both can run (main and both,
 * mini and both); of two custom entries on one chord the first in table order keeps it.
 */
export function effectiveTable(custom: CustomShortcuts, table: readonly CommandEntry[] = commandTable): CommandEntry[] {
  const claims: Claim[] = []
  const taken = (key: string, variant: Variant) => claims.some((c) => c.key === key && meet(c.variant, variant))
  const own = new Map<string, string | null>()
  for (const e of table) {
    if (!Object.hasOwn(custom, e.id)) continue
    const chord = custom[e.id]
    if (chord === null || chord === undefined) {
      own.set(e.id, null)
      continue
    }
    if (!isAssignableChord(chord)) continue
    const key = chordKey(parseChord(chord))
    if (taken(key, e.variant)) {
      own.set(e.id, null)
      continue
    }
    claims.push({ key, variant: e.variant })
    own.set(e.id, chord)
  }
  return table.map((e) => {
    if (own.has(e.id)) {
      const chord = own.get(e.id)
      const { accelerator: _a, alt: _alt, ...rest } = e
      return chord ? { ...rest, accelerator: chord } : rest
    }
    const free = (chord: string) => !taken(chordKey(parseChord(chord)), e.variant)
    const { accelerator, alt, ...rest } = e
    const alts = (alt ?? []).filter((a) => free(a.keys))
    return { ...rest, ...(accelerator && free(accelerator) ? { accelerator } : {}), ...(alts.length ? { alt: alts } : {}) }
  })
}

const cache = new WeakMap<object, Map<string, CommandEntry>>()

/** The command `id` with the user's keys, for `settings.global['shortcuts.custom']` as state carries it. */
export function effectiveEntry(id: string, customValue: unknown): CommandEntry | undefined {
  const custom = customShortcuts(customValue)
  let byId = cache.get(custom)
  if (!byId) {
    byId = new Map(effectiveTable(custom).map((e) => [e.id, e]))
    cache.set(custom, byId)
  }
  return byId.get(id)
}

/** The command's chords now: its main chord first, then its alternates. */
export function keysOf(entry: CommandEntry | undefined): string[] {
  if (!entry) return []
  return [...(entry.accelerator ? [entry.accelerator] : []), ...(entry.alt ?? []).map((a) => a.keys)]
}

/** The command other than `forId` that answers to `chord` where `forId` could run, if any. */
export function chordOwner(chord: string, forId: string, effective: readonly CommandEntry[]): CommandEntry | undefined {
  const me = effective.find((e) => e.id === forId)
  const c = tryParse(chord)
  if (!me || !c) return undefined
  const key = chordKey(c)
  return effective.find((e) => e.id !== forId && meet(e.variant, me.variant) && keysOf(e).some((k) => chordKey(parseChord(k)) === key))
}

const CODE_KEYS: Record<string, string> = {
  BracketLeft: '[', BracketRight: ']', Equal: '=', Minus: '-', Period: '.', Comma: ',', Slash: '/', Semicolon: ';', Quote: "'",
  Backquote: '`', Backslash: '\\', ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down', Tab: 'Tab',
  Backspace: 'Backspace', Enter: 'Enter', Escape: 'Escape', Space: 'Space'
}

export interface KeyLike { code: string; metaKey: boolean; ctrlKey: boolean; altKey: boolean; shiftKey: boolean }

/** The chord a key press makes, in the table's syntax (Cmd, Ctrl, Option, Shift, then the key), or null for a lone modifier or a key the table cannot name. */
export function chordFromKey(e: KeyLike): string | null {
  const letter = /^Key([A-Z])$/.exec(e.code)
  const digit = /^Digit([0-9])$/.exec(e.code)
  const key = letter?.[1] ?? digit?.[1] ?? CODE_KEYS[e.code]
  if (!key) return null
  const mods = [e.metaKey && 'Cmd', e.ctrlKey && 'Ctrl', e.altKey && 'Option', e.shiftKey && 'Shift'].filter(Boolean)
  return [...mods, key].join('+')
}
