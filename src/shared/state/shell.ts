// State fields of the shell module.
import { CARD_INSET, CARD_RADIUS, SIDEBAR_WIDTH, TRAFFIC_SPACER, type ShellLayout } from '../shell/layout'
import type { SidebarAnim } from '../shell/tween'

export interface ShellWindowFields {
  shell: {
    sidebar: { width: number; effectiveWidth: number; hidden: boolean; peeking: boolean; anim: SidebarAnim | null }
    focusMode: boolean
    /** Toolbar mode: the URL bar across the top of the page instead of the pill in the sidebar (⇧⌘D). */
    toolbar: boolean
    /** In the window's own full screen, the height the bar macOS slides down at the screen's top covers at the
     *  window's top (the menu bar and the titlebar); 0 or absent otherwise. */
    titlebarReveal?: number
    layout: ShellLayout
  }
}
export const defaultShellWindow = (): ShellWindowFields => ({
  shell: {
    sidebar: { width: SIDEBAR_WIDTH, effectiveWidth: SIDEBAR_WIDTH, hidden: false, peeking: false, anim: null },
    focusMode: false,
    toolbar: false,
    layout: {
      contentSize: { width: 0, height: 0 },
      pageRect: { x: SIDEBAR_WIDTH, y: CARD_INSET, width: 0, height: 0 },
      sidebarRect: { x: 0, y: 0, width: SIDEBAR_WIDTH, height: 0 },
      hotZone: null,
      radius: CARD_RADIUS,
      effectiveWidth: SIDEBAR_WIDTH,
      trafficSpacer: TRAFFIC_SPACER,
      trafficLights: true
    }
  }
})
