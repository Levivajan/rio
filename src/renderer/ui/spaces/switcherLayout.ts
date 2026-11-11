// Switcher row layout: every space keeps its icon while the boxes can shrink
// evenly to their minimums (the active 24 wide, the others 16.5, on a pitch 2 under the box); past that the
// inactive spaces become dots; past that the row shows the active space with as many neighbors as fit. There is no
// overflow button: every space stays reachable with ⌃N, swipes and Move to.
export const ACTIVE_MIN = 24
export const ITEM_MIN = 16.5
/** Neighboring boxes overlap by 2 (a 36 box on a 34 pitch). */
export const OVERLAP = 2
export const ICON_SLOT = 28
/** A dot box (8 pt dot, 12 box) on the same 2 overlap. */
export const DOT_SLOT = 10

export interface SwitcherItem { index: number; kind: 'icon' | 'dot' }
export interface SwitcherLayout { items: SwitcherItem[] }

export function switcherLayout(count: number, active: number, width: number): SwitcherLayout {
  const n = Number.isFinite(count) ? Math.max(0, Math.floor(count)) : 0
  if (n === 0) return { items: [] }
  const a = Number.isInteger(active) && active >= 0 && active < n ? active : 0
  const w = Number.isFinite(width) ? Math.max(0, width) : 0
  const all = (kind: (i: number) => SwitcherItem['kind']) => Array.from({ length: n }, (_, index) => ({ index, kind: kind(index) }))
  if (ACTIVE_MIN - OVERLAP + (n - 1) * (ITEM_MIN - OVERLAP) <= w) return { items: all(() => 'icon') }
  if (ICON_SLOT + (n - 1) * DOT_SLOT <= w) return { items: all((i) => (i === a ? 'icon' : 'dot')) }
  const room = Math.max(0, Math.floor((w - ICON_SLOT) / DOT_SLOT))
  const lo = Math.max(0, Math.min(a - Math.floor(room / 2), n - 1 - room))
  const items: SwitcherItem[] = []
  for (let i = lo; i <= Math.min(n - 1, lo + room); i++) items.push({ index: i, kind: i === a ? 'icon' : 'dot' })
  return { items }
}
