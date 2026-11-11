// Overlay surfaces of the multitask module.
import type { SurfaceRegistry } from '../core/types'
import { PeekChrome } from './PeekChrome'

export const surfaces: SurfaceRegistry = { peek: PeekChrome }
