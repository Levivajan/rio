import { describe, expect, it } from 'vitest'
import { chordKey, commandTable, parseChord, routerBindings, type CommandEntry } from './commands'

const BANNED = ['Arc', 'Browser Company', 'Little Arc', 'Boost', 'Boosts', 'Easel', 'Arc Max']
const variants = (e: CommandEntry) => (e.variant === 'both' ? ['main', 'mini'] : [e.variant])

describe('command table', () => {
  it('has unique ids', () => {
    const ids = commandTable.map((e) => e.id)
    expect(new Set(ids).size).toBe(ids.length)
  })
  it('has no duplicate chord per variant (code plus exact modifiers)', () => {
    const seen = new Map<string, string>()
    for (const e of commandTable) {
      const chords = [e.accelerator, ...(e.alt ?? []).map((a) => a.keys)].filter((c): c is string => !!c)
      for (const chord of chords) {
        for (const v of variants(e)) {
          const key = `${v}|${chordKey(parseChord(chord))}`
          expect(seen.get(key), `${chord} in ${v}: ${e.id} vs ${seen.get(key)}`).toBeUndefined()
          seen.set(key, e.id)
        }
      }
    }
  })
  it('gives every router entry a menu item', () => {
    for (const b of routerBindings()) expect(commandTable.find((e) => e.id === b.id)?.menu).toBeTruthy()
  })
  it('contains no banned word in titles (Import from Arc names the browser it imports from)', () => {
    for (const e of commandTable.filter((c) => c.id !== 'settings.importArc')) for (const w of BANNED) expect(new RegExp(`\\b${w}\\b`).test(e.title), `${e.id}: ${e.title}`).toBe(false)
  })
  it.each([
    ['Ctrl+1', 'Ctrl+Shift+1'],
    ['Cmd+]', 'Cmd+Shift+]'],
    ['Cmd+=', 'Ctrl+Shift+=']
  ])('keeps %s and %s apart', (a, b) => {
    expect(chordKey(parseChord(a))).not.toBe(chordKey(parseChord(b)))
  })
  it('parses punctuation by code', () => {
    expect(parseChord('Cmd+Shift+[')).toEqual({ code: 'BracketLeft', meta: true, ctrl: false, shift: true, alt: false })
    expect(parseChord('Cmd+Plus')).toMatchObject({ code: 'Equal', shift: true })
    expect(() => parseChord('Hyper+X')).toThrow()
  })
})
