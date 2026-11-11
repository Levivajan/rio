import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { OverlaySurfaceState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { ConfirmSurface } from './Confirm'

const WIN = '00000000-0000-4000-8000-000000000001'
const INST = '77777777-7777-4777-8777-777777777777'

function open() {
  const stub = stubClu()
  window.clu = { ...stub.clu, layer: 'top', slot: null }
  const instance = {
    instanceId: INST, surface: 'confirm', layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null,
    props: { title: 'Delete your “Work” Space?', body: 'Its 2 Today tabs move to Archive.', confirmLabel: 'Delete', cancelLabel: 'Cancel', destructive: true }
  } as OverlaySurfaceState
  render(<ConfirmSurface instance={instance} windowId={WIN} />)
  const responses = () => stub.calls.filter((c) => c.name === 'overlay.respond').map((c) => c.payload)
  return { responses }
}

describe('ConfirmSurface', () => {
  it('shows esc on Cancel and return on the confirm button, without changing their names', () => {
    open()
    const cancel = screen.getByRole('button', { name: 'Cancel' })
    const del = screen.getByRole('button', { name: 'Delete' })
    expect(cancel.querySelector('kbd')?.textContent).toBe('esc')
    expect(del.querySelector('kbd')?.textContent).toBe('return')
    expect(del.dataset.variant).toBe('danger')
  })

  it('Return confirms even while Cancel has focus, Esc cancels', () => {
    const { responses } = open()
    const cancel = screen.getByRole('button', { name: 'Cancel' })
    expect(document.activeElement).toBe(cancel)
    fireEvent.keyDown(cancel, { key: 'Enter' })
    expect(responses()).toEqual([{ instanceId: INST, result: true }])
    fireEvent.keyDown(cancel, { key: 'Escape' })
    expect(responses()).toEqual([{ instanceId: INST, result: true }, { instanceId: INST, result: false }])
  })

  it('an Enter that ends an IME composition does not confirm', () => {
    const { responses } = open()
    fireEvent.keyDown(screen.getByRole('button', { name: 'Cancel' }), { key: 'Enter', isComposing: true })
    expect(responses()).toEqual([])
  })
})
