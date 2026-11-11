import { act, renderHook, waitFor } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { barState, callsOf, hist, installBar, PROFILE, SESSION, tabId, WIN } from './testing'
import { defaultSpaceState } from '../../../shared/state'
import { useCommandBar, type KeyLike } from './useCommandBar'
import { CONTACT_URL } from '../../../shared/command/rank'

const key = (k: string, mods: Partial<KeyLike> = {}) => ({ key: k, preventDefault: vi.fn(), ...mods })
const typed = { insertedAtEnd: true, composing: false }

// The Ask row has its own tests (useCommandBar.ask.test.ts); these count the other rows without it.
const noAsk = (state = barState()) => {
  state.settings.global = { ...state.settings.global, 'ai.askRow': false }
  return state
}

function setup(answers: Parameters<typeof installBar>[0] = {}, state = barState()) {
  const stub = installBar(answers)
  const hook = renderHook(() => useCommandBar(WIN))
  stub.push(state)
  return { ...stub, hook, bar: () => hook.result.current }
}

const FOO = [hist('https://foo.example/one', 'Foo one'), hist('https://foo.example/two', 'Foo two'), hist('https://foo.example/three', 'Foo three')]

describe('useCommandBar', () => {
  it('wraps the selection: Down past the last row selects the first, Up from the first the last', async () => {
    const { bar } = setup({ history: FOO })
    act(() => bar().input('foo', typed))
    await waitFor(() => expect(bar().rows.filter((r) => r.group === 'history')).toHaveLength(3))
    const n = bar().rows.length
    expect(bar().active).toBe(0)
    for (let i = 1; i < n; i++) act(() => bar().keyDown(key('ArrowDown')))
    expect(bar().active).toBe(n - 1)
    const down = key('ArrowDown')
    act(() => bar().keyDown(down))
    expect(down.preventDefault).toHaveBeenCalled()
    expect(bar().active).toBe(0)
    act(() => bar().keyDown(key('ArrowUp')))
    expect(bar().active).toBe(n - 1)
    act(() => bar().keyDown(key('n', { ctrlKey: true })))
    expect(bar().active).toBe(0)
  })

  it('Backspace on an inline completion removes only the completion', async () => {
    const { bar } = setup({ history: [hist('https://github.com/', 'GitHub', { typedCount: 1 })] })
    act(() => bar().input('git', typed))
    await waitFor(() => expect(bar().text).toBe('github.com'))
    expect(bar().completionRange).toEqual([3, 10])
    expect(bar().rows[0]?.result).toMatchObject({ kind: 'history', url: 'https://github.com/' })

    const backspace = key('Backspace')
    act(() => bar().keyDown(backspace))
    expect(backspace.preventDefault).toHaveBeenCalled()
    expect(bar().text).toBe('git')
    expect(bar().completionRange).toBeNull()
    // With no completion left, Backspace is the browser's own and deletes a typed character.
    const again = key('Backspace')
    act(() => bar().keyDown(again))
    expect(again.preventDefault).not.toHaveBeenCalled()
  })

  it('never completes during IME composition or after an edit that is not typing at the end', async () => {
    const { bar } = setup({ history: [hist('https://github.com/', 'GitHub', { typedCount: 1 })] })
    act(() => bar().input('gi', typed))
    await waitFor(() => expect(bar().text).toBe('github.com'))
    act(() => bar().input('git', { insertedAtEnd: true, composing: true }))
    expect(bar().text).toBe('git')
    expect(bar().completionRange).toBeNull()
    act(() => bar().input('gith', { insertedAtEnd: false, composing: false }))
    expect(bar().text).toBe('gith')
  })

  it('Tab and Right accept the completion, so the input keeps the completed text', async () => {
    const { bar } = setup({ history: [hist('https://github.com/', 'GitHub', { typedCount: 1 })] })
    act(() => bar().input('git', typed))
    await waitFor(() => expect(bar().text).toBe('github.com'))
    act(() => bar().keyDown(key('Tab')))
    expect(bar().text).toBe('github.com')
    expect(bar().completionRange).toBeNull()
  })

  it('late suggestions append and keep the selected result key', async () => {
    const { bar } = setup({ history: FOO }, noAsk())
    // Suggestions answer only when the test releases them, after the user has moved the selection.
    let release = () => {}
    const gate = new Promise<void>((r) => (release = r))
    const asked: string[] = []
    Object.assign(window.clu.command, {
      suggest: async (p: { requestId: number; query: string }) => {
        asked.push(p.query)
        await gate
        return { ok: true, value: { requestId: p.requestId, items: ['foo fighters', 'foo bar'] } }
      }
    })
    act(() => bar().input('foo', typed))
    await waitFor(() => expect(bar().rows.filter((r) => r.group === 'history')).toHaveLength(3))
    await waitFor(() => expect(asked).toEqual(['foo']))
    act(() => bar().keyDown(key('ArrowDown')))
    act(() => bar().keyDown(key('ArrowDown')))
    const selected = bar().rows[bar().active]?.key
    const before = bar().active
    expect(selected).toMatch(/^history:/)
    await act(async () => release())
    // The search row and three history rows leave one of the five rows for a suggestion.
    await waitFor(() => expect(bar().rows.filter((r) => r.group === 'suggestions')).toHaveLength(1))
    expect(bar().rows).toHaveLength(5)
    expect(bar().rows[bar().active]?.key).toBe(selected)
    expect(bar().active).toBe(before)
  })

  it('a deleted history row stops completing inline and leaves a top row for Enter', async () => {
    const { bar, calls } = setup({ history: [hist('https://github.com/', 'GitHub', { typedCount: 1 })] })
    act(() => bar().input('git', typed))
    await waitFor(() => expect(bar().text).toBe('github.com'))
    expect(bar().rows[0]?.historyUrl).toBe('https://github.com/')
    act(() => bar().keyDown(key('Backspace', { metaKey: true, altKey: true })))
    expect(callsOf(calls, 'command.deleteEntry')).toEqual([{ url: 'https://github.com/' }])
    expect(bar().text).toBe('git')
    expect(bar().completionRange).toBeNull()
    expect(bar().rows[0]?.result).toMatchObject({ kind: 'search', query: 'git' })
    act(() => bar().input('gith', typed))
    expect(bar().text).toBe('gith')
  })

  it('keeps the last suggestions listed while the next answer is pending, and drops them for an empty input', async () => {
    const { bar } = setup()
    const gates: (() => void)[] = []
    Object.assign(window.clu.command, {
      suggest: async (p: { requestId: number; query: string }) => {
        await new Promise<void>((r) => gates.push(r))
        return { ok: true, value: { requestId: p.requestId, items: [`${p.query} one`, `${p.query} two`] } }
      }
    })
    const suggested = () => bar().rows.filter((r) => r.group === 'suggestions').map((r) => r.title)
    act(() => bar().input('foo', typed))
    await waitFor(() => expect(gates).toHaveLength(1))
    await act(async () => gates[0]?.())
    await waitFor(() => expect(suggested()).toEqual(['foo one', 'foo two']))
    act(() => bar().input('foob', typed))
    expect(suggested()).toEqual(['foo one', 'foo two'])
    await waitFor(() => expect(gates).toHaveLength(2))
    await act(async () => gates[1]?.())
    await waitFor(() => expect(suggested()).toEqual(['foob one', 'foob two']))
    act(() => bar().input('', { insertedAtEnd: false, composing: false }))
    expect(suggested()).toEqual([])
  })

  it('Cmd+Option+Backspace deletes the selected history row and drops it from the list', async () => {
    const { bar, calls } = setup({ history: FOO }, noAsk())
    act(() => bar().input('foo', typed))
    await waitFor(() => expect(bar().rows.filter((r) => r.group === 'history')).toHaveLength(3))
    act(() => bar().keyDown(key('ArrowDown')))
    const row = bar().rows[bar().active]
    const next = bar().rows[bar().active + 1]
    expect(row?.historyUrl).toBeTruthy()
    const e = key('Backspace', { metaKey: true, altKey: true })
    act(() => bar().keyDown(e))
    expect(e.preventDefault).toHaveBeenCalled()
    expect(callsOf(calls, 'command.deleteEntry')).toEqual([{ url: row?.historyUrl }])
    expect(bar().rows.map((r) => r.key)).not.toContain(row?.key)
    expect(bar().rows[bar().active]?.key).toBe(next?.key)
    // The top search row has no history entry: nothing is deleted.
    act(() => bar().keyDown(key('ArrowUp')))
    act(() => bar().keyDown(key('Backspace', { metaKey: true, altKey: true })))
    expect(callsOf(calls, 'command.deleteEntry')).toHaveLength(1)
  })

  it('a history row main refuses to delete (the history deletion policy) comes back', async () => {
    const { bar } = setup({ history: FOO })
    Object.assign(window.clu.command, {
      deleteEntry: async () => ({ ok: false, error: { code: 'not-allowed', message: 'history deletion is off' } })
    })
    act(() => bar().input('foo', typed))
    await waitFor(() => expect(bar().rows.filter((r) => r.group === 'history')).toHaveLength(3))
    act(() => bar().keyDown(key('ArrowDown')))
    const row = bar().rows[bar().active]
    expect(row?.historyUrl).toBeTruthy()
    act(() => bar().keyDown(key('Backspace', { metaKey: true, altKey: true })))
    await waitFor(() => expect(bar().rows.map((r) => r.key)).toContain(row?.key))
    expect(bar().rows.filter((r) => r.group === 'history')).toHaveLength(3)
  })

  it('Enter runs the selected row; Cmd+Enter opens it in a new tab and Option+Enter in a split', async () => {
    const { bar, calls } = setup()
    act(() => bar().input('rust book', typed))
    act(() => bar().keyDown(key('Enter')))
    act(() => bar().keyDown(key('Enter', { metaKey: true })))
    act(() => bar().keyDown(key('Enter', { altKey: true })))
    const runs = callsOf(calls, 'command.execute') as { disposition: string; result: unknown; input: string }[]
    expect(runs.map((r) => r.disposition)).toEqual(['default', 'new-tab', 'split'])
    expect(runs[0]).toMatchObject({ sessionId: SESSION, input: 'rust book', result: { kind: 'search', query: 'rust book' } })
  })

  it('Tab on an empty input shows only actions; Backspace on the empty filter leaves it', async () => {
    const { bar, calls } = setup(
      { actions: (q) => [{ id: 'sidebar.toggle', title: 'Toggle Sidebar', accelerator: 'Cmd+S' }, { id: 'x', title: q || 'Use Bing', providerId: 'engine', token: 'bing' }] },
      barState({ tabs: [{ n: 1, url: 'https://a.example/', title: 'A' }, { n: 2, url: 'https://b.example/', title: 'B' }], activeTab: 2 })
    )
    expect(bar().rows.map((r) => r.group)).toEqual(['recent', 'help'])
    act(() => bar().keyDown(key('Tab')))
    expect(bar().actionsOnly).toBe(true)
    await waitFor(() => expect(bar().rows.map((r) => r.group)).toEqual(['actions', 'actions']))
    expect(bar().rows[0]).toMatchObject({ result: { kind: 'command', id: 'sidebar.toggle' }, accelerator: 'Cmd+S' })
    expect(bar().rows[1]?.result).toEqual({ kind: 'provided', providerId: 'engine', token: 'bing' })
    expect(callsOf(calls, 'command.actions').at(-1)).toEqual({ sessionId: SESSION, query: '' })
    act(() => bar().keyDown(key('Backspace')))
    expect(bar().actionsOnly).toBe(false)
    await waitFor(() => expect(bar().rows.map((r) => r.group)).toEqual(['recent', 'help']))
  })

  it('an action whose title is the query goes first and Enter runs it; other actions sit after the tabs', async () => {
    const { bar, calls } = setup(
      {
        actions: (q) =>
          q === 'toggle sidebar'
            ? [{ id: 'sidebar.toggle', title: 'Toggle Sidebar', accelerator: 'Cmd+S', exact: true }]
            : [{ id: 'sidebar.toggle', title: 'Toggle Sidebar', accelerator: 'Cmd+S' }]
      },
      barState({ tabs: [{ n: 1, url: 'https://a.example/', title: 'Untoggled list' }] })
    )
    act(() => bar().input('toggle', typed))
    await waitFor(() => expect(bar().rows.map((r) => r.group)).toEqual(['top', 'tabs', 'ask', 'actions']))
    expect(bar().rows[0]?.result.kind).toBe('search')
    act(() => bar().input('toggle sidebar', typed))
    await waitFor(() => expect(bar().rows[0]?.result).toEqual({ kind: 'command', id: 'sidebar.toggle' }))
    // The Ask row follows the action and the search.
    expect(bar().rows.map((r) => r.group)).toEqual(['top', 'top', 'ask'])
    expect(bar().rows[1]?.result.kind).toBe('search')
    act(() => bar().keyDown(key('Enter')))
    expect(callsOf(calls, 'command.execute').at(-1)).toMatchObject({ result: { kind: 'command', id: 'sidebar.toggle' }, disposition: 'default' })
  })

  it('asks main for the actions once per session and matches them on every keystroke, with the table aliases', async () => {
    const { bar, calls } = setup({
      // Main sends palette rows as { id, title }; the chord and the aliases come from the overlay's table.
      actions: () => [
        { id: 'theme.editTheme', title: 'Edit Theme…' },
        { id: 'spaces', providerId: 'spaces', token: 'x', title: 'Switch to Work' }
      ]
    })
    act(() => bar().input('change space colors', typed))
    await waitFor(() => expect(bar().rows.some((r) => r.result.kind === 'command')).toBe(true))
    act(() => bar().input('switch to w', typed))
    await waitFor(() => expect(bar().rows.some((r) => r.result.kind === 'provided')).toBe(true))
    expect(bar().rows.some((r) => r.result.kind === 'command')).toBe(false)
    act(() => bar().input('t', typed))
    expect(bar().rows.some((r) => r.group === 'actions')).toBe(false)
    expect(callsOf(calls, 'command.actions')).toEqual([{ sessionId: SESSION, query: '' }])
  })

  it('Switch to and Move tab to rows carry the named space, so they draw its symbol or emoji', async () => {
    const state = noAsk(barState())
    const WORK = '00000000-0000-4000-8000-0000000000a2'
    const READ = '00000000-0000-4000-8000-0000000000a3'
    state.spaces[WORK] = defaultSpaceState({ id: WORK, name: 'Work', icon: { kind: 'symbol', value: 'rocket' }, color: 'blue', profileId: PROFILE, ephemeral: false })
    state.spaces[READ] = defaultSpaceState({ id: READ, name: 'Read', icon: { kind: 'emoji', value: '📚' }, color: 'blue', profileId: PROFILE, ephemeral: false })
    const { bar } = setup(
      {
        actions: () => [
          { id: 'spaces', providerId: 'spaces', token: WORK, title: 'Switch to Work' },
          { id: 'spaces', providerId: 'spaces', token: READ, title: 'Switch to Read' },
          { id: 'tabs.moveTab', providerId: 'tabs.moveTab', token: WORK, title: 'Move tab to Work' },
          // A space that left the state before the bar listed it, and an engine row: no space icon.
          { id: 'spaces', providerId: 'spaces', token: 'gone', title: 'Switch to Gone' },
          { id: 'x', providerId: 'engine', token: 'work', title: 'Set search engine: Work' }
        ]
      },
      state
    )
    act(() => bar().keyDown(key('Tab')))
    await waitFor(() => expect(bar().rows).toHaveLength(5))
    const byTitle = (t: string) => bar().rows.find((r) => r.title === t)
    expect(byTitle('Switch to Work')?.space).toEqual({ name: 'Work', icon: { kind: 'symbol', value: 'rocket' } })
    expect(byTitle('Switch to Read')?.space).toEqual({ name: 'Read', icon: { kind: 'emoji', value: '📚' } })
    expect(byTitle('Move tab to Work')?.space?.icon).toEqual({ kind: 'symbol', value: 'rocket' })
    expect(byTitle('Switch to Gone')?.space).toBeUndefined()
    expect(byTitle('Set search engine: Work')?.space).toBeUndefined()
  })

  it('keeps generic actions out of short or loose queries: "wi" lists no New Window, "new w" does', async () => {
    const { bar, calls } = setup(
      { actions: () => [{ id: 'window.new', title: 'New Window' }, { id: 'window.newPrivate', title: 'New Private Window' }] },
      barState({ tabs: [{ n: 1, url: 'https://wiki.example/', title: 'Wiki' }] })
    )
    await waitFor(() => expect(callsOf(calls, 'command.actions').length).toBeGreaterThan(0))
    act(() => bar().input('wi', typed))
    expect(bar().rows.some((r) => r.group === 'actions')).toBe(false)
    act(() => bar().input('wind', typed))
    await waitFor(() => expect(bar().rows.filter((r) => r.group === 'actions')).toHaveLength(2))
    act(() => bar().input('new w', typed))
    await waitFor(() => expect(bar().rows.filter((r) => r.group === 'actions').map((r) => r.title)).toEqual(['New Window']))
  })

  it('contextual rows: Move to Today first for a pinned tab, then the search, Open and Pin to; Open and Pin to run in the overlay', async () => {
    const state = barState({ tabs: [{ n: 1, url: 'https://apple.example/', title: 'Apple' }], activeTab: 1 })
    const space = Object.keys(state.spaces)[0] as string
    const tab = state.tabs[tabId(1)]
    const home = state.spaces[space]
    if (!tab || !home) throw new Error('fixture')
    state.tabs[tabId(1)] = { ...tab, section: 'pinned' }
    state.spaces[space] = { ...home, name: 'Rio test', today: [] }
    const folder = 'ffffffff-ffff-4fff-8fff-ffffffffffff'
    state.folders[folder] = { id: folder, spaceId: space, name: 'Rio folder', icon: null, collapsed: true, children: [] }
    const { bar, calls } = setup({}, noAsk(state))
    act(() => bar().input('rio', typed))
    await waitFor(() => expect(bar().rows.map((r) => r.context?.verb ?? r.result.kind).slice(0, 4)).toEqual(['Move to Today in', 'search', 'Open', 'Pin to']))
    expect(bar().rows[0]).toMatchObject({ result: { kind: 'command', id: 'tabs.togglePin' }, accelerator: 'Cmd+D' })
    act(() => bar().keyDown(key('ArrowDown')))
    act(() => bar().keyDown(key('ArrowDown')))
    expect(bar().text).toBe('Open Rio folder')
    act(() => bar().keyDown(key('Enter')))
    expect(callsOf(calls, 'folders.setCollapsed')).toEqual([{ folderId: folder, collapsed: false }])
    expect(callsOf(calls, 'command.execute')).toEqual([])
    expect(bar().closing).toBe(true)
  })

  it('labels search rows with the query alone, whatever engine the session names', async () => {
    const state = barState()
    const w = state.windows[WIN]
    if (w?.command) w.command = { ...w.command, engineName: 'Fixture' }
    const { bar } = setup({ suggest: (q) => [`${q} one`] }, state)
    act(() => bar().input('rust book', typed))
    expect(bar().rows[0]?.title).toBe('rust book')
    await waitFor(() => expect(bar().rows.find((r) => r.group === 'suggestions')?.title).toBe('rust book one'))
  })

  it('Esc plays the close animation first and closes the session once', async () => {
    const { bar, calls } = setup()
    act(() => bar().keyDown(key('Escape')))
    expect(bar().closing).toBe(true)
    act(() => bar().keyDown(key('Enter')))
    expect(callsOf(calls, 'command.execute')).toEqual([])
    act(() => bar().finishClose())
    act(() => bar().finishClose())
    expect(callsOf(calls, 'command.close')).toEqual([{ sessionId: SESSION }])
  })

  it('an edit-url session starts with the URL and offers the current tab, then Contact the Team', () => {
    const { bar } = setup({}, barState({ mode: 'edit-url', tabs: [{ n: 1, url: 'https://a.example/x', title: 'A' }], activeTab: 1, targetTab: 1, initialText: 'https://a.example/x' }))
    expect(bar().text).toBe('https://a.example/x')
    expect(bar().rows.map((r) => r.result)).toEqual([{ kind: 'reload', tabId: tabId(1) }, { kind: 'navigate', url: CONTACT_URL }])
    expect(bar().rows[0]?.title).toBe('A')
  })
})
