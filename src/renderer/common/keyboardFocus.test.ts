import { afterEach, describe, expect, it } from 'vitest'
import { trackKeyboardFocus } from './keyboardFocus'

let stop: (() => void) | null = null
afterEach(() => {
  stop?.()
  stop = null
})

const mark = () => document.documentElement.dataset.focusKeys

describe('trackKeyboardFocus', () => {
  it('marks the root after a key and clears it on a pointer press', () => {
    stop = trackKeyboardFocus()
    expect(mark()).toBeUndefined()
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowDown', bubbles: true }))
    expect(mark()).toBe('true')
    document.dispatchEvent(new Event('pointerdown', { bubbles: true }))
    expect(mark()).toBeUndefined()
  })

  it('ignores a lone modifier and a shortcut chord', () => {
    stop = trackKeyboardFocus()
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Shift', bubbles: true }))
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 't', metaKey: true, bubbles: true }))
    expect(mark()).toBeUndefined()
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', bubbles: true }))
    expect(mark()).toBe('true')
  })

  it('stops listening and clears the mark when disposed', () => {
    stop = trackKeyboardFocus()
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', bubbles: true }))
    stop()
    stop = null
    expect(mark()).toBeUndefined()
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', bubbles: true }))
    expect(mark()).toBeUndefined()
  })
})
