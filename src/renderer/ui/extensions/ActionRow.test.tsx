import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultWindowState } from '../../../shared/state'
import type { ActionButton, ActionRowState } from '../../../shared/state/extensions'
import { stubClu } from '../../../test-utils/stubClu'
import { ActionRow, actionLabel } from './ActionRow'

const WIN = '00000000-0000-4000-8000-000000000001'
const PROFILE = '00000000-0000-4000-8000-000000000002'
const A = 'a'.repeat(32)
const B = 'b'.repeat(32)
const action = (over: Partial<ActionButton> = {}): ActionButton => ({ id: A, name: 'Notes', title: 'Notes', icon: 'data:image/png;base64,AA==', badge: '', enabled: true, pinned: true, ...over })

function mount(actionRow: ActionRowState | null) {
  const stub = stubClu({ 'extensions.runAction': () => null, 'extensions.openMenu': () => null })
  window.clu = { ...stub.clu, windowId: WIN }
  const view = render(<ActionRow />)
  const push = (row: ActionRowState | null) => {
    const win = { ...defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN }), actionRow: row }
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
  }
  push(actionRow)
  return { ...view, push, stub }
}

describe('ActionRow', () => {
  it('is absent when the profile has no extension', () => {
    const { container } = mount(null)
    expect(container.querySelector('[data-action-row]')).toBeNull()
  })

  it('shows only pinned actions, then the menu button', () => {
    const { container } = mount({ profileId: PROFILE, actions: [action(), action({ id: B, name: 'Vault', title: 'Vault', pinned: false })] })
    expect([...container.querySelectorAll('[data-action]')].map((b) => b.getAttribute('data-action'))).toEqual([A])
    expect(container.querySelector('[data-extensions-menu-button]')).not.toBeNull()
  })

  it('keeps the menu button when nothing is pinned', () => {
    const { container } = mount({ profileId: PROFILE, actions: [action({ pinned: false })] })
    expect(container.querySelectorAll('[data-action]')).toHaveLength(0)
    expect(screen.getByRole('button', { name: 'Extensions' })).toBeTruthy()
  })

  it('runs the action with the clicked button as the popup anchor', () => {
    const { container, stub } = mount({ profileId: PROFILE, actions: [action()] })
    fireEvent.click(container.querySelector(`[data-action="${A}"]`) as HTMLElement)
    const call = stub.calls.find((c) => c.name === 'extensions.runAction')
    expect(call?.payload).toEqual({ id: A, anchor: { x: 0, y: 0, width: 0, height: 0 } })
  })

  it('opens the extensions menu under its button', () => {
    const { stub } = mount({ profileId: PROFILE, actions: [action()] })
    fireEvent.click(screen.getByRole('button', { name: 'Extensions' }))
    expect(stub.calls.some((c) => c.name === 'extensions.openMenu')).toBe(true)
  })

  it('reads the badge and marks an action that does nothing on this page', () => {
    const { container } = mount({ profileId: PROFILE, actions: [action({ badge: '7', enabled: false, icon: null })] })
    const button = container.querySelector(`[data-action="${A}"]`) as HTMLElement
    expect(button.getAttribute('aria-label')).toBe('Notes, 7')
    expect(button.getAttribute('aria-disabled')).toBe('true')
    expect(button.textContent).toBe('N')
  })
})

describe('actionLabel', () => {
  it('falls back to the name for an empty title', () => {
    expect(actionLabel(action({ title: '  ' }))).toBe('Notes')
  })
})
