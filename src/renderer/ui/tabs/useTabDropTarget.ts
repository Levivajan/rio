// Space drop target (02-tabs.md F8): spreads data-space-drop on a space icon so a sidebar drag can drop
// there or spring-load into the space after 500 ms; data-drop-hover is true while a drag rests on it.
import { useDrag } from './useSidebarDrag'

export function useTabDropTarget({ spaceId }: { spaceId: string }): { 'data-space-drop': string; 'data-drop-hover': 'true' | 'false' } {
  const hover = useDrag((v) => v.dragging && v.spaceHover === spaceId)
  return { 'data-space-drop': spaceId, 'data-drop-hover': hover ? 'true' : 'false' }
}
