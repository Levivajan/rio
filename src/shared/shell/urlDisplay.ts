// URL pill display values (docs/plan/01-shell.md F6). Pure; used by the UI and the overlay.
// displayHost shows the host without `www.`; a punycode label turns into Unicode only when it passes the
// script-mixing check below, so a spoof like `xn--pple-43d.com` (Cyrillic а + "pple") stays ASCII.

export const MAX_DISPLAY = 200
export type Security = 'secure' | 'insecure' | 'internal'

const INTERNAL_TITLES: Record<string, string> = { settings: 'Settings', history: 'History', extensions: 'Extensions', about: 'About Rio' }

// eslint-disable-next-line no-control-regex
const CONTROL = /[\u0000-\u001f\u007f-\u009f]/g

function clip(text: string): string {
  return text.replace(CONTROL, ' ').replace(/\s+/g, ' ').trim().slice(0, MAX_DISPLAY)
}

// RFC 3492 punycode decoding of one label body (after `xn--`). Null on malformed input.
const BASE = 36
const T_MIN = 1
const T_MAX = 26
const MAX_INT = 0x7fffffff

function adapt(delta: number, points: number, first: boolean): number {
  let d = first ? Math.floor(delta / 700) : delta >> 1
  d += Math.floor(d / points)
  let k = 0
  while (d > ((BASE - T_MIN) * T_MAX) >> 1) {
    d = Math.floor(d / (BASE - T_MIN))
    k += BASE
  }
  return k + Math.floor(((BASE - T_MIN + 1) * d) / (d + 38))
}

function digitOf(c: number): number {
  if (c >= 0x30 && c <= 0x39) return c - 22
  if (c >= 0x41 && c <= 0x5a) return c - 0x41
  if (c >= 0x61 && c <= 0x7a) return c - 0x61
  return BASE
}

export function decodePunycode(input: string): string | null {
  const out: number[] = []
  const basic = Math.max(0, input.lastIndexOf('-'))
  for (let j = 0; j < basic; j++) {
    const c = input.charCodeAt(j)
    if (c >= 0x80) return null
    out.push(c)
  }
  let n = 128
  let bias = 72
  let i = 0
  for (let at = basic > 0 ? basic + 1 : 0; at < input.length; ) {
    const old = i
    let w = 1
    for (let k = BASE; ; k += BASE) {
      if (at >= input.length) return null
      const digit = digitOf(input.charCodeAt(at++))
      if (digit >= BASE) return null
      i += digit * w
      if (i > MAX_INT) return null
      const t = k <= bias ? T_MIN : k >= bias + T_MAX ? T_MAX : k - bias
      if (digit < t) break
      w *= BASE - t
      if (w > MAX_INT) return null
    }
    const len = out.length + 1
    bias = adapt(i - old, len, old === 0)
    n += Math.floor(i / len)
    i %= len
    if (n > 0x10ffff || (n >= 0xd800 && n <= 0xdfff)) return null
    out.splice(i, 0, n)
    i++
  }
  return String.fromCodePoint(...out)
}

const SCRIPTS: [string, RegExp][] = [
  ['Latin', /\p{Script=Latin}/u],
  ['Cyrillic', /\p{Script=Cyrillic}/u],
  ['Greek', /\p{Script=Greek}/u],
  ['Armenian', /\p{Script=Armenian}/u],
  ['Hebrew', /\p{Script=Hebrew}/u],
  ['Arabic', /\p{Script=Arabic}/u],
  ['Devanagari', /\p{Script=Devanagari}/u],
  ['Thai', /\p{Script=Thai}/u],
  ['Georgian', /\p{Script=Georgian}/u],
  ['Hangul', /\p{Script=Hangul}/u],
  ['Hiragana', /\p{Script=Hiragana}/u],
  ['Katakana', /\p{Script=Katakana}/u],
  ['Bopomofo', /\p{Script=Bopomofo}/u],
  ['Han', /\p{Script=Han}/u]
]
const NEUTRAL = /[\p{Script=Common}\p{Script=Inherited}]/u
const LABEL_CHAR = /^[\p{L}\p{M}\p{N}-]$/u
/** East Asian scripts that legitimately mix with each other and with Latin. */
const CJK_SETS = [
  new Set(['Latin', 'Han', 'Hiragana', 'Katakana']),
  new Set(['Latin', 'Han', 'Bopomofo']),
  new Set(['Latin', 'Han', 'Hangul'])
]
/** Cyrillic and Greek letters that read as Latin ones; a label made only of them imitates an ASCII label. */
const LATIN_LOOKALIKES = new Set([...'аеорсухіјѕԁһӏԛԝьѵοαρνικυ'])

/** True when a decoded label may be shown in Unicode. */
export function isSafeLabel(label: string): boolean {
  const scripts = new Set<string>()
  const chars = [...label]
  for (const ch of chars) {
    if (!LABEL_CHAR.test(ch)) return false
    if (NEUTRAL.test(ch)) continue
    const script = SCRIPTS.find(([, re]) => re.test(ch))?.[0] ?? `other:${ch}`
    scripts.add(script)
  }
  if (scripts.size > 1 && !CJK_SETS.some((set) => [...scripts].every((s) => set.has(s)))) return false
  if ((scripts.has('Cyrillic') || scripts.has('Greek')) && chars.every((ch) => LATIN_LOOKALIKES.has(ch) || NEUTRAL.test(ch))) return false
  return true
}

function displayLabel(label: string): string {
  if (!label.startsWith('xn--')) return label
  const decoded = decodePunycode(label.slice(4))
  return decoded && isSafeLabel(decoded) ? decoded : label
}

/** What the URL pill shows: the host (no `www.`, IDN only when safe), an internal page's title, '' for a blank
 *  tab, or the raw string truncated to 200 characters when it does not parse. */
export function displayHost(input: string, title?: string): string {
  if (typeof input !== 'string') return ''
  let parsed: URL
  try {
    parsed = new URL(input.trim())
  } catch {
    return clip(input)
  }
  switch (parsed.protocol) {
    case 'about:':
      return parsed.href === 'about:blank' ? '' : clip(parsed.href)
    case 'rio:':
      return clip(title ?? '') || INTERNAL_TITLES[parsed.hostname.toLowerCase()] || 'Rio'
    case 'http:':
    case 'https:': {
      const host = parsed.hostname.replace(/\.$/, '').split('.').map(displayLabel).join('.')
      return clip(`${host.replace(/^www\./, '')}${parsed.port ? `:${parsed.port}` : ''}`)
    }
    default:
      return clip(parsed.hostname || parsed.href)
  }
}

/** The URL pill's text: the host of a web page (displayHost), but Rio's own pages by their full rio: URL
 *  (`…://settings/`). */
export function pillText(input: string, title?: string): string {
  if (typeof input !== 'string') return ''
  try {
    const parsed = new URL(input.trim())
    if (parsed.protocol === 'rio:') return clip(parsed.href)
  } catch {
    // displayHost clips what does not parse.
  }
  return displayHost(input, title)
}

/** The toolbar pill's text with Advanced's "Show full URL when Toolbar is enabled" on: a web page's whole URL, scheme
 *  and path included, its host decoded only where displayHost would (so a lookalike stays punycode); anything else as
 *  pillText shows it. */
export function fullUrlText(input: string, title?: string): string {
  if (typeof input !== 'string') return ''
  let parsed: URL
  try {
    parsed = new URL(input.trim())
  } catch {
    return pillText(input, title)
  }
  if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') return pillText(input, title)
  const host = parsed.hostname.split('.').map(displayLabel).join('.')
  return clip(`${parsed.protocol}//${host}${parsed.port ? `:${parsed.port}` : ''}${parsed.pathname}${parsed.search}${parsed.hash}`)
}

/** The pill's security state: https is secure, http insecure ("Not secure" glyph), Rio's own pages internal, and
 *  so are Chromium's pages that keep their chrome: URL (the password manager, Chromium's settings, downloads).
 *  Anything else, junk included, counts as insecure. */
export function security(input: string): Security {
  let protocol: string
  try {
    protocol = new URL(String(input).trim()).protocol
  } catch {
    return 'insecure'
  }
  if (protocol === 'https:') return 'secure'
  if (protocol === 'rio:' || protocol === 'chrome:' || protocol === 'about:' || protocol === 'clu-file:') return 'internal'
  return 'insecure'
}
