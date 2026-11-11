import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { OverlaySurfaceState } from '../../../shared/state'
import { CommandBar, groups } from './CommandBar'
import { barState, callsOf, hist, installBar, WIN, type StubAnswers, type TabSpec } from './testing'

const instance = { instanceId: '00000000-0000-4000-8000-0000000000d1', surface: 'command', layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null, props: {} } as unknown as OverlaySurfaceState
const SHA = 'a'.repeat(64)

function setup(tabs: TabSpec[], history = [hist('https://docs.example/guide', 'Docs guide')], activeTab?: number, actions?: StubAnswers['actions']) {
  const stub = installBar({ history, ...(actions ? { actions } : {}) })
  const view = render(<CommandBar instance={instance} windowId={WIN} />)
  const state = barState({ tabs, ...(activeTab ? { activeTab } : {}) })
  // The Ask row has its own tests; these count the other rows without it.
  state.settings.global = { ...state.settings.global, 'ai.askRow': false }
  stub.push(state)
  return { ...stub, view, input: () => screen.getByRole('combobox') }
}

const type = (input: HTMLElement, value: string, init: InputEventInit = {}) =>
  fireEvent.input(input, { target: { value }, inputType: 'insertText', data: value.slice(-1), ...init })

describe('CommandBar', () => {
  it('renders group headers, highlight ranges, switch badges and the letter-tile fallback', async () => {
    const { input } = setup([
      { n: 1, url: 'https://docs.example/a', title: 'Docs home', favicon: `clu://favicon/${SHA}` },
      { n: 2, url: 'https://docs.example/b', title: 'Docs split', splitId: 's1' },
      { n: 3, url: 'https://docs.example/c', title: 'Docs junk icon', favicon: 'javascript:alert(1)' }
    ])
    type(input(), 'doc')
    await waitFor(() => expect(screen.getByRole('group', { name: 'Tabs' })).toBeInTheDocument())
    const tabsGroup = screen.getByRole('group', { name: 'Tabs' })
    const tabRows = within(tabsGroup).getAllByRole('option')
    expect(tabRows).toHaveLength(3)
    const home = tabRows.find((r) => r.textContent?.includes('Docs home'))
    const split = tabRows.find((r) => r.textContent?.includes('Docs split'))
    const junk = tabRows.find((r) => r.textContent?.includes('Docs junk icon'))
    expect(home).toHaveTextContent('Switch to Tab')
    expect(split).toHaveTextContent('Switch to Split')
    // Bold runs are the matched ranges of the query.
    expect(home?.querySelector('.clu-cmd-title strong')).toHaveTextContent('Doc')
    expect(home?.querySelector('img')).toHaveAttribute('src', `clu://favicon/${SHA}`)
    // A favicon outside Clu's store, a history row and a favicon that fails to load all get the letter tile.
    expect(junk?.querySelector('img')).toBeNull()
    expect(junk?.querySelector('.clu-letter-tile')).toHaveTextContent('D')
    fireEvent.error(home?.querySelector('img') as HTMLImageElement)
    expect(home?.querySelector('img')).toBeNull()
    expect(home?.querySelector('.clu-letter-tile')).not.toBeNull()
    // The top row has no header; the combobox points at the selected option.
    const options = screen.getAllByRole('option')
    expect(options[0]).toHaveAttribute('data-group', 'top')
    expect(options[0]).toHaveAttribute('aria-selected', 'true')
    expect(input()).toHaveAttribute('aria-activedescendant', options[0]?.id)
    expect(input()).toHaveAttribute('aria-controls', screen.getByRole('listbox').id)
    // The five rows were the completion, the search and the tabs; a narrower query reaches the history row.
    expect(options).toHaveLength(5)
    type(input(), 'guide')
    await waitFor(() => expect(screen.getByRole('group', { name: 'History' })).toBeInTheDocument())
    const historyRow = within(screen.getByRole('group', { name: 'History' })).getByRole('option')
    expect(historyRow.querySelector('.clu-letter-tile')).toHaveTextContent('D')
    expect(historyRow.querySelector('[data-remove]')).not.toBeNull()
  })

  it('the field icon follows the selected row once something is typed: favicon, then the magnifier', async () => {
    const { input, view } = setup([{ n: 1, url: 'https://docs.example/a', title: 'Docs home', favicon: `clu://favicon/${SHA}` }])
    const icon = () => view.container.querySelector('.clu-cmd-field-icon') as HTMLElement
    expect(icon().querySelector('circle')).not.toBeNull()
    type(input(), 'docs h', { inputType: 'insertFromPaste' })
    await waitFor(() => expect(icon()).toHaveAttribute('data-follows', 'tab'))
    expect(icon().querySelector('img')).toHaveAttribute('src', `clu://favicon/${SHA}`)
    fireEvent.keyDown(input(), { key: 'ArrowDown' })
    expect(icon()).toHaveAttribute('data-follows', 'search')
    expect(icon().querySelector('circle')).not.toBeNull()
  })

  it('renders titles and URLs as text nodes only', async () => {
    const evil = '<img src=x onerror="window.pwned=1"><b>bold</b>'
    const { input, view } = setup([{ n: 1, url: 'https://evil.example/', title: evil }], [hist('https://evil.example/<script>alert(1)</script>', '<script>alert(2)</script> evil')])
    type(input(), 'evil')
    await waitFor(() => expect(screen.getByRole('group', { name: 'History' })).toBeInTheDocument())
    expect(view.container.querySelector('script, b')).toBeNull()
    expect(view.container.querySelectorAll('img')).toHaveLength(0)
    expect(screen.getByText(/onerror="window.pwned=1"/, { exact: false })).toBeInTheDocument()
    expect(view.container.textContent).toContain('<script>alert(2)</script> evil')
  })

  it('completes inline only for typing at the end, never inside an IME composition', async () => {
    const { input } = setup([], [hist('https://github.com/', 'GitHub', { typedCount: 1 })])
    await act(async () => {})
    fireEvent.compositionStart(input())
    type(input(), 'git', { isComposing: true })
    fireEvent.compositionEnd(input())
    expect(input()).toHaveValue('git')
    // A paste of several characters at the end is not typing.
    type(input(), 'gi', { inputType: 'insertFromPaste', data: 'gi' })
    expect(input()).toHaveValue('gi')
    // Typing one character at the end completes and selects the added text.
    type(input(), 'git')
    await waitFor(() => expect(input()).toHaveValue('github.com'))
    const el = input() as HTMLInputElement
    expect([el.selectionStart, el.selectionEnd]).toEqual([3, 10])
    fireEvent.keyDown(input(), { key: 'Backspace' })
    expect(input()).toHaveValue('git')
  })

  it('shows Recent tabs for an empty input and the actions-only chip after Tab', async () => {
    const { input } = setup([{ n: 1, url: 'https://a.example/', title: 'Alpha' }, { n: 2, url: 'https://b.example/', title: 'Beta' }], [], 2)
    expect(screen.getByRole('group', { name: 'Recent tabs' })).toHaveTextContent('Alpha')
    fireEvent.keyDown(input(), { key: 'Tab' })
    expect(screen.getByText('Actions')).toHaveClass('clu-cmd-chip')
    expect(input()).toHaveAccessibleName('Search actions')
  })

  it('renders provider rows with their titles and subtitles, and palette rows with their shortcut', async () => {
    const actions = () => [
      { id: 'sidebar.toggle', title: 'Toggle Sidebar', accelerator: 'Cmd+S' },
      { id: 'search.engine', providerId: 'search.engine', token: 'google', title: 'Set search engine: Google', subtitle: 'Current' },
      { id: 'search.engine', providerId: 'search.engine', token: 'duckduckgo', title: 'Set search engine: DuckDuckGo' }
    ]
    const { input } = setup([], [], undefined, actions)
    fireEvent.keyDown(input(), { key: 'Tab' })
    const group = await screen.findByRole('group', { name: 'Actions' })
    await waitFor(() => expect(within(group).getAllByRole('option')).toHaveLength(3))
    const rows = within(group).getAllByRole('option')
    expect(rows.map((r) => r.getAttribute('data-kind'))).toEqual(['command', 'provided', 'provided'])
    expect(rows[0]).toHaveTextContent('Toggle Sidebar')
    expect(rows[0]).toHaveTextContent('⌘S')
    expect(rows[1]).toHaveTextContent('Set search engine: Google')
    expect(rows[1]).toHaveTextContent('Current')
    expect(rows[2]).toHaveTextContent('Set search engine: DuckDuckGo')
  })

  it('reports cmd:open:painted once per session and a cmd:keystroke measure per typed change', async () => {
    // Paint marks still queued by earlier tests' bars land before this test's stub is installed.
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))
    const { input, calls, push } = setup([])
    const painted = () => callsOf(calls, 'perf.mark').filter((p) => (p as { name: string }).name === 'cmd:open:painted')
    await waitFor(() => expect(painted()).toHaveLength(1))
    // A mode switch keeps the session: no second open mark.
    push({ ...barState({ mode: 'edit-url', initialText: 'https://a.example/' }), rev: 2 })
    await act(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(() => requestAnimationFrame(r)))))
    expect(painted()).toHaveLength(1)
    // A new session after a close is a new open.
    const next = barState()
    const command = next.windows[WIN]?.command
    if (command) command.sessionId = '00000000-0000-4000-8000-0000000000c2'
    push({ ...next, rev: 3 })
    await waitFor(() => expect(painted()).toHaveLength(2))
    expect(painted()[1]).toMatchObject({ at: expect.any(Number) })

    performance.clearMeasures('cmd:keystroke')
    type(input(), 'd')
    await waitFor(() => expect(performance.getEntriesByName('cmd:keystroke', 'measure')).toHaveLength(1))
    expect(performance.getEntriesByName('cmd:input', 'mark')).toHaveLength(0)
  })
  it('dims only the page card behind the bar, at its rect and radius, and not in the Mini window', async () => {
    const stub = installBar()
    render(<CommandBar instance={instance} windowId={WIN} />)
    const withPage = (mode?: 'mini') => {
      const s = barState(mode ? { mode } : {})
      const w = s.windows[WIN]
      if (w) s.windows[WIN] = { ...w, shell: { ...w.shell, layout: { ...w.shell.layout, pageRect: { x: 214, y: 10, width: 800, height: 600 }, radius: 10 } } }
      return s
    }
    stub.push(withPage())
    const dim = await waitFor(() => {
      const el = document.querySelector<HTMLElement>('[data-command-dim]')
      expect(el).not.toBeNull()
      return el as HTMLElement
    })
    expect(dim.style.left).toBe('214px')
    expect(dim.style.top).toBe('10px')
    expect(dim.style.width).toBe('800px')
    expect(dim.style.height).toBe('600px')
    expect(dim.style.borderRadius).toBe('10px')
    // The dim sits in the scrim, so a click on the dimmed page still closes the bar.
    expect(dim.parentElement).toHaveClass('clu-cmd-scrim')
    stub.push(withPage('mini'))
    await waitFor(() => expect(document.querySelector('[data-command-dim]')).toBeNull())
  })
})

// The Ask row sits third, inside a run of tabs: the two runs of one group get distinct keys (a repeated React key
// left a stale row on screen).
describe('groups', () => {
  it('keys a second run of a group apart', () => {
    const g = (group: string) => ({ group }) as { group: 'top' | 'tabs' | 'ask' }
    expect(groups([g('top'), g('tabs'), g('ask'), g('tabs'), g('tabs')])).toEqual([
      { group: 'top', key: 'top', start: 0, end: 1 },
      { group: 'tabs', key: 'tabs', start: 1, end: 2 },
      { group: 'ask', key: 'ask', start: 2, end: 3 },
      { group: 'tabs', key: 'tabs-2', start: 3, end: 5 }
    ])
  })
})
