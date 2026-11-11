// Overlay surfaces of the data module: the clear browsing data dialog.
import type { SurfaceRegistry } from '../core/types'
import { ClearDataSurface } from './ClearData'

export const surfaces: SurfaceRegistry = { 'clear-data': ClearDataSurface }
