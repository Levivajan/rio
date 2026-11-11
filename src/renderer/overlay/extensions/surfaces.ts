// Overlay surfaces of the extensions module.
import type { SurfaceRegistry } from '../core/types'
import { ExtensionsMenu } from './ExtensionsMenu'
import { InstallPrompt } from './InstallPrompt'
import { SiteControlsPopover } from './SiteControlsPopover'

export const surfaces: SurfaceRegistry = { 'site-controls': SiteControlsPopover, 'install-prompt': InstallPrompt, 'extensions-menu': ExtensionsMenu }
