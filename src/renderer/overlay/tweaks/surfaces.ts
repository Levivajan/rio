// Overlay surfaces of the tweaks module: the Site tweaks editor (a passive view docked over the page area).
import type { SurfaceRegistry } from '../core/types'
import { TweakEditor } from './TweakEditor'

export const surfaces: SurfaceRegistry = { 'tweak-editor': TweakEditor }
