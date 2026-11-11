// Switcher drag reorder (docs/plan/03-spaces.md "Switcher row"): which gap the pointer is over and the
// spaces.reorder index it means. Pure, unit tested.
export const DRAG_THRESHOLD = 4

export interface IconSlot { index: number; left: number; right: number }

/** Gap 0..n in space order: before the first shown icon whose center lies right of `x`, else after the last one. */
export function dropGap(x: number, slots: readonly IconSlot[]): number | null {
  if (!Number.isFinite(x) || slots.length === 0) return null
  const sorted = [...slots].sort((a, b) => a.index - b.index)
  for (const s of sorted) if (x < (s.left + s.right) / 2) return s.index
  return (sorted[sorted.length - 1] as IconSlot).index + 1
}

/** The toIndex for spaces.reorder when the space at `from` drops into `gap`, or null when it stays put. */
export function reorderTarget(from: number, gap: number | null): number | null {
  if (gap === null || !Number.isInteger(from) || from < 0) return null
  const to = gap > from ? gap - 1 : gap
  return to === from ? null : to
}
