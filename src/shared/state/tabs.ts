// State fields of the tabs module.
import type { FolderId, ProfileId, SpaceId, SplitId, TabId } from '../types'

export type NodeRef = { kind: 'tab'; id: TabId } | { kind: 'folder'; id: FolderId }
export type TabSection = 'favorite' | 'pinned' | 'today'

export interface TabInit { id: TabId; profileId: ProfileId; spaceId: SpaceId | null; section: TabSection; url: string; title?: string; lastActiveAt?: number }

export interface TabsTabFields {
  id: TabId
  profileId: ProfileId
  spaceId: SpaceId | null
  section: TabSection
  parentId: FolderId | null
  url: string
  pinnedUrl: string | null
  offPinned: boolean
  title: string
  titleOverride: string | null
  /** A loaded Google Calendar pinned row's next event, "Event at 4:30 PM", read from its page; null otherwise. */
  subtitle: string | null
  favicon: string | null
  /** The live page's icon color, #rrggbb; null without a live icon. The selected favorite's ring takes the space accent. */
  iconColor: string | null
  /** A favorite's Show Unread Badge (its menu): false hides the unread dot. */
  unreadBadge: boolean
  customIcon: string | null
  muted: boolean
  lastActiveAt: number
  splitId: SplitId | null
  internal: boolean
}
export function defaultTabsTab(init: TabInit): TabsTabFields {
  return {
    id: init.id, profileId: init.profileId, spaceId: init.spaceId, section: init.section, parentId: null, url: init.url,
    pinnedUrl: null, offPinned: false, title: init.title ?? '', titleOverride: null, subtitle: null, favicon: null, iconColor: null, unreadBadge: true, customIcon: null, muted: false,
    lastActiveAt: init.lastActiveAt ?? 0, splitId: null, internal: init.url.startsWith('rio:')
  }
}

export interface TabsSpaceFields { pinned: NodeRef[]; today: TabId[]; pinnedCollapsed: boolean }
export const defaultTabsSpace = (): TabsSpaceFields => ({ pinned: [], today: [], pinnedCollapsed: false })

/** A main menu command's ask of the sidebar (Tabs ▸ New Folder…, Reveal Tab in Sidebar): `seq` grows with each ask, so the
 * same ask twice acts twice. `rename` puts the row in inline rename, `reveal` scrolls it into view. */
/** `floating`: the floating sidebar was open when the ask came, so its list answers it rather than the docked one's. */
export interface SidebarRequest { seq: number; kind: 'rename' | 'reveal'; nodeId: string; floating?: boolean }
export interface TabsWindowFields { activeTabId: TabId | null; sidebarRequest: SidebarRequest | null }
export const defaultTabsWindow = (): TabsWindowFields => ({ activeTabId: null, sidebarRequest: null })

export interface FolderState { id: FolderId; spaceId: SpaceId; name: string; icon: string | null; collapsed: boolean; children: NodeRef[] }
export interface TabsTopFields { favorites: Record<ProfileId, TabId[]>; folders: Record<FolderId, FolderState> }
export const defaultTabsTop = (): TabsTopFields => ({ favorites: {}, folders: {} })
