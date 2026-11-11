// Command table composition and chord helpers.
import { coreCommands } from './commands/core'
import type { CommandEntry, Route, Variant } from './commands/define'
// #region clu:platform (edit only inside this block)
import { platformCommands } from './commands/platform'
// #endregion clu:platform
// #region clu:data (edit only inside this block)
import { dataCommands } from './commands/data'
// #endregion clu:data
// #region clu:spaces (edit only inside this block)
import { spacesCommands } from './commands/spaces'
// #endregion clu:spaces
// #region clu:engine (edit only inside this block)
import { engineCommands } from './commands/engine'
// #endregion clu:engine
// #region clu:capabilities (edit only inside this block)
import { capabilitiesCommands } from './commands/capabilities'
// #endregion clu:capabilities
// #region clu:extensions (edit only inside this block)
import { extensionsCommands } from './commands/extensions'
// #endregion clu:extensions
// #region clu:tweaks (edit only inside this block)
import { tweaksCommands } from './commands/tweaks'
// #endregion clu:tweaks
// #region clu:tabs (edit only inside this block)
import { tabsCommands } from './commands/tabs'
// #endregion clu:tabs
// #region clu:archive (edit only inside this block)
import { archiveCommands } from './commands/archive'
// #endregion clu:archive
// #region clu:shell (edit only inside this block)
import { shellCommands } from './commands/shell'
// #endregion clu:shell
// #region clu:theme (edit only inside this block)
import { themeCommands } from './commands/theme'
// #endregion clu:theme
// #region clu:command (edit only inside this block)
import { commandCommands } from './commands/command'
// #endregion clu:command
// #region clu:split (edit only inside this block)
import { splitCommands } from './commands/split'
// #endregion clu:split
// #region clu:multitask (edit only inside this block)
import { multitaskCommands } from './commands/multitask'
// #endregion clu:multitask
// #region clu:quality (edit only inside this block)
import { qualityCommands } from './commands/quality'
// #endregion clu:quality

export type { CommandEntry, MenuName, Route, Variant } from './commands/define'

export const commandTable: readonly CommandEntry[] = [
  ...coreCommands,
  // #region clu:platform (edit only inside this block)
  ...platformCommands,
  // #endregion clu:platform
  // #region clu:data (edit only inside this block)
  ...dataCommands,
  // #endregion clu:data
  // #region clu:spaces (edit only inside this block)
  ...spacesCommands,
  // #endregion clu:spaces
  // #region clu:engine (edit only inside this block)
  ...engineCommands,
  // #endregion clu:engine
  // #region clu:capabilities (edit only inside this block)
  ...capabilitiesCommands,
  // #endregion clu:capabilities
  // #region clu:extensions (edit only inside this block)
  ...extensionsCommands,
  // #endregion clu:extensions
  // #region clu:tweaks (edit only inside this block)
  ...tweaksCommands,
  // #endregion clu:tweaks
  // #region clu:tabs (edit only inside this block)
  ...tabsCommands,
  // #endregion clu:tabs
  // #region clu:archive (edit only inside this block)
  ...archiveCommands,
  // #endregion clu:archive
  // #region clu:shell (edit only inside this block)
  ...shellCommands,
  // #endregion clu:shell
  // #region clu:theme (edit only inside this block)
  ...themeCommands,
  // #endregion clu:theme
  // #region clu:command (edit only inside this block)
  ...commandCommands,
  // #endregion clu:command
  // #region clu:split (edit only inside this block)
  ...splitCommands,
  // #endregion clu:split
  // #region clu:multitask (edit only inside this block)
  ...multitaskCommands,
  // #endregion clu:multitask
  // #region clu:quality (edit only inside this block)
  ...qualityCommands
  // #endregion clu:quality
]

const byId = new Map(commandTable.map((e) => [e.id, e]))
export function commandEntry(id: string): CommandEntry | undefined {
  return byId.get(id)
}
export function isCommandId(id: string): boolean {
  return byId.has(id)
}

// ---- chords --------------------------------------------------------------------------------

export interface Chord { code: string; meta: boolean; ctrl: boolean; shift: boolean; alt: boolean }

const NAMED_CODES: Record<string, string> = {
  '[': 'BracketLeft', ']': 'BracketRight', '=': 'Equal', '-': 'Minus', '.': 'Period', ',': 'Comma', '/': 'Slash',
  ';': 'Semicolon', "'": 'Quote', '`': 'Backquote', '\\': 'Backslash',
  Left: 'ArrowLeft', Right: 'ArrowRight', Up: 'ArrowUp', Down: 'ArrowDown',
  Tab: 'Tab', Backspace: 'Backspace', Enter: 'Enter', Escape: 'Escape', Esc: 'Escape', Space: 'Space'
}

/** Parses `Cmd+Shift+]` into a physical key code plus exact modifiers. `Plus` is Shift+=. */
export function parseChord(chord: string): Chord {
  const parts = chord.split('+')
  // `Cmd+=` and `Ctrl+Shift+-` split cleanly; a trailing empty part means the key was '+'.
  let key = parts.pop() ?? ''
  if (key === '' && parts.at(-1) === '') {
    parts.pop()
    key = 'Plus'
  }
  const mods = new Set(parts)
  const c: Chord = { code: '', meta: mods.has('Cmd'), ctrl: mods.has('Ctrl'), shift: mods.has('Shift'), alt: mods.has('Option') || mods.has('Alt') }
  for (const m of mods) {
    if (!['Cmd', 'Ctrl', 'Shift', 'Option', 'Alt'].includes(m)) throw new Error(`unknown modifier ${m} in ${chord}`)
  }
  if (key === 'Plus') {
    c.code = 'Equal'
    c.shift = true
  } else if (/^[A-Z]$/.test(key)) c.code = `Key${key}`
  else if (/^[0-9]$/.test(key)) c.code = `Digit${key}`
  else {
    const named = NAMED_CODES[key]
    if (named === undefined) throw new Error(`unknown key ${key} in ${chord}`)
    c.code = named
  }
  return c
}

export function chordKey(c: Chord): string {
  return `${c.meta ? 'M' : ''}${c.ctrl ? 'C' : ''}${c.alt ? 'A' : ''}${c.shift ? 'S' : ''}:${c.code}`
}

/** Display form for menus and the command bar (`Cmd+Shift+T` gives `⇧⌘T`, macOS order). */
export function formatChord(chord: string): string {
  const c = parseChord(chord)
  const key = chord.split('+').pop() || '+'
  const keyLabel: Record<string, string> = { Left: '←', Right: '→', Up: '↑', Down: '↓', Backspace: '⌫', Tab: '⇥', Enter: '↩', Escape: '⎋', Plus: '+' }
  return `${c.ctrl ? '⌃' : ''}${c.alt ? '⌥' : ''}${c.shift ? '⇧' : ''}${c.meta ? '⌘' : ''}${keyLabel[key] ?? key}`
}

/** One keycap per key, macOS order (⌃⌥⇧⌘ then the key), as tooltips show a shortcut: `Cmd+S` gives ['⌘', 'S']. */
export function chordKeys(chord: string): string[] {
  return Array.from(formatChord(chord))
}

export interface RouterBinding { id: string; chord: Chord; variant: Variant }

/** Every chord the router handles (`route: 'router'` entries plus router-route alts). */
export function routerBindings(table: readonly CommandEntry[] = commandTable): RouterBinding[] {
  const out: RouterBinding[] = []
  for (const e of table) {
    if (e.accelerator && e.route === 'router') out.push({ id: e.id, chord: parseChord(e.accelerator), variant: e.variant })
    for (const alt of e.alt ?? []) {
      const route: Route = alt.route ?? e.route
      if (route === 'router') out.push({ id: e.id, chord: parseChord(alt.keys), variant: e.variant })
    }
  }
  return out
}

export function variantMatches(v: Variant, kind: 'main' | 'mini'): boolean {
  return v === 'both' || v === kind
}
