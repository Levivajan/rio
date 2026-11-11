// Overlay surfaces of the capabilities module.
import type { SurfaceRegistry } from '../core/types'
import { DownloadsPopover } from './DownloadsPopover'
import { PermissionPrompt } from './PermissionPrompt'
import { SitePermissionsPopover } from './SitePermissionsPopover'

export const surfaces: SurfaceRegistry = {
  downloads: DownloadsPopover,
  'permission-prompt': PermissionPrompt,
  'site-permissions': SitePermissionsPopover
}
