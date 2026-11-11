// The sidebar against a stub window.clu: an update of a self-signed build gives the Safe Storage entry a new per-build
// partition list (macOS asks for the keychain once), and the sidebar never shows a card offering to stop that.
import { act, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { COMMAND_NAMES } from '../../../shared/ipc'
import { defaultState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { Sidebar } from './Sidebar'

afterEach(() => vi.unstubAllGlobals())

describe('Sidebar keychain', () => {
  it('shows no keychain card when the entry has a per-build partition list', () => {
    vi.stubGlobal('ResizeObserver', class { observe() {} unobserve() {} disconnect() {} })
    const stub = stubClu()
    window.clu = stub.clu
    const base = defaultState()
    // What an older browser published while the entry carried this build's list.
    const app = { ...base.app, update: { status: 'ready', version: '158.0.8127.19', checkedAt: 1, dismissed: false }, keychainTrust: 'ask' }
    const state = { ...base, rev: 1, app } as CluState
    render(<Sidebar mode="main" />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
    expect(screen.getByText(/158\.0\.8127\.19/)).toBeInTheDocument()
    expect(screen.queryByText('Stop the keychain prompt after each update?')).toBeNull()
    expect(screen.queryByRole('region', { name: 'Keychain' })).toBeNull()
    expect(COMMAND_NAMES).not.toContain('platform.keychainTrust')
  })
})
