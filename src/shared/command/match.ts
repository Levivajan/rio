// Fuzzy matcher for the command bar (04-command F13). Pure; the overlay runs it synchronously per keystroke.
// A CandidateIndex is a struct of arrays built once per index sync: folded title and URL, a character bitmask and the
// normalized frecency. A query is whitespace tokens that must all match (AND). Each token scores against the title and
// the URL (host first): a string prefix 1.0, a word-boundary prefix 0.85, a substring 0.6, an in-order subsequence
// 0.3 to 0.5, else no match. Hits carry ranges into the display title and URL for bold highlighting.
import { MAX_URL } from '../schema'
import { normalizeForDedupe } from '../url'
import { displayUrl } from './normalize'

export const MAX_TITLE = 300
/** Longest URL text kept for matching and display (the part after the scheme and www.). */
const MAX_URL_TEXT = 1024
/** Longest query that is matched; nothing longer can fit a clamped title. */
const MAX_QUERY = MAX_TITLE
const MAX_TOKENS = 16
export const DEFAULT_K = 16

export const SCORE_PREFIX = 1
export const SCORE_WORD = 0.85
export const SCORE_SUBSTRING = 0.6

export type Range = readonly [start: number, end: number]

/** One row offered to the index. Anything may be junk: url and title come from the DB or the state channel. */
export interface IndexInput {
  url: unknown
  title: unknown
  frecency: number
  lastVisitAt: number
  /** Added to the score as is (+10 active space, +5 pinned for tabs). */
  boost?: number
}

export interface CandidateIndex {
  size: number
  /** Position of each candidate in the input rows. */
  source: Int32Array
  /** Canonical href (http or https). */
  url: string[]
  /** normalizeForDedupe(url), for dedupe and switch-to-tab. */
  norm: string[]
  /** Display title: control characters stripped, clamped to MAX_TITLE. May be ''. */
  title: string[]
  /** Display URL without the scheme and a leading www. (IDNA hosts in Unicode when safe). */
  urlText: string[]
  foldTitle: string[]
  foldUrl: string[]
  /** Folded index to original index, or null when folding kept every position. */
  titleMap: (Int32Array | null)[]
  urlMap: (Int32Array | null)[]
  /** Length of host[:port] at the start of foldUrl. */
  hostLen: Int32Array
  mask: Int32Array
  frecencyNorm: Float32Array
  lastVisitAt: Float64Array
  boost: Float32Array
  /** Input rows dropped as junk: no URL, an unparseable one or a scheme other than http and https. */
  skipped: number
}

export interface Hit {
  /** Candidate position in the index. */
  i: number
  /** 100 * match + 20 * frecencyNorm + boost. */
  score: number
  /** Mean of the token scores, 0 to 1. */
  match: number
  /** Every token hit the title or the host with a prefix or word-boundary prefix. */
  strong: boolean
  titleRanges: Range[]
  urlRanges: Range[]
}

/** Survivors of the last query on one index, so a query that extends it scans only those. */
export interface MatchState {
  index: CandidateIndex
  query: string
  survivors: Int32Array
}

// Control and bidi-override characters never reach the display title.
// eslint-disable-next-line no-control-regex
const CONTROL = /[\u0000-\u001f\u007f-\u009f‪-‮⁦-⁩]/g
const MARKS = /\p{M}/gu
// eslint-disable-next-line no-control-regex
const ASCII = /^[\u0000-\u007f]*$/

/** Cuts to max UTF-16 units without splitting a surrogate pair. */
function clamp(s: string, max: number): string {
  if (s.length <= max) return s
  const cut = /[\ud800-\udbff]/.test(s.charAt(max - 1)) ? max - 1 : max
  return s.slice(0, cut)
}

export interface Folded { text: string; map: Int32Array | null }

/** Lowercase, NFKD, combining marks stripped. `map[k]` is the original index of folded unit k (null when equal). */
export function fold(s: string): Folded {
  if (ASCII.test(s)) return { text: s.toLowerCase(), map: null }
  let text = ''
  const map: number[] = []
  let same = true
  for (let i = 0; i < s.length; ) {
    const cp = s.codePointAt(i) ?? 0
    const ch = String.fromCodePoint(cp)
    const f = ch.normalize('NFKD').replace(MARKS, '').toLowerCase()
    if (f.length !== ch.length) same = false
    for (let j = 0; j < f.length; j++) map.push(i + Math.min(j, ch.length - 1))
    text += f
    i += ch.length
  }
  return { text, map: same ? null : Int32Array.from(map) }
}

/** a to z on bits 0 to 25, digits in 6 buckets on bits 26 to 31. Other characters set no bit. */
export function charMask(s: string): number {
  let m = 0
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i)
    if (c >= 97 && c <= 122) m |= 1 << (c - 97)
    else if (c >= 48 && c <= 57) m |= 1 << (26 + ((c - 48) % 6))
  }
  return m
}

const finiteOr0 = (n: unknown) => (typeof n === 'number' && Number.isFinite(n) ? n : 0)

/** The canonical http(s) href, or null for junk (missing, too long, unparseable, other scheme). */
function webHref(url: unknown): string | null {
  if (typeof url !== 'string' || url === '' || url.length > MAX_URL) return null
  try {
    const u = new URL(url)
    if ((u.protocol !== 'http:' && u.protocol !== 'https:') || !u.hostname || u.href.length > MAX_URL) return null
    return u.href
  } catch {
    return null
  }
}

/** The URL as the bar shows it: no scheme, no leading www., no slash after a bare host ("github.com"), clamped. */
export function urlTextOf(href: string): string {
  return clamp(
    displayUrl(href)
      .replace(/^https?:\/\//i, '')
      .replace(/^www\./i, '')
      .replace(/^([^/?#]+)\/$/, '$1'),
    MAX_URL_TEXT
  )
}

export function cleanTitle(title: unknown): string {
  if (typeof title !== 'string') return ''
  return clamp(clamp(title, MAX_TITLE * 2).toWellFormed().replace(CONTROL, '').trim(), MAX_TITLE)
}

/** Builds the index once per sync. Junk rows are skipped and counted, never thrown on. */
export function buildIndex(rows: readonly IndexInput[]): CandidateIndex {
  const source: number[] = []
  const url: string[] = []
  const norm: string[] = []
  const title: string[] = []
  const urlText: string[] = []
  const foldTitle: string[] = []
  const foldUrl: string[] = []
  const titleMap: (Int32Array | null)[] = []
  const urlMap: (Int32Array | null)[] = []
  const hostLen: number[] = []
  const mask: number[] = []
  const frecency: number[] = []
  const lastVisitAt: number[] = []
  const boost: number[] = []
  let skipped = 0
  let maxFrecency = 0
  for (let r = 0; r < rows.length; r++) {
    const row = rows[r]
    const href = row ? webHref(row.url) : null
    if (!row || href === null) {
      skipped++
      continue
    }
    const t = cleanTitle(row.title)
    const u = urlTextOf(href)
    const ft = fold(t)
    const fu = fold(u)
    const host = fu.text.search(/[/?#]/)
    const f = Math.max(0, finiteOr0(row.frecency))
    source.push(r)
    url.push(href)
    norm.push(normalizeForDedupe(href))
    title.push(t)
    urlText.push(u)
    foldTitle.push(ft.text)
    foldUrl.push(fu.text)
    titleMap.push(ft.map)
    urlMap.push(fu.map)
    hostLen.push(host < 0 ? fu.text.length : host)
    mask.push(charMask(ft.text) | charMask(fu.text))
    frecency.push(f)
    lastVisitAt.push(finiteOr0(row.lastVisitAt))
    boost.push(finiteOr0(row.boost))
    if (f > maxFrecency) maxFrecency = f
  }
  return {
    size: url.length,
    source: Int32Array.from(source),
    url, norm, title, urlText, foldTitle, foldUrl, titleMap, urlMap,
    hostLen: Int32Array.from(hostLen),
    mask: Int32Array.from(mask),
    frecencyNorm: Float32Array.from(frecency, (f) => (maxFrecency > 0 ? f / maxFrecency : 0)),
    lastVisitAt: Float64Array.from(lastVisitAt),
    boost: Float32Array.from(boost),
    skipped
  }
}

/** Folded query tokens. A leading scheme and www. are dropped, since the URL field has neither. */
export function queryTokens(query: string): string[] {
  const q = fold(clamp(typeof query === 'string' ? query : '', MAX_QUERY).toWellFormed()).text
    .trim()
    .replace(/^https?:\/\/(www\.)?/, '')
    .replace(/^www\./, '')
  if (!q) return []
  return q.split(/\s+/).filter(Boolean).slice(0, MAX_TOKENS)
}

const isWordChar = (c: number) => (c >= 97 && c <= 122) || (c >= 48 && c <= 57) || c > 127

// Start of the last scoreField match, for the strong (host) check.
let lastStart = 0

/** Score of token t in folded field f (0 when absent). With `out`, pushes the matched folded ranges. */
function scoreField(f: string, t: string, out: number[] | null): number {
  if (t.length > f.length) return 0
  let idx = f.indexOf(t)
  if (idx === 0) {
    lastStart = 0
    out?.push(0, t.length)
    return SCORE_PREFIX
  }
  if (idx > 0) {
    const first = idx
    while (idx !== -1) {
      if (!isWordChar(f.charCodeAt(idx - 1))) {
        lastStart = idx
        out?.push(idx, idx + t.length)
        return SCORE_WORD
      }
      idx = f.indexOf(t, idx + 1)
    }
    lastStart = first
    out?.push(first, first + t.length)
    return SCORE_SUBSTRING
  }
  return subsequence(f, t, out)
}

/** In-order subsequence with gaps: forward to the first full match, back to its tightest start. 0.3 to 0.5, lower
 *  for a wider span and more gaps. */
function subsequence(f: string, t: string, out: number[] | null): number {
  let ti = 0
  let end = -1
  for (let i = 0; i < f.length; i++) {
    if (f.charCodeAt(i) === t.charCodeAt(ti) && ++ti === t.length) {
      end = i
      break
    }
  }
  if (end < 0) return 0
  let start = end
  ti = t.length - 1
  for (let i = end; i >= 0; i--) {
    if (f.charCodeAt(i) === t.charCodeAt(ti) && --ti < 0) {
      start = i
      break
    }
  }
  let gaps = 0
  let runStart = -1
  let prev = -2
  ti = 0
  for (let i = start; i <= end && ti < t.length; i++) {
    if (f.charCodeAt(i) !== t.charCodeAt(ti)) continue
    ti++
    if (i !== prev + 1) {
      if (runStart >= 0) {
        gaps++
        out?.push(runStart, prev + 1)
      }
      runStart = i
    }
    prev = i
  }
  out?.push(runStart, prev + 1)
  lastStart = start
  return 0.3 + (0.2 * (t.length / (end - start + 1))) / Math.max(1, gaps)
}

/** Match and strong flag of candidate i, or -1 when a token misses. */
function scoreCandidate(index: CandidateIndex, i: number, tokens: readonly string[]): number {
  const ft = index.foldTitle[i] ?? ''
  const fu = index.foldUrl[i] ?? ''
  const host = index.hostLen[i] ?? 0
  let sum = 0
  let strong = true
  for (const t of tokens) {
    const st = scoreField(ft, t, null)
    const su = scoreField(fu, t, null)
    const best = st > su ? st : su
    if (best === 0) return -1
    sum += best
    if (!(st >= SCORE_WORD || (su >= SCORE_WORD && lastStart < host))) strong = false
  }
  scratchStrong = strong
  return sum / tokens.length
}
let scratchStrong = false

/** Score of one display string (an action title or alias) against a query, with the same token rules as the index:
 *  the mean of each token's best score, 0 when a token misses or the query is empty. */
export function matchText(text: string, query: string): number {
  const tokens = queryTokens(query)
  if (tokens.length === 0) return 0
  const f = fold(cleanTitle(text)).text
  let sum = 0
  for (const t of tokens) {
    const s = scoreField(f, t, null)
    if (s === 0) return 0
    sum += s
  }
  return sum / tokens.length
}

/** Folded ranges of one field mapped to the original string, sorted and merged. */
function toRanges(pairs: number[], map: Int32Array | null, original: string): Range[] {
  const spans: [number, number][] = []
  for (let k = 0; k < pairs.length; k += 2) {
    const a = pairs[k] ?? 0
    const b = pairs[k + 1] ?? 0
    if (b <= a) continue
    if (!map) {
      spans.push([a, b])
      continue
    }
    const start = map[a] ?? original.length
    const last = map[b - 1] ?? original.length - 1
    spans.push([start, last + (/[\ud800-\udbff]/.test(original.charAt(last)) ? 2 : 1)])
  }
  spans.sort((x, y) => x[0] - y[0])
  const merged: [number, number][] = []
  for (const s of spans) {
    const top = merged.at(-1)
    if (top && s[0] <= top[1]) top[1] = Math.max(top[1], s[1])
    else merged.push(s)
  }
  return merged
}

function rangesOf(index: CandidateIndex, i: number, tokens: readonly string[]): { titleRanges: Range[]; urlRanges: Range[] } {
  const ft = index.foldTitle[i] ?? ''
  const fu = index.foldUrl[i] ?? ''
  const tp: number[] = []
  const up: number[] = []
  for (const t of tokens) {
    const st = scoreField(ft, t, null)
    const su = scoreField(fu, t, null)
    if (st >= su && st > 0) scoreField(ft, t, tp)
    else if (su > 0) scoreField(fu, t, up)
  }
  return {
    titleRanges: toRanges(tp, index.titleMap[i] ?? null, index.title[i] ?? ''),
    urlRanges: toRanges(up, index.urlMap[i] ?? null, index.urlText[i] ?? '')
  }
}

/** Fixed-size min-heap of the K best (score, then lastVisitAt) candidates. */
class TopK {
  private readonly idx: Int32Array
  private readonly score: Float64Array
  private readonly match: Float64Array
  private readonly strong: Uint8Array
  size = 0
  constructor(private readonly k: number, private readonly last: Float64Array) {
    this.idx = new Int32Array(k)
    this.score = new Float64Array(k)
    this.match = new Float64Array(k)
    this.strong = new Uint8Array(k)
  }
  private less(a: number, b: number): boolean {
    const sa = this.score[a] ?? 0
    const sb = this.score[b] ?? 0
    if (sa !== sb) return sa < sb
    return (this.last[this.idx[a] ?? 0] ?? 0) < (this.last[this.idx[b] ?? 0] ?? 0)
  }
  private swap(a: number, b: number): void {
    const i = this.idx[a] ?? 0, s = this.score[a] ?? 0, m = this.match[a] ?? 0, g = this.strong[a] ?? 0
    this.idx[a] = this.idx[b] ?? 0; this.score[a] = this.score[b] ?? 0; this.match[a] = this.match[b] ?? 0; this.strong[a] = this.strong[b] ?? 0
    this.idx[b] = i; this.score[b] = s; this.match[b] = m; this.strong[b] = g
  }
  push(i: number, score: number, match: number, strong: boolean): void {
    if (this.k === 0) return
    let at: number
    if (this.size < this.k) {
      at = this.size++
    } else {
      const rootScore = this.score[0] ?? 0
      if (score < rootScore || (score === rootScore && (this.last[i] ?? 0) <= (this.last[this.idx[0] ?? 0] ?? 0))) return
      at = 0
    }
    this.idx[at] = i
    this.score[at] = score
    this.match[at] = match
    this.strong[at] = strong ? 1 : 0
    if (at > 0 || this.size === 1) {
      while (at > 0) {
        const parent = (at - 1) >> 1
        if (!this.less(at, parent)) break
        this.swap(at, parent)
        at = parent
      }
      return
    }
    for (;;) {
      const l = 2 * at + 1
      const r = l + 1
      let min = at
      if (l < this.size && this.less(l, min)) min = l
      if (r < this.size && this.less(r, min)) min = r
      if (min === at) return
      this.swap(at, min)
      at = min
    }
  }
  /** Entries best first. */
  drain(): { i: number; score: number; match: number; strong: boolean }[] {
    const out: { i: number; score: number; match: number; strong: boolean }[] = []
    for (let k = 0; k < this.size; k++) out.push({ i: this.idx[k] ?? 0, score: this.score[k] ?? 0, match: this.match[k] ?? 0, strong: this.strong[k] === 1 })
    return out.sort((a, b) => b.score - a.score || (this.last[b.i] ?? 0) - (this.last[a.i] ?? 0))
  }
}

/** The K best hits of the query, best first, with highlight ranges. `prev` narrows the scan when the query extends
 *  the previous one on the same index. An empty query matches nothing. */
export function matchIndex(index: CandidateIndex, query: string, k = DEFAULT_K, prev: MatchState | null = null): { hits: Hit[]; state: MatchState } {
  const tokens = queryTokens(query)
  const key = tokens.join(' ')
  if (tokens.length === 0) return { hits: [], state: { index, query: '', survivors: new Int32Array(0) } }
  const narrow = prev !== null && prev.index === index && prev.query !== '' && key.startsWith(prev.query)
  let qmask = 0
  for (const t of tokens) qmask |= charMask(t)
  const heap = new TopK(Math.max(0, Math.floor(k)), index.lastVisitAt)
  const survivors = new Int32Array(narrow ? prev.survivors.length : index.size)
  let n = 0
  const total = narrow ? prev.survivors.length : index.size
  for (let s = 0; s < total; s++) {
    const i = narrow ? (prev.survivors[s] ?? 0) : s
    if (((index.mask[i] ?? 0) & qmask) !== qmask) continue
    const match = scoreCandidate(index, i, tokens)
    if (match < 0) continue
    survivors[n++] = i
    heap.push(i, 100 * match + 20 * (index.frecencyNorm[i] ?? 0) + (index.boost[i] ?? 0), match, scratchStrong)
  }
  const hits = heap.drain().map((h) => ({ ...h, ...rangesOf(index, h.i, tokens) }))
  return { hits, state: { index, query: key, survivors: survivors.slice(0, n) } }
}
