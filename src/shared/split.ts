// Split types, limits and the layout validator shared by main and renderers.
import { LOOK } from './look/tokens'
import { finite, id, z } from './schema'
import type { Rect, SplitNode } from './types'

export type { SplitNode } from './types'
export type { SplitState, PaneRect, SplitView, SplitPaneView, SplitDividerView, SplitDropState } from './state/split'

export const MAX_PANES = 4
/** Groups nest at most this deep: a root group and one level of groups inside it. */
export const MAX_DEPTH = 2
/** Minimum page area of a pane in DIP (the pane bar sits above it). */
export const MIN_PANE_WIDTH = 220
export const MIN_PANE_HEIGHT = 160
/** The gap between panes: 10, Chromium's own resize area on the native split. */
export const PANE_GAP = LOOK.split.gap
export const PANE_BAR = 32
export const SIZE_TOLERANCE = 0.001
export const SPLIT_FULL_TOAST = 'A split holds up to 4 tabs'
/** Overlay surface that previews where a dragged tab or pane lands. */
export const SPLIT_DROP_SURFACE = 'split-drop'

export type SplitEdge = 'left' | 'right' | 'top' | 'bottom'
export type SplitDir = 'row' | 'col'
export type SplitGroup = Extract<SplitNode, { kind: 'group' }>

export function sumsToOne(sizes: readonly number[]): boolean {
  return Math.abs(sizes.reduce((a, b) => a + b, 0) - 1) <= SIZE_TOLERANCE
}

/** Pane tab ids in tree order (left to right, top to bottom). */
export function leaves(node: SplitNode): string[] {
  return node.kind === 'pane' ? [node.tabId] : node.children.flatMap(leaves)
}

function depth(node: SplitNode): number {
  return node.kind === 'pane' ? 0 : 1 + Math.max(...node.children.map(depth))
}

/** One child's share of its group: finite and strictly between 0 and 1. */
export const splitSize = () => finite().pipe(z.number().gt(0).lt(1))

const nodeSchema: z.ZodType<SplitNode> = z.lazy(() =>
  z.union([
    z.object({ kind: z.literal('pane'), tabId: id() }).strict(),
    z
      .object({ kind: z.literal('group'), dir: z.enum(['row', 'col']), sizes: z.array(splitSize()).min(2).max(MAX_PANES), children: z.array(nodeSchema).min(2).max(MAX_PANES) })
      .strict()
      .refine((g) => g.sizes.length === g.children.length, 'one size per child')
      .refine((g) => sumsToOne(g.sizes), 'sizes must sum to 1')
  ])
)

/** A stored split root: a group of 2 to 4 unique panes, at most 2 levels deep. */
export const splitLayoutSchema = nodeSchema
  .refine((n) => n.kind === 'group', 'a split root is a group')
  .refine((n) => {
    const ids = leaves(n)
    return ids.length >= 2 && ids.length <= MAX_PANES && new Set(ids).size === ids.length
  }, 'a split holds 2 to 4 unique tabs')
  .refine((n) => depth(n) <= MAX_DEPTH, 'a split nests at most 2 levels')

/** Parses a stored layout JSON string; junk of any shape returns null. */
export function parseSplitLayout(raw: unknown): SplitNode | null {
  if (typeof raw !== 'string' || raw.length > 64 * 1024) return null
  let value: unknown
  try {
    value = JSON.parse(raw)
  } catch {
    return null
  }
  const r = splitLayoutSchema.safeParse(value)
  return r.success ? r.data : null
}

/** A pane's whole card in window DIP: its 32 px bar and the page view under it. */
export function paneCard(p: { rect: Rect; bar: Rect }): Rect {
  return { x: p.bar.x, y: p.bar.y, width: p.bar.width, height: p.bar.height + p.rect.height }
}
