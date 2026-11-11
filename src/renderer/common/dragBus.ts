// Pointer drag events shared inside one renderer.
export type DragPhase = 'start' | 'move' | 'outside' | 'end' | 'cancel'
export interface DragEvent { phase: DragPhase; x: number; y: number; tabId: string; tabIds: string[] }
type Handler = (e: DragEvent) => void

const handlers = new Map<DragPhase, Set<Handler>>()

export const dragBus = {
  emit(e: DragEvent): void {
    for (const fn of handlers.get(e.phase) ?? []) fn(e)
  },
  on(phase: DragPhase, fn: Handler): () => void {
    const set = handlers.get(phase) ?? new Set<Handler>()
    set.add(fn)
    handlers.set(phase, set)
    return () => set.delete(fn)
  }
}
