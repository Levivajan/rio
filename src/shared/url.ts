// The one URL policy. Pure; shared by main and renderers.
import { MAX_URL } from './schema'

export const INTERNAL_PAGES = ['settings', 'history', 'extensions', 'about'] as const
export type InternalPage = (typeof INTERNAL_PAGES)[number]
export const SURFACE_HOSTS = ['ui', 'overlay', 'mini'] as const
export type SurfaceHost = (typeof SURFACE_HOSTS)[number]

export type BlockedReason = 'scheme' | 'malformed' | 'too-long' | 'credentials'
export type UrlClass =
  | { kind: 'web'; url: string }
  | { kind: 'internal'; page: InternalPage | SurfaceHost; url: string }
  | { kind: 'local-file'; url: string }
  | { kind: 'external-app'; scheme: string; url: string }
  | { kind: 'blocked'; reason: BlockedReason }

const BLOCKED_SCHEMES = new Set([
  'javascript', 'data', 'file', 'blob', 'chrome', 'chrome-extension', 'chrome-untrusted', 'devtools',
  'view-source', 'filesystem', 'ws', 'wss', 'vbscript'
])
const SCHEME = /^[a-z][a-z0-9+.-]*$/

function isInternalPage(host: string): host is InternalPage {
  return (INTERNAL_PAGES as readonly string[]).includes(host)
}
function isSurfaceHost(host: string): host is SurfaceHost {
  return (SURFACE_HOSTS as readonly string[]).includes(host)
}

export function classifyUrl(input: string): UrlClass {
  if (typeof input !== 'string') return { kind: 'blocked', reason: 'malformed' }
  if (input.length > MAX_URL) return { kind: 'blocked', reason: 'too-long' }
  const text = input.trim()
  // NUL and other C0 controls never belong in an accepted URL.
  // eslint-disable-next-line no-control-regex
  if (text === '' || /[\u0000-\u0008\u000e-\u001f]/.test(text)) return { kind: 'blocked', reason: 'malformed' }
  let parsed: URL
  try {
    parsed = new URL(text)
  } catch {
    return { kind: 'blocked', reason: 'malformed' }
  }
  const scheme = parsed.protocol.slice(0, -1).toLowerCase()
  const href = parsed.href
  if (href.length > MAX_URL) return { kind: 'blocked', reason: 'too-long' }
  switch (scheme) {
    case 'http':
    case 'https':
      if (!parsed.hostname) return { kind: 'blocked', reason: 'malformed' }
      return { kind: 'web', url: href }
    case 'about':
      return href === 'about:blank' ? { kind: 'web', url: href } : { kind: 'blocked', reason: 'scheme' }
    case 'rio': {
      const host = parsed.hostname.toLowerCase()
      if (isInternalPage(host) || isSurfaceHost(host)) return { kind: 'internal', page: host, url: href }
      return { kind: 'blocked', reason: 'scheme' }
    }
    case 'clu-file':
      return parsed.hostname ? { kind: 'local-file', url: href } : { kind: 'blocked', reason: 'malformed' }
    default:
      if (BLOCKED_SCHEMES.has(scheme)) return { kind: 'blocked', reason: 'scheme' }
      if (!SCHEME.test(scheme)) return { kind: 'blocked', reason: 'malformed' }
      return { kind: 'external-app', scheme, url: href }
  }
}

/** Tabs open http, https, about:blank and the four internal pages only. */
export function isAllowedTabUrl(input: string): boolean {
  const c = classifyUrl(input)
  return c.kind === 'web' || (c.kind === 'internal' && isInternalPage(c.page))
}

/**
 * Normalized URL for a tab, or null when the URL is not allowed in a tab. clu-file:// counts as Clu's own (platform
 * F10): main opens it with a browser-initiated load, and the navigation guard and popups still keep pages off it.
 */
export function tabUrlOrNull(input: string): string | null {
  const c = classifyUrl(input)
  if (c.kind === 'web' || c.kind === 'local-file') return c.url
  if (c.kind === 'internal' && isInternalPage(c.page)) return c.url
  return null
}

const TRACKING_KEYS = new Set([
  'fbclid', 'gclid', 'gclsrc', 'dclid', 'msclkid', 'mc_cid', 'mc_eid', 'igshid', 'yclid', 'twclid', '_hsenc', '_hsmi', 'ref_src'
])

function isTrackingKey(rawKey: string): boolean {
  let key = rawKey
  try {
    key = decodeURIComponent(rawKey.replace(/\+/g, ' '))
  } catch {
    // keep the raw key
  }
  key = key.toLowerCase()
  return key.startsWith('utm_') || TRACKING_KEYS.has(key)
}

/** Removes tracking parameters, keeping the order and raw encoding of every other parameter and the hash. */
export function stripTracking(input: string): string {
  let parsed: URL
  try {
    parsed = new URL(input)
  } catch {
    return input
  }
  if (!parsed.search) return parsed.href
  const kept = parsed.search
    .slice(1)
    .split('&')
    .filter((pair) => pair !== '' && !isTrackingKey(pair.split('=')[0] ?? ''))
  parsed.search = kept.length ? `?${kept.join('&')}` : ''
  return parsed.href
}

/** Key for "same page" comparisons: no hash, no tracking parameters, lowercase host, no trailing slash. */
export function normalizeForDedupe(input: string): string {
  const stripped = stripTracking(input)
  let parsed: URL
  try {
    parsed = new URL(stripped)
  } catch {
    return stripped
  }
  parsed.hash = ''
  let out = parsed.href
  if (out.endsWith('/') && parsed.search === '') out = out.slice(0, -1)
  return out
}

/** Host for display, or '' for URLs without one. */
export function displayHost(input: string): string {
  try {
    const parsed = new URL(input)
    if (parsed.protocol === 'rio:') return `rio://${parsed.hostname}`
    return parsed.hostname.replace(/^www\./, '')
  } catch {
    return ''
  }
}
