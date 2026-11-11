// Multitask types shared by main and renderers (seeded by the scaffold).
import type { Rect } from './types'
export type MiniSource = 'other-app' | 'command' | 'link' | 'file'
export const MAX_MINI_WINDOWS = 6
/** `features` is the window.open feature string when the caller has it (a sized one means a popup, never a Peek). */
export interface OpenIntercept { windowId: string; openerTabId: string | null; url: string; disposition: string; shift: boolean; features?: string }
/** Props of the `peek` overlay surface while it plays the close animation (the Peek itself is gone from state). */
export interface PeekClosingFrame { seq: number; originRect: Rect; cardRect: Rect; image: string | null; shrinking: boolean }
export interface PeekSurfaceProps { closing?: PeekClosingFrame }
/** What the Peek rail and the Peek page's own keys can do to the Peek page. */
export const PEEK_NAV_ACTIONS = ['back', 'forward', 'reload', 'hardReload', 'stop'] as const
export type PeekNavAction = (typeof PEEK_NAV_ACTIONS)[number]

// Mini window (05 Mini window, decisions 27 and 37).
/** Toolbar height and the page card's gutter, in DIP. The page card sits under the toolbar. */
export const MINI_TOOLBAR_H = 36
export const MINI_INSET = 8
export const MINI_SIZE = { width: 880, height: 620 } as const
export const MINI_MIN_SIZE = { width: 480, height: 329 } as const
/** The empty prompt (Cmd+Opt+N): a 490 x 329 window, 20 in from the screen's top-right corner. */
export const MINI_PROMPT_SIZE = { width: 490, height: 329 } as const
export const MINI_PROMPT_INSET = 20
/** Each further Mini window opens this far down and right of the previous one. */
export const MINI_CASCADE = 22
export const miniCapToast = (open: number) => `Opened in a tab: ${open} Mini windows are open`
/** The confirm strip under the toolbar when the target space uses another profile (05 Moving to a space). */
export const MINI_CONFIRM_H = 40
export const miniConfirmText = (space: string) => `${space} uses a different profile. Logins and form data won't carry over.`
/** A Mini window nobody has typed in, clicked or focused for this long closes and archives its page (decision 54). */
export const MINI_IDLE_MS = 6 * 60 * 60 * 1000

/** Bounds of the Mini window's page view inside a content area of `size`; the confirm strip pushes it down. */
export function miniPageRect(size: { width: number; height: number }, confirming = false): Rect {
  const w = Number.isFinite(size.width) ? Math.max(0, Math.round(size.width)) : 0
  const h = Number.isFinite(size.height) ? Math.max(0, Math.round(size.height)) : 0
  const top = MINI_TOOLBAR_H + (confirming ? MINI_CONFIRM_H : 0)
  return { x: MINI_INSET, y: top, width: Math.max(0, w - MINI_INSET * 2), height: Math.max(0, h - top - MINI_INSET) }
}
