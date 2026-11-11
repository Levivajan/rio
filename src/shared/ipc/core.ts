// Core commands.
import { empty, id, json, z } from '../schema'
import type { CluState } from '../state'
import { commandId } from './common'
import { defineCommand, out } from './define'

export const coreIpc = {
  'state.get': defineCommand({ input: empty(), output: out<CluState>(), allow: ['ui', 'overlay', 'mini-ui', 'internal:settings', 'internal:history', 'internal:extensions', 'internal:about'] }),
  'window.new': defineCommand({ input: empty() }),
  'window.close': defineCommand({ input: empty(), allow: ['ui', 'overlay', 'mini-ui'] }),
  /** The floating sidebar's own window buttons: the native ones sit under its overlay. */
  'window.minimize': defineCommand({ input: empty(), allow: ['overlay'] }),
  'window.toggleFullscreen': defineCommand({ input: empty(), allow: ['overlay'] }),
  'ui.ready': defineCommand({ input: empty(), allow: ['ui', 'overlay', 'mini-ui'] }),
  'overlay.respond': defineCommand({ input: z.object({ instanceId: id(), result: json() }).strict(), allow: ['overlay'] }),
  'overlay.close': defineCommand({ input: z.object({ instanceId: id() }).strict(), allow: ['overlay'] }),
  'commands.run': defineCommand({ input: z.object({ id: commandId() }).strict(), allow: ['ui', 'overlay', 'mini-ui'] })
} as const
