// Builds the blocker's resources (docs/chromium/m7.md):
//   node scripts/chromium/run-ts.mjs scripts/chromium/blocker/build.ts --fixture
//     writes chromium/clu/test/data/blocker/{rules,cosmetic}.json from the fixture filters (clu_browsertests load them)
// build-webui.mjs calls buildBlocker() for the product bundle: the lists scripts/chromium/fetch-filter-lists.mjs
// keeps in out/filter-lists (EasyList and EasyPrivacy), or the fixture list with a warning when there are none.
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { compile, resources, type Compiled } from './abp'

export const FIXTURE_DIR = 'chromium/clu/test/data/blocker'
export const LISTS_DIR = 'out/filter-lists'

export function compileFiles(files: string[]): Compiled {
  return compile(files.map((f) => readFileSync(f, 'utf8')))
}

export function fixture(): { rules: string; cosmetic: string } {
  return resources(compileFiles([join(FIXTURE_DIR, 'filters.txt')]))
}

/** Writes blocker-rules.json and blocker-cosmetic.json into `dir`; returns what was compiled from what. */
export function buildBlocker(dir: string, listsDir = LISTS_DIR): { lists: string[]; compiled: Compiled } {
  const found = existsSync(listsDir) ? readdirSync(listsDir).filter((f) => f.endsWith('.txt')).sort().map((f) => join(listsDir, f)) : []
  const lists = found.length ? found : [join(FIXTURE_DIR, 'filters.txt')]
  if (!found.length) console.warn(`blocker: no filter lists in ${listsDir}; the bundle carries the fixture list (run scripts/chromium/fetch-filter-lists.mjs)`)
  const compiled = compileFiles(lists)
  const out = resources(compiled)
  mkdirSync(dir, { recursive: true })
  writeFileSync(join(dir, 'blocker-rules.json'), out.rules)
  writeFileSync(join(dir, 'blocker-cosmetic.json'), out.cosmetic)
  return { lists, compiled }
}

export function main(args: string[]): number {
  if (!args.includes('--fixture')) {
    console.error('usage: build.ts --fixture')
    return 2
  }
  const out = fixture()
  writeFileSync(resolve(FIXTURE_DIR, 'rules.json'), out.rules)
  writeFileSync(resolve(FIXTURE_DIR, 'cosmetic.json'), out.cosmetic)
  console.log(`wrote ${FIXTURE_DIR}/rules.json and cosmetic.json`)
  return 0
}
