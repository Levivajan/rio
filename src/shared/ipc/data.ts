// Contract for the data module. settings.set is seeded.
import { empty, id, int, json, str, url, z } from '../schema'
import { defineCommand, out } from './define'

export interface HistoryRow { id: number; urlId: number; url: string; title: string; visitedAt: number; transition: string; favicon: string | null }
export interface ImportDetect {
  chrome: { profiles: { dir: string; name: string; hasBookmarks: boolean; hasHistory: boolean }[] }
  /** Whether a sidebar file from another browser exists; its contents are read only on import. */
  /** `previous`: Rio profiles an earlier import made (and "Imported N" ones a broken run left); a new import asks to replace them. */
  arc: { found: boolean; previous: number }
  /** The space of the settings tab that asked, the default import target. */
  currentSpaceId: string | null
}
/**
 * `error` is set when the import stopped early; what was imported before it stays. `passwords` and `cookies` (the
 * sign-ins) came over with the key from the keychain item "Chrome Safe Storage"; `secretsDenied` when the user didn't
 * allow it, so none did.
 */
export interface ImportResult {
  bookmarks: number
  history: number
  passwords: number
  cookies: number
  secretsDenied: boolean
  /** Site data: sites whose Local Storage or IndexedDB was staged (it goes in at the next launch), site permissions and saved addresses written, and the default search engine when it became Rio's. */
  siteStorage?: number
  permissions?: number
  addresses?: number
  searchEngine?: string | null
  skipped: number
  error: string | null
}

/** The app settings window's tabs, in toolbar order. */
export const SETTINGS_TABS = ['general', 'profiles', 'ai', 'links', 'shortcuts', 'icon', 'advanced'] as const
export type SettingsTab = (typeof SETTINGS_TABS)[number]
/** The pages of Chromium's settings the app settings link to (chrome://settings/<path>, see clu_window_handlers.cc). */
export const BROWSER_SETTINGS_PAGES = ['root', 'privacy', 'passwords', 'payments', 'notifications', 'siteSettings', 'search', 'downloads', 'about'] as const


/** `historyFiles`: source profiles whose History file couldn't be read. */
export interface SidebarSkips { unsupported: number; missing: number; cycles: number; badUrls: number; overCap: number; duplicates: number; historyFiles: number }
/**
 * The sidebar-file import (`import.run` with source 'arc'): profiles, spaces with pinned and Today tabs, favorites, then
 * each source profile's history, then (when asked) its saved passwords and cookies. A file Rio can't take writes nothing: with `error` set, the counts are 0.
 */
export interface SidebarImportResult {
  profiles: number
  spaces: number
  pinned: number
  today: number
  favorites: number
  history: number
  /** Bookmarks from each source profile, as a pinned "Imported bookmarks" folder in its first space. */
  bookmarks: number
  /** The source's default search engine, when it set one and Rio made it the default. */
  searchEngine: string | null
  /** Web Store extensions the source had, for one-click reinstall. */
  extensions: { id: string; name: string }[]
  /** Saved passwords and cookies, decrypted with the other browser's keychain key. */
  passwords: number
  cookies: number
  /** The user denied the keychain prompt (or there was no key): no passwords or cookies came over. */
  secretsDenied: boolean
  /** Site data: sites whose Local Storage or IndexedDB was staged (it goes in at the next launch), site permissions and saved addresses. */
  siteStorage?: number
  permissions?: number
  addresses?: number
  /** Passwords and cookies that didn't decrypt, junk rows and rows past the caps. */
  secretsSkipped: number
  skipped: SidebarSkips
  error: string | null
}
// The summaries live in a module with no zod, so the frame can show them without building schemas.
export { importSummary, isSidebarResult, sidebarSkipped, sidebarSummary } from '../import-summary'

export const dataIpc = {
  'history.query': defineCommand({
    input: z
      .object({
        text: str(200).optional(),
        cursor: z.object({ visitedAt: int(0, Number.MAX_SAFE_INTEGER), id: int(0, Number.MAX_SAFE_INTEGER) }).strict().optional(),
        limit: int(1, 200).optional()
      })
      .strict(),
    output: out<{ rows: HistoryRow[]; nextCursor: { visitedAt: number; id: number } | null }>(),
    allow: ['internal:history', 'overlay']
  }),
  'history.deleteVisits': defineCommand({ input: z.object({ ids: z.array(int(0, Number.MAX_SAFE_INTEGER)).min(1).max(1000) }).strict(), allow: ['internal:history'] }),
  'history.deleteUrl': defineCommand({ input: z.object({ url: url() }).strict(), allow: ['internal:history'] }),
  'history.open': defineCommand({ input: empty() }),
  /** The app settings window (docs/chromium/parity-settings.md), at one of its tabs; it opens or comes to the front. */
  'settings.open': defineCommand({ input: z.object({ section: z.enum(SETTINGS_TABS).optional() }).strict(), allow: ['ui', 'overlay', 'internal:settings'] }),
  /** Chromium's own settings (rio://settings) as a row of the window's space, at one of its pages. */
  'settings.openBrowser': defineCommand({ input: z.object({ page: z.enum(BROWSER_SETTINGS_PAGES).optional() }).strict(), allow: ['ui', 'overlay', 'internal:settings'] }),
  'settings.set': defineCommand({
    input: z.object({ key: str(64), scope: z.union([z.literal('global'), id()]), value: json() }).strict(),
    allow: ['ui', 'overlay', 'internal:settings']
  }),
  'browsingData.open': defineCommand({ input: z.object({ profileId: id().optional() }).strict(), allow: ['ui', 'overlay', 'internal:history', 'internal:settings'] }),
  'browsingData.clear': defineCommand({
    input: z
      .object({
        profileId: id(),
        range: z.enum(['hour', 'day', 'week', 'month', 'all']),
        types: z.object({ history: z.boolean(), cookies: z.boolean(), cache: z.boolean(), downloads: z.boolean(), permissions: z.boolean(), archive: z.boolean() }).strict()
      })
      .strict(),
    allow: ['overlay']
  }),
  // The first-run screen (the frame) asks too.
  'import.detect': defineCommand({ input: empty(), output: out<ImportDetect>(), allow: ['ui', 'internal:settings'] }),
  'import.run': defineCommand({
    input: z.union([
      z
        .object({
          source: z.literal('chrome'),
          profileDir: z.string().regex(/^(Default|Profile \d{1,3})$/),
          bookmarks: z.boolean(),
          history: z.boolean(),
          // Saved passwords and sign-ins (cookies): macOS asks the user for the keychain item "Chrome Safe Storage".
          passwords: z.boolean(),
          cookies: z.boolean(),
          // Saved addresses into Autofill, and Google Chrome's default search engine as Rio's.
          autofill: z.boolean(),
          searchEngine: z.boolean(),
          targetSpaceId: id()
        })
        .strict(),
      // One checkbox per item; an item left out comes over.
      z
        .object({
          source: z.literal('arc'),
          passwords: z.boolean(),
          cookies: z.boolean(),
          favorites: z.boolean().optional(),
          pinned: z.boolean().optional(),
          today: z.boolean().optional(),
          history: z.boolean().optional(),
          bookmarks: z.boolean().optional(),
          searchEngine: z.boolean().optional(),
          extensions: z.boolean().optional(),
          // Saved addresses into Autofill.
          autofill: z.boolean().optional(),
          // The user said yes to replacing the previous import.
          replace: z.boolean().optional()
        })
        .strict()
    ]),
    output: out<ImportResult | SidebarImportResult>(),
    allow: ['ui', 'internal:settings']
  }),
  /** Chromium's own password import from a CSV file (chrome://password-manager/settings?import=true) in the window's space. */
  'import.openPasswords': defineCommand({ input: empty(), allow: ['ui', 'internal:settings'] })
} as const
