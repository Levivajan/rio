// How rows move when the tab list changes.
// A pinned or unpinned row flies to its place and the rows it passes close up in about 110 ms; a folder
// expands in about 190 ms and collapses in about 225; the pinned section collapses in about 95 ms and expands in about
// 190. All decelerate with no overshoot. Each easing is a progress curve sampled on 17 ms frames. Pure.

export interface FlipTiming {
  ms: number
  easing: string
}

export interface ListShape {
  /** Folder id to collapsed. */
  folders: Readonly<Record<string, boolean>>
  pinnedCollapsed: boolean
}

export const ROW_MOVE: FlipTiming = { ms: 110, easing: 'linear(0, 0.41 15.5%, 0.64 30%, 0.83 45.5%, 0.92 61%, 0.976 75.5%, 0.998 91%, 1)' }
/** A closed or unpinned row fades in place, no move or scale, while the rows below close up: opacity 0.88, 0.57,
 *  0.32, 0.17, 0.07, 0.03, 0 on 17 ms frames. */
export const ROW_FADE: FlipTiming = { ms: 117, easing: 'linear(0, 0.12 14.5%, 0.43 28.5%, 0.68 43%, 0.83 57%, 0.93 71%, 0.97 85.5%, 1)' }
/** Clear fades the Today divider as New Tab slides up over it: gone in about 50 ms. */
export const CLEAR_FADE: FlipTiming = { ms: 50, easing: 'linear' }
export const FOLDER_EXPAND: FlipTiming = {
  ms: 190,
  easing: 'linear(0, 0.16 9%, 0.28 17.4%, 0.39 26%, 0.5 35%, 0.6 43.7%, 0.74 57%, 0.82 66%, 0.88 75%, 0.94 83%, 0.98 92%, 1)'
}
export const FOLDER_COLLAPSE: FlipTiming = {
  ms: 225,
  easing: 'linear(0, 0.06 7.5%, 0.17 14.7%, 0.32 22%, 0.54 33%, 0.65 41%, 0.74 48%, 0.82 55.5%, 0.88 63%, 0.95 78%, 0.99 92%, 1)'
}
export const PINNED_COLLAPSE: FlipTiming = { ms: 95, easing: 'linear(0, 0.15, 0.46 18%, 0.71 44%, 0.85 53%, 0.97 79%, 1)' }
export const PINNED_EXPAND: FlipTiming = {
  ms: 190,
  easing: 'linear(0, 0.12 9%, 0.18 18%, 0.35 27%, 0.47 36%, 0.63 45%, 0.76 54%, 0.83 63%, 0.89 72%, 0.94 81%, 0.98 90%, 1)'
}

/** The timing for the rows' move from `before` to `after`: a section or folder that opened or closed sets it, else a
 *  row moved. */
export function flipTiming(before: ListShape, after: ListShape): FlipTiming {
  if (before.pinnedCollapsed !== after.pinnedCollapsed) return after.pinnedCollapsed ? PINNED_COLLAPSE : PINNED_EXPAND
  let expanded = false
  for (const [id, collapsed] of Object.entries(after.folders)) {
    const was = before.folders[id]
    if (was === undefined || was === collapsed) continue
    if (collapsed) return FOLDER_COLLAPSE
    expanded = true
  }
  return expanded ? FOLDER_EXPAND : ROW_MOVE
}

/** Rows farther than this moved because the list was rebuilt (a space switch, a scroll jump), not by an action. */
export const MAX_FLIP_PX = 2000
