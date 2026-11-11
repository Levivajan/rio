// URL display for the command bar (04-command F12): IDNA hosts shown in Unicode unless the label could spoof another
// host. Dedupe keys and the tracking strip are the scaffold's one policy in src/shared/url.ts (normalizeForDedupe,
// stripTracking); this file does not repeat them.

// RFC 3492 bootstring parameters for punycode.
const BASE = 36
const T_MIN = 1
const T_MAX = 26
const SKEW = 38
const DAMP = 700
const INITIAL_BIAS = 72
const INITIAL_N = 128

function adapt(delta: number, points: number, first: boolean): number {
  let d = first ? Math.floor(delta / DAMP) : delta >> 1
  d += Math.floor(d / points)
  let k = 0
  while (d > ((BASE - T_MIN) * T_MAX) >> 1) {
    d = Math.floor(d / (BASE - T_MIN))
    k += BASE
  }
  return k + Math.floor(((BASE - T_MIN + 1) * d) / (d + SKEW))
}

function digit(code: number): number {
  if (code >= 48 && code <= 57) return code - 22
  if (code >= 65 && code <= 90) return code - 65
  if (code >= 97 && code <= 122) return code - 97
  return BASE
}

/** Decodes one punycode label body (without "xn--"), or null when it is malformed. */
export function decodePunycode(input: string): string | null {
  if (input.length > 63) return null
  const out: number[] = []
  const lastDash = input.lastIndexOf('-')
  for (let j = 0; j < Math.max(lastDash, 0); j++) {
    const c = input.charCodeAt(j)
    if (c >= 0x80) return null
    out.push(c)
  }
  let n = INITIAL_N
  let bias = INITIAL_BIAS
  let i = 0
  for (let pos = lastDash > 0 ? lastDash + 1 : 0; pos < input.length; ) {
    const oldi = i
    let w = 1
    for (let k = BASE; ; k += BASE) {
      if (pos >= input.length) return null
      const d = digit(input.charCodeAt(pos++))
      if (d >= BASE) return null
      i += d * w
      if (i > 0x10ffff * 64) return null
      const t = k <= bias ? T_MIN : k >= bias + T_MAX ? T_MAX : k - bias
      if (d < t) break
      w *= BASE - t
    }
    bias = adapt(i - oldi, out.length + 1, oldi === 0)
    n += Math.floor(i / (out.length + 1))
    i %= out.length + 1
    if (n > 0x10ffff || (n >= 0xd800 && n <= 0xdfff)) return null
    out.splice(i++, 0, n)
  }
  return String.fromCodePoint(...out)
}

const SCRIPTS: [string, RegExp][] = [
  ['latin', /\p{Script=Latin}/u],
  ['cyrillic', /\p{Script=Cyrillic}/u],
  ['greek', /\p{Script=Greek}/u],
  ['cjk', /[\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Hangul}]/u],
  ['arabic', /\p{Script=Arabic}/u],
  ['hebrew', /\p{Script=Hebrew}/u]
]

function scriptsOf(label: string): Set<string> {
  const found = new Set<string>()
  for (const ch of label) {
    if (/[\p{N}\p{P}\p{M}\p{Zs}-]/u.test(ch)) continue
    const hit = SCRIPTS.find(([, re]) => re.test(ch))
    found.add(hit ? hit[0] : 'other')
  }
  return found
}

/** Hostname for display: xn-- labels decoded to Unicode when each label uses one script and a Cyrillic or Greek label
 *  does not sit under an ASCII TLD (where it could pass for a Latin name). Anything doubtful stays in ASCII. */
export function displayHostname(asciiHost: string): string {
  const labels = asciiHost.split('.')
  const tld = labels.filter((l) => l !== '').at(-1) ?? ''
  const tldIsAscii = !tld.toLowerCase().startsWith('xn--')
  const decoded: string[] = []
  for (const label of labels) {
    if (!label.toLowerCase().startsWith('xn--')) {
      decoded.push(label)
      continue
    }
    const text = decodePunycode(label.slice(4))
    if (text === null) return asciiHost
    const scripts = scriptsOf(text)
    if (scripts.size !== 1 || scripts.has('other')) return asciiHost
    if (tldIsAscii && (scripts.has('cyrillic') || scripts.has('greek'))) return asciiHost
    decoded.push(text)
  }
  return decoded.join('.')
}

/** URL for display in the bar: the href with its host shown through displayHostname. Unparseable input is returned
 *  as given. */
export function displayUrl(url: string): string {
  let parsed: URL
  try {
    parsed = new URL(url)
  } catch {
    return url
  }
  if (!parsed.hostname) return parsed.href
  const shown = displayHostname(parsed.hostname)
  if (shown === parsed.hostname) return parsed.href
  const userinfo = parsed.username || parsed.password ? `${parsed.username}${parsed.password ? `:${parsed.password}` : ''}@` : ''
  const prefix = `${parsed.protocol}//${userinfo}`
  if (!parsed.href.startsWith(prefix + parsed.hostname)) return parsed.href
  return prefix + shown + parsed.href.slice(prefix.length + parsed.hostname.length)
}
