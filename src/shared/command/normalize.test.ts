import { describe, expect, it } from 'vitest'
import { normalizeForDedupe, stripTracking } from '../url'
import { decodePunycode, displayHostname, displayUrl } from './normalize'

describe('display', () => {
  it('decodes punycode labels', () => {
    expect(decodePunycode('mnchen-3ya')).toBe('münchen')
    expect(decodePunycode('e1afmkfd')).toBe('пример')
    expect(decodePunycode('p1ai')).toBe('рф')
  })
  it.each(['!!', 'zzzzzzzzzzzzzzzzzzzzzzzzzzzz', '9'.repeat(80), 'a-é'])('rejects malformed punycode %j', (s) => {
    expect(() => decodePunycode(s)).not.toThrow()
  })
  it('shows IDNA hosts in Unicode', () => {
    expect(displayHostname('xn--mnchen-3ya.de')).toBe('münchen.de')
    expect(displayHostname('xn--e1afmkfd.xn--p1ai')).toBe('пример.рф')
    expect(displayUrl('https://xn--mnchen-3ya.de/stra%C3%9Fe')).toBe('https://münchen.de/stra%C3%9Fe')
    expect(displayUrl('https://user:pw@xn--mnchen-3ya.de:8443/x')).toBe('https://user:pw@münchen.de:8443/x')
  })
  it('keeps hosts that could spoof a Latin name in ASCII', () => {
    // Cyrillic "а" + Latin "pple" mixes scripts; an all-Cyrillic label under .com reads as Latin.
    expect(displayHostname('xn--pple-43d.com')).toBe('xn--pple-43d.com')
    expect(displayHostname('xn--80ak6aa92e.com')).toBe('xn--80ak6aa92e.com')
    expect(displayHostname('xn--a-btb.com')).toBe('xn--a-btb.com')
    expect(displayHostname('xn--a-lq6a.com')).toBe('xn--a-lq6a.com')
  })
  it('leaves ASCII and unparseable input alone', () => {
    expect(displayUrl('https://example.com/a')).toBe('https://example.com/a')
    expect(displayUrl('about:blank')).toBe('about:blank')
    expect(displayUrl('not a url')).toBe('not a url')
  })
})

describe('dedupe and tracking strip (src/shared/url.ts)', () => {
  it('strips tracking parameters and keeps the rest and the fragment', () => {
    expect(stripTracking('https://a.com/?utm_source=x&id=2#h')).toBe('https://a.com/?id=2#h')
  })
  it('normalizes for dedupe: case, default port, fragment, trailing slash on an empty path', () => {
    expect(normalizeForDedupe('HTTPS://A.COM:443/#x')).toBe(normalizeForDedupe('https://a.com'))
    expect(normalizeForDedupe('http://a.com:80/p?q=1#y')).toBe('http://a.com/p?q=1')
  })
})
