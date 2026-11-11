// How the sidebar adapts to its width: the breakpoints below. Pure, shared by the frame and the overlay's hover peek.

/** Widths up to this one fold the sidebar toggle and Reload into the "..." button. */
export const COMPACT_TOP_ROW_MAX = 211
/** From this width the pill reads the long placeholder and the favorites take 4 columns. */
export const WIDE_PILL_MIN = 228
export const TILE_PITCH = 55
export const MAX_FAVORITE_COLUMNS = 4
/** Bottom bar: the Library and + boxes take 41 on each side; a space item is 36 wide on a 34 pitch. */
export const SPACE_PITCH = 34
export const SPACE_ITEM_OVERLAP = 2
export const SPACE_BAR_SIDES = 82
export const ACTIVE_SPACE_MIN = 24

const finite = (n: number, fallback: number) => (Number.isFinite(n) ? n : fallback)

export type TopRowMode = 'compact' | 'full'

export const topRowMode = (width: number): TopRowMode => (finite(width, 214) <= COMPACT_TOP_ROW_MAX ? 'compact' : 'full')

export const pillPlaceholder = (width: number): string => (finite(width, 214) < WIDE_PILL_MIN ? 'Search...' : 'Search or Enter URL...')

/** `min(4, floor((w - 8) / 55))`, never under 1: 2 columns below 173, 3 from 173, 4 from 228. */
export function favoriteColumns(width: number): number {
  const w = finite(width, 214)
  return Math.max(1, Math.min(MAX_FAVORITE_COLUMNS, Math.floor((w - 8) / TILE_PITCH)))
}

/** Pitch of the space items: 34 while they fit between the Library and +, else the room shared evenly. */
export function spacePitch(width: number, spaces: number): number {
  const n = Math.max(1, Math.floor(finite(spaces, 1)))
  const room = Math.max(0, finite(width, 214) - SPACE_BAR_SIDES)
  return Math.min(SPACE_PITCH, room / n)
}
