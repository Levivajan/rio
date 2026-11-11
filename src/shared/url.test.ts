import { describe, expect, it } from 'vitest'
import { classifyUrl, isAllowedTabUrl, normalizeForDedupe, stripTracking, tabUrlOrNull } from './url'

describe('classifyUrl', () => {
  it.each(['http://a.test', 'https://a.test/x?y', 'about:blank'])('%s is web', (u) => {
    expect(classifyUrl(u).kind).toBe('web')
  })
  it('rio://settings is internal', () => {
    expect(classifyUrl('rio://settings')).toMatchObject({ kind: 'internal', page: 'settings' })
  })
  it.each(['mailto:x@y.z', 'zoommtg://z'])('%s is external-app', (u) => {
    expect(classifyUrl(u).kind).toBe('external-app')
  })
  it.each([
    ['javascript:alert(1)', 'scheme'],
    ['data:text/html,x', 'scheme'],
    ['file:///etc/hosts', 'scheme'],
    ['chrome://gpu', 'scheme'],
    ['view-source:https://a.test', 'scheme'],
    ['about:config', 'scheme'],
    ['http://', 'malformed'],
    ['%%%', 'malformed'],
    [`https://a.test/${'x'.repeat(40 * 1024)}`, 'too-long'],
    ['rio://nope', 'scheme']
  ])('%s is blocked (%s)', (u, reason) => {
    expect(classifyUrl(u)).toEqual({ kind: 'blocked', reason })
  })
  it('never throws on junk', () => {
    const junk = ['https://a.test/\u0000x', `https://${'a'.repeat(10_000)}.test/`, 'https://a.test/\ud800', '\udfff', '', ' ', 'https://[::1', null as unknown as string]
    for (const j of junk) expect(() => classifyUrl(j)).not.toThrow()
    expect(classifyUrl('https://a.test/\u0000x').kind).toBe('blocked')
  })
  it('tabs allow only web and internal pages', () => {
    expect(isAllowedTabUrl('https://a.test')).toBe(true)
    expect(isAllowedTabUrl('rio://about')).toBe(true)
    expect(isAllowedTabUrl('rio://ui/index.html')).toBe(false)
    expect(isAllowedTabUrl('javascript:alert(1)')).toBe(false)
  })
  it('a tab takes a clu-file:// URL from main, but a page popup never does', () => {
    const file = 'clu-file://0123456789abcdef0123456789abcdef/local%20file.html'
    expect(tabUrlOrNull(file)).toBe(file)
    expect(isAllowedTabUrl(file)).toBe(false)
    expect(tabUrlOrNull('clu-file:///x.html')).toBeNull()
    expect(tabUrlOrNull('file:///etc/hosts')).toBeNull()
    expect(tabUrlOrNull('rio://ui/index.html')).toBeNull()
  })
})

describe('stripTracking', () => {
  it('drops tracking params and keeps order and hash', () => {
    expect(stripTracking('https://a.com/?utm_source=x&id=2&fbclid=y#h')).toBe('https://a.com/?id=2#h')
  })
  it('drops the ? when nothing is left', () => {
    expect(stripTracking('https://a.com/p?utm_medium=a&gclid=b')).toBe('https://a.com/p')
  })
  it('keeps raw encoding of other params', () => {
    expect(stripTracking('https://a.com/?q=a+b%20c&mc_cid=1')).toBe('https://a.com/?q=a+b%20c')
  })
  it('leaves junk untouched', () => {
    expect(stripTracking('not a url')).toBe('not a url')
  })
  it('normalizes for dedupe', () => {
    expect(normalizeForDedupe('https://A.com/?utm_x=1#top')).toBe('https://a.com')
  })
})
