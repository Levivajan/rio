// Contract for the platform module.
import { empty, id, json, str, z } from '../schema'
import { defineCommand, out } from './define'

/** One process from app.getAppMetrics() for clu://about; `titles` names what a Tab process renders. */
export interface ProcessRow { pid: number; type: string; name: string | null; memoryMb: number; titles: string[] }

export const platformIpc = {
  'platform.requestDefaultBrowser': defineCommand({
    input: empty(),
    output: out<{ status: 'requested' | 'already-default' | 'not-packaged' }>(),
    allow: ['ui', 'internal:settings']
  }),
  'platform.showLogs': defineCommand({ input: empty(), allow: ['ui', 'overlay', 'internal:about'] }),
  'platform.copyDiagnostics': defineCommand({ input: empty(), allow: ['ui', 'overlay', 'internal:about'] }),
  'platform.processes': defineCommand({ input: empty(), output: out<ProcessRow[]>(), allow: ['internal:about'] }),
  'platform.openFile': defineCommand({ input: empty() }),
  /** A check the user asked for; its outcome arrives as app.update and a toast. */
  'platform.checkForUpdates': defineCommand({ input: empty(), output: out<{ status: 'checking' | 'busy' | 'ready' | 'unavailable' }>() }),
  /** Relaunches into the update app.update says is ready. */
  'platform.installUpdate': defineCommand({ input: empty(), output: out<{ status: 'installing' | 'not-ready' }>() }),
  /** The update card's close button: hides the card for this version; the update still installs on quit. */
  'platform.dismissUpdate': defineCommand({ input: empty(), output: out<{ status: 'dismissed' | 'not-ready' }>(), allow: ['ui'] }),
  'externalApp.respond': defineCommand({ input: z.object({ promptId: id(), action: z.enum(['open', 'cancel']), always: z.boolean().optional() }).strict(), allow: ['overlay'] }),
  'log.write': defineCommand({
    input: z.object({ level: z.enum(['debug', 'info', 'warn', 'error']), scope: str(64), msg: str(2048), data: json().optional() }).strict(),
    allow: ['ui', 'overlay', 'mini-ui', 'internal:settings', 'internal:history', 'internal:extensions', 'internal:about']
  })
} as const
