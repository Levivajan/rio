// Contract for the engine module. nav.* handlers are seeded.
import { id, int, str, z } from '../schema'
import { optionalTab } from './common'
import { defineCommand, out } from './define'

export interface NavHistoryRow { index: number; title: string; url: string; favicon: string | null }

export const engineIpc = {
  'nav.back': defineCommand({ input: optionalTab() }),
  'nav.forward': defineCommand({ input: optionalTab() }),
  'nav.stop': defineCommand({ input: optionalTab() }),
  'nav.reload': defineCommand({ input: z.object({ tabId: id().optional(), hard: z.boolean().optional() }).strict() }),
  'nav.getHistory': defineCommand({ input: z.object({ tabId: id().optional(), direction: z.enum(['back', 'forward']) }).strict(), output: out<NavHistoryRow[]>() }),
  'nav.goToIndex': defineCommand({ input: z.object({ tabId: id().optional(), index: int(0, 10_000) }).strict() }),
  'find.update': defineCommand({ input: z.object({ tabId: id().optional(), text: str(1000), matchCase: z.boolean().optional() }).strict() }),
  'find.step': defineCommand({ input: z.object({ tabId: id().optional(), forward: z.boolean() }).strict() }),
  'find.close': defineCommand({ input: optionalTab() }),
  'zoom.step': defineCommand({ input: z.object({ tabId: id().optional(), dir: z.union([z.literal(-1), z.literal(0), z.literal(1)]) }).strict(), output: out<{ factor: number }>() }),
  'menu.invoke': defineCommand({ input: z.object({ menuId: id(), itemId: str(128) }).strict() }),
  'prompt.respond': defineCommand({
    input: z.object({ promptId: id(), action: str(32), values: z.record(str(64), str(4096)).optional(), remember: z.boolean().optional() }).strict(),
    allow: ['overlay']
  }),
  'issue.act': defineCommand({ input: z.object({ tabId: id(), action: z.enum(['reload', 'back', 'proceed', 'wait']) }).strict(), allow: ['ui', 'overlay', 'mini-ui'] }),
  'page.print': defineCommand({ input: optionalTab() }),
  'page.devtools': defineCommand({ input: optionalTab() }),
  'page.savePage': defineCommand({ input: optionalTab() })
} as const
