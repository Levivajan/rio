import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { compile, isSafeSelector, parseCosmetic, parseNetwork, resources } from './abp'
import { buildBlocker, fixture, FIXTURE_DIR } from './build'

const rule = (line: string) => {
  const n = parseNetwork(line)
  if (typeof n === 'string') throw new Error(n)
  return n.rule
}

describe('parseNetwork', () => {
  it('keeps ABP anchors, separators and wildcards as the DNR url filter', () => {
    expect(rule('/ads/banner.js')).toEqual({ priority: 1, action: { type: 'block' }, condition: { urlFilter: '/ads/banner.js' } })
    expect(rule('|https://ads.example/*.gif^')?.condition.urlFilter).toBe('|https://ads.example/*.gif^')
    expect(rule('||cdn.example/track/')?.condition.urlFilter).toBe('||cdn.example/track/')
  })

  it('turns a host-only filter into a request domain', () => {
    expect(rule('||Tracker.Example^')?.condition).toEqual({ requestDomains: ['tracker.example'] })
  })

  it('maps type, party, domain and case options', () => {
    expect(rule('/pixel.$image,script,third-party,domain=a.com|~b.a.com,match-case')?.condition).toEqual({
      urlFilter: '/pixel.',
      resourceTypes: ['image', 'script'],
      domainType: 'thirdParty',
      initiatorDomains: ['a.com'],
      excludedInitiatorDomains: ['b.a.com'],
      isUrlFilterCaseSensitive: true
    })
    expect(rule('/x.js$~script,1p')?.condition).toEqual({ urlFilter: '/x.js', excludedResourceTypes: ['script', 'main_frame'], domainType: 'firstParty' })
  })

  it('makes exceptions allow rules above blocks, and document exceptions allow the whole page', () => {
    expect(rule('@@/ads/ok.js')).toEqual({ priority: 2, action: { type: 'allow' }, condition: { urlFilter: '/ads/ok.js' } })
    expect(rule('@@||news.example^$document')).toEqual({ priority: 2, action: { type: 'allowAllRequests' }, condition: { urlFilter: '||news.example^', resourceTypes: ['main_frame', 'sub_frame'] } })
  })

  it('skips what DNR cannot say exactly, and never blocks a page', () => {
    expect(parseNetwork('/banner\\d+/')).toBe('regex')
    expect(parseNetwork('/ads.js$redirect=noop.js')).toBe('option')
    expect(parseNetwork('/ads.js$csp=script-src')).toBe('option')
    expect(parseNetwork('||example.com^$document')).toBe('document-block')
    expect(parseNetwork('/ads.js$domain=example.*')).toBe('domain-option')
    expect(parseNetwork('||*.example^')).toBe('too-broad')
    expect(parseNetwork('*')).toBe('too-broad')
    expect(parseNetwork('/pübli/')).toBe('regex')
    expect(parseNetwork('/pübli')).toBe('non-ascii')
    expect(parseNetwork(`/${'a'.repeat(2000)}`)).toBe('too-long')
  })
})

describe('parseCosmetic', () => {
  it('reads generic, site and exception hiding', () => {
    expect(parseCosmetic('##.ad-slot')).toEqual({ kind: 'hide', include: [], exclude: [], selector: '.ad-slot' })
    expect(parseCosmetic('a.com,~b.a.com##div[id^="ad-"]')).toEqual({ kind: 'hide', include: ['a.com'], exclude: ['b.a.com'], selector: 'div[id^="ad-"]' })
    expect(parseCosmetic('a.com#@#.ad')).toEqual({ kind: 'unhide', include: ['a.com'], exclude: [], selector: '.ad' })
  })

  it('skips scriptlets, extended CSS and selectors that could leave their rule', () => {
    expect(parseCosmetic('a.com##+js(set, x, 1)')).toBe('scriptlet')
    expect(parseCosmetic('a.com#?#div:-abp-has(.ad)')).toBe('scriptlet')
    expect(parseCosmetic('a.com#$#abort-on-property-read x')).toBe('scriptlet')
    expect(parseCosmetic('##div:has-text(Sponsored)')).toBe('selector')
    expect(parseCosmetic('##.a{color:red}')).toBe('selector')
    expect(parseCosmetic('##.a;}body{display:none')).toBe('selector')
    expect(parseCosmetic('example.*##.ad')).toBe('domain-option')
    expect(isSafeSelector('div:has(> .ad)')).toBe(true)
    expect(isSafeSelector(`.${'a'.repeat(1100)}`)).toBe(false)
  })
})

describe('compile', () => {
  it('groups host-only blocks, keeps exceptions first and numbers rules from 1', () => {
    const c = compile(['! comment\n[Adblock Plus 2.0]\n||a.example^\n||b.example^\n||c.example^$third-party\n||a.example^$third-party\n/ads.js\n@@/ads.js$domain=ok.example\n\n  junk$$$ line'])
    expect(c.rules.map((r) => r.id)).toEqual([1, 2, 3, 4])
    expect(c.skipped.junk).toBe(1)
    expect(c.rules[0]?.action.type).toBe('allow')
    expect(c.rules[1]?.condition).toEqual({ requestDomains: ['a.example', 'b.example'] })
    // a.example is blocked from every party already, so the third-party group leaves it out.
    expect(c.rules[2]?.condition).toEqual({ requestDomains: ['c.example'], domainType: 'thirdParty' })
    expect(c.rules.at(-1)?.condition.urlFilter).toBe('/ads.js')
  })

  it('splits large host groups and caps the rule count, dropping blocks before exceptions', () => {
    const hosts = Array.from({ length: 2500 }, (_, i) => `||h${i}.example^`).join('\n')
    const c = compile([`${hosts}\n@@/keep.js\n/one.js\n/two.js`], { maxRules: 4 })
    expect(c.rules).toHaveLength(4)
    expect(c.rules[0]?.action.type).toBe('allow')
    expect(c.rules[1]?.condition.requestDomains).toHaveLength(1000)
    expect(c.skipped['rule-cap']).toBe(2)
  })

  it('collects cosmetic data, drops generic selectors a generic exception names, and caps generic hiding', () => {
    const c = compile(['##.a\n##.b\n##.c\n#@#.b\n~x.example##.d\nx.example##.e\n@@||y.example^$generichide\n##.a'], { maxGeneric: 2 })
    expect(c.cosmetic.generic).toEqual(['.a', '.c'])
    expect(c.cosmetic.exceptions).toEqual({ 'x.example': ['.d'] })
    expect(c.cosmetic.domains).toEqual({ 'x.example': ['.e'] })
    expect(c.cosmetic.genericHide).toEqual(['y.example'])
    expect(c.skipped['generic-cap']).toBe(1)
  })

  it('versions the output by content', () => {
    expect(compile(['||a.example^']).version).toBe(compile(['||a.example^']).version)
    expect(compile(['||a.example^']).version).not.toBe(compile(['||b.example^']).version)
    expect(compile(['||a.example^']).version).toMatch(/^[0-9a-f]{16}$/)
  })
})

describe('the blocker fixture', () => {
  it('matches the files clu_browsertests load (rerun build.ts --fixture after changing filters.txt)', () => {
    const out = fixture()
    expect(readFileSync(join(FIXTURE_DIR, 'rules.json'), 'utf8')).toBe(out.rules)
    expect(readFileSync(join(FIXTURE_DIR, 'cosmetic.json'), 'utf8')).toBe(out.cosmetic)
  })

  it('blocks the tracker host and the banner path and hides both ad slots', () => {
    const rules = JSON.parse(fixture().rules) as { rules: { condition: Record<string, unknown> }[] }
    expect(rules.rules.map((r) => r.condition)).toEqual([{ requestDomains: ['tracker.localhost'] }, { urlFilter: '/ads/banner.js' }])
    const cosmetic = JSON.parse(fixture().cosmetic) as { generic: string[]; domains: Record<string, string[]> }
    expect(cosmetic.generic).toEqual(['.ad-slot'])
    expect(cosmetic.domains['127.0.0.1']).toEqual(['.host-slot'])
  })
})

describe('buildBlocker', () => {
  it('compiles every list in the lists folder, else the fixture list', () => {
    const dir = mkdtempSync(join(tmpdir(), 'clu-blocker-'))
    try {
      writeFileSync(join(dir, 'b.txt'), '||b.example^\n')
      writeFileSync(join(dir, 'a.txt'), '||a.example^\n')
      const built = buildBlocker(join(dir, 'out'), dir)
      expect(built.lists.map((l) => l.slice(dir.length + 1))).toEqual(['a.txt', 'b.txt'])
      const written = JSON.parse(readFileSync(join(dir, 'out', 'blocker-rules.json'), 'utf8')) as { version: string; rules: unknown[] }
      expect(written).toEqual(JSON.parse(resources(built.compiled).rules))
      expect(buildBlocker(join(dir, 'out2'), join(dir, 'missing')).lists).toEqual([join(FIXTURE_DIR, 'filters.txt')])
    } finally {
      rmSync(dir, { recursive: true, force: true })
    }
  })
})
