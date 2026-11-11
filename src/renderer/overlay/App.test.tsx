import { act, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultWindowState } from '../../shared/state'
import { stubClu } from '../../test-utils/stubClu'
import { App } from './App'

const WIN = '00000000-0000-4000-8000-000000000001'
const INST = '77777777-7777-4777-8777-777777777777'

describe('overlay App', () => {
  it('renders a registered surface only for its own layer', () => {
    const stub = stubClu()
    window.clu = { ...stub.clu, layer: 'top', slot: null }
    render(<App />)
    const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
    win.overlay.surfaces = [
      { instanceId: INST, surface: 'confirm', layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null, props: { title: 'Sure?', body: '', confirmLabel: 'Yes', cancelLabel: 'No' } },
      { instanceId: '88888888-8888-4888-8888-888888888888', surface: 'toast', layer: 'passive', modality: 'passive', rect: null, openedFrom: null, focus: false, slot: 0, props: {} }
    ]
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
    expect(screen.getByRole('dialog', { name: 'Sure?' })).toBeInTheDocument()
    expect(screen.queryByRole('status')).toBeNull()
  })
})
