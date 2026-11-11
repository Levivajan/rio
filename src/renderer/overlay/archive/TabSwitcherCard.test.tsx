import { fireEvent, render } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { OverlaySurfaceState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { TabSwitcher } from './TabSwitcher'

const WIN = '00000000-0000-4000-8000-000000000001'
const A = '11111111-1111-4111-8111-111111111111'
const B = '22222222-2222-4222-8222-222222222222'

function renderSwitcher() {
  const stub = stubClu()
  window.clu = stub.clu
  const instance = {
    instanceId: '77777777-7777-4777-8777-777777777777',
    surface: 'tab-switcher',
    layer: 'passive',
    modality: 'passive',
    rect: null,
    openedFrom: null,
    focus: false,
    slot: 1,
    props: { cards: [{ tabId: A, title: 'A', host: 'a.test' }, { tabId: B, title: 'B', host: 'b.test' }], selected: 1 }
  } as OverlaySurfaceState
  const view = render(<TabSwitcher instance={instance} windowId={WIN} />)
  const card = (id: string) => view.container.querySelector(`[data-tab-id="${id}"]`) as HTMLElement
  const commits = () => stub.calls.filter((c) => c.name === 'switcher.respond').map((c) => c.payload)
  return { card, commits }
}

describe('TabSwitcher cards', () => {
  it('commits the card the primary button releases on, with Control held as the hand holds it', () => {
    const { card, commits } = renderSwitcher()
    fireEvent.pointerDown(card(A), { button: 0, ctrlKey: true })
    fireEvent.pointerUp(card(A), { button: 0, ctrlKey: true })
    expect(commits()).toEqual([{ action: 'commit', tabId: A }])
    fireEvent.pointerUp(card(B), { button: 0 })
    expect(commits()).toEqual([{ action: 'commit', tabId: A }, { action: 'commit', tabId: B }])
  })

  it('ignores the other buttons and keeps the context menu away', () => {
    const { card, commits } = renderSwitcher()
    fireEvent.pointerUp(card(A), { button: 2 })
    expect(commits()).toEqual([])
    expect(fireEvent.contextMenu(card(A))).toBe(false)
  })
})
