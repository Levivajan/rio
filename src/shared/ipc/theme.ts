// Contract for the theme module. Themes use the theme validator (h in [0,360)).
import { id, rect, z } from '../schema'
import { spaceThemeSchema } from '../theme/schema'
import { defineCommand } from './define'

export const themeIpc = {
  'theme.setMode': defineCommand({ input: z.object({ mode: z.enum(['auto', 'light', 'dark']) }).strict(), allow: ['ui', 'overlay', 'internal:settings'] }),
  'theme.previewSpaceTheme': defineCommand({ input: z.object({ spaceId: id(), theme: spaceThemeSchema() }).strict() }),
  'theme.setSpaceTheme': defineCommand({ input: z.object({ spaceId: id(), theme: spaceThemeSchema() }).strict() }),
  'theme.resetSpaceTheme': defineCommand({ input: z.object({ spaceId: id() }).strict() }),
  'theme.openPicker': defineCommand({ input: z.object({ spaceId: id(), anchor: rect() }).strict() }),
  'onboarding.complete': defineCommand({ input: z.object({ skipped: z.boolean() }).strict() })
} as const
