// Overlay surfaces of the split module.
import { SPLIT_DROP_SURFACE } from '../../../shared/split'
import type { SurfaceRegistry } from '../core/types'
import { SplitDrop } from './SplitDrop'

export const surfaces: SurfaceRegistry = { [SPLIT_DROP_SURFACE]: SplitDrop }
