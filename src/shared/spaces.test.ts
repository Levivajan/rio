import { describe, expect, it } from 'vitest'
import { deleteSpaceText, isSpaceColor, MAX_NAME, MAX_NAME_UNITS, nextDefaultName, normalizeName, parseEmoji, spaceLetter } from './spaces'

const graphemeCount = (s: string) => Array.from(new Intl.Segmenter(undefined, { granularity: 'grapheme' }).segment(s)).length

describe('normalizeName', () => {
  it('strips control characters and collapses whitespace', () => {
    expect(normalizeName('Wo\u0000rk\u0007')).toBe('Work')
    expect(normalizeName('  Side\t\n  project \r\n')).toBe('Side project')
    expect(normalizeName('a \u0000 b')).toBe('a b')
    expect(normalizeName('‮evilName')).toBe('evilName')
    expect(normalizeName('C1\u0085\u009fchars')).toBe('C1chars')
  })
  it('returns null for empty, whitespace-only and non-string input', () => {
    expect(normalizeName('')).toBeNull()
    expect(normalizeName('   \t\n ')).toBeNull()
    expect(normalizeName('\u0000\u0001\u001f')).toBeNull()
    expect(normalizeName(undefined)).toBeNull()
    expect(normalizeName(42)).toBeNull()
  })
  it('cuts a 10,000-character name to 60 graphemes', () => {
    const out = normalizeName('x'.repeat(10_000))
    expect(out).toBe('x'.repeat(MAX_NAME))
  })
  it('counts emoji as graphemes, never splitting one', () => {
    const family = '👩‍👩‍👧‍👦'
    const out = normalizeName(family.repeat(100)) ?? ''
    // 11 UTF-16 units each: the unit cap (240) stops at 21 whole families, below the 60-grapheme cap.
    expect(out).toBe(family.repeat(Math.floor(MAX_NAME_UNITS / family.length)))
    const flags = normalizeName('🇦🇲'.repeat(70)) ?? ''
    expect(graphemeCount(flags)).toBe(MAX_NAME)
    expect(flags).toBe('🇦🇲'.repeat(MAX_NAME))
    expect(normalizeName('Work 💼')).toBe('Work 💼')
  })
  it('never ends on a trailing space after the cut', () => {
    expect(normalizeName(`${'a'.repeat(59)} b`)).toBe('a'.repeat(59))
  })
})

describe('parseEmoji', () => {
  it('accepts single emoji, a ZWJ sequence and a flag pair', () => {
    expect(parseEmoji('💼')).toBe('💼')
    expect(parseEmoji('👩‍💻')).toBe('👩‍💻')
    expect(parseEmoji('🇦🇲')).toBe('🇦🇲')
    expect(parseEmoji('👍🏽')).toBe('👍🏽')
    expect(parseEmoji('❤️')).toBe('❤️')
    expect(parseEmoji(' 🚀 ')).toBe('🚀')
  })
  it('rejects text, markup, empty input and more than one emoji', () => {
    expect(parseEmoji('ab')).toBeNull()
    expect(parseEmoji('a')).toBeNull()
    expect(parseEmoji('<script>')).toBeNull()
    expect(parseEmoji('')).toBeNull()
    expect(parseEmoji('   ')).toBeNull()
    expect(parseEmoji('💼🚀')).toBeNull()
    expect(parseEmoji('🇦🇲🇦🇲')).toBeNull()
    expect(parseEmoji('🇦')).toBeNull()
    expect(parseEmoji('💼a')).toBeNull()
    expect(parseEmoji('1')).toBeNull()
    expect(parseEmoji(null)).toBeNull()
    expect(parseEmoji('👩‍💻'.repeat(3))).toBeNull()
  })
})

describe('helpers', () => {
  it('isSpaceColor knows the palette only', () => {
    expect(isSpaceColor('teal')).toBe(true)
    expect(isSpaceColor('chartreuse')).toBe(false)
    expect(isSpaceColor(1)).toBe(false)
  })
  it('nextDefaultName skips taken names', () => {
    expect(nextDefaultName('Space', 2, ['Home'])).toBe('Space 2')
    expect(nextDefaultName('Space', 2, ['Home', 'Space 2', 'Space 3'])).toBe('Space 4')
  })
  it('spaceLetter takes the first grapheme of the name', () => {
    expect(spaceLetter('work')).toBe('W')
    expect(spaceLetter('  émile')).toBe('É')
    expect(spaceLetter('👩‍💻 Code')).toBe('👩‍💻')
    expect(spaceLetter('')).toBe('?')
  })
})

describe('deleteSpaceText', () => {
  it('names the space and counts pinned and Today tabs with plurals', () => {
    expect(deleteSpaceText('Work', 2, 3)).toEqual({ title: 'Delete your “Work” Space?', body: 'Its 2 pinned tabs close and its 3 Today tabs move to Archive.' })
    expect(deleteSpaceText('Work', 1, 0).body).toBe('Its 1 pinned tab closes.')
    expect(deleteSpaceText('Work', 0, 1).body).toBe('Its 1 Today tab moves to Archive.')
    expect(deleteSpaceText('Work', 0, 0).body).toBe('This space has no tabs.')
    expect(deleteSpaceText('Work', Number.NaN, -4).body).toBe('This space has no tabs.')
  })
})
