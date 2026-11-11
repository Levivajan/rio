// Core shared types. Ids are UUID v4 strings, including window ids.

export interface Rect { x: number; y: number; width: number; height: number }
export interface Point { x: number; y: number }

export type Json = null | boolean | number | string | Json[] | { [key: string]: Json }

export type TabId = string
export type FolderId = string
export type SpaceId = string
export type ProfileId = string
export type WindowId = string
export type SplitId = string
export type InstanceId = string
export type CommandId = string

export const SPACE_COLORS = ['slate', 'red', 'orange', 'amber', 'lime', 'green', 'teal', 'cyan', 'blue', 'indigo', 'violet', 'pink'] as const
export type SpaceColor = (typeof SPACE_COLORS)[number]
/** The monochrome glyphs a space icon can be (Rio's own drawings, src/renderer/common/spaceSymbols.tsx). The C++ list
 * kSpaceSymbols in chromium/clu/browser/spaces/clu_space_rules.cc matches this one, in this order. */
export const SPACE_SYMBOLS = [
  'planet', 'globe', 'house', 'briefcase', 'laptop', 'code', 'terminal', 'book',
  'bookmark', 'folder', 'box', 'pencil', 'palette', 'camera', 'film', 'music',
  'gamepad', 'cart', 'dollar', 'chart', 'calendar', 'chat', 'bell', 'megaphone',
  'bulb', 'flask', 'gear', 'wrench', 'rocket', 'plane', 'star', 'heart',
  'flame', 'bolt', 'sun', 'moon', 'cloud', 'leaf', 'paw', 'cup'
] as const
export type SpaceSymbol = (typeof SPACE_SYMBOLS)[number]
export type SpaceIcon = { kind: 'emoji'; value: string } | { kind: 'letter' } | { kind: 'symbol'; value: SpaceSymbol }

export interface ThemeColor { h: number; s: number }
export interface SpaceTheme { colors: ThemeColor[]; intensity: number; grain: number }

export interface Layout {
  contentSize: { width: number; height: number }
  pageRect: Rect
  sidebarRect: Rect
  hotZone: Rect | null
  radius: number
}

export type SplitNode =
  | { kind: 'pane'; tabId: TabId }
  | { kind: 'group'; dir: 'row' | 'col'; sizes: number[]; children: SplitNode[] }

export type CommandResult =
  | { kind: 'navigate'; url: string }
  | { kind: 'search'; engineId: string; query: string }
  | { kind: 'tab'; tabId: TabId; spaceId: SpaceId; pinned: boolean; url: string; title: string }
  | { kind: 'history'; url: string; title: string }
  | { kind: 'action'; actionId: string; args?: Json }
  | { kind: 'reload'; tabId: TabId }
  | { kind: 'command'; id: CommandId }
  | { kind: 'provided'; providerId: string; token: string }

export type MenuItem =
  | { kind: 'item'; id: string; label: string; enabled?: boolean; destructive?: boolean; checked?: boolean; submenu?: MenuItem[]; keys?: string }
  | { kind: 'separator' }

/** Overlay surface ids. Core surfaces plus every lane's; lanes add ids in their own surfaces.ts registry. */
export type SurfaceId = string

export type Modality = 'modal' | 'light-dismiss' | 'passive'
export type OverlayLayer = 'top' | 'under-peek' | 'passive'
