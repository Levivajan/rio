// Core state fields: window core, overlay surfaces, toasts, app version.
import type { InstanceId, Json, Modality, OverlayLayer, Rect, SurfaceId, WindowId } from '../types'

export interface OverlaySurfaceState {
  instanceId: InstanceId
  surface: SurfaceId
  layer: OverlayLayer
  modality: Modality
  rect: Rect | null
  openedFrom: InstanceId | null
  focus: boolean
  /** Passive pool slot (0..3) for `layer: 'passive'`, else null. */
  slot: number | null
  props: Json
}
export interface ToastState { id: string; text: string; action?: { label: string; commandId: string }; until: number }

export interface CoreWindowFields {
  id: WindowId
  kind: 'main' | 'mini'
  private: boolean
  focused: boolean
  fullscreen: boolean
  /**
   * `topRect`: the part of the window the top overlay covers now, in window coordinates, while it shows only passive
   * surfaces with a rect (the tweak editor), so the page beside them takes clicks; null or absent while it covers the
   * whole window. The overlay WebUI draws from that origin (docs/chromium/fixes.md).
   */
  overlay: { surfaces: OverlaySurfaceState[]; topRect?: Rect | null }
  toasts: ToastState[]
}
export interface WindowInit { id: WindowId; kind: 'main' | 'mini'; private: boolean; activeSpaceId: string }

export function defaultCoreWindow(init: WindowInit): CoreWindowFields {
  return { id: init.id, kind: init.kind, private: init.private, focused: false, fullscreen: false, overlay: { surfaces: [] }, toasts: [] }
}

export interface CoreAppFields { version: string; isPackaged: boolean; versions: { chrome: string; node: string } }
export function defaultCoreApp(): CoreAppFields {
  return { version: '0.0.0', isPackaged: false, versions: { chrome: '', node: '' } }
}
