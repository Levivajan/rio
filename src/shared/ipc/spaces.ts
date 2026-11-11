// Contract for the spaces module. create, switch and profiles.create are seeded.
import { empty, finite, id, int, str, z } from '../schema'
import { anchor, spaceColor, spaceIcon } from './common'
import { defineCommand, out } from './define'

export const spacesIpc = {
  'spaces.create': defineCommand({
    input: z.object({ name: str(240).optional(), icon: spaceIcon().optional(), color: spaceColor().optional(), profileId: id().optional() }).strict(),
    output: out<{ spaceId: string }>()
  }),
  'spaces.update': defineCommand({ input: z.object({ spaceId: id(), name: str(240).optional(), icon: spaceIcon().optional(), color: spaceColor().optional() }).strict() }),
  /** toIndex is clamped to 0..n-1 in main (PLAN 3.11); the bound only keeps junk numbers out. */
  'spaces.reorder': defineCommand({ input: z.object({ spaceId: id(), toIndex: int(-1000, 1000) }).strict() }),
  'spaces.delete': defineCommand({ input: z.object({ spaceId: id() }).strict() }),
  'spaces.switch': defineCommand({
    input: z.union([
      z.object({ spaceId: id() }).strict(),
      z.object({ index: int(0, 8) }).strict(),
      z.object({ step: z.union([z.literal(1), z.literal(-1)]) }).strict()
    ])
  }),
  'spaces.setProfile': defineCommand({
    input: z.object({ spaceId: id(), profileId: id(), confirmed: z.boolean().optional() }).strict(),
    output: out<{ status: 'done' } | { status: 'needs-confirmation'; tabCount: number }>()
  }),
  /** The switcher's overflow button: a menu of every space; picking one switches the sender's window. */
  'spaces.openOverflow': defineCommand({ input: z.object({ anchor: anchor() }).strict(), allow: ['ui'] }),
  /** Opens the space editor: a new space without `spaceId`; `view: 'profiles'` opens it on the profile manager. */
  'spaces.openEditor': defineCommand({ input: z.object({ spaceId: id().optional(), view: z.enum(['space', 'profiles']).optional() }).strict() }),
  /** The switcher icon's context menu: Edit, Move Left, Move Right, Delete (with a confirmation). */
  'spaces.openIconMenu': defineCommand({ input: z.object({ spaceId: id(), anchor: anchor(), light: z.boolean().optional() }).strict(), allow: ['ui'] }),
  'spaces.swipe': defineCommand({
    input: z.object({ phase: z.enum(['begin', 'update', 'end', 'cancel']), dx: finite(), dy: finite(), momentum: z.boolean() }).strict(),
    allow: ['ui']
  }),
  /** Also from the settings window's Profiles tab: its list has +, − and edit buttons. */
  'profiles.create': defineCommand({ input: z.object({ name: str(240).optional() }).strict(), output: out<{ profileId: string }>(), allow: ['ui', 'overlay', 'internal:settings'] }),
  'profiles.rename': defineCommand({ input: z.object({ profileId: id(), name: str(240) }).strict(), allow: ['ui', 'overlay', 'internal:settings'] }),
  'profiles.delete': defineCommand({ input: z.object({ profileId: id() }).strict(), allow: ['ui', 'overlay', 'internal:settings'] }),
  'window.newPrivate': defineCommand({ input: empty() })
} as const
