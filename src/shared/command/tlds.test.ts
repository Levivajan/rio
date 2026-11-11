import { spawnSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { TLDS } from './tlds'

const HERE = dirname(fileURLToPath(import.meta.url))
const ROOT = join(HERE, '..', '..', '..')

function generate(): string {
  const r = spawnSync(process.execPath, [join(ROOT, 'scripts', 'gen-tlds.mjs'), '--stdout'], {
    encoding: 'utf8',
    timeout: 15_000
  })
  if (r.status !== 0) throw new Error(`gen-tlds failed: ${r.stderr}`)
  return r.stdout
}

describe('tlds', () => {
  it('scripts/gen-tlds.mjs output is checked in and deterministic', () => {
    const first = generate()
    expect(generate()).toBe(first)
    expect(readFileSync(join(HERE, 'tlds.ts'), 'utf8')).toBe(first)
  })

  it('holds the IANA list in lowercase', () => {
    expect(TLDS.size).toBeGreaterThan(1000)
    for (const t of ['com', 'de', 'md', 'zip', 'xn--p1ai', 'org']) expect(TLDS.has(t)).toBe(true)
    for (const t of ['js', 'yaml', 'corp', 'lan', 'local', 'COM', '']) expect(TLDS.has(t)).toBe(false)
  })
})
