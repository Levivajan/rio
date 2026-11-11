// The menu bar's Help ▸ Contact the Team (kCluContactUrl in clu_urls.h) and the command bar's help row (CONTACT_URL)
// open the same public issue form.
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { CONTACT_URL } from './rank'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..')

describe('Contact the Team', () => {
  it('the C++ kCluContactUrl matches CONTACT_URL', () => {
    const h = readFileSync(join(root, 'chromium/clu/common/clu_urls.h'), 'utf8')
    const url = /kCluContactUrl\[\]\s*=\s*"([^"]*)"/.exec(h)?.[1]
    expect(url).toBe(CONTACT_URL)
  })
  it('is the public issue form', () => {
    expect(CONTACT_URL).toBe('https://github.com/Levivajan/rio-releases/issues/new')
  })
})
