// Contract for the capabilities module.
import { empty, id, rect, str, url, z } from '../schema'
import { defineCommand, out } from './define'

export interface PermissionRow { profileId: string; origin: string; permission: string; decision: 'allow' | 'deny'; updatedAt: number }
/** A file of the Library's folders: the Mac's Downloads folder, or the screenshot folder for Media. */
export interface LibraryFile { name: string; size: number; modifiedAt: number; kind: 'image' | 'video' | 'file' }
export const LIBRARY_SOURCES = ['downloads', 'media'] as const
export type LibrarySource = (typeof LIBRARY_SOURCES)[number]
export interface HibernationRow { tabId: string; title: string; lifecycle: string; lastActiveAt: number; reason: string | null }

export const capabilitiesIpc = {
  'download.act': defineCommand({ input: z.object({ id: id(), action: z.enum(['pause', 'resume', 'cancel', 'retry', 'open', 'reveal', 'remove', 'drag']) }).strict() }),
  'download.clear': defineCommand({ input: empty() }),
  'download.openPopover': defineCommand({ input: empty() }),
  'download.openFolder': defineCommand({ input: empty() }),
  // Where the sidebar's downloads button is (frame WebUI coordinates), none while it does not show: Chromium's downloads
  // bubble hangs from it then, from Cmd+Shift+J and a new download too (docs/chromium/fixes.md).
  // The Library's folders: the files in the Mac's Downloads folder or the screenshot folder (Media), newest first.
  // A file is addressed by its source and bare name, never a path: the browser resolves it inside that folder. The
  // floating sidebar (the overlay's peek) shows the Library and its pop-up too.
  'download.files': defineCommand({ input: z.object({ source: z.enum(LIBRARY_SOURCES) }).strict(), output: out<LibraryFile[]>(), allow: ['ui', 'overlay'] }),
  // The right-click menu adds copy (the file), copyPath, share (macOS's share picker at `anchor`), trash and rename
  // (to `newName`, a bare name in the same folder). drag, on either command, starts the file's native drag under
  // the pointer from a dragstart, as chrome://downloads' rows do.
  'download.fileAct': defineCommand({
    input: z
      .object({
        source: z.enum(LIBRARY_SOURCES),
        name: str(255),
        action: z.enum(['open', 'reveal', 'copy', 'copyPath', 'share', 'trash', 'rename', 'drag']),
        newName: str(255).optional(),
        anchor: rect().optional()
      })
      .strict(),
    allow: ['ui', 'overlay']
  }),
  // A Library file's thumbnail: QuickLook's 80 px rendering of it as a PNG data URL, '' when it has none.
  'download.thumb': defineCommand({ input: z.object({ source: z.enum(LIBRARY_SOURCES), name: str(255) }).strict(), output: out<string>(), allow: ['ui', 'overlay'] }),
  'download.setButtonRect': defineCommand({ input: z.object({ rect: rect().optional() }).strict(), allow: ['ui'] }),
  'permission.respond': defineCommand({ input: z.object({ promptId: id(), action: z.enum(['allow', 'block', 'dismiss']), remember: z.boolean().optional() }).strict(), allow: ['overlay'] }),
  'permission.list': defineCommand({ input: z.object({ profileId: id(), origin: url().optional() }).strict(), output: out<PermissionRow[]>(), allow: ['ui', 'overlay', 'internal:settings'] }),
  'permission.set': defineCommand({ input: z.object({ profileId: id(), origin: url(), permission: str(64), decision: z.enum(['ask', 'allow', 'block']) }).strict(), allow: ['ui', 'overlay', 'internal:settings'] }),
  'permission.clear': defineCommand({ input: z.object({ profileId: id(), origin: url().optional(), permission: str(64).optional() }).strict(), allow: ['internal:settings', 'overlay'] }),
  'hibernation.list': defineCommand({ input: empty(), output: out<HibernationRow[]>(), allow: ['internal:about'] })
} as const
