// Appearance helpers (docs/plan/01-shell.md F8 and F10): mode parsing, nativeTheme source,
// translucency alpha of the theme layer and the backdrop color of page views.
import type { AppearanceMode } from './types'

export type ResolvedMode = 'light' | 'dark'

const MODES: readonly AppearanceMode[] = ['auto', 'light', 'dark']

/** A stored or received mode; anything else reads as auto. */
export function parseAppearanceMode(value: unknown): AppearanceMode {
  return MODES.includes(value as AppearanceMode) ? (value as AppearanceMode) : 'auto'
}

/** The `nativeTheme.themeSource` that applies a mode: auto follows macOS. */
export function themeSourceFor(mode: AppearanceMode): 'system' | 'light' | 'dark' {
  return mode === 'auto' ? 'system' : mode
}

/** Intensity of a space with no theme row (default theme: Slate at 0.5). */
export const DEFAULT_INTENSITY = 0.5

export interface TranslucencyInput { resolved: ResolvedMode; intensity: number; reduceTransparency: boolean; focused: boolean }

/**
 * Alpha of the theme layer over the window's vibrancy: 0.30 + 0.55 x intensity in light, 0.40 + 0.50 x intensity
 * in dark. Opaque (1.0) with macOS "Reduce transparency" or while the window is inactive, as macOS sidebars flatten.
 */
export function translucencyAlpha({ resolved, intensity, reduceTransparency, focused }: TranslucencyInput): number {
  if (reduceTransparency || !focused) return 1
  const i = Number.isFinite(intensity) ? Math.min(1, Math.max(0, intensity)) : DEFAULT_INTENSITY
  const alpha = resolved === 'dark' ? 0.4 + 0.5 * i : 0.3 + 0.55 * i
  return Math.round(alpha * 1000) / 1000
}

/** Page view background before its first commit (tokens.css `--surface` light, `--surface-2` dark), never white in dark. */
export function pageBackdrop(resolved: ResolvedMode): string {
  return resolved === 'dark' ? '#1e1e1e' : '#ffffff'
}
