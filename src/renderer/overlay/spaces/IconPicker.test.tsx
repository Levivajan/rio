import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { SPACE_SYMBOLS } from '../../../shared/types'
import { sameIcon } from './editorModel'
import { IconPicker } from './IconPicker'

describe('the space icon picker', () => {
  it('opens on Emoji with the letter and the emoji grid, and switches to Symbols', () => {
    const onPick = vi.fn()
    render(<IconPicker icon={{ kind: 'letter' }} name="Work" color="teal" onPick={onPick} />)
    const emojiTab = screen.getByRole('tab', { name: 'Emoji' })
    expect(emojiTab).toHaveAttribute('aria-selected', 'true')
    expect(screen.getByRole('button', { name: 'Letter' })).toHaveAttribute('aria-pressed', 'true')
    fireEvent.click(screen.getByRole('tab', { name: 'Symbols' }))
    expect(screen.getByRole('tab', { name: 'Symbols' })).toHaveAttribute('aria-selected', 'true')
    expect(screen.getByRole('tabpanel')).toHaveAttribute('aria-labelledby', 'clu-editor-tab-symbols')
    const grid = screen.getByRole('group', { name: 'Symbols' })
    expect(grid.querySelectorAll('button')).toHaveLength(SPACE_SYMBOLS.length)
    fireEvent.click(screen.getByRole('button', { name: 'Planet' }))
    expect(onPick).toHaveBeenCalledWith({ kind: 'symbol', value: 'planet' })
  })
  it('opens on Symbols for a space with a symbol and marks it', () => {
    render(<IconPicker icon={{ kind: 'symbol', value: 'moon' }} name="Night" color="indigo" onPick={() => {}} />)
    expect(screen.getByRole('tab', { name: 'Symbols' })).toHaveAttribute('aria-selected', 'true')
    expect(screen.getByRole('button', { name: 'Moon' })).toHaveAttribute('aria-pressed', 'true')
    expect(screen.getByRole('button', { name: 'Sun' })).toHaveAttribute('aria-pressed', 'false')
  })
  it('searches symbols by name and says when nothing matches', () => {
    render(<IconPicker icon={{ kind: 'letter' }} name="W" color="teal" onPick={() => {}} />)
    fireEvent.click(screen.getByRole('tab', { name: 'Symbols' }))
    const search = screen.getByRole('textbox', { name: 'Search symbols' })
    fireEvent.change(search, { target: { value: 'pl' } })
    expect(screen.getAllByRole('button', { pressed: false }).map((b) => b.getAttribute('aria-label'))).toEqual(['Planet', 'Plane'])
    fireEvent.change(search, { target: { value: 'qqq' } })
    expect(screen.queryByRole('group', { name: 'Symbols' })).toBeNull()
    expect(screen.getByText('No symbols match')).toBeInTheDocument()
  })
  it('compares symbol icons by value', () => {
    expect(sameIcon({ kind: 'symbol', value: 'moon' }, { kind: 'symbol', value: 'moon' })).toBe(true)
    expect(sameIcon({ kind: 'symbol', value: 'moon' }, { kind: 'symbol', value: 'sun' })).toBe(false)
    expect(sameIcon({ kind: 'symbol', value: 'moon' }, { kind: 'letter' })).toBe(false)
    expect(sameIcon({ kind: 'emoji', value: '🌙' }, { kind: 'symbol', value: 'moon' })).toBe(false)
  })
})
