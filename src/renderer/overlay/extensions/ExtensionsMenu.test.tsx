import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultWindowState, type OverlaySurfaceState } from '../../../shared/state'
import type { ActionButton, ActionRowState } from '../../../shared/state/extensions'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { ExtensionsMenu, STORE_URL } from './ExtensionsMenu'

const WIN = '00000000-0000-4000-8000-000000000001'
const PROFILE = '00000000-0000-4000-8000-000000000002'
const INST = '77777777-7777-4777-8777-777777777777'
const A = 'a'.repeat(32)
const B = 'b'.repeat(32)
const ANCHOR = { x: 200, y: 40, width: 24, height: 24 }
const instance: OverlaySurfaceState = { instanceId: INST, surface: 'extensions-menu', layer: 'top', modality: 'light-dismiss', rect: ANCHOR, openedFrom: null, focus: true, slot: null, props: {} }
const action = (over: Partial<ActionButton> = {}): ActionButton => ({ id: A, name: 'Notes', title: 'Notes', icon: null, badge: '', enabled: true, pinned: true, ...over })

function mount(row: ActionRowState | null, isPrivate = false) {
  const stub = stubClu({ 'extensions.runAction': () => null, 'extensions.setPinned': () => null, 'overlay.close': () => null, 'commands.run': () => null, 'tabs.create': () => ({ tabId: WIN }) })
  window.clu = { ...stub.clu, windowId: WIN }
  stateMirror()
  const win = { ...defaultWindowState({ id: WIN, kind: 'main', private: isPrivate, activeSpaceId: WIN }), actionRow: row }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
  render(<ExtensionsMenu instance={instance} windowId={WIN} />)
  return stub
}

describe('ExtensionsMenu', () => {
  it('lists every extension, pinned or not', () => {
    mount({ profileId: PROFILE, actions: [action(), action({ id: B, name: 'Vault', pinned: false })] })
    expect(screen.getByText('Notes')).toBeTruthy()
    expect(screen.getByText('Vault')).toBeTruthy()
    expect(screen.getByRole('button', { name: 'Unpin Notes' }).getAttribute('aria-pressed')).toBe('true')
    expect(screen.getByRole('button', { name: 'Pin Vault' }).getAttribute('aria-pressed')).toBe('false')
  })

  it('pins and unpins through the browser', () => {
    const stub = mount({ profileId: PROFILE, actions: [action({ pinned: false })] })
    fireEvent.click(screen.getByRole('button', { name: 'Pin Notes' }))
    expect(stub.calls.find((c) => c.name === 'extensions.setPinned')?.payload).toEqual({ id: A, pinned: true })
  })

  it('has no pin buttons in a private window', () => {
    mount({ profileId: PROFILE, actions: [action(), action({ id: B, name: 'Vault', pinned: false })] }, true)
    expect(screen.getByText('Notes')).toBeTruthy()
    expect(screen.queryByRole('button', { name: 'Unpin Notes' })).toBeNull()
    expect(screen.queryByRole('button', { name: 'Pin Vault' })).toBeNull()
  })

  it('opens an extension from its row, anchored to the menu button, and closes', () => {
    const stub = mount({ profileId: PROFILE, actions: [action()] })
    fireEvent.click(screen.getByText('Notes'))
    expect(stub.calls.find((c) => c.name === 'extensions.runAction')?.payload).toEqual({ id: A, anchor: ANCHOR })
    expect(stub.calls.find((c) => c.name === 'overlay.close')?.payload).toEqual({ instanceId: INST })
  })

  it('does not open an extension that does nothing on this page', () => {
    mount({ profileId: PROFILE, actions: [action({ enabled: false })] })
    expect((screen.getByText('Notes').closest('button') as HTMLButtonElement).disabled).toBe(true)
  })

  it('links to the extensions page and the store', () => {
    const stub = mount({ profileId: PROFILE, actions: [] })
    expect(screen.getByText('No extensions yet')).toBeTruthy()
    fireEvent.click(screen.getByText('Manage extensions'))
    expect(stub.calls.find((c) => c.name === 'commands.run')?.payload).toEqual({ id: 'extensions.open' })
    fireEvent.click(screen.getByText('Get extensions'))
    expect(stub.calls.find((c) => c.name === 'tabs.create')?.payload).toEqual({ url: STORE_URL })
  })

  it('closes itself when the row goes away', () => {
    const stub = mount(null)
    expect(stub.calls.find((c) => c.name === 'overlay.close')?.payload).toEqual({ instanceId: INST })
  })

  it('closes on Esc', () => {
    const stub = mount({ profileId: PROFILE, actions: [action()] })
    fireEvent.keyDown(window, { key: 'Escape' })
    expect(stub.calls.filter((c) => c.name === 'overlay.close')).toHaveLength(1)
  })
})
