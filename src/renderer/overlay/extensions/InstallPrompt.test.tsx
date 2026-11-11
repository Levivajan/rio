import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultWindowState, type OverlaySurfaceState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { InstallPrompt } from './InstallPrompt'

const WIN = '00000000-0000-4000-8000-000000000001'
const REQ = '00000000-0000-4000-8000-000000000002'
const instance: OverlaySurfaceState = { instanceId: REQ, surface: 'install-prompt', layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null, props: {} }

function mount(prompt: Partial<NonNullable<ReturnType<typeof defaultWindowState>['installPrompt']>> | null) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  stateMirror()
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.installPrompt = prompt ? { requestId: REQ, name: 'Thing', iconUrl: null, permissions: [], warnUnsupported: false, ...prompt } : null
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
  render(<InstallPrompt instance={instance} windowId={WIN} />)
  return stub
}

describe('InstallPrompt', () => {
  it('renders the name, icon and permissions, and Cancel has first focus', () => {
    mount({ iconUrl: 'data:image/png;base64,AA==', permissions: ['Read your browsing history'] })
    expect(screen.getByRole('dialog', { name: 'Add “Thing” to Rio?' })).toBeInTheDocument()
    expect(screen.getByText('Read your browsing history')).toBeInTheDocument()
    expect(document.querySelector('img')?.getAttribute('src')).toBe('data:image/png;base64,AA==')
    expect(screen.getByRole('button', { name: 'Cancel' })).toHaveFocus()
    expect(screen.queryByRole('note')).toBeNull()
  })

  it('warns about APIs Rio cannot provide', () => {
    mount({ warnUnsupported: true })
    expect(screen.getByRole('note')).toHaveTextContent('Some features of this extension may not work in Rio.')
  })

  it('answers with Add, Cancel and Esc', () => {
    const stub = mount({})
    fireEvent.click(screen.getByRole('button', { name: 'Add' }))
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }))
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' })
    expect(stub.calls.filter((c) => c.name === 'extensions.respondInstall').map((c) => c.payload)).toEqual([
      { requestId: REQ, allow: true },
      { requestId: REQ, allow: false },
      { requestId: REQ, allow: false }
    ])
  })

  it('renders nothing without a prompt', () => {
    mount(null)
    expect(screen.queryByRole('dialog')).toBeNull()
  })
})
