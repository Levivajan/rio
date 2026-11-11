import { describe, expect, it } from 'vitest'
import { siteKey } from './site'
import { computeLayout } from './shell/layout'
import { sanitizeTitle } from './engine'

describe('siteKey', () => {
  it('returns the lowercase host without one leading www.', () => {
    expect(siteKey('https://www.a.com/x')).toBe('a.com')
    expect(siteKey('HTTP://WWW.A.COM')).toBe('a.com')
    expect(siteKey('https://www.www.a.com')).toBe('www.a.com')
    expect(siteKey('https://sub.a.com/x?y#z')).toBe('sub.a.com')
  })

  it('keeps IP hosts and localhost', () => {
    expect(siteKey('http://127.0.0.1:8080')).toBe('127.0.0.1')
    expect(siteKey('http://localhost:3000/')).toBe('localhost')
    expect(siteKey('http://[::1]:3000/')).toBe('[::1]')
  })

  it('returns null for everything else', () => {
    for (const junk of ['about:blank', 'clu://x', 'chrome-extension://x', 'file:///etc/hosts', 'not a url', '', 'x'.repeat(10 * 1024 * 1024), `https://a.com/${'x'.repeat(10 * 1024 * 1024)}`]) {
      expect(siteKey(junk)).toBeNull()
    }
    for (const junk of [null, undefined, 42, {}]) expect(siteKey(junk)).toBeNull()
  })

  it('never returns an empty key', () => {
    expect(siteKey('https://www./')).toBe('www.')
  })
})

describe('computeLayout', () => {
  it('puts the page card at x 214, y 10 for 1280 x 800', () => {
    expect(computeLayout({ width: 1280, height: 800 }).pageRect).toEqual({ x: 214, y: 10, width: 1056, height: 780 })
  })
  it('survives junk sizes', () => {
    expect(computeLayout({ width: Number.NaN, height: -5 }).pageRect.width).toBe(0)
  })
})

describe('sanitizeTitle', () => {
  it('strips controls, collapses whitespace, caps at 1024 and falls back to the host', () => {
    expect(sanitizeTitle('a\u0000b\n\n  c', 'https://x.test')).toBe('a b c')
    expect(sanitizeTitle('y'.repeat(100_000), 'https://x.test')).toHaveLength(1024)
    expect(sanitizeTitle('   ', 'https://x.test/p')).toBe('x.test')
    expect(sanitizeTitle(42, 'junk')).toBe('junk')
  })
})
