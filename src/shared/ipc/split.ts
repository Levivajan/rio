// Contract for the split module. Static checks live here; main refines create, dropTab and swap against state
// (every id a known tab of one space, see src/main/split/payloads.ts).
import { MAX_PANES, splitSize, sumsToOne } from '../split'
import { id, int, z } from '../schema'
import { defineCommand, out } from './define'

const edge = z.enum(['left', 'right', 'top', 'bottom'])
const path = () => z.array(int(0, MAX_PANES - 1)).max(2)
const unique = (ids: readonly string[]) => new Set(ids).size === ids.length

export const splitCreateInput = z
  .object({ tabIds: z.array(id()).min(2).max(MAX_PANES), dir: z.enum(['row', 'col']) })
  .strict()
  .refine((v) => unique(v.tabIds), 'duplicate tab ids')
export const splitDropTabInput = z
  .object({ tabId: id(), targetTabId: id(), edge })
  .strict()
  .refine((v) => v.tabId !== v.targetTabId, 'a tab cannot drop onto itself')
export const splitSwapInput = z
  .object({ tabId: id(), targetTabId: id() })
  .strict()
  .refine((v) => v.tabId !== v.targetTabId, 'a tab cannot swap with itself')
export const splitResizeInput = z
  .object({ splitId: id(), path: path(), sizes: z.array(splitSize()).min(2).max(MAX_PANES) })
  .strict()
  .refine((v) => sumsToOne(v.sizes), 'sizes must sum to 1')

export const splitIpc = {
  'split.create': defineCommand({ input: splitCreateInput, output: out<{ splitId: string }>() }),
  'split.dropTab': defineCommand({ input: splitDropTabInput, output: out<{ splitId: string }>() }),
  'split.swap': defineCommand({ input: splitSwapInput }),
  'split.resize': defineCommand({ input: splitResizeInput }),
  'split.equalize': defineCommand({ input: z.object({ splitId: id(), path: path() }).strict() }),
  'split.removePane': defineCommand({ input: z.object({ tabId: id(), mode: z.enum(['separate', 'close']) }).strict() }),
  'split.focusPane': defineCommand({
    input: z.union([
      z.object({ tabId: id() }).strict(),
      z.object({ index: int(1, 4) }).strict(),
      z.object({ step: z.union([z.literal(1), z.literal(-1)]) }).strict()
    ])
  }),
  'split.dragState': defineCommand({
    input: z.object({ phase: z.enum(['start', 'hover', 'end', 'cancel']), tabId: id(), targetTabId: id().optional(), edge: edge.optional() }).strict()
  }),
  'media.toggle': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  /** The media bar's previous and next track buttons: the page's media session handles them, as the system keys do. */
  'media.previous': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'media.next': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'media.focus': defineCommand({ input: z.object({ tabId: id() }).strict() })
} as const
