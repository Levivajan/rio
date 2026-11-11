// The active space's tint (its main color) and accent on the overlay view's root, as ThemeBackground publishes them in
// the frame: menus, tooltips, hover cards and the Peek backdrop are tinted by the window under them. The overlay wears
// the same chrome set as the frame (useTintChrome): a light space gets light menus and a light command bar even when
// macOS is dark.
import { useEffect } from 'react'
import { chromeForTint, inkForTint } from '../../../shared/look/derive'
import type { ResolvedMode } from '../../../shared/theme/appearance'
import { hexToRgb } from '../../../shared/theme/space'
import type { CluState } from '../../../shared/state'
import { useCluState } from '../../common/useCluState'
import { frameLayer, mainTint, selectionTint, sidebarTint, strongAccent, type Layer } from '../../ui/theme/layer'

/** What the popovers mix into near-black in a space with no theme: #151516 for the panel, #58585b for the hovered row. */
export const POPOVER_GRAY = '#8e8e93'

const layerOf = (s: CluState, windowId: string): { layer: Layer; colors: number } => {
  const w = s.windows[windowId]
  const space = w?.activeSpaceId ? s.spaces[w.activeSpaceId] : undefined
  const layer = frameLayer({
    active: space ? { theme: space.theme, color: space.color } : undefined,
    swipe: null,
    resolved: s.app.appearance.resolved,
    reduceTransparency: s.app.appearance.reduceTransparency,
    focused: true
  })
  return { layer, colors: space?.theme?.colors.length ?? 0 }
}

/** The chrome set for the color the window's sidebar sits on, the frame's useTintChrome rule. */
export function overlayChrome(s: CluState, windowId: string): ResolvedMode {
  const { layer, colors } = layerOf(s, windowId)
  const rgb = hexToRgb(sidebarTint(layer.stops, colors))
  return rgb ? chromeForTint(rgb, s.app.appearance.resolved) : s.app.appearance.resolved
}

/** Publishes the space's colors on the overlay root and returns the chrome set the overlay wears. */
export function useSpaceColors(windowId: string): ResolvedMode {
  const tint = useCluState((s) => mainTint(layerOf(s, windowId).layer))
  const chrome = useCluState((s) => overlayChrome(s, windowId))
  const accent = useCluState((s) => s.spaces[s.windows[windowId]?.activeSpaceId ?? '']?.accent ?? null)
  // A space with no theme draws a gray sidebar, so the popovers tinted by it are gray too.
  const gray = useCluState((s) => {
    const space = s.spaces[s.windows[windowId]?.activeSpaceId ?? '']
    return space ? !space.theme : false
  })
  const light = chrome === 'light'
  useEffect(() => {
    const root = document.documentElement.style
    const rgb = hexToRgb(tint)
    if (!rgb) return
    root.setProperty('--tint', tint)
    if (light) root.setProperty('--ink', inkForTint(rgb))
    else root.removeProperty('--ink')
  }, [tint, light])
  useEffect(() => {
    const root = document.documentElement.style
    if (accent && hexToRgb(accent)) root.setProperty('--accent', accent)
    const strong = accent ? strongAccent(accent, light) : null
    if (strong) root.setProperty('--accent-strong', strong)
    else root.removeProperty('--accent-strong')
    const select = accent ? selectionTint(accent, light) : null
    if (select) root.setProperty('--accent-select', select)
    else root.removeProperty('--accent-select')
  }, [accent, light])
  useEffect(() => {
    const root = document.documentElement.style
    if (gray) root.setProperty('--popover-accent', POPOVER_GRAY)
    else root.removeProperty('--popover-accent')
  }, [gray])
  return chrome
}
