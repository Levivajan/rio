// The Site tweaks settings section against a stub window.clu (tweaks-t4). A .ts file: the settings page globs every
// .tsx in this folder.
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import { createElement } from 'react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, type CluState } from '../../../shared/state'
import type { SiteTweakRow } from '../../../shared/tweaks'
import { stubClu } from '../../../test-utils/stubClu'
import { profileOptions, SiteTweaksSection } from './SiteTweaks'
import { tweakSignature } from './useSiteTweaks'

const P = '11111111-1111-4111-8111-111111111111'
const P2 = '11111111-1111-4111-8111-111111111112'
const TAB = '22222222-2222-4222-8222-222222222221'

const row = (site: string, w: Partial<SiteTweakRow> = {}): SiteTweakRow => ({ site, enabled: true, hasCss: false, hasJs: false, hasControls: false, zapCount: 0, updatedAt: 1, ...w })
const profile = (id: string, name: string, isDefault: boolean, ephemeral = false) => ({ id, name, isDefault, ephemeral }) as CluState['profiles'][string]

function snapshot(rev: number, tweak: CluState['tabs'][string]['tweak'] = null): CluState {
  const tab = { ...defaultTabState({ id: TAB, profileId: P, spaceId: null, section: 'today', url: 'http://a.test/', title: '', lastActiveAt: 0 }), tweak }
  return { ...defaultState(), rev, profiles: { [P]: profile(P, 'Personal', true), [P2]: profile(P2, 'Work', false) }, tabs: { [TAB]: tab } }
}

function mount(lists: Record<string, SiteTweakRow[]>, overrides: Record<string, (p: unknown) => unknown> = {}) {
  const stub = stubClu({ 'tweaks.list': (p) => lists[(p as { profileId: string }).profileId] ?? [], ...overrides })
  window.clu = stub.clu
  render(createElement(SiteTweaksSection))
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: snapshot(1) }))
  return stub
}

describe('Site tweaks settings section', () => {
  it('shows the empty state', async () => {
    mount({})
    expect(await screen.findByText('No site tweaks yet')).toBeInTheDocument()
  })

  it('lists each site with what it holds, toggles and deletes after a confirm', async () => {
    const lists = { [P]: [row('a.test', { hasCss: true }), row('b.test', { enabled: false, zapCount: 3 })] }
    const stub = mount(lists, {
      'tweaks.setEnabledForSite': () => null,
      'tweaks.deleteForSite': () => {
        lists[P] = lists[P]?.filter((r) => r.site !== 'a.test') ?? []
        return null
      }
    })
    const a = await screen.findByText('a.test')
    expect(screen.getByText('On · CSS')).toBeInTheDocument()
    expect(screen.getByText('Off · 3 hidden')).toBeInTheDocument()
    expect(a).toHaveAttribute('title', 'a.test')

    fireEvent.click(screen.getByRole('switch', { name: 'Site tweaks for a.test' }))
    expect(stub.calls).toContainEqual({ name: 'tweaks.setEnabledForSite', payload: { profileId: P, site: 'a.test', enabled: false } })

    // Cancel keeps the row; Delete removes it.
    fireEvent.click(screen.getByRole('button', { name: 'Delete site tweak for a.test' }))
    const dialog = screen.getByRole('dialog', { name: 'Delete site tweak?' })
    expect(within(dialog).getByRole('button', { name: 'Cancel' })).toHaveFocus()
    fireEvent.click(within(dialog).getByRole('button', { name: 'Cancel' }))
    expect(stub.calls.some((c) => c.name === 'tweaks.deleteForSite')).toBe(false)
    fireEvent.click(screen.getByRole('button', { name: 'Delete site tweak for a.test' }))
    fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Delete' }))
    expect(stub.calls).toContainEqual({ name: 'tweaks.deleteForSite', payload: { profileId: P, site: 'a.test' } })
    await waitFor(() => expect(screen.queryByText('a.test')).toBeNull())
    expect(screen.queryByRole('dialog')).toBeNull()
  })

  it('reloads when a tab’s tweak changes and switches profiles', async () => {
    const lists = { [P]: [] as SiteTweakRow[], [P2]: [row('work.test', { hasJs: true })] }
    const stub = mount(lists)
    await screen.findByText('No site tweaks yet')
    lists[P] = [row('a.test', { hasCss: true })]
    act(() => stub.push({ kind: 'snapshot', rev: 2, state: snapshot(2, { enabled: true, hasCss: true, hasJs: false, hasControls: false, zapCount: 0 }) }))
    expect(await screen.findByText('a.test')).toBeInTheDocument()

    fireEvent.change(screen.getByRole('combobox', { name: 'Profile' }), { target: { value: P2 } })
    expect(await screen.findByText('work.test')).toBeInTheDocument()
    expect(screen.queryByText('a.test')).toBeNull()
    expect(stub.calls).toContainEqual({ name: 'tweaks.list', payload: { profileId: P2 } })
  })

  it('reports a failed toggle and shows the stored state again', async () => {
    const stub = mount({ [P]: [row('a.test', { hasCss: true })] })
    const sw = await screen.findByRole('switch', { name: 'Site tweaks for a.test' })
    fireEvent.click(sw)
    expect(await screen.findByRole('alert')).toHaveTextContent('Couldn’t turn site tweaks off. Try again.')
    await waitFor(() => expect(screen.getByRole('switch', { name: 'Site tweaks for a.test' })).toHaveAttribute('aria-checked', 'true'))
    expect(stub.calls.filter((c) => c.name === 'tweaks.list').length).toBeGreaterThan(1)
  })

  it('lists saved profiles default first and signs only the profile’s tab tweaks', () => {
    const s = { ...snapshot(1, { enabled: true, hasCss: true, hasJs: false, hasControls: false, zapCount: 2 }) }
    s.profiles['33333333-3333-4333-8333-333333333333'] = profile('33333333-3333-4333-8333-333333333333', 'Private', false, true)
    expect(profileOptions(s).map((p) => p.name)).toEqual(['Personal', 'Work'])
    expect(tweakSignature(s, P)).toBe(`${TAB}:11002`)
    expect(tweakSignature(s, P2)).toBe('')
  })
})
