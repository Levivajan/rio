import { describe, expect, it } from 'vitest'
import { classifyUrl } from '../url'
import { classifyInput, cleanInput, MAX_INPUT, type InputClass } from './classify'

const url = (u: string): InputClass => ({ kind: 'url', url: u })
const search = (q: string): InputClass => ({ kind: 'search', query: q })
const empty: InputClass = { kind: 'empty' }

const TABLE: [string, InputClass][] = [
  // Bare hosts with an IANA TLD get https.
  ['example.com', url('https://example.com/')],
  ['EXAMPLE.COM', url('https://example.com/')],
  ['  example.com  ', url('https://example.com/')],
  ['www.example.com', url('https://www.example.com/')],
  ['example.com/path?q=1#frag', url('https://example.com/path?q=1#frag')],
  ['example.com:8080', url('https://example.com:8080/')],
  ['example.com:8080/a/b', url('https://example.com:8080/a/b')],
  ['sub.domain.example.co.uk', url('https://sub.domain.example.co.uk/')],
  ['example.com.', url('https://example.com./')],
  ['readme.md', url('https://readme.md/')],
  ['file.zip', url('https://file.zip/')],
  ['github.io', url('https://github.io/')],
  ['münchen.de', url('https://xn--mnchen-3ya.de/')],
  ['MÜNCHEN.de/straße', url('https://xn--mnchen-3ya.de/stra%C3%9Fe')],
  ['пример.рф', url('https://xn--e1afmkfd.xn--p1ai/')],
  ['example.com/a b', search('example.com/a b')],
  // Explicit http and https.
  ['https://example.com', url('https://example.com/')],
  ['http://example.com/x', url('http://example.com/x')],
  ['HTTPS://Example.COM/Path', url('https://example.com/Path')],
  ['https://intranet', url('https://intranet/')],
  ['http://localhost:3000', url('http://localhost:3000/')],
  ['https://example.com/a b', url('https://example.com/a%20b')],
  ['http:example.com', url('http://example.com/')],
  // localhost and IPs get http.
  ['localhost', url('http://localhost/')],
  ['localhost:5173/a', url('http://localhost:5173/a')],
  ['LOCALHOST:5173', url('http://localhost:5173/')],
  ['app.localhost', url('http://app.localhost/')],
  ['api.app.localhost:8080/x', url('http://api.app.localhost:8080/x')],
  ['192.168.1.1', url('http://192.168.1.1/')],
  ['127.0.0.1:8080/health', url('http://127.0.0.1:8080/health')],
  ['10.0.0.255', url('http://10.0.0.255/')],
  ['[::1]:8080', url('http://[::1]:8080/')],
  ['[::1]', url('http://[::1]/')],
  ['[2001:db8::1]/x', url('http://[2001:db8::1]/x')],
  // Intranet hosts: url only when followed by '/'.
  ['intranet.corp/', url('https://intranet.corp/')],
  ['intranet.corp/wiki/page', url('https://intranet.corp/wiki/page')],
  ['intranet.corp', search('intranet.corp')],
  ['printer.lan', search('printer.lan')],
  // Internal pages and about:blank.
  ['about:blank', url('about:blank')],
  ['ABOUT:BLANK', url('about:blank')],
  ['rio://settings', url('rio://settings')],
  ['rio://history', url('rio://history')],
  // Searches.
  ['foo bar', search('foo bar')],
  ['foo', search('foo')],
  ['?example.com', search('example.com')],
  ['? hello', search('hello')],
  ['what is example.com ?', search('what is example.com')],
  ['https://example.com ?', search('https://example.com')],
  ['node.js', search('node.js')],
  ['config.yaml', search('config.yaml')],
  ['3.14', search('3.14')],
  ['1.2.3', search('1.2.3')],
  ['999.1.1.1', search('999.1.1.1')],
  ['example.com:99999', search('example.com:99999')],
  ['user@example.com', search('user@example.com')],
  ['user:pw@example.com', search('user:pw@example.com')],
  ['a:b:c', search('a:b:c')],
  ['[::1', search('[::1')],
  ['https://', search('https://')],
  ['https://exa mple.com', search('https://exa mple.com')],
  ['c++ & rust', search('c++ & rust')],
  ['com', search('com')],
  ['.com', search('.com')],
  ['example..com', search('example..com')],
  // Every scheme outside the allowlist searches the whole text.
  ['javascript:alert(1)', search('javascript:alert(1)')],
  ['JavaScript:alert(document.cookie)', search('JavaScript:alert(document.cookie)')],
  ['java\nscript:alert(1)', search('java script:alert(1)')],
  ['file:///etc/passwd', search('file:///etc/passwd')],
  ['data:text/html,<script>alert(1)</script>', search('data:text/html,<script>alert(1)</script>')],
  ['view-source:https://example.com', search('view-source:https://example.com')],
  ['chrome://settings', search('chrome://settings')],
  ['about:config', search('about:config')],
  ['rio://ui/index.html', search('rio://ui/index.html')],
  ['rio://nope', search('rio://nope')],
  ['mailto:a@example.com', search('mailto:a@example.com')],
  ['ftp://example.com', search('ftp://example.com')],
  ['blob:https://example.com/x', search('blob:https://example.com/x')],
  // Empty input.
  ['', empty],
  ['   ', empty],
  ['\n\t', empty],
  ['?', empty],
  ['? ', empty]
]

describe('classifyInput table', () => {
  it('has at least 60 cases', () => {
    expect(TABLE.length).toBeGreaterThanOrEqual(60)
  })
  it.each(TABLE)('%j', (input, expected) => {
    expect(classifyInput(input)).toEqual(expected)
  })
})

describe('classifyInput junk', () => {
  it('truncates a 10,000-char input to 8,192 without throwing', () => {
    const long = 'a'.repeat(10_000)
    expect(cleanInput(long)).toHaveLength(MAX_INPUT)
    expect(classifyInput(long)).toEqual(search('a'.repeat(MAX_INPUT)))
    const longUrl = `https://example.com/${'x'.repeat(10_000)}`
    const r = classifyInput(longUrl)
    expect(r.kind).toBe('url')
    expect(r.kind === 'url' && r.url.length).toBe(MAX_INPUT)
  })
  it('does not split a surrogate pair at the cap', () => {
    const text = `${'a'.repeat(MAX_INPUT - 1)}😀tail`
    const cleaned = cleanInput(text)
    expect(cleaned).toHaveLength(MAX_INPUT - 1)
    expect(cleaned.isWellFormed()).toBe(true)
  })
  it.each(['\ud800', '\udfff', 'a\ud800b.com', 'https://example.com/\ud800', '\ud800.com', 'search \udc00 me', 'xn--\ud83d.com/'])(
    'lone surrogate %j never throws',
    (input) => {
      expect(() => classifyInput(input)).not.toThrow()
      const r = classifyInput(input)
      if (r.kind === 'search') expect(r.query.isWellFormed()).toBe(true)
      if (r.kind === 'url') expect(classifyUrl(r.url).kind).toBe('web')
    }
  )
  it('drops control and bidi-override characters and collapses line breaks', () => {
    expect(cleanInput('a\u0000b\u202ec\r\n\r\nd')).toBe('abc d')
    expect(classifyInput('exa\u0000mple.com')).toEqual(url('https://example.com/'))
  })
  it('handles non-string input', () => {
    expect(classifyInput(null as unknown as string)).toEqual(empty)
    expect(classifyInput(42 as unknown as string)).toEqual(empty)
  })

  it('every url result passes classifyUrl as web, everything else is a search or empty', () => {
    // Deterministic pseudo-random inputs built from URL-ish fragments and junk characters.
    let seed = 0x2f6b1d
    const rand = (n: number) => {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff
      return seed % n
    }
    const parts = [
      'http://', 'https://', 'javascript:', 'file://', 'data:', 'about:', 'localhost', ':', '8080', '/', '?', '#', '@', '[',
      ']', '::1', '.', 'com', 'de', 'corp', 'md', 'xn--', 'münchen', '192.168.1.1', ' ', '\t', '\n', '%', '%00', '\u0000',
      '\ud800', '\udc00', '😀', 'а', 'é', '\u202e', 'a', 'b', '-', '_', '..', '\\', '"', '<', '>'
    ]
    for (let i = 0; i < 5000; i++) {
      let input = ''
      const n = 1 + rand(8)
      for (let j = 0; j < n; j++) input += parts[rand(parts.length)]
      expect(() => classifyInput(input), JSON.stringify(input)).not.toThrow()
      const r = classifyInput(input)
      if (r.kind === 'url') expect(classifyUrl(r.url).kind, JSON.stringify(input)).toBe('web')
      else if (r.kind === 'search') expect(r.query.trim(), JSON.stringify(input)).not.toBe('')
    }
  })
})
