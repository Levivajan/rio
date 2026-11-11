import { render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { defaultState, defaultTabState } from '../../shared/state'
import { createStateMirror, setStateMirrorForTest } from './store'
import { useCluState } from './useCluState'

const A = '11111111-1111-4111-8111-111111111111'
const B = '22222222-2222-4222-8222-222222222222'

function Url({ tabId }: { tabId: string }) {
  return <span data-testid="url">{useCluState((s) => s.tabs[tabId]?.url ?? '')}</span>
}

afterEach(() => setStateMirrorForTest(null))

describe('useCluState', () => {
  it('reruns a selector that closes over a prop when the prop changes and the state does not', () => {
    const state = defaultState()
    for (const [id, url] of [[A, 'https://a.example.com/'], [B, 'https://b.example.com/']] as const) {
      state.tabs[id] = defaultTabState({ id, profileId: A, spaceId: null, section: 'today', url, title: url, lastActiveAt: 0 })
    }
    const mirror = createStateMirror(async () => null)
    mirror.receive({ kind: 'snapshot', rev: 1, state: { ...state, rev: 1 } })
    setStateMirrorForTest(mirror)
    const { rerender } = render(<Url tabId={A} />)
    expect(screen.getByTestId('url').textContent).toBe('https://a.example.com/')
    rerender(<Url tabId={B} />)
    expect(screen.getByTestId('url').textContent).toBe('https://b.example.com/')
  })
})
