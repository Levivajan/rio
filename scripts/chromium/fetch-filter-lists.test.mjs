import { mkdtempSync, rmSync, utimesSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { isFresh, LISTS, looksLikeList } from './fetch-filter-lists.mjs'

describe('fetch-filter-lists', () => {
  it('fetches EasyList and EasyPrivacy over https', () => {
    expect(LISTS.map((l) => l.file)).toEqual(['easylist.txt', 'easyprivacy.txt'])
    for (const l of LISTS) expect(l.url.startsWith('https://')).toBe(true)
  })

  it('accepts only Adblock Plus lists', () => {
    expect(looksLikeList('[Adblock Plus 2.0]\n||a.example^')).toBe(true)
    expect(looksLikeList('<html>blocked</html>')).toBe(false)
    expect(looksLikeList(undefined)).toBe(false)
  })

  it('keeps a list younger than a day', () => {
    const dir = mkdtempSync(join(tmpdir(), 'clu-lists-'))
    try {
      const path = join(dir, 'easylist.txt')
      expect(isFresh(path)).toBe(false)
      writeFileSync(path, '[Adblock Plus 2.0]\n')
      expect(isFresh(path)).toBe(true)
      const old = (Date.now() - 25 * 60 * 60 * 1000) / 1000
      utimesSync(path, old, old)
      expect(isFresh(path)).toBe(false)
    } finally {
      rmSync(dir, { recursive: true, force: true })
    }
  })
})
