// The Ask row and Shift+Enter in the bar's state (docs/chromium/parity-command.md).
import { act, renderHook, waitFor } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import type { Json } from '../../../shared/types'
import { barState, callsOf, hist, installBar, SESSION, WIN } from './testing'
import { ASK_KEY, useCommandBar, type KeyLike } from './useCommandBar'

const key = (k: string, mods: Partial<KeyLike> = {}) => ({ key: k, preventDefault: vi.fn(), ...mods })
const typed = { insertedAtEnd: true, composing: false }

function setup(global: Record<string, Json> = {}, history = [hist('https://soup.example/', 'Weeknight soup')]) {
  const stub = installBar({ history, suggest: (q) => [`${q} recipe`, `${q} ideas`] })
  const hook = renderHook(() => useCommandBar(WIN))
  const state = barState()
  state.settings.global = { ...state.settings.global, ...global }
  stub.push(state)
  return { ...stub, state, hook, bar: () => hook.result.current }
}

const askIndex = (rows: readonly { group: string }[]) => rows.findIndex((r) => r.group === 'ask')

describe('useCommandBar: the Ask row', () => {
  it('sits third for a sentence, asks the saved assistant, and Enter on it opens the address', async () => {
    const { bar, calls } = setup({ 'ai.askAssistant': 'claude' })
    act(() => bar().input('weeknight soup with beans', typed))
    await waitFor(() => expect(bar().rows.length).toBeGreaterThan(3))
    expect(askIndex(bar().rows)).toBe(2)
    const row = bar().rows[2]
    expect(row).toMatchObject({ key: ASK_KEY, ask: 'claude', title: 'weeknight soup with beans', result: { kind: 'navigate', url: 'https://claude.ai/new?q=weeknight%20soup%20with%20beans' } })
    act(() => bar().run(2))
    expect(callsOf(calls, 'command.execute').at(-1)).toMatchObject({ sessionId: SESSION, result: { kind: 'navigate', url: 'https://claude.ai/new?q=weeknight%20soup%20with%20beans' }, disposition: 'default' })
  })

  it('takes the last place when fewer rows exist, and never shows for an address', () => {
    const { bar } = setup({}, [])
    act(() => bar().input('zz qq', typed))
    expect(bar().rows.map((r) => r.group)).toEqual(['top', 'ask'])
    act(() => bar().input('example.com', typed))
    expect(askIndex(bar().rows)).toBe(-1)
  })

  it('never shows with the AI tab switch off, Tab or not', () => {
    const off = setup({ 'ai.askRow': false }, [])
    act(() => off.bar().input('zz qq', typed))
    expect(askIndex(off.bar().rows)).toBe(-1)
    act(() => off.bar().keyDown(key('Tab')))
    expect(askIndex(off.bar().rows)).toBe(-1)
  })

  it('shows for one word; Tab jumps to the Ask row; Tab again moves on; Shift+Tab moves back', async () => {
    const { bar } = setup({}, [])
    act(() => bar().input('soup', typed))
    expect(askIndex(bar().rows)).toBe(2)
    act(() => bar().keyDown(key('Tab')))
    expect(bar().rows[bar().active]?.key).toBe(ASK_KEY)
    const at = bar().active
    act(() => bar().keyDown(key('Tab')))
    expect(bar().active).toBe((at + 1) % bar().rows.length)
    act(() => bar().keyDown(key('Tab', { shiftKey: true })))
    expect(bar().active).toBe(at)
  })

  it('a typed assistant name shows the hint; Tab opens its prompt with only the Ask row; Backspace leaves it', () => {
    const { bar, calls } = setup({}, [])
    act(() => bar().input('chat', typed))
    expect(bar().tabAssistant?.id).toBe('chatgpt')
    act(() => bar().keyDown(key('Tab')))
    expect(bar().askMode?.id).toBe('chatgpt')
    expect(bar().text).toBe('')
    expect(bar().rows).toEqual([])
    act(() => bar().input('why?', typed))
    expect(bar().rows).toHaveLength(1)
    expect(bar().rows[0]?.result).toEqual({ kind: 'navigate', url: 'https://chatgpt.com/?q=why%3F' })
    // The prompt's assistant is a one-off: the saved pick does not change.
    expect(callsOf(calls, 'settings.set')).toEqual([])
    act(() => bar().input('', typed))
    const back = key('Backspace')
    act(() => bar().keyDown(back))
    expect(back.preventDefault).toHaveBeenCalled()
    expect(bar().askMode).toBeNull()
  })

  it('Option+Down opens the assistant menu; Up, Down and Enter pick and save; the row follows at once', async () => {
    const { bar, calls } = setup({}, [])
    act(() => bar().input('soup tonight', typed))
    act(() => bar().keyDown(key('Tab')))
    act(() => bar().keyDown(key('ArrowDown', { altKey: true })))
    expect(bar().picker).toEqual({ open: true, index: 1 })
    act(() => bar().keyDown(key('ArrowDown')))
    act(() => bar().keyDown(key('ArrowDown')))
    expect(bar().picker.index).toBe(0)
    act(() => bar().keyDown(key('Enter')))
    expect(bar().picker.open).toBe(false)
    expect(bar().assistant.id).toBe('grok')
    expect(bar().rows[bar().active]).toMatchObject({ key: ASK_KEY, ask: 'grok' })
    expect(callsOf(calls, 'settings.set')).toEqual([{ key: 'ai.askAssistant', scope: 'global', value: 'grok' }])
    // Escape closes only the menu.
    act(() => bar().keyDown(key('ArrowDown', { altKey: true })))
    act(() => bar().keyDown(key('Escape')))
    expect(bar().picker.open).toBe(false)
    expect(bar().closing).toBe(false)
  })
})

describe('useCommandBar: Shift+Enter opens the top result', () => {
  it('uses the typed query, or the selected suggestion, on engines with a first-result address', async () => {
    const { bar, calls } = setup({}, [])
    act(() => bar().input('soup tonight', typed))
    act(() => bar().keyDown(key('Enter', { shiftKey: true })))
    expect(callsOf(calls, 'command.execute').at(-1)).toMatchObject({ result: { kind: 'navigate', url: 'https://www.google.com/search?q=soup+tonight&btnI=1' } })
    await waitFor(() => expect(bar().rows.some((r) => r.group === 'suggestions')).toBe(true))
    const i = bar().rows.findIndex((r) => r.group === 'suggestions')
    act(() => bar().hover(i))
    act(() => bar().keyDown(key('Enter', { shiftKey: true, metaKey: true })))
    expect(callsOf(calls, 'command.execute').at(-1)).toMatchObject({ result: { kind: 'navigate', url: 'https://www.google.com/search?q=soup+tonight+recipe&btnI=1' }, disposition: 'new-tab' })
  })

  it('runs the row as Enter does with the switch off', () => {
    const off = setup({ 'ai.instantLinks': false }, [])
    act(() => off.bar().input('soup tonight', typed))
    act(() => off.bar().keyDown(key('Enter', { shiftKey: true })))
    expect(callsOf(off.calls, 'command.execute').at(-1)).toMatchObject({ result: { kind: 'search', query: 'soup tonight' } })
  })

  it('runs the row as Enter does for an address', () => {
    const url = setup({}, [])
    act(() => url.bar().input('example.com', typed))
    act(() => url.bar().keyDown(key('Enter', { shiftKey: true })))
    expect(callsOf(url.calls, 'command.execute').at(-1)).toMatchObject({ result: { kind: 'navigate', url: 'https://example.com/' } })
  })
})

describe('useCommandBar: Up and Down show the selected row in the input', () => {
  it('shows a search row\'s query, keeps the typed text on the Ask row and the first row, and typing goes on from it', async () => {
    const { bar } = setup({}, [])
    act(() => bar().input('soup tonight', typed))
    await waitFor(() => expect(bar().rows.some((r) => r.group === 'suggestions')).toBe(true))
    const groups = bar().rows.map((r) => r.group)
    expect(groups.slice(0, 3)).toEqual(['top', 'suggestions', 'ask'])
    act(() => bar().keyDown(key('ArrowDown')))
    expect(bar().text).toBe('soup tonight recipe')
    act(() => bar().keyDown(key('ArrowDown')))
    expect(bar().active).toBe(2)
    expect(bar().text).toBe('soup tonight')
    act(() => bar().keyDown(key('ArrowUp')))
    act(() => bar().keyDown(key('ArrowUp')))
    expect(bar().active).toBe(0)
    expect(bar().text).toBe('soup tonight')
    act(() => bar().keyDown(key('ArrowDown')))
    act(() => bar().input('soup tonight recipes', typed))
    expect(bar().text).toBe('soup tonight recipes')
    expect(bar().active).toBe(0)
  })
})
