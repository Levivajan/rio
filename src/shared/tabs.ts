// Tabs types shared by main and renderers.
import type { CluState, NodeRef } from './state'
import { displayHost, tabUrlOrNull } from './url'
export type { NodeRef, TabSection, FolderState } from './state/tabs'
export type DropTarget =
  | { section: 'favorite'; index: number }
  | { section: 'pinned'; spaceId: string; parentId: string | null; index: number }
  | { section: 'today'; spaceId: string; index: number }
export const MAX_FAVORITES = 48
/** Folder levels: a root folder is level 1, so a folder at level 8 can hold tabs but no folder. */
export const MAX_FOLDER_DEPTH = 8
export const MAX_FOLDER_NAME = 200
export const DEFAULT_FOLDER_NAME = 'Untitled'
/** Section rules: which sections a node kind may enter. */
export const SECTION_ACCEPTS: Record<DropTarget['section'], readonly ('tab' | 'folder')[]> = {
  favorite: ['tab'],
  pinned: ['tab', 'folder'],
  today: ['tab']
}

/** Why TabStore refused a structural change (IpcError reason), with the toast the UI shows for it. */
export type MoveRejection = 'not-found' | 'bad-target' | 'folder-not-allowed' | 'favorites-full' | 'too-deep' | 'cycle' | 'private'
export const MOVE_REJECTION_TEXT: Record<MoveRejection, string> = {
  'not-found': "That item no longer exists",
  'bad-target': "Can't move there",
  'folder-not-allowed': 'Folders can only go in Pinned',
  'favorites-full': 'Favorites are full',
  'too-deep': 'Folders can nest 8 levels deep',
  cycle: "A folder can't go inside itself",
  private: "Tabs can't move in or out of a private window"
}

const KEYCAP = /^[0-9#*]️?⃣$/u
const PICTOGRAPH = /\p{Extended_Pictographic}|\p{Regional_Indicator}/u

/** True for exactly one emoji grapheme (custom icons on folders and pinned rows, tabs `0201`). */
export function isOneEmoji(value: unknown): value is string {
  if (typeof value !== 'string' || value.length === 0 || value.length > 32) return false
  const graphemes = [...new Intl.Segmenter(undefined, { granularity: 'grapheme' }).segment(value)]
  if (graphemes.length !== 1) return false
  return KEYCAP.test(value) || PICTOGRAPH.test(value)
}

/** Inline error for a custom icon that is not exactly one emoji. */
export const ICON_ERROR = 'Use a single emoji'

/** F3 rename: control characters become spaces, runs of white space collapse, then trimmed and capped at 200. */
export function cleanName(raw: string): string {
  // eslint-disable-next-line no-control-regex
  return raw.replace(/[\u0000-\u001f\u007f-\u009f\u2028\u2029]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, MAX_FOLDER_NAME)
}

function comparable(url: string): string {
  try {
    const u = new URL(url)
    u.hash = ''
    if (u.pathname.length > 1 && u.pathname.endsWith('/')) u.pathname = u.pathname.slice(0, -1)
    return u.href
  } catch {
    return url
  }
}

/** F2: an anchored tab is off its pinned URL when the current URL differs after dropping the fragment and a trailing slash. */
export function isOffPinned(url: string, pinnedUrl: string | null): boolean {
  return pinnedUrl !== null && comparable(url) !== comparable(pinnedUrl)
}

/** F2 typed pinned URL: trims the text and adds https:// when it has no scheme; the URL policy still runs in main. */
export function typedPinnedUrl(text: string): string | null {
  const v = text.trim()
  if (!v || /\s/.test(v)) return null
  const candidate = /^[a-z][a-z0-9+.-]*:/i.test(v) && !/^[^:/]+:\d+(\/|$)/.test(v) ? v : `https://${v}`
  return tabUrlOrNull(candidate)
}

// ---- hover card (F15) --------------------------------------------------------------------------

export type HoverChip = 'Playing' | 'Muted' | 'Asleep'

/** F15 chips: Muted wins over Playing, and a tab without a live page is Asleep. */
export function hoverChips(t: { audible: boolean; muted: boolean; lifecycle: string }): HoverChip[] {
  const chips: HoverChip[] = []
  if (t.muted) chips.push('Muted')
  else if (t.audible) chips.push('Playing')
  if (t.lifecycle === 'unloaded' || t.lifecycle === 'hibernated') chips.push('Asleep')
  return chips
}

/** Keeps the start and the end of `text` around one ellipsis so it fits `max` characters (code points, never split). */
export function middleTruncate(text: string, max: number): string {
  const chars = Array.from(text)
  if (chars.length <= max) return text
  if (max <= 1) return '…'
  const keep = max - 1
  const head = Math.ceil(keep / 2)
  return `${chars.slice(0, head).join('')}…${chars.slice(chars.length - (keep - head)).join('')}`
}

/** Host and path of a URL without the scheme (query and fragment dropped); the raw text when it does not parse. */
export function hostAndPath(url: string): string {
  const raw = url.slice(0, 4096)
  try {
    const u = new URL(raw)
    if (u.protocol !== 'http:' && u.protocol !== 'https:') return u.protocol === 'rio:' ? `rio://${u.host}${u.pathname === '/' ? '' : u.pathname}` : raw
    return `${u.host.replace(/^www\./, '')}${u.pathname === '/' ? '' : u.pathname}`
  } catch {
    return raw
  }
}

/** The hover card is a title pill: 28 tall, 11 in, at most `maxPill` wide, hung `offsetX`/`offsetY`
 *  right of and below the pointer. Its view adds `margin` on every side for the shadow. */
export const HOVER_CARD = { pill: 28, padX: 11, maxPill: 320, margin: 8, offsetX: 11, offsetY: 12, locationChars: 44 } as const

export interface HoverCardModel { title: string; titleLines: 1 | 2; location: string; pinned: string | null; chips: HoverChip[]; width: number; height: number; kind: 'title' | 'mail' | 'preview'; unread: number; site: string | null; mailSource?: MailSource; mailItems?: MailItem[] }

/** Where Gmail's card has its count from: the account's Atom feed (the count and the newest messages), the
 *  tab title's count (the feed unavailable), or neither (an asleep row whose stored title has no count). */
export type MailSource = 'feed' | 'title' | 'unknown'
/** One unread message from the feed: `issued` is ISO 8601, `url` the message on mail.google.com or empty. */
export interface MailItem { author: string; title: string; issued: string; url: string }

/** Gmail's card, 239 wide: 64 tall for one or two lines ("No unread emails, you can exhale!"),
 *  else a 30 tall header over a 44 tall row per message, at most 5, inside 6 of padding.
 *  MailCardHeight() in clu_overlay_rects.cc. It hangs from the tile's lower right corner, 3.5 over it (the
 *  first tile's card at left 65, top 133.5). */
export const MAIL_CARD = { width: 239, short: 64, header: 30, row: 44, pad: 6, maxRows: 5 } as const
export function mailCardHeight(rows: number): number {
  const n = Math.max(0, Math.min(Number.isFinite(rows) ? Math.floor(rows) : 0, MAIL_CARD.maxRows))
  return n === 0 ? MAIL_CARD.short : 2 * MAIL_CARD.pad + MAIL_CARD.header + n * MAIL_CARD.row
}

/** A mail favorite's preview card, 239 x 64 with radius 15, hung from the tile's lower right corner 230 ms after
 *  the hover (235). It shows the unread count the page's title carries. */
export const MAIL_PREVIEW = { width: 239, height: 64, delayMs: 230, overlap: 3.5 } as const

/** Where a mail favorite's card hangs (the hover anchor, before HOVER_CARD's offsets): from the tile's lower
 *  right corner, 3.5 over it (the first tile's card at left 65, top 133.5). A tile in a later column hangs
 *  it at the first column's place instead, so every card starts inside the sidebar just under its own tile, over the
 *  sidebar, never out over the page. `firstRight` is the grid's first tile's right edge; junk falls back to the tile. */
export function mailCardAnchor(tile: { right: number; bottom: number }, firstRight: number): { x: number; y: number } {
  const right = Number.isFinite(firstRight) && firstRight > 0 ? Math.min(tile.right, firstRight) : tile.right
  return { x: right - MAIL_PREVIEW.overlap - HOVER_CARD.offsetX, y: tile.bottom - MAIL_PREVIEW.overlap - HOVER_CARD.offsetY }
}

/** Whether the page has the mail preview card: Gmail. */
export function isMailPreview(url: string): boolean {
  return previewSite(url) === 'Gmail'
}

/** A favorite or pinned row on one of these sites opens a preview card with a small live view of the site's
 *  compact page (the browser lays it over the card, loaded in the tab's own profile; LivePreviewUrl() in
 *  clu_overlay_rects.cc). Every other site keeps the title card. 239 x 256, hung
 *  beside a row, centered on it, 12.5 right. */
export const LIVE_PREVIEW = { width: 239, height: 256, rowGap: 12.5 } as const
const PREVIEW_SITES: Record<string, string> = {
  'mail.google.com': 'Gmail',
  'calendar.google.com': 'Google Calendar',
  'meet.google.com': 'Google Meet'
}

/** The preview site a page belongs to, or null (junk URLs and other sites). */
export function previewSite(url: string): string | null {
  let host: string
  try {
    const u = new URL(url)
    if (u.protocol !== 'https:' && u.protocol !== 'http:') return null
    host = u.hostname.replace(/^www\./, '')
  } catch {
    return null
  }
  return Object.hasOwn(PREVIEW_SITES, host) ? (PREVIEW_SITES[host] ?? null) : null
}

/** The unread count in a page title, as mail and social sites put it ("(3) Inbox"); 0 without one. */
export function unreadCount(title: string): number {
  const m = /(?:^|\s)\((\d{1,6})\+?\)(?=\s|$)/.exec(title.slice(0, 300))
  return m ? Number(m[1]) : 0
}

/** What the hover card knows and its view's exact size (the overlay view is sized to it). It draws the title only. */
export function hoverCardModel(t: { title: string; titleOverride: string | null; url: string; pinnedUrl: string | null; offPinned: boolean; audible: boolean; muted: boolean; lifecycle: string }): HoverCardModel {
  // eslint-disable-next-line no-control-regex
  const clean = (t.titleOverride || t.title || '').replace(/[\u0000-\u001f\u007f-\u009f]/g, ' ').trim().slice(0, 300)
  const location = middleTruncate(hostAndPath(t.url), HOVER_CARD.locationChars)
  const title = clean || displayHost(t.url) || location || 'Untitled'
  const pinned = t.offPinned && t.pinnedUrl ? middleTruncate(hostAndPath(t.pinnedUrl), HOVER_CARD.locationChars - 8) : null
  const { maxPill, pill, margin } = HOVER_CARD
  const site = previewSite(t.url)
  // As BuildHoverCard without a feed answer: Gmail's card is the mail card with the title's count. A title with no
  // count (a message, a label, a search, a page still loading, an old stored title) says nothing about the mail, so
  // the card is the plain one and never claims zero.
  if (site === 'Gmail') {
    const unread = unreadCount(t.title)
    if (unread > 0) return { title, titleLines: 1, location, pinned, chips: [], width: MAIL_CARD.width + 2 * margin, height: MAIL_CARD.short + 2 * margin, kind: 'mail', unread, site, mailSource: 'title', mailItems: [] }
    return { title, titleLines: 1, location, pinned, chips: hoverChips(t), width: maxPill + 2 * margin, height: pill + 2 * margin, kind: 'title', unread: 0, site: null }
  }
  // As BuildHoverCard: a preview site gets the live preview whether its tab is loaded or asleep (the browser
  // lays the compact page over it): a restored favorite or pinned row is asleep until opened.
  if (site) return { title, titleLines: 1, location, pinned, chips: [], width: LIVE_PREVIEW.width + 2 * margin, height: LIVE_PREVIEW.height + 2 * margin, kind: 'preview', unread: 0, site }
  return { title, titleLines: 1, location, pinned, chips: hoverChips(t), width: maxPill + 2 * margin, height: pill + 2 * margin, kind: 'title', unread: 0, site: null }
}

/** The card's view in window coordinates: hung from the pointer (`anchor`'s origin), kept inside the window. */
export function hoverCardRect(content: { width: number; height: number }, anchor: { x: number; y: number }, model: Pick<HoverCardModel, 'width' | 'height'>): { x: number; y: number; width: number; height: number } {
  const clamp = (v: number, max: number) => Math.max(0, Math.min(Number.isFinite(v) ? Math.round(v) : 0, Math.max(0, max)))
  return {
    x: clamp(anchor.x + HOVER_CARD.offsetX - HOVER_CARD.margin, content.width - model.width),
    y: clamp(anchor.y + HOVER_CARD.offsetY - HOVER_CARD.margin, content.height - model.height),
    width: model.width,
    height: model.height
  }
}

// ---- sidebar order (F12) ----------------------------------------------------------------------
/**
 * Favorites left to right, then the window's current space's visible pinned rows top to bottom, then Today.
 * Folder rows do not count. A collapsed folder (or a collapsed Pinned section) hides its tabs except the window's
 * active tab, which shows beneath it and counts at that place.
 */
export function sidebarOrder(state: CluState, windowId: string): string[] {
  const w = state.windows[windowId]
  const space = w ? state.spaces[w.activeSpaceId] : undefined
  if (!w || !space) return []
  const active = w.activeTabId
  const out: string[] = [...(state.favorites[space.profileId] ?? [])]

  const contains = (refs: readonly NodeRef[], tabId: string, guard: Set<string>): boolean =>
    refs.some((r) => {
      if (r.kind === 'tab') return r.id === tabId
      if (guard.has(r.id)) return false
      guard.add(r.id)
      return contains(state.folders[r.id]?.children ?? [], tabId, guard)
    })

  const walk = (refs: readonly NodeRef[], guard: Set<string>): void => {
    for (const r of refs) {
      if (r.kind === 'tab') {
        out.push(r.id)
        continue
      }
      const folder = state.folders[r.id]
      if (!folder || guard.has(r.id)) continue
      guard.add(r.id)
      if (!folder.collapsed) walk(folder.children, guard)
      else if (active && contains(folder.children, active, new Set())) out.push(active)
    }
  }

  if (!space.pinnedCollapsed) walk(space.pinned, new Set())
  else if (active && contains(space.pinned, active, new Set())) out.push(active)
  out.push(...space.today)
  return out.filter((id) => state.tabs[id] !== undefined)
}

/** Most tabs one multi-select bulk action takes (tabs.bulk). */
export const MAX_BULK = 200

// ---- collapsed folder popover ---------------------------------------------------------------------------------------
/**
 * Geometry of the popover a collapsed folder opens beside the sidebar: 4 right of the sidebar, a 43 tall
 * search header (with its hairline), 39 tall rows inset 4, 3 below the last; centered on the folder row and kept 10
 * from the window top and 25.5 from its bottom. 213 wide.
 */
export const FOLDER_PEEK = {
  gap: 4, width: 213, header: 43, row: 39, inset: 4, bottom: 3, marginTop: 10, marginBottom: 25.5, delayMs: 300, closeMs: 250, maxRows: 50,
  /** Room the passive view keeps around the card for its shadow; none toward the sidebar, so the rows stay reachable. */
  shadow: { top: 8, right: 16, bottom: 20 }
} as const

/** The tabs a folder's popover lists: its tabs and its subfolders' tabs, depth first, at most FOLDER_PEEK.maxRows. */
export function folderPeekTabs(state: CluState, folderId: string): string[] {
  const out: string[] = []
  const seen = new Set<string>()
  const walk = (id: string) => {
    if (seen.has(id)) return
    seen.add(id)
    for (const r of state.folders[id]?.children ?? []) {
      if (out.length >= FOLDER_PEEK.maxRows) return
      if (r.kind === 'folder') walk(r.id)
      else if (state.tabs[r.id]) out.push(r.id)
    }
  }
  walk(folderId)
  return out
}

type Box = { x: number; y: number; width: number; height: number }

/** The popover card's rect for a folder row, the sidebar's right edge and the window height (one row when empty). */
export function folderPeekCard(row: { y: number; height: number }, sidebarRight: number, windowHeight: number, rows: number): Box {
  const { gap, width, header, row: rowH, inset, bottom, marginTop, marginBottom } = FOLDER_PEEK
  const room = Math.max(header + inset + rowH + bottom, windowHeight - marginTop - marginBottom)
  const height = Math.min(header + inset + Math.max(1, rows) * rowH + bottom, room)
  const y = Math.min(Math.max(marginTop, row.y + row.height / 2 - height / 2), marginTop + room - height)
  return { x: sidebarRight + gap, y: Math.round(y * 2) / 2, width, height }
}

/** The passive view's rect: the card plus room for its shadow (FOLDER_PEEK.shadow). */
export function folderPeekRect(row: { y: number; height: number }, sidebarRight: number, windowHeight: number, rows: number): Box {
  const card = folderPeekCard(row, sidebarRight, windowHeight, rows)
  const { top, right, bottom } = FOLDER_PEEK.shadow
  return { x: card.x, y: card.y - top, width: card.width + right, height: card.height + top + bottom }
}

/** "Just now", "5 minutes ago", "17 hours ago", "4 days ago", "3 months ago", "2 years ago", as the label of
 *  a row; an empty string for a missing time. A time ahead of `now` reads as now. */
export function timeAgo(ms: number, now: number): string {
  if (!Number.isFinite(ms) || ms <= 0 || !Number.isFinite(now)) return ''
  const s = Math.max(0, now - ms) / 1000
  const steps: [number, string][] = [[60, 'minute'], [3600, 'hour'], [86_400, 'day'], [2_629_800, 'month'], [31_557_600, 'year']]
  if (s < 60) return 'Just now'
  let unit = steps[0] as [number, string]
  for (const step of steps) if (s >= step[0]) unit = step
  const n = Math.floor(s / unit[0])
  return `${n} ${unit[1]}${n === 1 ? '' : 's'} ago`
}
