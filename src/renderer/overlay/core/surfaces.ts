// Core overlay surfaces. Lanes register theirs in src/renderer/overlay/<lane>/surfaces.ts.
import { ConfirmSurface } from './Confirm'
import { MenuSurface } from './Menu'
import { ToastSurface } from './Toast'
import type { SurfaceRegistry } from './types'

export const surfaces: SurfaceRegistry = {
  toast: ToastSurface,
  menu: MenuSurface,
  confirm: ConfirmSurface
}
