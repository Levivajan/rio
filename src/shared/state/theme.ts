// State fields of the theme module.
import type { SpaceTheme } from '../types'

export interface Appearance { mode: 'auto' | 'light' | 'dark'; resolved: 'light' | 'dark'; reduceTransparency: boolean; reduceMotion: boolean }
export interface ThemeAppFields { appearance: Appearance; onboarding: { show: boolean } }
export const defaultThemeApp = (): ThemeAppFields => ({
  appearance: { mode: 'auto', resolved: 'light', reduceTransparency: false, reduceMotion: false },
  onboarding: { show: false }
})

export interface ThemeSpaceFields { theme: SpaceTheme | null; accent: string }
export const defaultThemeSpace = (): ThemeSpaceFields => ({ theme: null, accent: '#3b82f6' })
