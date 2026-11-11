import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { ExtensionInfo } from '../../../shared/ipc/extensions'
import { defaultState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { ExtensionsPage } from './ExtensionsPage'
import { profileOptions } from './useExtensionsPage'

const P = '11111111-1111-4111-8111-111111111111'
const P2 = '11111111-1111-4111-8111-111111111112'
const A = 'a'.repeat(32)

const ext = (over: Partial<ExtensionInfo> = {}): ExtensionInfo => ({ id: A, name: 'Thing', version: '1.2.3', enabled: true, icon: null, hasAction: true, hasOptions: true, permissions: ['Read your browsing history', 'Show notifications'], pinned: false, ...over })
const profile = (id: string, name: string, isDefault: boolean, extensions: ExtensionInfo[] = [], ephemeral = false) => ({ id, name, isDefault, ephemeral, extensions }) as unknown as CluState['profiles'][string]

function mount(profiles: CluState['profiles'], overrides: Record<string, (p: unknown) => unknown> = {}) {
  const stub = stubClu({ 'extensions.pageProfile': () => ({ profileId: null }), 'extensions.list': () => [], ...overrides })
  window.clu = stub.clu
  render(<ExtensionsPage />)
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, profiles } }))
  return stub
}
const called = (stub: ReturnType<typeof stubClu>, name: string) => stub.calls.filter((c) => c.name === name).map((c) => c.payload)

describe('ExtensionsPage', () => {
  it('lists the extensions with version and permissions, and wakes the profile session', async () => {
    const stub = mount({ [P]: profile(P, 'Personal', true, [ext()]) })
    const row = within(screen.getByRole('listitem'))
    expect(row.getByText('Thing')).toBeInTheDocument()
    expect(row.getByText('1.2.3')).toBeInTheDocument()
    expect(row.getByText('Read your browsing history · Show notifications')).toBeInTheDocument()
    await waitFor(() => expect(called(stub, 'extensions.list')).toContainEqual({ profileId: P }))
  })

  it('shows an empty state, and a row without options has no Options button', () => {
    mount({ [P]: profile(P, 'Personal', true) })
    expect(screen.getByText('No extensions in this profile yet.')).toBeInTheDocument()
    expect(screen.queryByRole('list')).toBeNull()
  })

  it('hides Options for an extension without a page and disables it for one turned off', () => {
    mount({ [P]: profile(P, 'Personal', true, [ext({ hasOptions: false }), ext({ id: 'b'.repeat(32), name: 'Off', enabled: false })]) })
    expect(screen.queryByRole('button', { name: 'Options for Thing' })).toBeNull()
    expect(screen.getByRole('button', { name: 'Options for Off' })).toBeDisabled()
  })

  it('toggles, opens options and opens the store', async () => {
    const stub = mount({ [P]: profile(P, 'Personal', true, [ext()]) }, { 'extensions.setEnabled': () => null, 'extensions.openOptions': () => null, 'extensions.openStore': () => null })
    fireEvent.click(screen.getByRole('switch', { name: 'Turn on Thing' }))
    fireEvent.click(screen.getByRole('button', { name: 'Options for Thing' }))
    fireEvent.click(screen.getByRole('link', { name: 'Get extensions' }))
    await waitFor(() => expect(called(stub, 'extensions.openStore')).toHaveLength(1))
    expect(called(stub, 'extensions.setEnabled')).toEqual([{ profileId: P, id: A, enabled: false }])
    expect(called(stub, 'extensions.openOptions')).toEqual([{ profileId: P, id: A }])
  })

  it('removes only after the confirm', async () => {
    const stub = mount({ [P]: profile(P, 'Personal', true, [ext()]) }, { 'extensions.remove': () => null })
    fireEvent.click(screen.getByRole('button', { name: 'Remove Thing' }))
    const dialog = within(screen.getByRole('dialog', { name: 'Remove “Thing”?' }))
    fireEvent.click(dialog.getByRole('button', { name: 'Cancel' }))
    expect(screen.queryByRole('dialog')).toBeNull()
    expect(called(stub, 'extensions.remove')).toEqual([])
    fireEvent.click(screen.getByRole('button', { name: 'Remove Thing' }))
    fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Remove' }))
    await waitFor(() => expect(called(stub, 'extensions.remove')).toEqual([{ profileId: P, id: A }]))
  })

  it('reports a failed action', async () => {
    mount({ [P]: profile(P, 'Personal', true, [ext()]) })
    fireEvent.click(screen.getByRole('switch', { name: 'Turn on Thing' }))
    expect(await screen.findByRole('alert')).toHaveTextContent('Couldn’t turn the extension off. Try again.')
  })

  it('opens on the profile of its own tab, offers the others and leaves private profiles out', async () => {
    const stub = mount(
      { [P]: profile(P, 'Personal', true, [ext()]), [P2]: profile(P2, 'Work', false, [ext({ name: 'Work thing' })]), x: profile('x', 'Private', false, [], true) },
      { 'extensions.pageProfile': () => ({ profileId: P2 }) }
    )
    expect(await screen.findByText('Work thing')).toBeInTheDocument()
    fireEvent.change(screen.getByLabelText('Profile'), { target: { value: P } })
    expect(await screen.findByText('Thing')).toBeInTheDocument()
    await waitFor(() => expect(called(stub, 'extensions.list')).toContainEqual({ profileId: P }))
    expect(screen.queryByRole('option', { name: 'Private' })).toBeNull()
  })

  it('profileOptions puts the default first', () => {
    const s = { ...defaultState(), profiles: { [P2]: profile(P2, 'Alpha', false), [P]: profile(P, 'Zed', true) } }
    expect(profileOptions(s).map((p) => p.name)).toEqual(['Zed', 'Alpha'])
  })
})
