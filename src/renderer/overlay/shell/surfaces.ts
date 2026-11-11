// Overlay surfaces of the shell module.
import type { SurfaceRegistry } from '../core/types'
import { MenuPopover } from './MenuPopover'
import { NavHistoryMenu } from './NavHistoryMenu'
import { SidebarPeek } from './SidebarPeek'
import { TooltipSurface } from './TooltipSurface'

export const surfaces: SurfaceRegistry = {
  'sidebar-peek': SidebarPeek,
  'nav-history': NavHistoryMenu,
  'menu-popover': MenuPopover,
  tooltip: TooltipSurface
}
