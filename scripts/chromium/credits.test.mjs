// The filter lists the blocker ships carry their credit on chrome://credits (docs/chromium/fixes.md, leftovers 6):
// tools/licenses/licenses.py lists every directory under a third_party directory that has a README.chromium with its
// mandatory fields and a license file.
import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { LISTS } from './fetch-filter-lists.mjs'

const DIR = 'chromium/clu/third_party/easylist'
const MANDATORY = ['Name', 'URL', 'License', 'License File', 'Shipped']

function fields(text) {
  const out = {}
  for (const line of text.split('\n')) {
    const m = /^([A-Za-z ]+):\s*(.*)$/.exec(line)
    if (m && !(m[1] in out)) out[m[1]] = m[2].trim()
  }
  return out
}

describe('filter list credits', () => {
  const readme = readFileSync(join(DIR, 'README.chromium'), 'utf8')
  const f = fields(readme)

  it('has every field licenses.py needs, shipped', () => {
    for (const key of MANDATORY) expect(f[key], key).toBeTruthy()
    expect(f.Shipped).toBe('yes')
    expect(f.Name).toContain('EasyList')
    expect(f.Name).toContain('EasyPrivacy')
  })

  it('names every list the build fetches, and its license text credits the EasyList authors', () => {
    for (const { url } of LISTS) expect(readme).toContain(url)
    const license = join(DIR, f['License File'])
    expect(existsSync(license)).toBe(true)
    const text = readFileSync(license, 'utf8')
    expect(text).toContain('The EasyList authors')
    expect(text).toContain('Creative Commons Attribution-ShareAlike 3.0 Unported')
  })
})
