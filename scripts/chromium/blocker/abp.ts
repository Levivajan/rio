// Filter lists (Adblock Plus syntax: EasyList, EasyPrivacy, the test fixture) to what Clu's blocker loads
// (docs/chromium/m7.md). Network filters become declarativeNetRequest rules, which the browser
// installs as dynamic rules of Clu's component extension; element hiding becomes plain selectors the browser turns into
// user-origin CSS per page. Anything this subset cannot express exactly is skipped and counted, never guessed at.
import { createHash } from 'node:crypto'

/** Dynamic rules Chromium accepts for one extension (MAX_NUMBER_OF_DYNAMIC_RULES), less room for the site exceptions. */
export const MAX_RULES = 29_000
/** Host-only filters share one rule per this many domains. */
export const DOMAINS_PER_RULE = 1_000
/** Generic hiding selectors kept; each becomes one CSS rule on every page. */
export const MAX_GENERIC = 8_000
export const MAX_SELECTOR = 1_024
const MAX_URL_FILTER = 1_024

const BLOCK_PRIORITY = 1
const ALLOW_PRIORITY = 2

export interface DnrRule {
  id: number
  priority: number
  action: { type: 'block' | 'allow' | 'allowAllRequests' }
  condition: {
    urlFilter?: string
    isUrlFilterCaseSensitive?: boolean
    requestDomains?: string[]
    initiatorDomains?: string[]
    excludedInitiatorDomains?: string[]
    domainType?: 'firstParty' | 'thirdParty'
    resourceTypes?: string[]
    excludedResourceTypes?: string[]
  }
}

export interface Cosmetic {
  /** Hidden on every site, except where `exceptions` or `genericHide` say otherwise. */
  generic: string[]
  /** Host to selectors hidden on it and its subdomains. */
  domains: Record<string, string[]>
  /** Host to selectors not hidden on it and its subdomains (`host#@#sel`, `~host##sel`). */
  exceptions: Record<string, string[]>
  /** Hosts where no generic selector applies (`@@||host^$generichide` or `$elemhide`). */
  genericHide: string[]
}

export interface Compiled {
  /** sha256 of the rules and the cosmetic data, 16 hex digits: the browser reinstalls the rules when it changes. */
  version: string
  rules: DnrRule[]
  cosmetic: Cosmetic
  /** Lines skipped, by reason. */
  skipped: Record<string, number>
}

// ABP type options to DNR resource types. main_frame is never blocked: a page always loads (R7).
const TYPES: Record<string, string> = {
  script: 'script',
  image: 'image',
  stylesheet: 'stylesheet',
  css: 'stylesheet',
  object: 'object',
  xmlhttprequest: 'xmlhttprequest',
  xhr: 'xmlhttprequest',
  subdocument: 'sub_frame',
  frame: 'sub_frame',
  ping: 'ping',
  beacon: 'ping',
  media: 'media',
  font: 'font',
  websocket: 'websocket',
  other: 'other'
}

const HOST = /^(?=.{1,253}$)([a-z0-9-]{1,63}\.)*[a-z0-9-]{1,63}$/
// Extended CSS and scriptlets the browser cannot run as plain CSS.
const PROCEDURAL = /:-abp-|:has-text\(|:contains\(|:matches-css|:xpath\(|:upward\(|:remove\(|:style\(|:watch-attr\(|:min-text-length\(|:matches-path\(|:others\(|:if\(|:if-not\(/

export function isHost(s: string): boolean {
  return HOST.test(s) && !s.startsWith('-') && !s.includes('..')
}

/** A selector that cannot leave its CSS rule or run anything (the zap selector rule, plus no extended syntax). */
export function isSafeSelector(s: string): boolean {
  return s.length > 0 && s.length <= MAX_SELECTOR && !/[{};@<\\]|\/\*/.test(s) && !PROCEDURAL.test(s) && !/^[+:]/.test(s)
}

const ascii = (s: string) => /^[\x20-\x7e]+$/.test(s)

function splitDomains(list: string): { include: string[]; exclude: string[] } | null {
  const include: string[] = []
  const exclude: string[] = []
  for (const raw of list.split(/[|,]/)) {
    const d = raw.trim().toLowerCase()
    if (!d) continue
    const neg = d.startsWith('~')
    const host = neg ? d.slice(1) : d
    // Entity filters (example.*) and regex domains have no DNR form.
    if (!isHost(host)) return null
    ;(neg ? exclude : include).push(host)
  }
  return { include, exclude }
}

type Network = { allow: boolean; doc: boolean; genericHide: boolean; rule: Omit<DnrRule, 'id'> | null; host: string | null; thirdParty: boolean | null }

/** One network filter line, or a skip reason. */
export function parseNetwork(line: string): Network | string {
  let text = line
  const allow = text.startsWith('@@')
  if (allow) text = text.slice(2)
  let options: string[] = []
  const dollar = text.lastIndexOf('$')
  // A `$` inside a regex filter or right after a path is part of the pattern only when nothing option-like follows.
  if (dollar >= 0 && /^[\w~,=|.*-]+$/.test(text.slice(dollar + 1))) {
    options = text.slice(dollar + 1).split(',').map((o) => o.trim().toLowerCase()).filter(Boolean)
    text = text.slice(0, dollar)
  }
  if (text.length > 1 && text.startsWith('/') && text.endsWith('/')) return 'regex'
  if (!text || text === '*' || text === '|' || text === '||') return 'too-broad'
  if (/[\s$]/.test(text)) return 'junk'
  if (!ascii(text)) return 'non-ascii'
  if (text.length > MAX_URL_FILTER) return 'too-long'
  if (text.startsWith('||*')) return 'too-broad'

  const types: string[] = []
  const notTypes: string[] = []
  let thirdParty: boolean | null = null
  let caseSensitive = false
  let doc = false
  let genericHide = false
  let domains: { include: string[]; exclude: string[] } | null = { include: [], exclude: [] }
  for (const opt of options) {
    const neg = opt.startsWith('~')
    const name = neg ? opt.slice(1) : opt
    if (name === 'third-party' || name === '3p') thirdParty = !neg
    else if (name === 'first-party' || name === '1p') thirdParty = neg
    else if (name === 'match-case') caseSensitive = true
    else if (name === 'important') continue
    else if (name.startsWith('domain=')) {
      domains = splitDomains(name.slice(7))
      if (!domains) return 'domain-option'
    } else if (name === 'document' || name === 'doc') {
      if (!allow) return 'document-block'
      doc = true
    } else if (name === 'elemhide' || name === 'ehide' || name === 'generichide' || name === 'ghide') {
      if (!allow) return 'option'
      genericHide = true
    } else if (TYPES[name]) (neg ? notTypes : types).push(TYPES[name])
    else return 'option'
  }

  // `@@||host^$generichide`: no network rule, only a cosmetic exception.
  const hostOnly = /^\|\|([a-z0-9.-]+)\^?$/i.exec(text)
  const host = hostOnly?.[1]?.toLowerCase() ?? null
  if (genericHide) {
    if (!host || !isHost(host) || doc) return 'option'
    return { allow, doc, genericHide, rule: null, host, thirdParty }
  }

  const condition: DnrRule['condition'] = {}
  if (doc) condition.resourceTypes = ['main_frame', 'sub_frame']
  else if (types.length) condition.resourceTypes = [...new Set(types)]
  else if (notTypes.length) condition.excludedResourceTypes = [...new Set([...notTypes, 'main_frame'])]
  if (thirdParty !== null) condition.domainType = thirdParty ? 'thirdParty' : 'firstParty'
  if (domains.include.length) condition.initiatorDomains = domains.include
  if (domains.exclude.length) condition.excludedInitiatorDomains = domains.exclude
  // DNR's url filter speaks ABP's anchors (||, |), separator (^) and wildcard (*) as is.
  if (host && isHost(host) && !doc) condition.requestDomains = [host]
  else condition.urlFilter = text
  if (caseSensitive && condition.urlFilter) condition.isUrlFilterCaseSensitive = true
  const type = doc ? 'allowAllRequests' : allow ? 'allow' : 'block'
  return { allow, doc, genericHide, rule: { priority: allow ? ALLOW_PRIORITY : BLOCK_PRIORITY, action: { type }, condition }, host: host && isHost(host) ? host : null, thirdParty }
}

type CosmeticLine = { kind: 'hide' | 'unhide'; include: string[]; exclude: string[]; selector: string }

/** One element hiding line, or a skip reason. */
export function parseCosmetic(line: string): CosmeticLine | string {
  if (/#@?[?$%]#/.test(line)) return 'scriptlet'
  const m = /^([^#]*)(##|#@#)(.+)$/.exec(line)
  if (!m) return 'cosmetic-syntax'
  const [, list = '', sep, selector = ''] = m
  if (/^\+js\(/.test(selector)) return 'scriptlet'
  const sel = selector.trim()
  if (!isSafeSelector(sel)) return 'selector'
  const domains = splitDomains(list)
  if (!domains) return 'domain-option'
  return { kind: sep === '##' ? 'hide' : 'unhide', include: domains.include, exclude: domains.exclude, selector: sel }
}

const push = (map: Record<string, string[]>, key: string, value: string) => {
  const list = (map[key] ??= [])
  if (!list.includes(value)) list.push(value)
}

/** Compiles filter list texts, in order. Comments, headers and junk lines are skipped. */
export function compile(lists: string[], limits: { maxRules?: number; maxGeneric?: number } = {}): Compiled {
  const maxRules = limits.maxRules ?? MAX_RULES
  const maxGeneric = limits.maxGeneric ?? MAX_GENERIC
  const skipped: Record<string, number> = {}
  const skip = (why: string) => (skipped[why] = (skipped[why] ?? 0) + 1)
  // Host-only block filters, grouped by party, keep the rule count low (uBO Lite does the same).
  const hosts = { any: new Set<string>(), third: new Set<string>() }
  const other: Omit<DnrRule, 'id'>[] = []
  const seen = new Set<string>()
  const cosmetic: Cosmetic = { generic: [], domains: {}, exceptions: {}, genericHide: [] }
  const genericSet = new Set<string>()
  const genericUnhide = new Set<string>()

  for (const list of lists) {
    for (const raw of list.split(/\r?\n/)) {
      const line = raw.trim()
      if (!line || line.startsWith('!') || line.startsWith('[')) continue
      if (/#@?#|#@?[?$%]#/.test(line) && !line.startsWith('@@') && !line.startsWith('/')) {
        const c = parseCosmetic(line)
        if (typeof c === 'string') {
          skip(c)
          continue
        }
        if (c.kind === 'unhide') {
          if (c.include.length === 0) genericUnhide.add(c.selector)
          for (const d of c.include) push(cosmetic.exceptions, d, c.selector)
          continue
        }
        if (c.include.length === 0) {
          if (!genericSet.has(c.selector)) {
            genericSet.add(c.selector)
            cosmetic.generic.push(c.selector)
          }
          for (const d of c.exclude) push(cosmetic.exceptions, d, c.selector)
        } else {
          // `a.com,~b.a.com##sel`: hidden on a.com, kept on b.a.com.
          for (const d of c.include) push(cosmetic.domains, d, c.selector)
          for (const d of c.exclude) push(cosmetic.exceptions, d, c.selector)
        }
        continue
      }
      const n = parseNetwork(line)
      if (typeof n === 'string') {
        skip(n)
        continue
      }
      if (n.genericHide) {
        if (n.host && !cosmetic.genericHide.includes(n.host)) cosmetic.genericHide.push(n.host)
        continue
      }
      const rule = n.rule
      if (!rule) continue
      const c = rule.condition
      const plainHost = !n.allow && n.host && c.requestDomains && Object.keys(c).every((k) => k === 'requestDomains' || k === 'domainType')
      if (plainHost && n.host) {
        if (n.thirdParty === true) hosts.third.add(n.host)
        else if (n.thirdParty === null) hosts.any.add(n.host)
        else other.push(rule)
        continue
      }
      const key = JSON.stringify(rule)
      if (seen.has(key)) continue
      seen.add(key)
      other.push(rule)
    }
  }

  cosmetic.generic = cosmetic.generic.filter((s) => !genericUnhide.has(s))
  if (cosmetic.generic.length > maxGeneric) {
    skipped['generic-cap'] = cosmetic.generic.length - maxGeneric
    cosmetic.generic = cosmetic.generic.slice(0, maxGeneric)
  }
  cosmetic.genericHide.sort()

  const grouped: Omit<DnrRule, 'id'>[] = []
  const group = (set: Set<string>, thirdParty: boolean) => {
    const all = [...set].filter((h) => !(thirdParty && hosts.any.has(h))).sort()
    for (let i = 0; i < all.length; i += DOMAINS_PER_RULE) {
      const condition: DnrRule['condition'] = { requestDomains: all.slice(i, i + DOMAINS_PER_RULE) }
      if (thirdParty) condition.domainType = 'thirdParty'
      grouped.push({ priority: BLOCK_PRIORITY, action: { type: 'block' }, condition })
    }
  }
  group(hosts.any, false)
  group(hosts.third, true)
  // Exceptions first, so a cap never keeps a block whose exception it dropped.
  const ordered = [...other.filter((r) => r.action.type !== 'block'), ...grouped, ...other.filter((r) => r.action.type === 'block')]
  if (ordered.length > maxRules) skipped['rule-cap'] = ordered.length - maxRules
  const rules = ordered.slice(0, maxRules).map((r, i) => ({ id: i + 1, ...r }))
  const version = createHash('sha256').update(JSON.stringify({ rules, cosmetic })).digest('hex').slice(0, 16)
  return { version, rules, cosmetic, skipped }
}

/** The two resources the browser reads (IDR_CLU_OVERLAY_PAGE_BLOCKER_RULES_JSON and ..._COSMETIC_JSON). */
export function resources(c: Compiled): { rules: string; cosmetic: string } {
  return { rules: `${JSON.stringify({ version: c.version, rules: c.rules })}\n`, cosmetic: `${JSON.stringify({ version: c.version, ...c.cosmetic })}\n` }
}
