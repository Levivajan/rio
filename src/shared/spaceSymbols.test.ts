// The symbol icon kind: the contract takes a known symbol and refuses anything else, and the C++ list the
// browser validates against is SPACE_SYMBOLS in the same order (the mojom enum numbers both the same way).
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { spaceIcon } from './ipc/common'
import { SPACE_SYMBOLS } from './types'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')

describe('space symbol icons', () => {
  it('the contract accepts every bundled symbol and keeps emoji and letter icons', () => {
    for (const value of SPACE_SYMBOLS) expect(spaceIcon().safeParse({ kind: 'symbol', value }).success).toBe(true)
    expect(spaceIcon().safeParse({ kind: 'emoji', value: '💼' }).success).toBe(true)
    expect(spaceIcon().safeParse({ kind: 'letter' }).success).toBe(true)
  })
  it('the contract refuses unknown, missing or junk symbol values', () => {
    for (const bad of [
      { kind: 'symbol', value: 'Planet' },
      { kind: 'symbol', value: 'planet.fill' },
      { kind: 'symbol', value: '' },
      { kind: 'symbol' },
      { kind: 'symbol', value: 7 },
      { kind: 'symbol', value: 'planet', extra: 1 },
      { kind: 'glyph', value: 'planet' }
    ])
      expect(spaceIcon().safeParse(bad).success).toBe(false)
  })
  it('has 40 distinct names', () => {
    expect(SPACE_SYMBOLS).toHaveLength(40)
    expect(new Set(SPACE_SYMBOLS).size).toBe(40)
  })
  it('matches the C++ list in clu_space_rules.cc, in order', () => {
    const cc = readFileSync(join(root, 'chromium/clu/browser/spaces/clu_space_rules.cc'), 'utf8')
    const block = /kSpaceSymbols = std::to_array<std::string_view>\(\s*\{([^}]*)\}/.exec(cc)?.[1] ?? ''
    expect([...block.matchAll(/"([a-z]+)"/g)].map((m) => m[1])).toEqual([...SPACE_SYMBOLS])
    expect(cc).toContain(`kSpaceSymbols.size() == kSpaceSymbolCount`)
    const h = readFileSync(join(root, 'chromium/clu/browser/spaces/clu_space_rules.h'), 'utf8')
    expect(h).toContain(`kSpaceSymbolCount = ${SPACE_SYMBOLS.length};`)
  })
  it('the mojom enum lists the symbols in the same order', () => {
    const mojom = readFileSync(join(root, 'chromium/clu/common/mojom/types.mojom'), 'utf8')
    const block = /enum SpaceIconSymbolValue \{([^}]*)\}/.exec(mojom)?.[1] ?? ''
    expect([...block.matchAll(/\/\/ "([a-z]+)"/g)].map((m) => m[1])).toEqual([...SPACE_SYMBOLS])
  })
})
