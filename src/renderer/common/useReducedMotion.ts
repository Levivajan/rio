// Reduced motion from the OS and app state; sets data-reduced-motion on the root.
import { useEffect, useSyncExternalStore } from 'react'
import { useCluState } from './useCluState'

const query = () => (typeof window.matchMedia === 'function' ? window.matchMedia('(prefers-reduced-motion: reduce)') : null)

export function useReducedMotion(): boolean {
  const media = useSyncExternalStore(
    (cb) => {
      const q = query()
      q?.addEventListener('change', cb)
      return () => q?.removeEventListener('change', cb)
    },
    () => query()?.matches ?? false
  )
  const fromState = useCluState((s) => s.app.appearance.reduceMotion)
  const reduced = media || fromState
  useEffect(() => {
    document.documentElement.dataset.reducedMotion = String(reduced)
  }, [reduced])
  return reduced
}

/** Applies the resolved light or dark set to the root, or `chrome` when the caller picks the set itself (the frame
 * follows its tint). */
export function useAppearance(chrome?: 'light' | 'dark'): void {
  const resolved = useCluState((s) => s.app.appearance.resolved)
  const set = chrome ?? resolved
  useEffect(() => {
    document.documentElement.dataset.theme = set
  }, [set])
}
