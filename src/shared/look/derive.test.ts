import { describe, expect, it } from 'vitest'
import { colorRgb } from '../theme/space'
import { DARK_TINT, LIGHT_TINT, chromeForTint } from './derive'

describe('chromeForTint', () => {
  it('turns the chrome light for a pastel in dark mode and keeps it dark for a mid color', () => {
    // The sidebar for the wheel's edge (#e1b5b6) and its half radius (#ce3e3f) in dark mode.
    expect(chromeForTint([225, 181, 182], 'dark')).toBe('light')
    expect(chromeForTint([206, 62, 63], 'dark')).toBe('dark')
    expect(chromeForTint(colorRgb({ h: 330, s: 0.95 }, 'dark'), 'dark')).toBe('light')
  })

  it('keeps a dark color dark in light mode and a light tint light', () => {
    // The wheel's center in light mode paints #5e5c5d: light text, never near-black on it.
    expect(chromeForTint(colorRgb({ h: 0, s: 0 }, 'light'), 'light')).toBe('dark')
    expect(chromeForTint([43, 67, 103], 'light')).toBe('dark')
    expect(chromeForTint([225, 181, 182], 'light')).toBe('light')
  })

  it('switches where black and white text contrast equally in light mode, above the pastel line in dark', () => {
    expect(DARK_TINT).toBeLessThan(LIGHT_TINT)
    expect(chromeForTint([0, 0, 0], 'light')).toBe('dark')
    expect(chromeForTint([255, 255, 255], 'dark')).toBe('light')
  })
})
