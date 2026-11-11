import { render } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { SpaceIcon } from '../../../shared/types'
import { RowIcon } from './ResultRow'
import type { BarRow } from './useCommandBar'

const spaceRow = (icon: SpaceIcon, name = 'Work'): BarRow => ({
  key: `provided:spaces:${name}`,
  group: 'actions',
  result: { kind: 'provided', providerId: 'spaces', token: name },
  title: `Switch to ${name}`,
  url: '',
  titleRanges: [],
  urlRanges: [],
  space: { name, icon }
})

const icon = (row: BarRow) => render(<RowIcon row={row} favicon={null} onFaviconError={() => {}} />).container

describe('RowIcon', () => {
  // Switch to <space> drew the generic action glyph for every space.
  it("draws a Switch to row's space symbol, emoji or letter instead of the action glyph", () => {
    const symbol = icon(spaceRow({ kind: 'symbol', value: 'rocket' }))
    expect(symbol.querySelector('[data-icon="symbol"] svg[data-symbol="rocket"]')).not.toBeNull()

    const emoji = icon(spaceRow({ kind: 'emoji', value: '📚' }, 'Read'))
    expect(emoji.querySelector('[data-icon="emoji"]')?.textContent).toBe('📚')

    const letter = icon(spaceRow({ kind: 'letter' }, 'Home'))
    expect(letter.textContent).toBe('H')
    expect(letter.querySelector('svg[data-symbol]')).toBeNull()
  })

  it('keeps the action glyph for provided rows that name no space', () => {
    const { space: _, ...engine } = spaceRow({ kind: 'symbol', value: 'rocket' })
    const plain = icon({ ...engine, result: { kind: 'provided', providerId: 'engine', token: 'bing' } })
    expect(plain.querySelector('svg[data-symbol]')).toBeNull()
    expect(plain.querySelector('svg rect')).not.toBeNull()
  })
})
