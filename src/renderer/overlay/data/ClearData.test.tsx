// Clear browsing data dialog (data-t5): defaults, every type cleared by the range, Esc cancels, Clear sends the choice, spinner,
// error, and the profile picker.
import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { IpcResult } from '../../../shared/ipc'
import { defaultProfileState, defaultState, type CluState, type OverlaySurfaceState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { ClearDataSurface } from './ClearData'

const A = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
const B = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb'
const P = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'

function setup(opts: { profiles?: number; own?: string; reply?: () => IpcResult<null> | Promise<IpcResult<null>> } = {}) {
  const stub = stubClu()
  const profiles: CluState['profiles'] = {
    [A]: defaultProfileState({ id: A, name: 'Personal', isDefault: true, ephemeral: false }),
    [P]: defaultProfileState({ id: P, name: 'Private', isDefault: false, ephemeral: true })
  }
  if ((opts.profiles ?? 1) > 1) profiles[B] = defaultProfileState({ id: B, name: 'Work', isDefault: false, ephemeral: false })
  const clu = stub.clu as unknown as { browsingData: { clear: (p: unknown) => Promise<unknown> } }
  clu.browsingData.clear = async (p) => {
    stub.calls.push({ name: 'browsingData.clear', payload: p })
    return opts.reply ? opts.reply() : { ok: true, value: null }
  }
  window.clu = stub.clu
  const instance: OverlaySurfaceState = { instanceId: 'i1', surface: 'clear-data', layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null, props: { profileId: opts.own ?? A } }
  render(<ClearDataSurface instance={instance} windowId="w" />)
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, profiles } }))
  const calls = (name: string) => stub.calls.filter((c) => c.name === name).map((c) => c.payload)
  return { calls }
}

const box = (name: RegExp) => screen.getByRole('checkbox', { name }) as HTMLInputElement
const range = () => screen.getByRole('combobox', { name: 'Time range' }) as HTMLSelectElement

describe('clear browsing data dialog', () => {
  it('defaults to Last hour with history, cookies and cache ticked; every type clears by the range', () => {
    setup()
    expect(screen.getByRole('dialog', { name: 'Clear browsing data' })).toBeTruthy()
    expect(range().value).toBe('hour')
    expect(range().selectedOptions[0]?.textContent).toBe('Last hour')
    const checked = screen.getAllByRole('checkbox').map((c) => [c.closest('label')?.textContent, (c as HTMLInputElement).checked])
    expect(checked).toEqual([
      ['Browsing history', true],
      ['Cookies and site data', true],
      ['Cached images and files', true],
      ['Download history', false],
      ['Site permissions', false],
      ['Archived tabs', false]
    ])
    // Chromium clears cookies and cache by time too: no label changes with the range.
    fireEvent.change(range(), { target: { value: 'week' } })
    expect(box(/^Cookies and site data$/)).toBeTruthy()
    expect(box(/^Cached images and files$/)).toBeTruthy()
  })

  it('Esc and Cancel close the dialog without clearing', () => {
    const { calls } = setup()
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' })
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }))
    expect(calls('overlay.close')).toEqual([{ instanceId: 'i1' }, { instanceId: 'i1' }])
    expect(calls('browsingData.clear')).toEqual([])
  })

  it('Clear data sends the profile, range and types, shows a spinner, then closes', async () => {
    let finish: (r: IpcResult<null>) => void = () => {}
    const { calls } = setup({ reply: () => new Promise((r) => (finish = r)) })
    fireEvent.change(range(), { target: { value: 'day' } })
    fireEvent.click(box(/^Cached images/))
    fireEvent.click(box(/^Archived tabs/))
    fireEvent.click(screen.getByRole('button', { name: 'Clear data' }))
    expect(calls('browsingData.clear')).toEqual([
      { profileId: A, range: 'day', types: { history: true, cookies: true, cache: false, downloads: false, permissions: false, archive: true } }
    ])
    const busy = screen.getByRole('button', { name: 'Clearing…' }) as HTMLButtonElement
    expect(busy.disabled).toBe(true)
    // Esc while clearing does nothing; the clear finishes and the dialog closes itself.
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' })
    expect(calls('overlay.close')).toEqual([])
    await act(async () => finish({ ok: true, value: null }))
    expect(calls('overlay.close')).toEqual([{ instanceId: 'i1' }])
  })

  it('a failed clear keeps the dialog open with an error and lets the user try again', async () => {
    const { calls } = setup({ reply: () => ({ ok: false, error: { code: 'rejected' } }) })
    await act(async () => fireEvent.click(screen.getByRole('button', { name: 'Clear data' })))
    expect(screen.getByRole('alert').textContent).toBe("Rio couldn't clear browsing data. Try again.")
    expect(calls('overlay.close')).toEqual([])
    expect((screen.getByRole('button', { name: 'Clear data' }) as HTMLButtonElement).disabled).toBe(false)
  })

  it('Clear data is off with nothing ticked', () => {
    setup()
    for (const name of [/^Browsing history/, /^Cookies/, /^Cached/]) fireEvent.click(box(name))
    expect((screen.getByRole('button', { name: 'Clear data' }) as HTMLButtonElement).disabled).toBe(true)
  })

  it('shows a profile picker for more than one persistent profile, starting at the dialog’s profile', () => {
    const { calls } = setup({ profiles: 2, own: B })
    const picker = screen.getByRole('combobox', { name: 'Profile' }) as HTMLSelectElement
    expect([...picker.options].map((o) => o.textContent)).toEqual(['Personal', 'Work'])
    expect(picker.value).toBe(B)
    fireEvent.change(picker, { target: { value: A } })
    fireEvent.click(screen.getByRole('button', { name: 'Clear data' }))
    expect((calls('browsingData.clear')[0] as { profileId: string }).profileId).toBe(A)
  })

  it('one persistent profile shows no picker', () => {
    setup()
    expect(screen.queryByRole('combobox', { name: 'Profile' })).toBeNull()
  })

  it('a private profile’s dialog offers it next to the persistent ones', () => {
    setup({ own: P })
    const picker = screen.getByRole('combobox', { name: 'Profile' }) as HTMLSelectElement
    expect(picker.value).toBe(P)
    expect([...picker.options].map((o) => o.textContent)).toEqual(['Personal', 'Private'])
  })
})
