// Contract for the tabs module. create, activate, close and openMenu are seeded.
import { MAX_BULK } from '../tabs'
import { id, rect, str, url, z } from '../schema'
import { anchor, dropTarget, optionalTab } from './common'
import { defineCommand, out } from './define'

const bulkAction = z.enum(['archive', 'close', 'copyUrls', 'copyUrlsMarkdown', 'addToSplit'])

export const tabsIpc = {
  'tabs.create': defineCommand({ input: z.object({ spaceId: id().optional(), url: url(), activate: z.boolean().optional() }).strict(), output: out<{ tabId: string }>() }),
  'tabs.activate': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'tabs.close': defineCommand({ input: optionalTab() }),
  'tabs.duplicate': defineCommand({ input: optionalTab() }),
  'tabs.setMuted': defineCommand({ input: z.object({ tabId: id(), muted: z.boolean() }).strict() }),
  'tabs.rename': defineCommand({ input: z.object({ nodeId: id(), title: str(200) }).strict() }),
  'tabs.setIcon': defineCommand({ input: z.object({ nodeId: id(), icon: str(32).nullable() }).strict() }),
  /** `ask` shows the cross-profile confirm in the overlay instead of answering needs-confirmation. `drop` marks a
   * sidebar drag's drop, which ticks the trackpad (Advanced's tabs.reorderHaptic). */
  'tabs.move': defineCommand({
    input: z.object({ nodeId: id(), target: dropTarget(), confirmed: z.boolean().optional(), ask: z.boolean().optional(), drop: z.boolean().optional() }).strict(),
    output: out<{ status: 'moved' } | { status: 'needs-confirmation'; reason: 'cross-profile' }>()
  }),
  /** Multi-select actions on 1..200 tabs, in the given order. A move lands them together (TabStore.moveMany); `ask`
   * and `drop` work as in tabs.move. */
  'tabs.bulk': defineCommand({
    input: z.union([
      z.object({ tabIds: z.array(id()).min(1).max(MAX_BULK), action: bulkAction }).strict(),
      z.object({ tabIds: z.array(id()).min(1).max(MAX_BULK), action: z.literal('move'), target: dropTarget(), confirmed: z.boolean().optional(), ask: z.boolean().optional(), drop: z.boolean().optional() }).strict()
    ]),
    output: out<{ status: 'done' | 'moved' } | { status: 'needs-confirmation'; reason: 'cross-profile' }>()
  }),
  /** Tidy Today with the on-device model (ai.tidyTabs): the browser asks rio-ai to group the space's Today tabs by
   * topic and moves them into that order. `no-model` when the Mac has no model; the sidebar then uses its rules. */
  'tabs.tidyToday': defineCommand({
    input: z.object({ spaceId: id() }).strict(),
    output: out<{ status: 'tidied' | 'unchanged' | 'no-model' }>()
  }),
  'tabs.togglePin': defineCommand({ input: optionalTab() }),
  'tabs.resetToPinned': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'tabs.setPinnedUrl': defineCommand({ input: z.object({ tabId: id(), url: url() }).strict() }),
  'tabs.delete': defineCommand({ input: z.object({ nodeId: id() }).strict() }),
  'tabs.toNewWindow': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'tabs.openMenu': defineCommand({
    input: z
      .object({
        menu: z.enum(['row', 'empty', 'new', 'more', 'space']),
        nodeId: id().optional(),
        tabIds: z.array(id()).max(MAX_BULK).optional(),
        anchor: anchor(),
        openedFrom: id().optional(),
        /** The caller's chrome set is light: the native menu draws light on a light space. */
        light: z.boolean().optional()
      })
      .strict(),
    output: out<{ rename: string | null }>()
  }),
  /** F15 hover card: a row rect shows the card for that tab beside the sidebar; a null tabId hides it. */
  /** The row hover card; `held`: the overlay holds Gmail's card open while the pointer is on it, and false
   *  closes it. */
  'tabs.hover': defineCommand({ input: z.object({ tabId: id().nullable(), anchor: rect().optional(), held: z.boolean().optional() }).strict() }),
  /** A message row of Gmail's card opens the message (a mail.google.com URL) in that row's own tab. */
  'tabs.openMail': defineCommand({ input: z.object({ tabId: id(), url: url() }).strict() }),
  /** The collapsed-folder popover: the frame opens it at `rect` beside the sidebar; the overlay holds it while the
   *  pointer is over it (`held`); a null folderId closes it unless it is held. */
  'folders.peek': defineCommand({ input: z.object({ folderId: id().nullable(), rect: rect().optional(), held: z.boolean().optional() }).strict() }),
  'folders.create': defineCommand({ input: z.object({ spaceId: id(), parentId: id().nullable().optional() }).strict(), output: out<{ folderId: string }>() }),
  /** Live Folders (the space menu's Live Folders ▸): a pinned folder that follows a public feed, `source` being a feed
   *  URL or a GitHub "owner/repo". A source that names no feed answers bad-payload. */
  'folders.createLive': defineCommand({
    input: z.object({ spaceId: id(), kind: z.enum(['feed', 'githubReleases', 'githubCommits']), source: str(2048) }).strict(),
    output: out<{ folderId: string }>()
  }),
  'folders.setCollapsed': defineCommand({ input: z.object({ folderId: id(), collapsed: z.boolean() }).strict() }),
  'folders.delete': defineCommand({ input: z.object({ folderId: id() }).strict() }),
  'sections.setPinnedCollapsed': defineCommand({ input: z.object({ spaceId: id(), collapsed: z.boolean() }).strict() })
} as const
