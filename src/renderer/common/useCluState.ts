// useCluState(selector): state hook over the mirror with structural sharing.
import { useCallback, useRef, useSyncExternalStore } from 'react'
import type { CluState } from '../../shared/state'
import { stateMirror } from './store'
import { clu } from './clu'

export function useCluState<T>(selector: (s: CluState) => T, equal: (a: T, b: T) => boolean = Object.is): T {
  const mirror = stateMirror()
  const last = useRef<{ state: CluState; selector: (s: CluState) => T; value: T } | null>(null)
  const getSnapshot = useCallback(() => {
    const s = mirror.get()
    const prev = last.current
    // The selector is part of the key: one that closes over a prop (a tab id) must rerun on the same state.
    if (prev && prev.state === s && prev.selector === selector) return prev.value
    const value = selector(s)
    if (prev && equal(prev.value, value)) {
      last.current = { state: s, selector, value: prev.value }
      return prev.value
    }
    last.current = { state: s, selector, value }
    return value
  }, [mirror, selector, equal])
  return useSyncExternalStore(mirror.subscribe, getSnapshot, getSnapshot)
}

export function useHasSnapshot(): boolean {
  const mirror = stateMirror()
  return useSyncExternalStore(mirror.subscribe, mirror.hasSnapshot, mirror.hasSnapshot)
}

export function useWindowId(): string | null {
  return clu().windowId
}

/** Shallow array equality for selectors that build arrays. */
export function sameItems<T>(a: readonly T[], b: readonly T[]): boolean {
  return a.length === b.length && a.every((v, i) => v === b[i])
}
