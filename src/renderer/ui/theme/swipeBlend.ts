// The sidebar swipe's theme blend, kept in the frame: the drag no longer sends spaces.swipe to
// the browser per frame, so windows[id].swipe stays null while the fingers move. The swipe writes its target and
// fraction here in the same frame that moves the panels, and ThemeBackground writes the blended colors to its own style
// in that call, without a browser round trip or a React render per frame (it renders when the target changes).
import { useSyncExternalStore } from 'react'

export interface SwipeBlend {
  targetSpaceId: string
  /** 0 at the active space, 1 at the target. */
  fraction: number
}

/** The part of the sidebar's swipe view the blend needs (useSpaceSwipe's SwipeView). */
export interface SwipeBlendView {
  mode: 'dragging' | 'committing' | 'springing'
  travel: number
  width: number
  peer: string | null
}

let current: SwipeBlend | null = null
const listeners = new Set<() => void>()

/** The blend a swipe view draws: toward the neighbor while dragging or settling into it, none otherwise. */
export function blendOf(view: SwipeBlendView | null): SwipeBlend | null {
  if (!view || view.mode === 'springing' || !view.peer || !(view.width > 0) || !Number.isFinite(view.travel)) return null
  return { targetSpaceId: view.peer, fraction: Math.min(1, Math.abs(view.travel) / view.width) }
}

export function setSwipeBlend(next: SwipeBlend | null): void {
  const value = next && next.targetSpaceId && Number.isFinite(next.fraction) ? { targetSpaceId: next.targetSpaceId, fraction: Math.min(1, Math.abs(next.fraction)) } : null
  if (value === current || (value && current && value.targetSpaceId === current.targetSpaceId && value.fraction === current.fraction)) return
  current = value
  for (const fn of listeners) fn()
}

export function getSwipeBlend(): SwipeBlend | null {
  return current
}

/** Runs `fn` on every change of the blend (each drag frame); returns the unsubscribe. */
export function subscribeSwipeBlend(fn: () => void): () => void {
  listeners.add(fn)
  return () => {
    listeners.delete(fn)
  }
}

const targetOf = () => current?.targetSpaceId ?? null

/** The running swipe's target space; renders when the drag starts, changes neighbor or ends, never per frame. */
export function useSwipeTarget(): string | null {
  return useSyncExternalStore(subscribeSwipeBlend, targetOf, targetOf)
}
