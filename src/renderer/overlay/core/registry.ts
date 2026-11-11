// Merged surface registry: core plus every lane's surfaces.ts, collected by glob.
import { surfaces as core } from './surfaces'
import type { SurfaceRegistry } from './types'

const laneModules = import.meta.glob<{ surfaces: SurfaceRegistry }>('../*/surfaces.ts', { eager: true })

export function mergeRegistries(base: SurfaceRegistry, modules: Record<string, { surfaces?: SurfaceRegistry }>): SurfaceRegistry {
  const merged: SurfaceRegistry = { ...base }
  for (const [file, mod] of Object.entries(modules)) {
    // The glob also matches this folder's own surfaces.ts (as ./surfaces.ts); core is already in `base`.
    if (!file.startsWith('../') || file.startsWith('../core/')) continue
    for (const [id, component] of Object.entries(mod.surfaces ?? {})) {
      if (merged[id]) throw new Error(`overlay surface ${id} registered twice (${file})`)
      merged[id] = component
    }
  }
  return merged
}

export const registry: SurfaceRegistry = mergeRegistries(core, laneModules)
