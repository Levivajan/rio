// The Icon tab (docs/chromium/icons.md): the seven icons as rows, the default for a missing or junk value, "Current
// icon" under the one in use, and a pick saved globally.
import { act, fireEvent, render, screen, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { IpcResult } from '../../../../shared/ipc'
import { defaultState, type CluState } from '../../../../shared/state'
import type { Json } from '../../../../shared/types'
import { stubClu } from '../../../../test-utils/stubClu'
import { IconPane } from './IconPane'

type SetCall = { key: string; scope: string; value: Json }

describe('IconPane', () => {
  it('shows the seven icons, Flat warm for a junk value, and saves a pick globally', async () => {
    const stub = stubClu()
    const calls: SetCall[] = []
    let state: CluState = { ...defaultState(), rev: 1, settings: { global: { 'appearance.appIcon': 'neon' }, byProfile: {} } }
    ;(stub.clu.settings as { set: (p: SetCall) => Promise<IpcResult<null>> }).set = async (p) => {
      calls.push(p)
      return { ok: true, value: null }
    }
    window.clu = stub.clu
    render(<IconPane />)
    act(() => stub.push({ kind: 'snapshot', rev: state.rev, state }))

    const group = screen.getByRole('radiogroup', { name: 'App icon' })
    const radios = within(group).getAllByRole('radio') as HTMLInputElement[]
    expect(radios.map((r) => r.getAttribute('aria-label'))).toEqual(['Flat warm', 'Sunset', 'Ocean', 'Flat blue', 'Rio sunset', 'Warm', 'Dusk'])
    expect(radios.filter((r) => r.checked).map((r) => r.value)).toEqual(['flatWarm'])
    expect(within(group).getAllByText('Current icon')).toHaveLength(1)
    expect(new Set(Array.from(group.querySelectorAll('img'), (img) => img.getAttribute('src'))).size).toBe(7)

    fireEvent.click(within(group).getByRole('radio', { name: 'Dusk' }))
    expect(calls).toEqual([{ key: 'appearance.appIcon', scope: 'global', value: 'dusk' }])
    state = { ...state, rev: 2, settings: { global: { 'appearance.appIcon': 'dusk' }, byProfile: {} } }
    await act(async () => stub.push({ kind: 'snapshot', rev: state.rev, state }))
    expect(radios.filter((r) => r.checked).map((r) => r.value)).toEqual(['dusk'])
    expect(within(group).getByText('Current icon').closest('label')?.getAttribute('data-app-icon')).toBe('dusk')
    expect(screen.queryByRole('alert')).toBeNull()
  })

  it('keeps a stored Sunset from before Flat warm became the default', () => {
    const stub = stubClu()
    const state: CluState = { ...defaultState(), rev: 1, settings: { global: { 'appearance.appIcon': 'airySun' }, byProfile: {} } }
    window.clu = stub.clu
    render(<IconPane />)
    act(() => stub.push({ kind: 'snapshot', rev: state.rev, state }))
    const group = screen.getByRole('radiogroup', { name: 'App icon' })
    expect(within(group).getByRole('radio', { name: 'Sunset' })).toBeChecked()
    expect(within(group).getByText('Current icon').closest('label')?.getAttribute('data-app-icon')).toBe('airySun')
  })
})
