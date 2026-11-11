import { act, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultWindowState, type OverlaySurfaceState, type ToastState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { SHARE_ACTION, TOAST_EXIT_MS, ToastSurface } from './Toast'

const WIN = '00000000-0000-4000-8000-000000000001'
const instance: OverlaySurfaceState = { instanceId: '77777777-7777-4777-8777-777777777777', surface: 'toast', layer: 'passive', modality: 'passive', rect: null, openedFrom: null, focus: false, slot: null, props: {} }

function mount(toast: ToastState) {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  stateMirror()
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.toasts = [toast]
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win } } }))
  render(<ToastSurface instance={instance} windowId={WIN} />)
  return stub
}

describe('ToastSurface', () => {
  it('gives the copy toast a share button that opens the picker at the toast', () => {
    const stub = mount({ id: 't1', text: 'Copied Current URL', action: { label: 'Share', commandId: SHARE_ACTION }, until: 0 })
    expect(screen.queryByText('Share')).toBeNull()
    fireEvent.click(screen.getByRole('button', { name: 'Share' }))
    expect(stub.calls).toContainEqual({ name: 'siteControls.share', payload: {} })
    expect(stub.calls.some((c) => c.name === 'commands.run')).toBe(false)
  })

  afterEach(() => vi.useRealTimers())

  it('plays its exit motion so it ends as the browser removes it', () => {
    vi.useFakeTimers()
    vi.setSystemTime(1_000_000)
    mount({ id: 't3', text: 'Copied Current URL', until: 1_000_000 + 1500 })
    const toast = document.querySelector('.clu-toast')
    expect(toast).not.toHaveAttribute('data-leaving')
    act(() => vi.advanceTimersByTime(1500 - TOAST_EXIT_MS - 1))
    expect(toast).not.toHaveAttribute('data-leaving')
    act(() => vi.advanceTimersByTime(1))
    expect(toast).toHaveAttribute('data-leaving', 'true')
  })

  it('keeps Undo as a text button that runs its command', () => {
    const stub = mount({ id: 't2', text: 'Closed tab', action: { label: 'Undo', commandId: 'tabs.reopenClosed' }, until: 0 })
    fireEvent.click(screen.getByRole('button', { name: 'Undo' }))
    expect(stub.calls).toContainEqual({ name: 'commands.run', payload: { id: 'tabs.reopenClosed' } })
  })
})
