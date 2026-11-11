// Match budget (04-command F13): 10,000 history rows plus 500 tabs, 200 queries of 1 to 20 chars, p95 at or under
// 8 ms (the CI-tolerant gate). Each query runs the full rank without the incremental state, the worst case. The Mac
// is shared with other work, so the gate takes the best p95 of three rounds over the same queries.
import { performance } from 'node:perf_hooks'
import { expect, it } from 'vitest'
import { buildHistoryIndex, buildTabIndex, rank, type HistoryEntry, type TabEntry } from './rank'

const WORDS = [
  'github', 'mail', 'news', 'docs', 'calendar', 'review', 'pull', 'request', 'issue', 'design', 'report', 'weekly',
  'café', 'menu', 'recipe', 'travel', 'flight', 'hotel', 'münchen', 'market', 'stock', 'price', 'api', 'reference',
  'guide', 'tutorial', 'webkit', 'chromium', 'browser', 'sidebar', 'search', 'engine', 'video', 'music', 'playlist',
  'invoice', 'billing', 'account', 'settings', 'profile', 'project', 'roadmap', 'sprint', 'board', 'release', 'notes'
]

// Deterministic PRNG so every run sees the same rows and queries.
let seed = 0x2f6b
const rand = () => {
  seed = (seed * 1103515245 + 12345) & 0x7fffffff
  return seed / 0x7fffffff
}
const pick = <T>(xs: readonly T[]): T => xs[Math.floor(rand() * xs.length)] as T
const words = (n: number) => Array.from({ length: n }, () => pick(WORDS)).join(' ')
const NOW = Date.UTC(2026, 10, 7)

function syntheticUrl(i: number): string {
  const host = `${pick(WORDS).normalize('NFKD').replace(/\p{M}/gu, '')}${i % 97}.${pick(['com', 'org', 'dev', 'io', 'de'])}`
  return `https://${rand() < 0.3 ? 'www.' : ''}${host}/${pick(WORDS)}/${pick(WORDS)}-${i}?id=${i}`.normalize('NFC')
}

it('matches 10,000 history rows plus 500 tabs with p95 at or under 8 ms', () => {
  const history: HistoryEntry[] = Array.from({ length: 10_000 }, (_, i) => ({
    url: syntheticUrl(i),
    title: `${words(2 + Math.floor(rand() * 8))} ${i}`,
    visitCount: Math.floor(rand() * 50),
    typedCount: Math.floor(rand() * 3),
    lastVisitAt: NOW - Math.floor(rand() * 200) * 86_400_000
  }))
  const tabs: TabEntry[] = Array.from({ length: 500 }, (_, i) => ({
    tabId: `00000000-0000-4000-8000-${String(i).padStart(12, '0')}`,
    spaceId: i % 3 === 0 ? 'space-a' : 'space-b',
    url: syntheticUrl(20_000 + i),
    title: words(3 + Math.floor(rand() * 5)),
    pinned: i % 10 === 0,
    split: false,
    lastActiveAt: NOW - i * 60_000
  }))
  const h = buildHistoryIndex(history, NOW)
  const t = buildTabIndex(tabs, { activeSpaceId: 'space-a', activeTabId: null, now: NOW })
  expect(h.index.size).toBe(10_000)
  expect(t.index.size).toBe(500)

  // Queries of 1 to 20 chars: slices of titles and URLs, multi-word phrases and noise that matches nothing.
  const queries: string[] = []
  for (let q = 0; q < 200; q++) {
    const len = 1 + (q % 20)
    const r = rand()
    const source = r < 0.4 ? `${pick(WORDS)} ${pick(WORDS)} ${pick(WORDS)}` : r < 0.7 ? syntheticUrl(q).replace(/^https:\/\//, '') : r < 0.9 ? words(4) : 'zqxjvkwy'.repeat(3)
    const start = r < 0.7 ? 0 : Math.floor(rand() * 4)
    queries.push(source.slice(start, start + len).padEnd(len, 'e'))
  }
  expect(queries.every((x) => x.length >= 1 && x.length <= 20)).toBe(true)

  const run = (text: string) => rank({ text, mode: 'new-tab', engineId: 'google', tabs: t, history: h, complete: true })
  for (const q of queries.slice(0, 20)) run(q)
  let best = Infinity
  let matched = 0
  for (let round = 0; round < 3 && best > 8; round++) {
    const times: number[] = []
    matched = 0
    for (const q of queries) {
      const start = performance.now()
      const out = run(q)
      times.push(performance.now() - start)
      if (out.rows.length > 1) matched++
    }
    times.sort((a, b) => a - b)
    const p95 = times[Math.ceil(times.length * 0.95) - 1] ?? Infinity
    console.info(`command match round ${round + 1}: p50 ${times[100]?.toFixed(2)} ms, p95 ${p95.toFixed(2)} ms, max ${times.at(-1)?.toFixed(2)} ms`)
    best = Math.min(best, p95)
  }
  expect(matched).toBeGreaterThan(100)
  expect(best).toBeLessThanOrEqual(8)
}, 30_000)
