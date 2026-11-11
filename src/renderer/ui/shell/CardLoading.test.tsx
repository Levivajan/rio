import { act, render } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { LOOK } from '../../../shared/look/tokens'
import { defaultState, defaultTabState, defaultWindowState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { CardLoading, IDLE, nextLoadingTrack, pulseOpacity, type LoadingTrack } from './CardLoading'

const WIN = '00000000-0000-4000-8000-000000000001'
const TAB = '00000000-0000-4000-8000-000000000002'

function mount(loading: boolean) {
  const stub = stubClu({})
  window.clu = stub.clu
  const view = render(<CardLoading />)
  let rev = 0
  const push = (tab: { loading: boolean; progress?: number; title?: string }) => {
    rev += 1
    const state: CluState = { ...defaultState(), rev }
    const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
    win.activeTabId = TAB
    state.windows = { [WIN]: win }
    state.tabs = { [TAB]: { ...defaultTabState({ id: TAB, profileId: WIN, spaceId: WIN, section: 'today', url: 'https://example.com/' }), ...tab } }
    act(() => stub.push({ kind: 'snapshot', rev, state }))
  }
  push({ loading })
  return Object.assign(view.container, { push })
}

afterEach(() => vi.useRealTimers())

describe('CardLoading', () => {
  it('puts the grabber in a zone over the margin above the card while the page is not loading', () => {
    const root = mount(false)
    const zone = root.querySelector<HTMLElement>('[data-shell="card-grabber-zone"]')
    expect(zone?.querySelector('[data-shell="card-grabber"]')).not.toBeNull()
    expect(root.querySelector('[data-shell="card-loading"]')).toBeNull()
    const card = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN }).shell.layout.pageRect
    // The zone spans the card's width and the whole margin above it; CSS shows the pill only while it is hovered.
    expect(zone?.style.left).toBe(`${card.x}px`)
    expect(zone?.style.width).toBe(`${card.width}px`)
    expect(zone?.style.height).toBe(`${card.y}px`)
  })

  it('the loading pill replaces the grabber while the page loads', () => {
    const root = mount(true)
    expect(root.querySelector('[data-shell="card-loading"]')).not.toBeNull()
    expect(root.querySelector('[data-shell="card-grabber-zone"]')).toBeNull()
  })

  it('a progress patch in the same phase keeps the same element, so no animation restarts', () => {
    const root = mount(true)
    const pill = root.querySelector('[data-shell="card-loading"]')
    expect(pill?.getAttribute('data-phase')).toBe('waiting')
    root.push({ loading: true, progress: 0.05, title: 'Echo' })
    expect(root.querySelector('[data-shell="card-loading"]')).toBe(pill)
    root.push({ loading: true, progress: 0.4 })
    const receiving = root.querySelector('[data-shell="card-loading"]')
    expect(receiving).toBe(pill)
    expect(receiving?.getAttribute('data-phase')).toBe('receiving')
    root.push({ loading: true, progress: 0.7 })
    expect(root.querySelector('[data-shell="card-loading"]')?.getAttribute('data-phase')).toBe('receiving')
  })

  it('a finished load plays the done fade once, then shows the grabber (fake timers)', () => {
    vi.useFakeTimers()
    const root = mount(true)
    root.push({ loading: true, progress: 0.5 })
    root.push({ loading: false, progress: 1 })
    expect(root.querySelector('[data-shell="card-loading"]')?.getAttribute('data-phase')).toBe('done')
    root.push({ loading: false, progress: 1, title: 'Echo' })
    act(() => vi.advanceTimersByTime(LOOK.loading.doneMs - 1))
    expect(root.querySelector('[data-shell="card-loading"]')?.getAttribute('data-phase')).toBe('done')
    act(() => vi.advanceTimersByTime(1))
    expect(root.querySelector('[data-shell="card-loading"]')).toBeNull()
    expect(root.querySelector('[data-shell="card-grabber-zone"]')).not.toBeNull()
  })
})

describe('loading motion', () => {
  it('pulses 0 to 1 to 0, ease-in-out each way', () => {
    expect(pulseOpacity(0)).toBe(0)
    expect(pulseOpacity(400)).toBeCloseTo(0.5, 3)
    expect(pulseOpacity(800)).toBeCloseTo(1, 3)
    expect(pulseOpacity(1200)).toBeCloseTo(0.5, 3)
    expect(pulseOpacity(1600)).toBeCloseTo(0, 3)
    expect(pulseOpacity(200)).toBeLessThan(0.2)
    expect(pulseOpacity(Number.NaN)).toBe(0)
  })

  it('the same live phase keeps the same track (one animation per trigger)', () => {
    const waiting = nextLoadingTrack(IDLE, 'waiting', 1000, false)
    expect(waiting).toEqual({ phase: 'waiting', since: 1000, from: 0, pill: false })
    expect(nextLoadingTrack(waiting, 'waiting', 5000, false)).toBe(waiting)
    const done: LoadingTrack = { phase: 'done', since: 9000, from: 1, pill: false }
    expect(nextLoadingTrack(done, null, 9100, false)).toBe(done)
    expect(nextLoadingTrack(IDLE, null, 9100, false)).toBe(IDLE)
  })

  it('an interrupted phase continues from its current opacity', () => {
    const waiting = nextLoadingTrack(IDLE, 'waiting', 0, false)
    const receiving = nextLoadingTrack(waiting, 'receiving', 400, false)
    expect(receiving.phase).toBe('receiving')
    expect(receiving.from).toBeCloseTo(0.5, 3)
    // Done during the widening starts the fade from where the opacity got to.
    const early = nextLoadingTrack(receiving, null, 400 + LOOK.loading.openMs / 2, false)
    expect(early).toMatchObject({ phase: 'done', pill: false })
    expect(early.from).toBeCloseTo(0.75, 3)
    expect(nextLoadingTrack(receiving, null, 5000, false).from).toBe(1)
    // A page that finishes while still waiting fades its pill from the pulse's opacity.
    expect(nextLoadingTrack(waiting, null, 800, false)).toMatchObject({ phase: 'done', pill: true, from: expect.closeTo(1, 3) })
  })

  it('reduced motion skips the done fade', () => {
    const receiving = nextLoadingTrack(IDLE, 'receiving', 0, true)
    expect(nextLoadingTrack(receiving, null, 100, true)).toBe(IDLE)
  })
})
