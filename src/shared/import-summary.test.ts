import { describe, expect, it } from 'vitest'
import { importSummary, sidebarSummary } from './import-summary'

const chrome = { bookmarks: 13, history: 4, passwords: 3, cookies: 2, secretsDenied: false, skipped: 2, error: null }
const sidebar = {
  profiles: 1,
  spaces: 2,
  pinned: 4,
  today: 1,
  favorites: 2,
  history: 3,
  bookmarks: 0,
  searchEngine: null,
  extensions: [],
  passwords: 0,
  cookies: 5,
  secretsDenied: false,
  secretsSkipped: 0,
  skipped: { unsupported: 0, missing: 0, cycles: 0, badUrls: 0, overCap: 0, duplicates: 0, historyFiles: 0 },
  error: null
}

describe('import summaries', () => {
  it('a stopped import still says a denied keychain prompt skipped passwords and sign-ins', () => {
    const denied = 'Passwords and sign-ins were skipped because keychain access wasn’t allowed.'
    expect(importSummary({ ...chrome, secretsDenied: true, error: 'The import couldn’t finish.' })).toBe(
      `Import stopped: The import couldn’t finish. ${denied}`
    )
    expect(sidebarSummary({ ...sidebar, secretsDenied: true, error: 'The Rio profile went away' })).toBe(
      `Import stopped: The Rio profile went away. ${denied}`
    )
    // Allowed, the error reads alone.
    expect(importSummary({ ...chrome, error: 'The import couldn’t finish.' })).toBe('Import stopped: The import couldn’t finish.')
    expect(sidebarSummary({ ...sidebar, error: 'The import couldn’t finish.' })).toBe('Import stopped: The import couldn’t finish.')
  })

  it('a Chrome import counts its site permissions and addresses and says site storage finishes on the next launch', () => {
    expect(importSummary({ ...chrome, siteStorage: 4, permissions: 2, addresses: 1, searchEngine: 'Kagi' })).toBe(
      'Imported 13 bookmarks, 4 history entries, 3 saved passwords, 2 cookies, 2 site permissions and 1 address (2 skipped). Kagi is now the default search engine. Site storage for 4 sites finishes on the next launch of Rio.'
    )
  })

  it('a Chrome import without site data reads as before', () => {
    expect(importSummary(chrome)).toBe('Imported 13 bookmarks, 4 history entries, 3 saved passwords and 2 cookies (2 skipped)')
    expect(importSummary({ ...chrome, passwords: 0, cookies: 0, secretsDenied: true, siteStorage: 0, permissions: 0, addresses: 0 })).toBe(
      'Imported 13 bookmarks and 4 history entries (2 skipped). Passwords and sign-ins were skipped because keychain access wasn’t allowed.'
    )
  })

  it('a denied keychain prompt still lists the site storage, which isn’t encrypted', () => {
    expect(importSummary({ ...chrome, passwords: 0, cookies: 0, secretsDenied: true, siteStorage: 1, permissions: 1, addresses: 2 })).toBe(
      'Imported 13 bookmarks, 4 history entries, 1 site permission and 2 addresses (2 skipped). Passwords and sign-ins were skipped because keychain access wasn’t allowed. Site storage for 1 site finishes on the next launch of Rio.'
    )
  })

  it('a sidebar-file import counts site permissions, addresses and staged site storage', () => {
    expect(sidebarSummary({ ...sidebar, siteStorage: 3, permissions: 1, addresses: 2 })).toBe(
      'Imported 1 profile, 2 spaces, 4 pinned tabs, 1 Today tab, 2 favorites, 3 history entries, 5 cookies, 1 site permission and 2 addresses Site storage for 3 sites finishes on the next launch of Rio.'
    )
  })
})
