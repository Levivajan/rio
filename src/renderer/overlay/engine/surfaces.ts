// Overlay surfaces of the engine module.
import type { SurfaceRegistry } from '../core/types'
import { AuthDialog } from './AuthDialog'
import { ClientCertPrompt } from './ClientCertPrompt'
import { ContextMenu } from './ContextMenu'
import './engine.css'
import { FindBar } from './FindBar'
import { FullscreenNotice } from './FullscreenNotice'
import { LeaveSitePrompt } from './LeaveSitePrompt'
import { NavSwipeArrow } from './NavSwipeArrow'
import { UnresponsivePrompt } from './UnresponsivePrompt'
import { ZoomPill } from './ZoomPill'

export const surfaces: SurfaceRegistry = {
  'auth-dialog': AuthDialog,
  'client-cert-prompt': ClientCertPrompt,
  'find-bar': FindBar,
  'fullscreen-notice': FullscreenNotice,
  'leave-prompt': LeaveSitePrompt,
  'nav-swipe-arrow': NavSwipeArrow,
  'page-menu': ContextMenu,
  'unresponsive-prompt': UnresponsivePrompt,
  'zoom-pill': ZoomPill
}
