import { render } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { SPACE_SYMBOLS, type SpaceIcon } from '../../shared/types'
import { hasGlyph, searchSymbols, SPACE_SYMBOL_PATHS, SpaceIconGlyph, symbolLabel } from './spaceSymbols'

describe('space symbol glyphs', () => {
  it('draws every symbol in SPACE_SYMBOLS and nothing else', () => {
    expect(Object.keys(SPACE_SYMBOL_PATHS).sort()).toEqual([...SPACE_SYMBOLS].sort())
    for (const s of SPACE_SYMBOLS) {
      expect(SPACE_SYMBOL_PATHS[s].length).toBeGreaterThan(0)
      for (const d of SPACE_SYMBOL_PATHS[s]) expect(d).toMatch(/^M[-\d.]/)
    }
  })
  it('labels and searches by name', () => {
    expect(symbolLabel('planet')).toBe('Planet')
    expect(searchSymbols('')).toHaveLength(SPACE_SYMBOLS.length)
    expect(searchSymbols(' PL ')).toEqual(['planet', 'plane'])
    expect(searchSymbols('zzz')).toEqual([])
  })
  it('renders an emoji, a symbol, and nothing for the letter or an unknown symbol', () => {
    const { container, rerender } = render(<SpaceIconGlyph icon={{ kind: 'emoji', value: '💼' }} className="g" />)
    expect(container.querySelector('.g')).toHaveTextContent('💼')
    expect(container.querySelector('.g')).toHaveAttribute('data-icon', 'emoji')
    rerender(<SpaceIconGlyph icon={{ kind: 'symbol', value: 'planet' }} className="g" />)
    expect(container.querySelector('.g')).toHaveAttribute('data-icon', 'symbol')
    const svg = container.querySelector('svg[data-symbol="planet"]')
    expect(svg).toHaveAttribute('stroke', 'currentColor')
    expect(svg?.querySelectorAll('path')).toHaveLength(SPACE_SYMBOL_PATHS.planet.length)
    rerender(<SpaceIconGlyph icon={{ kind: 'letter' }} className="g" />)
    expect(container.innerHTML).toBe('')
    rerender(<SpaceIconGlyph icon={{ kind: 'symbol', value: 'nope' } as unknown as SpaceIcon} className="g" />)
    expect(container.innerHTML).toBe('')
    rerender(<SpaceIconGlyph icon={undefined} />)
    expect(container.innerHTML).toBe('')
  })
  it('hasGlyph is true for an emoji and a bundled symbol only', () => {
    expect(hasGlyph({ kind: 'emoji', value: '💼' })).toBe(true)
    expect(hasGlyph({ kind: 'emoji', value: '' })).toBe(false)
    expect(hasGlyph({ kind: 'symbol', value: 'moon' })).toBe(true)
    expect(hasGlyph({ kind: 'letter' })).toBe(false)
    expect(hasGlyph(undefined)).toBe(false)
  })
})
