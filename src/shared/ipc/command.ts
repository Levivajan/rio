// Contract for the command module. open, close, execute and copyUrl are seeded.
import { MAX_INPUT, MAX_SUGGEST_QUERY } from '../command/limits'
import { id, int, str, url, z } from '../schema'
import { commandResult } from './common'
import { defineCommand, out } from './define'

export const COMMAND_MODES = ['new-tab', 'edit-url', 'split-add', 'mini'] as const
export type CommandMode = (typeof COMMAND_MODES)[number]

export interface IndexRow { key: string; kind: 'tab' | 'history'; url: string; title: string; tabId?: string; spaceId?: string; visitCount?: number; typedCount?: number; lastVisitAt?: number }
/** An action row: a palette command (`id` is its table id) or a provider's row (`providerId` and `token`). `exact`
 *  marks a row whose title or alias equals the query, which the bar puts first. */
export interface ActionRow { id: string; title: string; aliases?: string[]; subtitle?: string; providerId?: string; token?: string; accelerator?: string; exact?: boolean }

export const commandIpc = {
  'command.open': defineCommand({ input: z.object({ mode: z.enum(COMMAND_MODES) }).strict() }),
  'command.close': defineCommand({ input: z.object({ sessionId: id() }).strict(), allow: ['overlay'] }),
  'command.execute': defineCommand({
    input: z.object({ sessionId: id(), result: commandResult(), disposition: z.enum(['default', 'new-tab', 'split']), input: str(MAX_INPUT) }).strict(),
    output: out<{ ok: boolean; error?: 'stale' | 'invalid' | 'blocked-url' }>(),
    allow: ['overlay']
  }),
  'command.syncIndex': defineCommand({
    input: z.object({ sinceRev: int(0, Number.MAX_SAFE_INTEGER) }).strict(),
    output: out<{ rev: number; full: boolean; upserts: IndexRow[]; deletes: string[] }>(),
    allow: ['overlay']
  }),
  'command.actions': defineCommand({ input: z.object({ sessionId: id(), query: str(256) }).strict(), output: out<ActionRow[]>(), allow: ['overlay'] }),
  'command.suggest': defineCommand({
    input: z.object({ sessionId: id(), requestId: int(0, Number.MAX_SAFE_INTEGER), query: str(MAX_SUGGEST_QUERY) }).strict(),
    output: out<{ requestId: number; items: string[] }>(),
    allow: ['overlay']
  }),
  'command.deleteEntry': defineCommand({ input: z.object({ url: url() }).strict(), allow: ['overlay'] }),
  'command.copyUrl': defineCommand({
    input: z.object({ tabId: id().optional(), format: z.enum(['plain', 'markdown']).optional() }).strict(),
    output: out<{ copied: string | null }>()
  })
} as const
