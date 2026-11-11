// The update card against a stub window.clu (M12): shows only for a ready update it was not closed for,
// restarts once, closes for this version only, and comes back for a newer version.
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, type CluState } from '../../../shared/state'
import type { UpdateState } from '../../../shared/state/platform'
import { stubClu } from '../../../test-utils/stubClu'
import { UpdateBanner } from './UpdateBanner'

function snapshot(update: Partial<UpdateState>, rev = 1): CluState {
  const base = defaultState()
  return { ...base, rev, app: { ...base.app, update: { status: 'ready', version: '158.0.8127.19', checkedAt: 1, dismissed: false, ...update } } }
}

describe('UpdateBanner', () => {
  it('stays hidden until an update is ready', () => {
    const stub = stubClu()
    window.clu = stub.clu
    const { container } = render(<UpdateBanner />)
    for (const [i, status] of (['unavailable', 'idle', 'checking', 'downloading', 'up-to-date', 'error'] as const).entries()) {
      act(() => stub.push({ kind: 'snapshot', rev: i + 1, state: snapshot({ status }, i + 1) }))
      expect(container).toBeEmptyDOMElement()
    }
  })

  it('names the ready version and restarts once', async () => {
    const stub = stubClu({ 'platform.installUpdate': () => ({ status: 'installing' }) })
    window.clu = stub.clu
    render(<UpdateBanner />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: snapshot({}) }))
    const card = screen.getByRole('region', { name: 'Update' })
    expect(card).toHaveAttribute('data-update', 'ready')
    expect(card).toHaveTextContent('Update ready')
    expect(card).toHaveTextContent('Rio 158.0.8127.19 installs when you quit.')
    const button = screen.getByRole('button', { name: 'Restart to update' })
    fireEvent.click(button)
    await waitFor(() => expect(button).toBeDisabled())
    fireEvent.click(button)
    expect(stub.calls.filter((c) => c.name === 'platform.installUpdate')).toHaveLength(1)
  })

  it('names no version it does not know and re-enables when the update is not ready', async () => {
    const stub = stubClu({ 'platform.installUpdate': () => ({ status: 'not-ready' }) })
    window.clu = stub.clu
    render(<UpdateBanner />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: snapshot({ version: null }) }))
    expect(screen.getByRole('region', { name: 'Update' })).toHaveTextContent('It installs when you quit Rio.')
    const button = screen.getByRole('button', { name: 'Restart to update' })
    fireEvent.click(button)
    await waitFor(() => expect(stub.calls.some((c) => c.name === 'platform.installUpdate')).toBe(true))
    await waitFor(() => expect(button).toBeEnabled())
  })

  it('closes for this version, stays closed while the browser says dismissed, and returns for a newer version', async () => {
    const stub = stubClu({ 'platform.dismissUpdate': () => ({ status: 'dismissed' }) })
    window.clu = stub.clu
    const { container } = render(<UpdateBanner />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: snapshot({}) }))
    fireEvent.click(screen.getByRole('button', { name: 'Close' }))
    expect(container).toBeEmptyDOMElement()
    await waitFor(() => expect(stub.calls.filter((c) => c.name === 'platform.dismissUpdate')).toHaveLength(1))
    act(() => stub.push({ kind: 'snapshot', rev: 2, state: snapshot({ dismissed: true }, 2) }))
    expect(container).toBeEmptyDOMElement()
    // A newer version is ready: the browser clears dismissed and the card is back.
    act(() => stub.push({ kind: 'snapshot', rev: 3, state: snapshot({ version: '158.0.8127.20', dismissed: false }, 3) }))
    expect(screen.getByRole('region', { name: 'Update' })).toHaveTextContent('Rio 158.0.8127.20 installs when you quit.')
  })

  it('stays hidden for a version the browser already has as dismissed (after a relaunch)', () => {
    const stub = stubClu()
    window.clu = stub.clu
    const { container } = render(<UpdateBanner />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: snapshot({ dismissed: true }) }))
    expect(container).toBeEmptyDOMElement()
  })

  it('comes back when the browser refuses the close', async () => {
    const stub = stubClu({ 'platform.dismissUpdate': () => ({ status: 'not-ready' }) })
    window.clu = stub.clu
    render(<UpdateBanner />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: snapshot({}) }))
    fireEvent.click(screen.getByRole('button', { name: 'Close' }))
    expect(await screen.findByRole('region', { name: 'Update' })).toBeInTheDocument()
  })

  it('treats a state without app.update as no update', () => {
    const stub = stubClu()
    window.clu = stub.clu
    const { container } = render(<UpdateBanner />)
    const base = defaultState()
    const junk = { ...base, app: { ...base.app, update: undefined } } as unknown as CluState
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: junk }))
    expect(container).toBeEmptyDOMElement()
  })
})
