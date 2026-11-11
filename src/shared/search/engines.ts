// Search engine registry (04-command F7, PLAN 7.3). Five built-in engines, all https. With CLU_E2E=1 main passes
// CLU_SEARCH_ORIGIN as the origin override, which replaces every search and suggest origin and keeps the paths.

export const ENGINE_IDS = ['google', 'duckduckgo', 'bing', 'brave', 'ecosia'] as const
export type EngineId = (typeof ENGINE_IDS)[number]
export const DEFAULT_ENGINE_ID: EngineId = 'google'
/** Longest query sent to an engine. Longer queries are cut (never inside a surrogate pair). */
export const MAX_SEARCH_QUERY = 2048

export interface SearchEngine {
  id: EngineId
  name: string
  /** Results page for the query. */
  searchUrl(query: string): string
  /** OpenSearch suggestions endpoint, answering `[query, string[]]`. */
  suggestUrl(query: string): string
  /** The query of a results page of this engine (a past search in history), or null. */
  matchesSearchUrl(url: string): string | null
}

interface EngineSpec {
  id: EngineId
  name: string
  origin: string
  searchPath: string
  suggestOrigin: string
  suggestPath: string
  suggestParam: string
  suggestExtra: string
}

const SPECS: readonly EngineSpec[] = [
  { id: 'google', name: 'Google', origin: 'https://www.google.com', searchPath: '/search', suggestOrigin: 'https://www.google.com', suggestPath: '/complete/search', suggestParam: 'q', suggestExtra: 'client=firefox&' },
  { id: 'duckduckgo', name: 'DuckDuckGo', origin: 'https://duckduckgo.com', searchPath: '/', suggestOrigin: 'https://duckduckgo.com', suggestPath: '/ac/', suggestParam: 'q', suggestExtra: 'type=list&' },
  { id: 'bing', name: 'Bing', origin: 'https://www.bing.com', searchPath: '/search', suggestOrigin: 'https://www.bing.com', suggestPath: '/osjson.aspx', suggestParam: 'query', suggestExtra: '' },
  { id: 'brave', name: 'Brave Search', origin: 'https://search.brave.com', searchPath: '/search', suggestOrigin: 'https://search.brave.com', suggestPath: '/api/suggest', suggestParam: 'q', suggestExtra: '' },
  { id: 'ecosia', name: 'Ecosia', origin: 'https://www.ecosia.org', searchPath: '/search', suggestOrigin: 'https://ac.ecosia.org', suggestPath: '/autocomplete', suggestParam: 'q', suggestExtra: 'type=list&' }
]

/** Query cut to MAX_SEARCH_QUERY with lone surrogates repaired, then form-encoded as engines do, spaces as `+`
 * (never throws). */
export function encodeQuery(query: string): string {
  let q = typeof query === 'string' ? query : ''
  if (q.length > MAX_SEARCH_QUERY) {
    q = q.slice(0, /[\ud800-\udbff]/.test(q.charAt(MAX_SEARCH_QUERY - 1)) ? MAX_SEARCH_QUERY - 1 : MAX_SEARCH_QUERY)
  }
  return encodeURIComponent(q.toWellFormed()).replace(/%20/g, '+')
}

const bareHost = (host: string) => host.replace(/^www\./, '')

function build(spec: EngineSpec, override: string | null): SearchEngine {
  const origin = override ?? spec.origin
  const suggestOrigin = override ?? spec.suggestOrigin
  const home = new URL(origin)
  return {
    id: spec.id,
    name: spec.name,
    searchUrl: (query) => `${origin}${spec.searchPath}?q=${encodeQuery(query)}`,
    suggestUrl: (query) => `${suggestOrigin}${spec.suggestPath}?${spec.suggestExtra}${spec.suggestParam}=${encodeQuery(query)}`,
    matchesSearchUrl(url) {
      let parsed: URL
      try {
        parsed = new URL(url)
      } catch {
        return null
      }
      if (parsed.protocol !== home.protocol || parsed.port !== home.port) return null
      if (bareHost(parsed.hostname) !== bareHost(home.hostname) || parsed.pathname !== spec.searchPath) return null
      const query = parsed.searchParams.get('q')?.trim()
      return query ? query : null
    }
  }
}

/** The built-in engines with their real https endpoints. */
export const ENGINES: readonly SearchEngine[] = SPECS.map((s) => build(s, null))

export function isEngineId(value: unknown): value is EngineId {
  return typeof value === 'string' && (ENGINE_IDS as readonly string[]).includes(value)
}

/** The engine for an id; unknown or corrupt ids fall back to Google. `originOverride` is the e2e search origin. */
export function engineById(id: unknown, originOverride: string | null = null): SearchEngine {
  const spec = SPECS.find((s) => s.id === id) ?? SPECS.find((s) => s.id === DEFAULT_ENGINE_ID)
  if (!spec) throw new Error('no default engine')
  const override = originOf(originOverride)
  if (override === null) return ENGINES[SPECS.indexOf(spec)] ?? build(spec, null)
  return build(spec, override)
}

/** The http(s) origin of an override string, or null when it is missing or not one. */
function originOf(value: string | null): string | null {
  if (!value) return null
  try {
    const u = new URL(value)
    return u.protocol === 'http:' || u.protocol === 'https:' ? u.origin : null
  } catch {
    return null
  }
}
