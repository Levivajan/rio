// The sidebar swipe's frame-local theme blend: the drag never sends spaces.swipe, so the frame
// background follows the blend the swipe writes, not windows[id].swipe.
import { Profiler } from 'react'
import { act, render } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { defaultSpaceState, defaultState, defaultWindowState, type CluState } from '../../../shared/state'
import type { SpaceTheme } from '../../../shared/theme/types'
import { stubClu } from '../../../test-utils/stubClu'
import { frameLayer } from './layer'
import { blendOf, getSwipeBlend, setSwipeBlend } from './swipeBlend'
import { layerSwipe, ThemeBackground } from './ThemeBackground'

const WIN = '00000000-0000-4000-8000-000000000001'
const BLUE = '00000000-0000-4000-8000-000000000002'
const ORANGE = '00000000-0000-4000-8000-000000000003'
const PROFILE = '00000000-0000-4000-8000-000000000004'
const ember: SpaceTheme = { colors: [{ h: 24, s: 0.9 }], intensity: 1, grain: 0.4 }

function state(activeSpaceId = BLUE): CluState {
  const space = (id: string, color: 'blue' | 'orange') => defaultSpaceState({ id, name: color, icon: { kind: 'letter' }, color, profileId: PROFILE, ephemeral: false })
  return {
    ...defaultState(),
    rev: 1,
    windows: { [WIN]: defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId }) },
    spaces: { [BLUE]: space(BLUE, 'blue'), [ORANGE]: { ...space(ORANGE, 'orange'), theme: ember } }
  }
}

afterEach(() => setSwipeBlend(null))

describe('blendOf', () => {
  it('follows the neighbor while dragging and settling, and drops it on a spring back or with no neighbor', () => {
    expect(blendOf({ mode: 'dragging', travel: -99, width: 198, peer: ORANGE })).toEqual({ targetSpaceId: ORANGE, fraction: 0.5 })
    expect(blendOf({ mode: 'committing', travel: 400, width: 198, peer: ORANGE })).toEqual({ targetSpaceId: ORANGE, fraction: 1 })
    expect(blendOf({ mode: 'springing', travel: 50, width: 198, peer: ORANGE })).toBeNull()
    expect(blendOf({ mode: 'dragging', travel: 50, width: 198, peer: null })).toBeNull()
    expect(blendOf(null)).toBeNull()
  })

  it('drops junk widths and travel', () => {
    expect(blendOf({ mode: 'dragging', travel: 50, width: 0, peer: ORANGE })).toBeNull()
    expect(blendOf({ mode: 'dragging', travel: Number.NaN, width: 198, peer: ORANGE })).toBeNull()
    setSwipeBlend({ targetSpaceId: ORANGE, fraction: Number.POSITIVE_INFINITY })
    expect(getSwipeBlend()).toBeNull()
    setSwipeBlend({ targetSpaceId: ORANGE, fraction: -2 })
    expect(getSwipeBlend()).toEqual({ targetSpaceId: ORANGE, fraction: 1 })
  })
})

describe('layerSwipe', () => {
  it('gives frameLayer the drag while windows[id].swipe stays null', () => {
    const s = state()
    expect(s.windows[WIN]?.swipe).toBeNull()
    const swipe = layerSwipe(s, WIN, { targetSpaceId: ORANGE, fraction: 0.5 })
    expect(swipe).toEqual({ target: { theme: ember, color: 'orange' }, fraction: 0.5 })
    expect(layerSwipe(s, WIN, null)).toBeNull()
  })

  it('ends the blend once its target is the active space, so the switch cross-fade finishes it', () => {
    expect(layerSwipe(state(ORANGE), WIN, { targetSpaceId: ORANGE, fraction: 0.7 })).toBeNull()
  })

  it('falls back to the browser swipe without a blend', () => {
    const s = state()
    const w = s.windows[WIN]
    if (w) s.windows[WIN] = { ...w, swipe: { targetSpaceId: ORANGE, fraction: 0.25 } }
    expect(layerSwipe(s, WIN, null)).toMatchObject({ fraction: 0.25, target: { color: 'orange' } })
  })
})

describe('ThemeBackground during a sidebar drag', () => {
  it('blends the background toward the target by the drag fraction, with no transition', () => {
    const stub = stubClu({})
    window.clu = { ...stub.clu, windowId: WIN }
    const view = render(<ThemeBackground />)
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: state() }))
    const bg = () => view.container.querySelector<HTMLElement>('.clu-theme-bg') as HTMLElement
    const at = (fraction: number) =>
      frameLayer({ active: { theme: null, color: 'blue' }, swipe: fraction ? { target: { theme: ember, color: 'orange' }, fraction } : null, resolved: state().app.appearance.resolved, reduceTransparency: false, focused: state().windows[WIN]?.focused ?? false })
    expect(bg().style.getPropertyValue('--clu-theme-c0')).toBe(at(0).stops[0])

    act(() => setSwipeBlend(blendOf({ mode: 'dragging', travel: 99, width: 198, peer: ORANGE })))
    expect(bg().style.getPropertyValue('--clu-theme-c0')).toBe(at(0.5).stops[0])
    expect(bg().style.getPropertyValue('--clu-theme-c0')).not.toBe(at(0).stops[0])
    expect(bg().dataset.swiping).toBe('true')
    expect(bg().style.transition).toBe('none')

    act(() => setSwipeBlend(null))
    expect(bg().style.getPropertyValue('--clu-theme-c0')).toBe(at(0).stops[0])
    view.unmount()
  })

  it('follows each drag frame with a style write, not a React render', () => {
    const stub = stubClu({})
    window.clu = { ...stub.clu, windowId: WIN }
    let renders = 0
    const view = render(
      <Profiler id="bg" onRender={() => renders++}>
        <ThemeBackground />
      </Profiler>
    )
    act(() => stub.push({ kind: 'snapshot', rev: 1, state: state() }))
    const bg = () => view.container.querySelector<HTMLElement>('.clu-theme-bg') as HTMLElement
    const at = (fraction: number) =>
      frameLayer({ active: { theme: null, color: 'blue' }, swipe: { target: { theme: ember, color: 'orange' }, fraction }, resolved: state().app.appearance.resolved, reduceTransparency: false, focused: state().windows[WIN]?.focused ?? false })
    act(() => setSwipeBlend(blendOf({ mode: 'dragging', travel: 20, width: 200, peer: ORANGE })))
    const begun = renders
    for (const travel of [40, 60, 80, 100, 150]) {
      act(() => setSwipeBlend(blendOf({ mode: 'dragging', travel, width: 200, peer: ORANGE })))
      expect(bg().style.getPropertyValue('--clu-theme-c0')).toBe(at(travel / 200).stops[0])
    }
    expect(renders).toBe(begun)
    view.unmount()
  })
})
