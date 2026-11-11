import { describe, expect, it } from 'vitest'
import { DEFAULT_ENGINE_ID, ENGINE_IDS, ENGINES, engineById, MAX_SEARCH_QUERY } from './engines'

describe('engines', () => {
  it('registers the five engines with Google as the default', () => {
    expect(ENGINES.map((e) => e.id)).toEqual([...ENGINE_IDS])
    expect(DEFAULT_ENGINE_ID).toBe('google')
  })

  it.each(ENGINES.map((e) => [e.id, e] as const))('%s builds encoded https search and suggest URLs', (_id, engine) => {
    for (const build of [engine.searchUrl, engine.suggestUrl]) {
      const u = new URL(build('c++ & rust #1 ?x=y'))
      expect(u.protocol).toBe('https:')
      expect(build('c++ & rust')).toContain('c%2B%2B+%26+rust')
      expect(build('a/b?c=d#e')).not.toMatch(/[ #]|\?c=d/)
    }
  })

  it.each(ENGINES.map((e) => [e.id, e] as const))('%s matchesSearchUrl round-trips its own search URL', (_id, engine) => {
    for (const q of ['c++ & rust', 'münchen', '100% = sure?', '"quoted" #tag']) expect(engine.matchesSearchUrl(engine.searchUrl(q))).toBe(q)
  })

  it('matchesSearchUrl rejects other pages and other engines', () => {
    const google = engineById('google')
    expect(google.matchesSearchUrl('https://google.com/search?q=x')).toBe('x')
    expect(google.matchesSearchUrl('https://www.google.com/search?q=a+b')).toBe('a b')
    expect(google.matchesSearchUrl('https://www.google.com/maps?q=x')).toBeNull()
    expect(google.matchesSearchUrl('https://www.google.com/search?q=%20')).toBeNull()
    expect(google.matchesSearchUrl('http://www.google.com/search?q=x')).toBeNull()
    expect(google.matchesSearchUrl('https://evil.example/search?q=x')).toBeNull()
    expect(google.matchesSearchUrl(engineById('bing').searchUrl('x'))).toBeNull()
    expect(google.matchesSearchUrl('not a url')).toBeNull()
  })

  it('falls back to Google for unknown or corrupt ids', () => {
    for (const id of ['', 'yahoo', null, 42, { id: 'bing' }, '__proto__']) expect(engineById(id).id).toBe('google')
    expect(engineById('duckduckgo').searchUrl('x')).toBe('https://duckduckgo.com/?q=x')
  })

  it('caps long queries and never throws on lone surrogates', () => {
    const q = encodeURIComponent('a'.repeat(MAX_SEARCH_QUERY))
    expect(engineById('google').searchUrl('a'.repeat(10_000))).toBe(`https://www.google.com/search?q=${q}`)
    expect(() => engineById('google').searchUrl('x\ud800y')).not.toThrow()
    expect(engineById('google').searchUrl('x\ud800y')).toBe('https://www.google.com/search?q=x%EF%BF%BDy')
    const edge = `${'a'.repeat(MAX_SEARCH_QUERY - 1)}😀`
    expect(engineById('google').matchesSearchUrl(engineById('google').searchUrl(edge))).toBe('a'.repeat(MAX_SEARCH_QUERY - 1))
  })

  it('the e2e origin override replaces search and suggest origins and keeps paths', () => {
    const e = engineById('ecosia', 'http://127.0.0.1:4321/')
    expect(e.searchUrl('q 1')).toBe('http://127.0.0.1:4321/search?q=q+1')
    expect(e.suggestUrl('q')).toBe('http://127.0.0.1:4321/autocomplete?type=list&q=q')
    expect(e.matchesSearchUrl('http://127.0.0.1:4321/search?q=q+1')).toBe('q 1')
    expect(e.matchesSearchUrl('http://127.0.0.1:4321/search?q=q%201')).toBe('q 1')
    expect(engineById('google', 'javascript:alert(1)').searchUrl('x')).toBe('https://www.google.com/search?q=x')
    expect(engineById('google', 'not a url').searchUrl('x')).toBe('https://www.google.com/search?q=x')
  })
})
