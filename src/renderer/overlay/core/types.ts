// Overlay surface registry types.
import type { ComponentType } from 'react'
import type { OverlaySurfaceState } from '../../../shared/state'
import { clu } from '../../common/clu'

export interface SurfaceProps { instance: OverlaySurfaceState; windowId: string }
export type SurfaceRegistry = Record<string, ComponentType<SurfaceProps>>

export const respond = (instanceId: string, result: import('../../../shared/types').Json) => void clu().overlay.respond({ instanceId, result })
export const closeInstance = (instanceId: string) => void clu().overlay.close({ instanceId })
