// Contract for the multitask module.
import { empty, id, int, url, z } from '../schema'
import { PEEK_NAV_ACTIONS } from '../multitask'
import { defineCommand } from './define'

export const multitaskIpc = {
  'peek.close': defineCommand({ input: empty() }),
  'peek.expand': defineCommand({ input: empty() }),
  'peek.toSplit': defineCommand({ input: empty() }),
  'peek.nav': defineCommand({ input: z.object({ action: z.enum(PEEK_NAV_ACTIONS) }).strict() }),
  'peek.animationDone': defineCommand({ input: z.object({ seq: int(0, Number.MAX_SAFE_INTEGER) }).strict(), allow: ['overlay'] }),
  'mini.open': defineCommand({ input: z.object({ url: url().optional() }).strict(), allow: ['mini-ui'] }),
  'mini.moveToSpace': defineCommand({ input: z.object({ spaceId: id().optional(), confirmProfileChange: z.boolean().optional() }).strict(), allow: ['mini-ui'] }),
  'mini.pickSpace': defineCommand({ input: empty(), allow: ['mini-ui'] }),
  // The confirm strip's Cancel.
  'mini.cancelMove': defineCommand({ input: empty(), allow: ['mini-ui'] }),
  // The toolbar's Back (the Mini window has no Forward).
  'mini.back': defineCommand({ input: empty(), allow: ['mini-ui'] })
} as const
