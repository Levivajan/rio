// Contract for the archive module.
import { empty, id, int, str, z } from '../schema'
import { defineCommand, out } from './define'

export interface ArchiveRow { id: string; profileId: string; spaceId: string | null; title: string; url: string; favicon: string | null; reason: string; archivedAt: number }

export const archiveIpc = {
  'archive.list': defineCommand({
    input: z.object({ profileId: id(), query: str(200).optional(), cursor: str(256).optional(), limit: int(1, 100).optional() }).strict(),
    output: out<{ rows: ArchiveRow[]; nextCursor: string | null }>()
  }),
  'archive.restore': defineCommand({ input: z.object({ archiveId: id() }).strict() }),
  'archive.delete': defineCommand({ input: z.object({ archiveId: id() }).strict() }),
  'archive.open': defineCommand({ input: empty() }),
  'archive.close': defineCommand({ input: empty() }),
  /** The width the Library's section wants (the Spaces view: every card and its "+"); 0 means the default
   *  417. The browser still keeps the page's minimum room. */
  'archive.setWidth': defineCommand({ input: z.object({ width: int(0, 4096) }).strict() }),
  'archive.clearToday': defineCommand({ input: z.object({ spaceId: id().optional() }).strict() }),
  'archive.undoClear': defineCommand({ input: empty() }),
  'archive.reopenClosed': defineCommand({ input: empty() }),
  'switcher.respond': defineCommand({ input: z.object({ action: z.enum(['commit', 'cancel']), tabId: id().optional() }).strict(), allow: ['overlay'] })
} as const
