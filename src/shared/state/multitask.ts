// State fields of the multitask module.
import type { Rect, SpaceId, TabId } from '../types'

/** A Mini window's one page and what its toolbar shows. `tabId` is the page's pageHost id (never a tabs record). */
export interface MiniWindowState {
  tabId: TabId
  targetSpaceId: SpaceId
  targetSpaceName: string
  profileMismatch: boolean
  confirming: boolean
  url: string
  title: string
  favicon: string | null
  loading: boolean
  /** 0..1 while loading; the toolbar's 2 px bar. */
  progress: number
  canGoBack: boolean
}

export interface MultitaskWindowFields {
  /** `favicon` is the peeked page's icon, which the row that opened the Peek shows at its right end. `summary`: a hover
   *  preview's two or three sentences from the on-device model once the page has loaded, else null. */
  peek: { seq: number; url: string; host: string; title: string; favicon: string | null; canGoBack: boolean; originRect: Rect; cardRect: Rect; summary: string | null } | null
  mini: MiniWindowState | null
}
export const defaultMultitaskWindow = (): MultitaskWindowFields => ({ peek: null, mini: null })
