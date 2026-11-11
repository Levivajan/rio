import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { OverlaySurfaceState } from '../../../shared/state'
import { defaultState, defaultTabState, defaultWindowState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { AuthDialog } from './AuthDialog'
import { ClientCertPrompt } from './ClientCertPrompt'
import { ContextMenu } from './ContextMenu'
import { FindBar } from './FindBar'
import { FullscreenNotice } from './FullscreenNotice'
import { LeaveSitePrompt } from './LeaveSitePrompt'
import { ZoomControls } from './ZoomControls'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-000000000002'
let rev = 0

function setup(tab: { zoomFactor?: number; find?: { text: string; active: number; total: number } }) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  const state = { ...defaultState(), rev: 1 }
  state.windows[WIN] = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  state.tabs[TAB] = { ...defaultTabState({ id: TAB, spaceId: WIN, profileId: 'p', url: 'http://a.test/', section: 'today' }), ...tab }
  return { stub, push: () => act(() => stateMirror().receive({ kind: 'snapshot', rev: ++rev, state: { ...state, rev } })) }
}
const lastCall = (stub: ReturnType<typeof stubClu>) => stub.calls.at(-1)

const barInstance: OverlaySurfaceState = { instanceId: 'i', surface: 'find-bar', layer: 'passive', modality: 'passive', rect: null, openedFrom: null, focus: true, slot: 0, props: { pageId: TAB, select: 1 } }

describe('ZoomControls', () => {
  it('renders minus, percent, plus and reset, and steps the tab it was given', () => {
    const { stub, push } = setup({ zoomFactor: 1.5 })
    push()
    render(<ZoomControls tabId={TAB} />)
    expect(screen.getByRole('status')).toHaveTextContent('150%')
    fireEvent.click(screen.getByRole('button', { name: 'Zoom out' }))
    expect(lastCall(stub)).toMatchObject({ name: 'zoom.step', payload: { tabId: TAB, dir: -1 } })
    fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }))
    expect(lastCall(stub)).toMatchObject({ name: 'zoom.step', payload: { tabId: TAB, dir: 1 } })
    fireEvent.click(screen.getByRole('button', { name: 'Reset' }))
    expect(lastCall(stub)).toMatchObject({ name: 'zoom.step', payload: { tabId: TAB, dir: 0 } })
  })

  it('disables the button at each end of the list', () => {
    const { push } = setup({ zoomFactor: 5 })
    push()
    render(<ZoomControls tabId={TAB} />)
    expect(screen.getByRole('button', { name: 'Zoom in' })).toBeDisabled()
    expect(screen.getByRole('button', { name: 'Zoom out' })).toBeEnabled()
  })
})

describe('FindBar', () => {
  const open = (find = { text: 'alpha', active: 2, total: 3 }) => {
    const s = setup({ find })
    s.push()
    render(<FindBar instance={barInstance} windowId={WIN} />)
    return s
  }

  it('shows the count and starts from the page text, selected', () => {
    open()
    const input = screen.getByRole<HTMLInputElement>('textbox', { name: 'Find in page' })
    expect(input.value).toBe('alpha')
    expect(input.selectionEnd).toBe(5)
    expect(screen.getByRole('status')).toHaveTextContent('2 of 3')
  })

  it('types through find.update and maps Enter, Shift+Enter and Esc', () => {
    const { stub } = open()
    const input = screen.getByRole('textbox', { name: 'Find in page' })
    fireEvent.change(input, { target: { value: 'beta' } })
    expect(lastCall(stub)).toMatchObject({ name: 'find.update', payload: { tabId: TAB, text: 'beta' } })
    fireEvent.keyDown(input, { key: 'Enter' })
    expect(lastCall(stub)).toMatchObject({ name: 'find.step', payload: { tabId: TAB, forward: true } })
    fireEvent.keyDown(input, { key: 'Enter', shiftKey: true })
    expect(lastCall(stub)).toMatchObject({ name: 'find.step', payload: { tabId: TAB, forward: false } })
    fireEvent.keyDown(input, { key: 'Escape' })
    expect(lastCall(stub)).toMatchObject({ name: 'find.close', payload: { tabId: TAB } })
  })

  it('shows no count for empty text and disables stepping with no matches', () => {
    open({ text: '', active: 0, total: 0 })
    expect(screen.getByRole('status')).toHaveTextContent('')
    expect(screen.getByRole('button', { name: 'Next match' })).toBeDisabled()
  })
})

describe('modal prompts', () => {
  const PROMPT = '00000000-0000-4000-8000-000000000009'
  const instance = (surface: string, props: Record<string, unknown>): OverlaySurfaceState =>
    ({ instanceId: 'i', surface, layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null, props: { promptId: PROMPT, ...props } }) as OverlaySurfaceState
  const start = () => {
    const stub = stubClu()
    window.clu = { ...stub.clu, windowId: WIN }
    return stub
  }
  const auth = (over: Record<string, unknown> = {}) => instance('auth-dialog', { origin: 'https://a.test', realm: '', proxy: false, insecure: false, wrong: false, ...over })

  it('AuthDialog sends what was typed on submit, by Sign In or Enter, and starts on the username', () => {
    const stub = start()
    render(<AuthDialog instance={auth()} windowId={WIN} />)
    expect(screen.getByRole('dialog', { name: 'Sign in' })).toBeInTheDocument()
    expect(screen.getByLabelText('Username')).toHaveFocus()
    expect(screen.getByLabelText('Password')).toHaveAttribute('type', 'password')
    expect(screen.queryByRole('alert')).toBeNull()
    fireEvent.change(screen.getByLabelText('Username'), { target: { value: 'ada' } })
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: 'pw' } })
    fireEvent.click(screen.getByRole('button', { name: 'Sign In' }))
    expect(lastCall(stub)).toMatchObject({ name: 'prompt.respond', payload: { promptId: PROMPT, action: 'sign-in', values: { username: 'ada', password: 'pw' } } })
  })

  it('AuthDialog cancels by button and by Esc, and shows no values', () => {
    const stub = start()
    render(<AuthDialog instance={auth({ realm: 'Area', insecure: true, wrong: true })} windowId={WIN} />)
    expect(screen.getByRole('alert')).toHaveTextContent('Wrong username or password')
    expect(screen.getByText('Your connection to this site is not private.')).toBeInTheDocument()
    expect(screen.getByText('It calls this area “Area”.')).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }))
    expect(lastCall(stub)).toEqual(expect.objectContaining({ name: 'prompt.respond', payload: { promptId: PROMPT, action: 'cancel' } }))
    stub.calls.length = 0
    fireEvent.keyDown(screen.getByLabelText('Username'), { key: 'Escape' })
    expect(lastCall(stub)).toMatchObject({ name: 'prompt.respond', payload: { action: 'cancel' } })
  })

  it('AuthDialog names a proxy', () => {
    start()
    render(<AuthDialog instance={auth({ origin: 'proxy.test:8080', proxy: true })} windowId={WIN} />)
    expect(screen.getByText('The proxy proxy.test:8080 asks for a username and password.')).toBeInTheDocument()
  })

  it('ClientCertPrompt starts with nothing selected, and focus or Continue choose nothing', () => {
    const stub = start()
    render(<ClientCertPrompt instance={instance('client-cert-prompt', { origin: 'https://a.test', items: ['Ada', 'Grace'] })} windowId={WIN} />)
    expect(screen.getByRole('radio', { name: 'Ada' })).toHaveFocus()
    expect(screen.getByRole('radio', { name: 'Ada' })).not.toBeChecked()
    expect(screen.getByRole('radio', { name: 'Grace' })).not.toBeChecked()
    const go = screen.getByRole('button', { name: 'Continue' })
    expect(go).toBeDisabled()
    fireEvent.click(go)
    expect(stub.calls.filter((c) => c.name === 'prompt.respond')).toEqual([])
    fireEvent.click(screen.getByRole('radio', { name: 'Grace' }))
    expect(go).toBeEnabled()
    fireEvent.click(go)
    expect(lastCall(stub)).toMatchObject({ name: 'prompt.respond', payload: { promptId: PROMPT, action: 'select', values: { index: '1' } } })
  })

  it('ClientCertPrompt cancels by button and by Esc', () => {
    const stub = start()
    render(<ClientCertPrompt instance={instance('client-cert-prompt', { origin: 'https://a.test', items: ['Ada'] })} windowId={WIN} />)
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }))
    expect(lastCall(stub)).toMatchObject({ payload: { action: 'cancel' } })
    stub.calls.length = 0
    fireEvent.keyDown(screen.getByRole('radio', { name: 'Ada' }), { key: 'Escape' })
    expect(lastCall(stub)).toMatchObject({ payload: { action: 'cancel' } })
  })

  it('LeaveSitePrompt starts on Stay; Stay, Esc and Leave answer', () => {
    const stub = start()
    render(<LeaveSitePrompt instance={instance('leave-prompt', { origin: 'https://a.test' })} windowId={WIN} />)
    expect(screen.getByRole('dialog', { name: 'Leave site?' })).toBeInTheDocument()
    const stay = screen.getByRole('button', { name: 'Stay' })
    expect(stay).toHaveFocus()
    fireEvent.click(stay)
    expect(lastCall(stub)).toMatchObject({ name: 'prompt.respond', payload: { promptId: PROMPT, action: 'stay' } })
    fireEvent.click(screen.getByRole('button', { name: 'Leave' }))
    expect(lastCall(stub)).toMatchObject({ payload: { action: 'leave' } })
    stub.calls.length = 0
    fireEvent.keyDown(stay, { key: 'Escape' })
    expect(lastCall(stub)).toMatchObject({ payload: { action: 'stay' } })
  })
})

describe('FullscreenNotice', () => {
  const notice = (host: string): OverlaySurfaceState => ({ instanceId: 'n', surface: 'fullscreen-notice', layer: 'passive', modality: 'passive', rect: null, openedFrom: null, focus: false, slot: 0, props: { pageId: TAB, host } })

  it('names the host, and the page when it has none', () => {
    const { rerender } = render(<FullscreenNotice instance={notice('example.com')} windowId={WIN} />)
    expect(screen.getByRole('status')).toHaveTextContent('example.com is full screen. Press Esc to exit')
    rerender(<FullscreenNotice instance={notice('')} windowId={WIN} />)
    expect(screen.getByRole('status')).toHaveTextContent('This page is full screen. Press Esc to exit')
  })
})

describe('ContextMenu', () => {
  const menu = (x: number, y: number): OverlaySurfaceState => ({ instanceId: 'm', surface: 'page-menu', layer: 'top', modality: 'light-dismiss', rect: null, openedFrom: null, focus: true, slot: null, props: { menuId: 'menu-1', x, y, items: [{ kind: 'item', id: 'link.copy', label: 'Copy Link' }, { kind: 'separator' }, { kind: 'item', id: 'edit.undo', label: 'Undo', enabled: false }] } })

  it('picks by id through menu.invoke and sends no URL', () => {
    const { stub } = setup({})
    render(<ContextMenu instance={menu(10, 20)} windowId={WIN} />)
    fireEvent.click(screen.getByRole('menuitem', { name: 'Undo' }))
    expect(stub.calls).toEqual([])
    fireEvent.click(screen.getByRole('menuitem', { name: 'Copy Link' }))
    expect(lastCall(stub)).toEqual({ name: 'menu.invoke', payload: { menuId: 'menu-1', itemId: 'link.copy' } })
  })

  it('opens at the click, keeps inside the window and Esc closes it', () => {
    const { stub } = setup({})
    const { container, rerender } = render(<ContextMenu instance={menu(10, 20)} windowId={WIN} />)
    const box = container.querySelector('.clu-page-menu') as HTMLElement
    expect(box.style.left).toBe('10px')
    expect(box.style.top).toBe('20px')
    rerender(<ContextMenu instance={menu(window.innerWidth + 500, window.innerHeight + 500)} windowId={WIN} />)
    expect(parseInt(box.style.left)).toBeLessThanOrEqual(window.innerWidth)
    expect(parseInt(box.style.top)).toBeLessThanOrEqual(window.innerHeight)
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'Escape' })
    expect(lastCall(stub)).toEqual({ name: 'overlay.close', payload: { instanceId: 'm' } })
  })
})
