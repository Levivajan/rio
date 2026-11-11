// The overlay's chrome set and colors follow the space under it: a light space gets light menus
// and a light command bar in dark mode, the selected row is a muted main color, and a space with no theme stays gray.
import { act, renderHook } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { defaultSpaceState, defaultState, defaultWindowState, type CluState } from '../../../shared/state'
import type { SpaceTheme } from '../../../shared/theme/types'
import { hexToRgb, rgbToHsl } from '../../../shared/theme/space'
import { stubClu } from '../../../test-utils/stubClu'
import { stateMirror } from '../../common/store'
import { overlayChrome, POPOVER_GRAY, useSpaceColors } from './useSpaceColors'

const WIN = '00000000-0000-4000-8000-000000000001'
const SPACE = '00000000-0000-4000-8000-000300000001'
/** A light green-to-purple space: its sidebar stays a pastel in dark mode too. */
const PASTEL: SpaceTheme = { colors: [{ h: 120, s: 1 }, { h: 280, s: 1 }], intensity: 1, grain: 0 }
/** The same hues at a lower saturation stay a dark sidebar in dark mode. */
const DEEP: SpaceTheme = { colors: [{ h: 120, s: 0.3 }, { h: 280, s: 0.3 }], intensity: 1, grain: 0 }

function stateWith(theme: SpaceTheme | null, resolved: 'light' | 'dark', accent = '#3b82f6'): CluState {
  const base = defaultState()
  const space = { ...defaultSpaceState({ id: SPACE, profileId: 'p', name: 'Home', color: 'green', icon: null, order: 0 } as never), theme, accent }
  return {
    ...base,
    rev: 1,
    app: { ...base.app, appearance: { ...base.app.appearance, resolved } },
    spaces: { [SPACE]: space },
    windows: { [WIN]: defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE }) }
  }
}

const rootVar = (n: string) => document.documentElement.style.getPropertyValue(n)

describe('overlayChrome', () => {
  it('wears the light set over a pastel space in dark mode and the dark set over a deep one', () => {
    expect(overlayChrome(stateWith(PASTEL, 'dark'), WIN)).toBe('light')
    expect(overlayChrome(stateWith(DEEP, 'dark'), WIN)).toBe('dark')
    expect(overlayChrome(stateWith(PASTEL, 'light'), WIN)).toBe('light')
  })

  it('falls back to the resolved mode for a missing window', () => {
    expect(overlayChrome(stateWith(PASTEL, 'dark'), 'nope')).toBe('dark')
  })
})

describe('useSpaceColors', () => {
  function mount(state: CluState) {
    const stub = stubClu()
    window.clu = { ...stub.clu, windowId: WIN }
    stateMirror()
    act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
    return renderHook(() => useSpaceColors(WIN))
  }

  it('returns the light chrome on a pastel space in dark mode and selects in a muted main color, never neon', () => {
    const { result } = mount(stateWith(PASTEL, 'dark', '#00ff00'))
    expect(result.current).toBe('light')
    const select = hexToRgb(rootVar('--accent-select'))
    expect(select).not.toBeNull()
    const { h, s, l } = rgbToHsl(select ?? [0, 0, 0])
    expect(Math.abs(h - 120)).toBeLessThan(2)
    expect(s).toBeLessThanOrEqual(0.59)
    expect(l).toBeCloseTo(0.68, 1)
    // Light chrome gets a near-black ink in the tint's hue.
    expect(rootVar('--ink')).not.toBe('')
  })

  it('keeps a space with no theme gray: the popovers and the dark hover card mix the gray, not the blue accent', () => {
    const { result } = mount(stateWith(null, 'dark'))
    expect(result.current).toBe('dark')
    expect(rootVar('--popover-accent')).toBe(POPOVER_GRAY)
  })
})
