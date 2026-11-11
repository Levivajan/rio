// Import summaries. No zod here: the frame's first-run screen shows them, and a schema
// built there before zod's jitless switch probes `new Function`, which Trusted Types reports.
import type { ImportResult, SidebarImportResult, SidebarSkips } from './ipc/data'

const plural = (n: number, one: string, many: string) => `${n.toLocaleString('en-US')} ${n === 1 ? one : many}`

export const isSidebarResult = (r: ImportResult | SidebarImportResult): r is SidebarImportResult => 'spaces' in r
export const sidebarSkipped = (s: SidebarSkips) => Object.values(s).reduce((a, b) => a + b, 0)

type SiteData = { permissions?: number; addresses?: number; siteStorage?: number }
/** Site permissions and saved addresses, when any came over. */
const siteParts = (r: SiteData) => [
  ...((r.permissions ?? 0) > 0 ? [plural(r.permissions ?? 0, 'site permission', 'site permissions')] : []),
  ...((r.addresses ?? 0) > 0 ? [plural(r.addresses ?? 0, 'address', 'addresses')] : [])
]
/** Staged site storage goes into the profile at the next launch, before its storage opens. */
const siteStorage = (r: SiteData) =>
  (r.siteStorage ?? 0) > 0 ? ` Site storage for ${plural(r.siteStorage ?? 0, 'site', 'sites')} finishes on the next launch of Rio.` : ''

const DENIED = 'Passwords and sign-ins were skipped because keychain access wasn’t allowed.'
/** The error that stopped an import, and the denied keychain prompt when there was one. */
const stopped = (error: string, denied: boolean | undefined) =>
  `Import stopped: ${error}${denied ? `${/[.!?]$/.test(error) ? '' : '.'} ${DENIED}` : ''}`

/**
 * "Imported 12 bookmarks, 4 history entries, 3 saved passwords, 20 cookies, 2 site permissions and 1 address (2
 * skipped).", or the error that stopped the import. Passwords, cookies, permissions and addresses show when any came
 * over; a denied keychain prompt (stopped or not), a new default search engine and staged site storage each add a
 * sentence.
 */
export function importSummary(r: ImportResult): string {
  if (r.error) return stopped(r.error, r.secretsDenied)
  const skipped = r.skipped > 0 ? ` (${r.skipped.toLocaleString('en-US')} skipped)` : ''
  const parts = [plural(r.bookmarks, 'bookmark', 'bookmarks'), plural(r.history, 'history entry', 'history entries')]
  if ((r.passwords ?? 0) > 0) parts.push(plural(r.passwords, 'saved password', 'saved passwords'))
  if ((r.cookies ?? 0) > 0) parts.push(plural(r.cookies, 'cookie', 'cookies'))
  parts.push(...siteParts(r))
  const notes = [
    r.secretsDenied ? DENIED : '',
    r.searchEngine ? `${r.searchEngine} is now the default search engine.` : '',
    siteStorage(r).trim()
  ].filter(Boolean)
  return `Imported ${parts.slice(0, -1).join(', ')} and ${parts[parts.length - 1]}${skipped}${notes.length > 0 ? `. ${notes.join(' ')}` : ''}`
}

/**
 * "Imported 2 profiles, 4 spaces, 9 pinned tabs, 3 Today tabs, 2 favorites, 120 history entries, 40 saved passwords and
 * 300 cookies (8 skipped)", or the error that stopped it. Passwords and cookies show when any came over; a denied
 * keychain prompt, stopped or not, adds a sentence saying they were skipped.
 */
export function sidebarSummary(r: SidebarImportResult): string {
  if (r.error) return stopped(r.error, r.secretsDenied)
  const n = sidebarSkipped(r.skipped) + (r.secretsSkipped ?? 0)
  const skipped = n > 0 ? ` (${n.toLocaleString('en-US')} skipped)` : ''
  const parts = [
    plural(r.profiles, 'profile', 'profiles'),
    plural(r.spaces, 'space', 'spaces'),
    plural(r.pinned, 'pinned tab', 'pinned tabs'),
    plural(r.today, 'Today tab', 'Today tabs'),
    plural(r.favorites, 'favorite', 'favorites'),
    plural(r.history, 'history entry', 'history entries')
  ]
  if ((r.bookmarks ?? 0) > 0) parts.push(plural(r.bookmarks, 'bookmark', 'bookmarks'))
  if (r.passwords > 0) parts.push(plural(r.passwords, 'saved password', 'saved passwords'))
  if (r.cookies > 0) parts.push(plural(r.cookies, 'cookie', 'cookies'))
  parts.push(...siteParts(r))
  const denied = r.secretsDenied ? ` ${DENIED}` : ''
  const engine = r.searchEngine ? ` ${r.searchEngine} is now the default search engine.` : ''
  const ext = r.extensions?.length ?? 0
  const extensions = ext > 0 ? ` ${plural(ext, 'extension', 'extensions')} to reinstall below.` : ''
  return `Imported ${parts.slice(0, -1).join(', ')} and ${parts[parts.length - 1]}${skipped}${denied}${engine}${siteStorage(r)}${extensions}`
}
