// Typed URL vs search (04-command F12). Pure; the overlay runs it per keystroke and main re-runs it on execute.
import { classifyUrl, isAllowedTabUrl } from '../url'
import { MAX_INPUT } from './limits'
import { TLDS } from './tlds'

export { MAX_INPUT }

export type InputClass = { kind: 'url'; url: string } | { kind: 'search'; query: string } | { kind: 'empty' }

const EMPTY: InputClass = { kind: 'empty' }
const IPV4_OCTET = '(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)'
const IPV4 = new RegExp(`^${IPV4_OCTET}(?:\\.${IPV4_OCTET}){3}$`)
// host[:port] before the first / ? or #. '@' is excluded so typed credentials never become a URL.
const HOST_PORT = /^(\[[0-9a-f:.]+\]|[^:@[\]]+)(?::(\d{1,5}))?$/i
const SCHEME_PREFIX = /^([a-z][a-z0-9+.-]*):/i

/** Caps at MAX_INPUT without splitting a surrogate pair, repairs lone surrogates, turns tabs and line breaks into one
 *  space each run and drops other control and bidi-override characters. Does not trim. */
export function cleanInput(text: string): string {
  if (typeof text !== 'string') return ''
  let s = text
  if (s.length > MAX_INPUT) {
    const cut = /[\ud800-\udbff]/.test(s.charAt(MAX_INPUT - 1)) ? MAX_INPUT - 1 : MAX_INPUT
    s = s.slice(0, cut)
  }
  return (
    s
      .toWellFormed()
      .replace(/[\t\n\v\f\r\u0085\u2028\u2029]+/g, ' ')
      // eslint-disable-next-line no-control-regex
      .replace(/[\u0000-\u0008\u000e-\u001f\u007f-\u0084\u0086-\u009f\u202a-\u202e\u2066-\u2069]/g, '')
  )
}

function search(text: string): InputClass {
  const query = text.trim()
  return query ? { kind: 'search', query } : EMPTY
}

/** The URL when the policy calls it a web URL with a host (http or https), else null. */
function webUrl(candidate: string): string | null {
  const c = classifyUrl(candidate)
  return c.kind === 'web' && c.url !== 'about:blank' ? c.url : null
}

/** The lowercase scheme the text starts with, or null. `host:1234` is a host and port, not a scheme. */
function explicitScheme(text: string): string | null {
  const m = SCHEME_PREFIX.exec(text)
  if (!m || !m[1]) return null
  if (/^\d+(?:[/?#]|$)/.test(text.slice(m[0].length))) return null
  return m[1].toLowerCase()
}

function withScheme(text: string, scheme: string): string | null {
  switch (scheme) {
    case 'http':
    case 'https':
      return webUrl(text)
    case 'about':
      return text.toLowerCase() === 'about:blank' ? 'about:blank' : null
    case 'rio': {
      const c = classifyUrl(text)
      return c.kind === 'internal' && isAllowedTabUrl(c.url) ? c.url : null
    }
    default:
      return null
  }
}

/** Rules 5 to 7: localhost, IPs, then host.tld from the IANA list, then an intranet host followed by '/'. */
function bareHost(text: string): string | null {
  const split = /^([^/?#]*)(.*)$/s.exec(text)
  const hostPort = split?.[1] ?? ''
  const rest = split?.[2] ?? ''
  const hp = HOST_PORT.exec(hostPort)
  const host = hp?.[1]
  if (!host) return null
  const lower = host.toLowerCase()
  if (lower === 'localhost' || lower.endsWith('.localhost') || IPV4.test(host) || host.startsWith('[')) {
    return webUrl(`http://${text}`)
  }
  const labels = lower.replace(/\.$/, '').split('.')
  if (labels.length < 2 || labels.some((l) => l === '') || /^\d+$/.test(labels.at(-1) ?? '')) return null
  const url = webUrl(`https://${text}`)
  if (!url) return null
  const tld = new URL(url).hostname.replace(/\.$/, '').split('.').at(-1) ?? ''
  if (TLDS.has(tld) || rest.startsWith('/')) return url
  return null
}

/** Free text to what Enter does. A `url` result always passes classifyUrl as web (or is an allowed rio:// page);
 *  text with any other scheme (javascript:, file:, data: and so on) is a search for the whole text. */
export function classifyInput(raw: string): InputClass {
  const text = cleanInput(raw).trim()
  if (!text) return EMPTY
  if (text.startsWith('?')) return search(text.slice(1))
  if (/\s\?$/.test(text)) return search(text.slice(0, -1))
  try {
    const scheme = explicitScheme(text)
    if (scheme !== null) {
      const url = withScheme(text, scheme)
      return url ? { kind: 'url', url } : search(text)
    }
    if (/\s/.test(text)) return search(text)
    const url = bareHost(text)
    return url ? { kind: 'url', url } : search(text)
  } catch {
    return search(text)
  }
}
