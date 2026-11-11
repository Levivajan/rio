import { describe, expect, it } from 'vitest'
import { pageBackdrop, parseAppearanceMode, themeSourceFor, translucencyAlpha } from './appearance'

describe('translucencyAlpha', () => {
  it('follows intensity by mode while the window is focused', () => {
    expect(translucencyAlpha({ resolved: 'light', intensity: 0, reduceTransparency: false, focused: true })).toBe(0.3)
    expect(translucencyAlpha({ resolved: 'light', intensity: 1, reduceTransparency: false, focused: true })).toBe(0.85)
    expect(translucencyAlpha({ resolved: 'dark', intensity: 0, reduceTransparency: false, focused: true })).toBe(0.4)
    expect(translucencyAlpha({ resolved: 'dark', intensity: 0.5, reduceTransparency: false, focused: true })).toBe(0.65)
  })

  it('is 1.0 with Reduce transparency or an unfocused window', () => {
    for (const resolved of ['light', 'dark'] as const) {
      expect(translucencyAlpha({ resolved, intensity: 0.2, reduceTransparency: true, focused: true })).toBe(1)
      expect(translucencyAlpha({ resolved, intensity: 0.2, reduceTransparency: false, focused: false })).toBe(1)
      expect(translucencyAlpha({ resolved, intensity: 0.2, reduceTransparency: true, focused: false })).toBe(1)
    }
  })

  it('clamps junk intensity', () => {
    expect(translucencyAlpha({ resolved: 'light', intensity: 7, reduceTransparency: false, focused: true })).toBe(0.85)
    expect(translucencyAlpha({ resolved: 'light', intensity: -1, reduceTransparency: false, focused: true })).toBe(0.3)
    expect(translucencyAlpha({ resolved: 'dark', intensity: Number.NaN, reduceTransparency: false, focused: true })).toBe(0.65)
  })
})

describe('appearance mode', () => {
  it('reads junk as auto', () => {
    for (const junk of ['Dark', 'system', '', null, undefined, 3, {}, ['dark']]) expect(parseAppearanceMode(junk)).toBe('auto')
    expect(parseAppearanceMode('dark')).toBe('dark')
    expect(parseAppearanceMode('light')).toBe('light')
  })

  it('maps auto to the system theme source', () => {
    expect(themeSourceFor('auto')).toBe('system')
    expect(themeSourceFor('light')).toBe('light')
    expect(themeSourceFor('dark')).toBe('dark')
  })

  it('never gives a white backdrop in dark mode', () => {
    expect(pageBackdrop('dark')).toBe('#1e1e1e')
    expect(pageBackdrop('light')).toBe('#ffffff')
  })
})
