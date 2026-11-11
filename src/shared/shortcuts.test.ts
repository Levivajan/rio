import { describe, expect, it } from 'vitest'
import { chordKey, commandTable, parseChord, routerBindings, type CommandEntry } from './commands'
import { readSetting } from './settings'
import { chordFromKey, chordOwner, customShortcuts, effectiveTable, isAssignableChord, isCustomShortcuts, isReservedChord, keysOf } from './shortcuts'

// The cases chromium/clu/browser/commands/clu_shortcuts_unittest.cc checks too, on the same small table.
const TABLE: CommandEntry[] = [
  { id: 'a.one', title: 'One', accelerator: 'Cmd+K', alt: [{ keys: 'Cmd+Shift+K' }], menu: 'File', variant: 'main', route: 'router', palette: true },
  { id: 'a.two', title: 'Two', accelerator: 'Cmd+J', menu: 'File', variant: 'main', route: 'menu', palette: true },
  { id: 'a.mini', title: 'Mini', accelerator: 'Cmd+O', menu: 'File', variant: 'mini', route: 'router', palette: false },
  { id: 'a.both', title: 'Both', accelerator: 'Cmd+L', menu: 'File', variant: 'both', route: 'router', palette: false },
  { id: 'a.none', title: 'None', menu: 'File', variant: 'main', route: 'menu', palette: true }
]
const keys = (custom: Record<string, string | null>) => Object.fromEntries(effectiveTable(custom, TABLE).map((e) => [e.id, keysOf(e)]))

describe('effectiveTable', () => {
  it('is the table itself without custom keys', () => {
    expect(keys({})).toEqual({ 'a.one': ['Cmd+K', 'Cmd+Shift+K'], 'a.two': ['Cmd+J'], 'a.mini': ['Cmd+O'], 'a.both': ['Cmd+L'], 'a.none': [] })
  })
  it('replaces a chord and its alternates, and null leaves a command no key', () => {
    expect(keys({ 'a.one': 'Ctrl+Shift+=', 'a.two': null })).toMatchObject({ 'a.one': ['Ctrl+Shift+='], 'a.two': [] })
  })
  it('gives a chord a custom entry took away from the command that had it, where both can run', () => {
    expect(keys({ 'a.none': 'Cmd+J' })).toMatchObject({ 'a.none': ['Cmd+J'], 'a.two': [] })
    expect(keys({ 'a.none': 'Cmd+Shift+K' })).toMatchObject({ 'a.none': ['Cmd+Shift+K'], 'a.one': ['Cmd+K'] })
    // A main command and a mini one never meet, so both keep Cmd+O.
    expect(keys({ 'a.none': 'Cmd+O' })).toMatchObject({ 'a.none': ['Cmd+O'], 'a.mini': ['Cmd+O'] })
    // A both command meets either.
    expect(keys({ 'a.mini': 'Cmd+L' })).toMatchObject({ 'a.mini': ['Cmd+L'], 'a.both': [] })
  })
  it('keeps the first of two custom entries on one chord, in table order', () => {
    expect(keys({ 'a.none': 'Cmd+Y', 'a.two': 'Cmd+Y' })).toMatchObject({ 'a.two': ['Cmd+Y'], 'a.none': [] })
  })
  it('ignores unknown ids and chords it cannot use, keeping the default', () => {
    expect(keys({ 'gone.command': 'Cmd+K', 'a.two': 'Cmd+Q', 'a.one': 'K' })).toMatchObject({ 'a.one': ['Cmd+K', 'Cmd+Shift+K'], 'a.two': ['Cmd+J'] })
  })
  it('matches modifiers in any order and Plus as Shift+=', () => {
    expect(keys({ 'a.none': 'Shift+Cmd+K' })).toMatchObject({ 'a.one': ['Cmd+K'] })
    expect(keys({ 'a.none': 'Ctrl+Plus', 'a.two': 'Ctrl+Shift+=' })).toMatchObject({ 'a.two': ['Ctrl+Shift+='], 'a.none': [] })
  })
  it('feeds the router: a moved router chord answers under its new key only', () => {
    const ids = (custom: Record<string, string | null>) => routerBindings(effectiveTable(custom, TABLE)).map((b) => `${b.id}:${chordKey(b.chord)}`)
    expect(ids({ 'a.one': 'Cmd+Option+K' })).toEqual(['a.one:MA:KeyK', 'a.mini:M:KeyO', 'a.both:M:KeyL'])
  })
})

describe('the real key table', () => {
  it('has no chord two commands that can meet both answer to', () => {
    const seen = new Map<string, CommandEntry>()
    const meet = (a: CommandEntry, b: CommandEntry) => a.variant === 'both' || b.variant === 'both' || a.variant === b.variant
    const clashes: string[] = []
    for (const e of commandTable) {
      for (const k of keysOf(e)) {
        const key = `${chordKey(parseChord(k))}`
        for (const [other, o] of seen) if (other.startsWith(`${key}|`) && o.id !== e.id && meet(o, e)) clashes.push(`${k}: ${o.id} and ${e.id}`)
        seen.set(`${key}|${e.id}`, e)
      }
    }
    expect(clashes).toEqual([])
  })
  it('stays as it is with an empty map', () => {
    expect(effectiveTable({})).toEqual(commandTable.map((e) => ({ ...e })))
  })
})

describe('validation', () => {
  it('accepts chords with Cmd, Ctrl or Option that macOS does not keep', () => {
    for (const c of ['Cmd+K', 'Ctrl+Shift+=', 'Option+Left', 'Cmd+Option+1', 'Ctrl+`', 'Cmd+Plus']) expect(isAssignableChord(c), c).toBe(true)
    for (const c of ['K', 'Shift+K', 'Cmd+Q', 'Cmd+Tab', 'Cmd+C', 'Cmd+Shift+Z', 'Cmd+F13', 'Hyper+K', '', 'Cmd+'.padEnd(40, 'K'), 42, null]) expect(isAssignableChord(c), String(c)).toBe(false)
    expect(isReservedChord('Cmd+H')).toBe(true)
    expect(isReservedChord('Cmd+K')).toBe(false)
  })
  it('takes an object of ids to chords or null, at most 256 entries', () => {
    expect(isCustomShortcuts({})).toBe(true)
    expect(isCustomShortcuts({ 'tabs.close': null, 'nav.back': 'Cmd+[' , 'unknown.id': 'Cmd+K' })).toBe(true)
    expect(isCustomShortcuts({ 'tabs.close': 'Cmd+Q' })).toBe(false)
    expect(isCustomShortcuts({ 'tabs.close': 3 })).toBe(false)
    expect(isCustomShortcuts({ '': 'Cmd+K' })).toBe(false)
    expect(isCustomShortcuts({ ['x'.repeat(65)]: 'Cmd+K' })).toBe(false)
    expect(isCustomShortcuts([])).toBe(false)
    expect(isCustomShortcuts(null)).toBe(false)
    expect(isCustomShortcuts(Object.fromEntries(Array.from({ length: 257 }, (_, i) => [`c.${i}`, null])))).toBe(false)
  })
  it('reads junk as no custom keys', () => {
    expect(readSetting('shortcuts.custom', 'junk')).toEqual({})
    expect(readSetting('shortcuts.custom', { 'tabs.close': 'Cmd+Q' })).toEqual({})
    expect(customShortcuts(undefined)).toEqual({})
    expect(customShortcuts({ 'tabs.close': 'Cmd+E' })).toEqual({ 'tabs.close': 'Cmd+E' })
  })
})

describe('chordOwner', () => {
  it('names the command that already answers to a chord where this one could run', () => {
    const t = effectiveTable({}, TABLE)
    expect(chordOwner('Cmd+J', 'a.none', t)?.id).toBe('a.two')
    expect(chordOwner('Cmd+Shift+K', 'a.none', t)?.id).toBe('a.one')
    expect(chordOwner('Cmd+O', 'a.none', t)).toBeUndefined()
    expect(chordOwner('Cmd+J', 'a.two', t)).toBeUndefined()
    expect(chordOwner('nonsense', 'a.two', t)).toBeUndefined()
  })
})

describe('chordFromKey', () => {
  const k = (code: string, mods: Partial<Record<'metaKey' | 'ctrlKey' | 'altKey' | 'shiftKey', boolean>> = {}) =>
    chordFromKey({ code, metaKey: false, ctrlKey: false, altKey: false, shiftKey: false, ...mods })
  it('names a key press in the table syntax', () => {
    expect(k('KeyK', { metaKey: true, shiftKey: true })).toBe('Cmd+Shift+K')
    expect(k('Equal', { ctrlKey: true, shiftKey: true })).toBe('Ctrl+Shift+=')
    expect(k('Digit3', { altKey: true, metaKey: true })).toBe('Cmd+Option+3')
    expect(k('ArrowLeft', { metaKey: true })).toBe('Cmd+Left')
    expect(k('Backquote', { ctrlKey: true })).toBe('Ctrl+`')
  })
  it('gives null for a lone modifier or a key the table has no name for', () => {
    expect(k('MetaLeft', { metaKey: true })).toBeNull()
    expect(k('F13', { metaKey: true })).toBeNull()
    expect(k('IntlBackslash', { metaKey: true })).toBeNull()
  })
  it('round-trips through parseChord', () => {
    expect(chordKey(parseChord(k('Slash', { metaKey: true, altKey: true }) ?? ''))).toBe('MA:Slash')
  })
})
