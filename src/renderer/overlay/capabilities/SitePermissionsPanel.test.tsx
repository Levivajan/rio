import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState, type TabState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { SitePermissionsPanel } from './SitePermissionsPanel'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-000000000002'

async function mount(overrides: Record<string, (payload: unknown) => unknown> = {}) {
  const stub = stubClu(overrides)
  window.clu = { ...stub.clu, windowId: WIN }
  stateMirror()
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  win.activeTabId = TAB
  const tabs: Record<string, TabState> = { [TAB]: defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url: 'https://a.com/' }) }
  act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, windows: { [WIN]: win }, tabs } }))
  await act(async () => {
    render(<SitePermissionsPanel tabId={TAB} />)
  })
  return stub
}

describe('SitePermissionsPanel', () => {
  it('shows an error, not an empty section, when the browser cannot read the permissions', async () => {
    const stub = await mount()
    expect(stub.calls.some((c) => c.name === 'permission.list')).toBe(true)
    expect(document.querySelector('.clu-site-permissions')).not.toBeNull()
    expect(screen.getByRole('alert').textContent).toBe('Could not read permissions')
  })

  it('shows each permission with the stored decision', async () => {
    await mount({ 'permission.list': () => [{ permission: 'camera', decision: 'allow' }] })
    expect(document.querySelector('[data-permission="camera"]')).toHaveAttribute('data-decision', 'allow')
    expect(document.querySelector('[data-permission="microphone"]')).toHaveAttribute('data-decision', 'ask')
    expect(screen.queryByRole('alert')).toBeNull()
  })

  it('adds the other permissions the site has a decision for, after the six, with their labels', async () => {
    await mount({
      'permission.list': () => [
        { permission: 'notifications', decision: 'deny' },
        { permission: 'midi', decision: 'deny' },
        { permission: 'sound', decision: 'allow' },
        { permission: 'futureKind', decision: 'allow' }
      ]
    })
    const shown = [...document.querySelectorAll('[data-permission]')].map((li) => li.getAttribute('data-permission'))
    expect(shown).toEqual(['camera', 'microphone', 'location', 'notifications', 'clipboard', 'popups', 'midi', 'sound', 'futureKind'])
    expect(document.querySelector('[data-permission="midi"]')).toHaveAttribute('data-decision', 'block')
    expect(screen.getByLabelText('MIDI devices')).toBeTruthy()
    expect(screen.getByLabelText('Sound')).toHaveValue('allow')
    // A kind this build has no label for shows as its name instead of breaking the list.
    expect(screen.getByLabelText('futureKind')).toBeTruthy()
  })

  it('Allow and Block set the decision, Ask clears it, and Reset clears the site', async () => {
    let rows: { permission: string; decision: string }[] = []
    const stub = await mount({
      'permission.list': () => rows,
      'permission.set': (p) => {
        const { permission, decision } = p as { permission: string; decision: string }
        rows = [...rows.filter((r) => r.permission !== permission), { permission, decision: decision === 'allow' ? 'allow' : 'deny' }]
      },
      'permission.clear': (p) => {
        const { permission } = p as { permission?: string }
        rows = permission ? rows.filter((r) => r.permission !== permission) : []
      }
    })
    await act(async () => {
      fireEvent.change(screen.getByLabelText('Camera'), { target: { value: 'block' } })
    })
    expect(stub.calls.find((c) => c.name === 'permission.set')?.payload).toEqual({ profileId: WIN, origin: 'https://a.com', permission: 'camera', decision: 'block' })
    expect(document.querySelector('[data-permission="camera"]')).toHaveAttribute('data-decision', 'block')
    expect(screen.getByRole('button', { name: 'Reload to apply' })).toBeTruthy()
    await act(async () => {
      fireEvent.change(screen.getByLabelText('Camera'), { target: { value: 'ask' } })
    })
    expect(stub.calls.find((c) => c.name === 'permission.clear')?.payload).toEqual({ profileId: WIN, origin: 'https://a.com', permission: 'camera' })
    expect(document.querySelector('[data-permission="camera"]')).toHaveAttribute('data-decision', 'ask')
    await act(async () => {
      fireEvent.change(screen.getByLabelText('Location'), { target: { value: 'allow' } })
    })
    await act(async () => {
      fireEvent.click(screen.getByRole('button', { name: 'Reset permissions' }))
    })
    expect(stub.calls.filter((c) => c.name === 'permission.clear').at(-1)?.payload).toEqual({ profileId: WIN, origin: 'https://a.com' })
    expect(document.querySelector('[data-permission="location"]')).toHaveAttribute('data-decision', 'ask')
  })

  it('shows nothing on a page with no web origin', async () => {
    const stub = stubClu()
    window.clu = { ...stub.clu, windowId: WIN }
    stateMirror()
    const tabs: Record<string, TabState> = { [TAB]: defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url: 'chrome://settings/' }) }
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: { ...defaultState(), rev: 1, tabs } }))
    await act(async () => {
      render(<SitePermissionsPanel tabId={TAB} />)
    })
    expect(document.querySelector('.clu-site-permissions')).toBeNull()
    expect(stub.calls.some((c) => c.name === 'permission.list')).toBe(false)
  })
})
