// State fields of the split module.
import type { Rect, SpaceId, SplitId, SplitNode, TabId } from '../types'

export interface SplitTabFields { mediaPlaying: boolean; pip: 'auto' | 'manual' | null }
export const defaultSplitTab = (): SplitTabFields => ({ mediaPlaying: false, pip: null })

/** One pane as the window draws it: the page view's rect and the 24 px bar above it, in window DIP. */
export interface SplitPaneView { tabId: TabId; rect: Rect; bar: Rect }
/** A divider in the gap between child `index` and `index + 1` of the group at `path`. */
export interface SplitDividerView { path: number[]; index: number; dir: 'row' | 'col'; rect: Rect; sizes: number[]; avail: number }
/** The split a window shows now, laid out over its page rect (services.split.paneLayout). */
export interface SplitView { splitId: SplitId; panes: SplitPaneView[]; dividers: SplitDividerView[] }

/**
 * A drag over the page area: a sidebar tab toward a pane edge (edge set) or a pane bar onto another pane (a swap,
 * edge null). `zone` is the preview rect in window DIP, null while the pointer is over no drop target.
 */
export interface SplitDropState { tabId: TabId; targetTabId: TabId | null; edge: 'left' | 'right' | 'top' | 'bottom' | null; zone: Rect | null }

export interface SplitWindowFields {
  activeSplitId: SplitId | null
  focusedTabId: TabId | null
  splitDrop: SplitDropState | null
  splitView: SplitView | null
  /** The sidebar media card's tab: the most recently started audible tab out of sight (src/main/split/media.ts). */
  mediaTabId: TabId | null
}
export const defaultSplitWindow = (): SplitWindowFields => ({ activeSplitId: null, focusedTabId: null, splitDrop: null, splitView: null, mediaTabId: null })

export interface SplitState { id: SplitId; spaceId: SpaceId; root: SplitNode; focusedTabId: TabId }
export interface SplitTopFields { splits: Record<SplitId, SplitState> }
export const defaultSplitTop = (): SplitTopFields => ({ splits: {} })

export interface PaneRect { tabId: TabId; rect: Rect }
