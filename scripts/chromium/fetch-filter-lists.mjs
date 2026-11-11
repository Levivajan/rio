// Fetches the filter lists the product blocker is compiled from (docs/chromium/m7.md) into
// out/filter-lists, where `npm run build:webui` finds them. build.sh runs it before the WebUI build. A list younger
// than a day is kept; a failed fetch keeps the last copy. Never run by tests: they use the fixture list.
//   node scripts/chromium/fetch-filter-lists.mjs [--force]
import { existsSync, mkdirSync, statSync, writeFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

export const LISTS = [
  { file: 'easylist.txt', url: 'https://easylist.to/easylist/easylist.txt' },
  { file: 'easyprivacy.txt', url: 'https://easylist.to/easylist/easyprivacy.txt' }
]
const DAY_MS = 24 * 60 * 60 * 1000
const MAX_BYTES = 32 * 1024 * 1024

/** True when `path` exists and is younger than a day at `now`. */
export function isFresh(path, now = Date.now()) {
  return existsSync(path) && now - statSync(path).mtimeMs < DAY_MS
}

/** An Adblock Plus list: a header line, then filters. */
export function looksLikeList(text) {
  return typeof text === 'string' && text.length < MAX_BYTES && /^\[Adblock Plus/i.test(text.trimStart())
}

export async function fetchLists({ dir = 'out/filter-lists', force = false } = {}) {
  mkdirSync(dir, { recursive: true })
  let missing = 0
  for (const { file, url } of LISTS) {
    const path = join(dir, file)
    if (!force && isFresh(path)) continue
    try {
      const res = await fetch(url, { signal: AbortSignal.timeout(60_000) })
      const text = res.ok ? await res.text() : ''
      if (!looksLikeList(text)) throw new Error(`HTTP ${res.status}, not a filter list`)
      writeFileSync(path, text)
      console.log(`filter lists: ${file} updated`)
    } catch (e) {
      console.warn(`filter lists: ${url}: ${e instanceof Error ? e.message : e}${existsSync(path) ? '; keeping the last copy' : ''}`)
      if (!existsSync(path)) missing += 1
    }
  }
  return missing
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exitCode = (await fetchLists({ force: process.argv.includes('--force') })) ? 1 : 0
}
