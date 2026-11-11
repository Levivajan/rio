// The `window.clu` shape every Clu surface sees. Built from the contract by the MojoBridge and the
// FakeBridge (src/renderer/common/bridge).
import type { CluAreas } from './ipc'
import type { StateMessage } from './state'

export interface CluBridgeExtras {
  onState(cb: (msg: StateMessage) => void): () => void
  /** The window this surface belongs to (null for internal pages). */
  windowId: string | null
  /** Overlay views only: their layer and passive slot. */
  layer: 'top' | 'under-peek' | 'passive' | null
  slot: number | null
}
export type CluBridge = CluAreas & CluBridgeExtras
