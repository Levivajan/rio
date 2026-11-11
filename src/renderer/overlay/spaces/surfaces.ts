// Overlay surfaces of the spaces module: the space editor (with the profile manager as its second view).
import { SPACE_EDITOR_SURFACE } from '../../../shared/spaces'
import type { SurfaceRegistry } from '../core/types'
import { SpaceEditorSurface } from './SpaceEditor'

export const surfaces: SurfaceRegistry = { [SPACE_EDITOR_SURFACE]: SpaceEditorSurface }
