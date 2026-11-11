// ZapBanner against a stub window.clu (tweaks-t2): shows only for the active tab in zap mode, Undo and Done.
import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultState, defaultTabState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { ZapBanner } from './ZapBanner'

const TAB = '22222222-2222-4222-8222-222222222221'

function snapshot(zapMode: CluState['tabs'][string]['zapMode'], zapCount: number, rev = 1): CluState {
  const base = defaultState()
  const tab = { ...defaultTabState({ id: TAB, profileId: TAB, spaceId: null, section: 'today', url: 'http://a.test/', title: '', lastActiveAt: 0 }), zapMode, tweak: { enabled: true, hasCss: false, hasJs: false, hasControls: false, zapCount } }
  const stub = stubClu()
  const windowId = stub.clu.windowId as string
  const win = { ...(Object.values(base.windows)[0] ?? {}), activeTabId: TAB } as CluState['windows'][string]
  return { ...base, rev, tabs: { [TAB]: tab }, windows: { [windowId]: win } }
}

describe('ZapBanner', () => {
  it('shows the count and last result while zapping, and sends Undo and Done for the tab', () => {
    const stub = stubClu({ 'tweaks.undoZap': () => null, 'tweaks.stopZap': () => null })
    window.clu = stub.clu
    const { container } = render(<ZapBanner />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: snapshot(null, 0) }))
    expect(container).toBeEmptyDOMElement()

    act(() => stub.push({ kind: 'snapshot', rev: 2, state: snapshot({ active: true, lastResult: null }, 1, 2) }))
    const banner = screen.getByRole('region', { name: 'Zap mode' })
    expect(banner).toHaveTextContent('Click an element to hide it')
    expect(banner).toHaveTextContent('1 hidden')
    act(() => stub.push({ kind: 'snapshot', rev: 3, state: snapshot({ active: true, lastResult: 'Hid 3 related elements' }, 2, 3) }))
    expect(screen.getByRole('status')).toHaveTextContent('Hid 3 related elements')
    expect(banner).toHaveTextContent('2 hidden')

    fireEvent.click(screen.getByRole('button', { name: 'Undo' }))
    fireEvent.click(screen.getByRole('button', { name: 'Done' }))
    expect(stub.calls.filter((c) => c.name.startsWith('tweaks.'))).toEqual([
      { name: 'tweaks.undoZap', payload: { tabId: TAB } },
      { name: 'tweaks.stopZap', payload: { tabId: TAB } }
    ])
  })
})
