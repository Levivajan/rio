// New Space is a panel over the sidebar: Create Space stays dim until a name is typed, Enter creates with the name,
// icon, theme color and profile, Esc and Cancel close it; the bottom bar knows the panel is open.
import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { SPACE_EDITOR_SURFACE } from '../../../shared/spaces'
import { defaultProfileState, defaultSpaceState, defaultState, defaultWindowState, type CluState } from '../../../shared/state'
import type { OverlaySurfaceState } from '../../../shared/state/core'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { creatingSpace } from '../../ui/spaces/NewSpaceCard'
import { SpaceEditorSurface } from './SpaceEditor'

const WIN = '00000000-0000-4000-8000-000000000001'
const SPACE = '00000000-0000-4000-8000-000300000001'
const PROFILE = '00000000-0000-4000-8000-000400000001'

function editor(props: OverlaySurfaceState['props'], instanceId = 'i1'): OverlaySurfaceState {
  return { instanceId, surface: SPACE_EDITOR_SURFACE, layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null, props }
}

function stateWith(surfaces: OverlaySurfaceState[]): CluState {
  const base = defaultState()
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE })
  return {
    ...base,
    rev: 1,
    profiles: { [PROFILE]: defaultProfileState({ id: PROFILE, name: 'Work', isDefault: true, ephemeral: false }) },
    spaces: { [SPACE]: defaultSpaceState({ id: SPACE, profileId: PROFILE, name: 'Home', color: 'green', icon: { kind: 'letter' }, order: 0 } as never) },
    spaceOrder: [SPACE],
    windows: { [WIN]: { ...win, overlay: { surfaces } } }
  }
}

function mount(props: OverlaySurfaceState['props'] = { spaceId: null, view: 'space' }) {
  const stub = stubClu({ 'spaces.create': () => ({ spaceId: 'new' }) })
  window.clu = { ...stub.clu, windowId: WIN }
  stateMirror()
  const instance = editor(props)
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: stateWith([instance]) }))
  render(<SpaceEditorSurface instance={instance} windowId={WIN} />)
  return stub
}

describe('CreateSpacePanel', () => {
  it('opens unnamed with the name field focused and Create Space dim until a name is typed', () => {
    mount()
    const panel = screen.getByRole('dialog', { name: 'Create a Space' })
    expect(panel.classList.contains('clu-create-space')).toBe(true)
    const name = screen.getByLabelText('Space name') as HTMLInputElement
    expect(name.value).toBe('')
    expect(document.activeElement).toBe(name)
    const create = screen.getByRole('button', { name: 'Create Space' }) as HTMLButtonElement
    expect(create.disabled).toBe(true)
    fireEvent.change(name, { target: { value: '   ' } })
    expect(create.disabled).toBe(true)
    fireEvent.change(name, { target: { value: 'Garden' } })
    expect(create.disabled).toBe(false)
    expect(screen.getByLabelText('Profile')).toHaveProperty('value', PROFILE)
    expect(screen.getByRole('button', { name: 'Choose a Theme' })).toBeTruthy()
  })

  it('Enter creates the space with the picked icon, theme color and profile, then closes', async () => {
    const stub = mount()
    fireEvent.change(screen.getByLabelText('Space name'), { target: { value: 'Garden' } })
    fireEvent.click(screen.getByRole('button', { name: 'Space icon' }))
    fireEvent.click(screen.getByRole('button', { name: '🎨' }))
    fireEvent.click(screen.getByRole('button', { name: 'Choose a Theme' }))
    fireEvent.click(screen.getByRole('button', { name: 'Teal' }))
    await act(async () => {
      fireEvent.submit(screen.getByRole('dialog', { name: 'Create a Space' }))
    })
    expect(stub.calls.find((c) => c.name === 'spaces.create')?.payload).toEqual({ name: 'Garden', icon: { kind: 'emoji', value: '🎨' }, color: 'teal', profileId: PROFILE })
    expect(stub.calls.some((c) => c.name === 'overlay.close')).toBe(true)
  })

  it('the icon well opens the edit sheet picker, with Emoji and Symbols tabs', () => {
    mount()
    const well = screen.getByRole('button', { name: 'Space icon' })
    expect(well).toHaveAttribute('aria-expanded', 'false')
    expect(screen.queryByRole('tablist')).toBeNull()
    fireEvent.click(well)
    expect(well).toHaveAttribute('aria-expanded', 'true')
    expect(screen.getByRole('tab', { name: 'Emoji' })).toHaveAttribute('aria-selected', 'true')
    expect(screen.getByRole('button', { name: 'Letter' })).toHaveAttribute('aria-pressed', 'true')
    fireEvent.click(screen.getByRole('tab', { name: 'Symbols' }))
    expect(screen.getByRole('textbox', { name: 'Search symbols' })).toBeTruthy()
    expect(screen.getByRole('group', { name: 'Symbols' }).querySelectorAll('button').length).toBeGreaterThan(0)
  })

  it('creates with the letter icon when none is picked, and the well stays dashed', async () => {
    const stub = mount()
    const well = screen.getByRole('button', { name: 'Space icon' })
    expect(well).toHaveAttribute('data-empty', 'true')
    fireEvent.change(screen.getByLabelText('Space name'), { target: { value: 'Garden' } })
    await act(async () => {
      fireEvent.submit(screen.getByRole('dialog', { name: 'Create a Space' }))
    })
    expect(stub.calls.find((c) => c.name === 'spaces.create')?.payload).toMatchObject({ name: 'Garden', icon: { kind: 'letter' } })
  })

  it('a pasted emoji shows in the well and is saved; Letter goes back to no icon', async () => {
    const stub = mount()
    fireEvent.change(screen.getByLabelText('Space name'), { target: { value: 'Garden' } })
    const well = screen.getByRole('button', { name: 'Space icon' })
    fireEvent.click(well)
    fireEvent.change(screen.getByRole('textbox', { name: 'Emoji' }), { target: { value: '🌵' } })
    expect(well).toHaveTextContent('🌵')
    expect(well).not.toHaveAttribute('data-empty')
    fireEvent.click(screen.getByRole('button', { name: 'Letter' }))
    expect(well).toHaveAttribute('data-empty', 'true')
    fireEvent.change(screen.getByRole('textbox', { name: 'Emoji' }), { target: { value: '🌵x' } })
    expect(screen.getByRole('alert')).toBeTruthy()
    expect(well).toHaveAttribute('data-empty', 'true')
    fireEvent.change(screen.getByRole('textbox', { name: 'Emoji' }), { target: { value: '🌵' } })
    await act(async () => {
      fireEvent.submit(screen.getByRole('dialog', { name: 'Create a Space' }))
    })
    expect(stub.calls.find((c) => c.name === 'spaces.create')?.payload).toMatchObject({ icon: { kind: 'emoji', value: '🌵' } })
  })

  it('a symbol picked on the Symbols tab draws in the well and Create Space saves it', async () => {
    const stub = mount()
    fireEvent.change(screen.getByLabelText('Space name'), { target: { value: 'Orbit' } })
    const well = screen.getByRole('button', { name: 'Space icon' })
    fireEvent.click(well)
    fireEvent.click(screen.getByRole('tab', { name: 'Symbols' }))
    fireEvent.change(screen.getByRole('textbox', { name: 'Search symbols' }), { target: { value: 'pla' } })
    fireEvent.click(screen.getByRole('button', { name: 'Planet' }))
    expect(screen.getByRole('button', { name: 'Planet' })).toHaveAttribute('aria-pressed', 'true')
    expect(well.querySelector('svg[data-symbol="planet"]')).not.toBeNull()
    expect(well).not.toHaveAttribute('data-empty')
    // Closing and reopening the picker starts on Symbols with the symbol marked.
    fireEvent.click(well)
    fireEvent.click(well)
    expect(screen.getByRole('tab', { name: 'Symbols' })).toHaveAttribute('aria-selected', 'true')
    expect(screen.getByRole('button', { name: 'Planet' })).toHaveAttribute('aria-pressed', 'true')
    await act(async () => {
      fireEvent.submit(screen.getByRole('dialog', { name: 'Create a Space' }))
    })
    expect(stub.calls.find((c) => c.name === 'spaces.create')?.payload).toEqual({ name: 'Orbit', icon: { kind: 'symbol', value: 'planet' }, color: 'indigo', profileId: PROFILE })
    expect(stub.calls.some((c) => c.name === 'overlay.close')).toBe(true)
  })

  it('Esc and Cancel close without creating', () => {
    const stub = mount()
    fireEvent.keyDown(screen.getByLabelText('Space name'), { key: 'Escape' })
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }))
    expect(stub.calls.filter((c) => c.name === 'overlay.close')).toHaveLength(2)
    expect(stub.calls.some((c) => c.name === 'spaces.create')).toBe(false)
  })

  it('editing an existing space keeps the dialog', () => {
    mount({ spaceId: SPACE, view: 'space' })
    expect(screen.queryByRole('dialog', { name: 'Create a Space' })).toBeNull()
    expect(screen.getByRole('dialog', { name: 'Edit Space' })).toBeTruthy()
  })
})

describe('creatingSpace', () => {
  it('is true only for the editor without a space on its form, and tolerates junk props', () => {
    expect(creatingSpace(stateWith([]), WIN)).toBe(false)
    expect(creatingSpace(stateWith([editor({ spaceId: null, view: 'space' })]), WIN)).toBe(true)
    expect(creatingSpace(stateWith([editor({})]), WIN)).toBe(true)
    expect(creatingSpace(stateWith([editor({ spaceId: SPACE, view: 'space' })]), WIN)).toBe(false)
    expect(creatingSpace(stateWith([editor({ spaceId: null, view: 'profiles' })]), WIN)).toBe(false)
    expect(creatingSpace(stateWith([{ ...editor(null), surface: 'menu' }]), WIN)).toBe(false)
    expect(creatingSpace(stateWith([editor(null)]), WIN)).toBe(true)
    expect(creatingSpace(stateWith([editor([1, 2])]), WIN)).toBe(true)
    expect(creatingSpace(stateWith([editor({})]), 'no-such-window')).toBe(false)
  })
})
